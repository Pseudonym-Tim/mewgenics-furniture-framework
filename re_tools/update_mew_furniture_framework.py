from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_pointer: int
    raw_size: int

@dataclass(frozen=True)
class MatchResult:
    symbol_name: str
    value: int
    section_name: str
    file_offset: int

@dataclass(frozen=True)
class DllPatch:
    symbol_name: str
    old_value: int
    new_value: int
    file_offsets: tuple[int, ...]

class PeImage:
    def __init__(self, image_path: Path) -> None:
        self.image_path: Path = image_path
        self.data: bytes = image_path.read_bytes()
        self.sections: list[Section] = self._read_sections()

    def _read_sections(self) -> list[Section]:
        if len(self.data) < 0x40 or self.data[0:2] != b"MZ":
            raise ValueError("Input is not a valid PE file: missing MZ header.")

        pe_offset: int = struct.unpack_from("<I", self.data, 0x3C)[0]

        if self.data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("Input is not a valid PE file: missing PE signature.")

        section_count: int = struct.unpack_from("<H", self.data, pe_offset + 6)[0]
        optional_header_size: int = struct.unpack_from("<H", self.data, pe_offset + 20)[0]
        section_table_offset: int = pe_offset + 24 + optional_header_size
        sections: list[Section] = []

        for index in range(section_count):
            offset: int = section_table_offset + (index * 40)
            raw_name: bytes = self.data[offset:offset + 8]
            name: str = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
            virtual_size: int
            virtual_address: int
            raw_size: int
            raw_pointer: int
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", self.data, offset + 8)
            sections.append(Section(name, virtual_address, virtual_size, raw_pointer, raw_size))

        return sections

    def file_offset_to_rva(self, file_offset: int) -> int:
        for section in self.sections:
            mapped_size: int = max(section.virtual_size, section.raw_size)

            if section.raw_pointer <= file_offset < section.raw_pointer + mapped_size:
                return section.virtual_address + (file_offset - section.raw_pointer)

        raise ValueError(f"File offset 0x{file_offset:X} is not inside a PE section.")

    def rva_to_file_offset(self, rva: int) -> tuple[int, Section]:
        for section in self.sections:
            mapped_size: int = max(section.virtual_size, section.raw_size)

            if section.virtual_address <= rva < section.virtual_address + mapped_size:
                return section.raw_pointer + (rva - section.virtual_address), section

        raise ValueError(f"RVA 0x{rva:X} is not inside a PE section.")

    def get_section_bytes(self, section: Section) -> bytes:
        mapped_size: int = max(section.virtual_size, section.raw_size)
        return self.data[section.raw_pointer:min(len(self.data), section.raw_pointer + mapped_size)]

def parse_pattern(pattern_text: str) -> tuple[bytes, list[bool]]:
    values: list[int] = []
    mask: list[bool] = []

    for token in pattern_text.split():
        if token == "?" or token == "??":
            values.append(0)
            mask.append(False)
            continue

        if not re.fullmatch(r"[0-9A-Fa-f]{2}", token):
            raise ValueError(f"Invalid pattern token: {token}")

        values.append(int(token, 16))
        mask.append(True)

    return bytes(values), mask

def find_pattern(buffer: bytes, pattern: bytes, mask: list[bool]) -> list[int]:
    pattern_length: int = len(pattern)

    if pattern_length == 0:
        return []

    fixed_indices: list[int] = [index for index, is_fixed in enumerate(mask) if is_fixed]

    if not fixed_indices:
        return []

    anchor_index: int = fixed_indices[0]
    anchor_value: int = pattern[anchor_index]
    matches: list[int] = []
    search_from: int = 0

    while True:
        anchor_position: int = buffer.find(bytes([anchor_value]), search_from)

        if anchor_position < 0:
            break

        start: int = anchor_position - anchor_index

        if start >= 0 and start + pattern_length <= len(buffer):
            is_match: bool = True

            for index in fixed_indices:
                if buffer[start + index] != pattern[index]:
                    is_match = False
                    break

            if is_match:
                matches.append(start)

        search_from = anchor_position + 1

    return matches

def get_pattern_texts(symbol_name: str, symbol_config: dict[str, Any]) -> list[str]:
    pattern_value: Any = symbol_config.get("patterns", symbol_config.get("pattern"))

    if isinstance(pattern_value, str):
        return [pattern_value]

    if isinstance(pattern_value, list) and pattern_value and all(isinstance(value, str) for value in pattern_value):
        return [str(value) for value in pattern_value]

    raise ValueError(f"{symbol_name}: expected a 'pattern' string or non-empty 'patterns' string array.")

def find_unique_pattern(pe_image: PeImage, symbol_name: str, symbol_config: dict[str, Any]) -> tuple[Section, int]:
    search_section_names: set[str] = set(str(value) for value in symbol_config.get("search_sections", []))
    ambiguous_results: list[str] = []

    for pattern_index, pattern_text in enumerate(get_pattern_texts(symbol_name, symbol_config), start=1):
        pattern: bytes
        mask: list[bool]
        pattern, mask = parse_pattern(pattern_text)
        found: list[tuple[Section, int]] = []

        for section in pe_image.sections:
            if search_section_names and section.name not in search_section_names:
                continue

            section_data: bytes = pe_image.get_section_bytes(section)

            for relative_offset in find_pattern(section_data, pattern, mask):
                found.append((section, relative_offset))

        if len(found) == 1:
            return found[0]

        if len(found) > 1:
            locations: str = ", ".join(f"{section.name}+0x{relative_offset:X}" for section, relative_offset in found[:8])
            ambiguous_results.append(f"pattern {pattern_index} matched {len(found)} locations: {locations}")

    if ambiguous_results:
        raise RuntimeError(f"{symbol_name}: " + "; ".join(ambiguous_results))

    raise RuntimeError(f"{symbol_name}: no configured signature matched anything.")

def resolve_symbol(pe_image: PeImage, symbol_name: str, symbol_config: dict[str, Any]) -> MatchResult:
    resolver: str = str(symbol_config.get("resolver", "pattern_start_rva"))

    if resolver == "constant":
        value: int = int(symbol_config["value"])
        return MatchResult(symbol_name, value, "constant", 0)

    section: Section
    relative_offset: int
    section, relative_offset = find_unique_pattern(pe_image, symbol_name, symbol_config)

    if resolver == "pattern_start_rva":
        match_offset: int = int(symbol_config.get("match_offset", 0))
        file_offset: int = section.raw_pointer + relative_offset + match_offset
        value = pe_image.file_offset_to_rva(file_offset)
        return MatchResult(symbol_name, value, section.name, file_offset)

    if resolver == "rip_relative_rva":
        displacement_offset: int = int(symbol_config["displacement_offset"])
        instruction_end_offset: int = int(symbol_config["instruction_end_offset"])
        displacement_file_offset: int = section.raw_pointer + relative_offset + displacement_offset
        displacement: int = struct.unpack_from("<i", pe_image.data, displacement_file_offset)[0]
        instruction_end_rva: int = section.virtual_address + relative_offset + instruction_end_offset
        value = instruction_end_rva + displacement
        resolved_file_offset: int
        resolved_section: Section
        resolved_file_offset, resolved_section = pe_image.rva_to_file_offset(value)
        return MatchResult(symbol_name, value, resolved_section.name, displacement_file_offset)

    if resolver == "u8_at_pattern_offset":
        value_offset: int = int(symbol_config["value_offset"])
        file_offset = section.raw_pointer + relative_offset + value_offset
        value = pe_image.data[file_offset]
        return MatchResult(symbol_name, value, section.name, file_offset)

    if resolver == "u32_at_pattern_offset":
        value_offset = int(symbol_config["value_offset"])
        file_offset = section.raw_pointer + relative_offset + value_offset
        value = struct.unpack_from("<I", pe_image.data, file_offset)[0]
        return MatchResult(symbol_name, value, section.name, file_offset)

    raise ValueError(f"{symbol_name}: unknown resolver '{resolver}'.")

def format_define_value(original_numeric_text: str, value: int) -> str:
    if original_numeric_text.lower().startswith("0x"):
        width: int = max(len(original_numeric_text) - 2, 2)
        return f"0x{value:0{width}X}"

    return str(value)

def update_header_text(header_text: str, resolved_values: dict[str, int]) -> tuple[str, list[str]]:
    updated_symbols: list[str] = []

    for symbol_name, value in resolved_values.items():
        pattern: re.Pattern[str] = re.compile(rf"(^\s*#define\s+{re.escape(symbol_name)}\s+)(0x[0-9A-Fa-f]+|\d+)([uUlL]*)(.*$)", re.MULTILINE)

        def replace(match: re.Match[str]) -> str:
            original_numeric_text: str = match.group(2)
            suffix: str = match.group(3)
            updated_symbols.append(symbol_name)
            return f"{match.group(1)}{format_define_value(original_numeric_text, value)}{suffix}{match.group(4)}"

        header_text, replacement_count = pattern.subn(replace, header_text, count=1)

        if replacement_count != 1:
            raise RuntimeError(f"{symbol_name}: could not find matching #define in header.")

    return header_text, updated_symbols

def read_header_define_values(header_text: str, symbol_names: list[str]) -> dict[str, int]:
    values: dict[str, int] = {}

    for symbol_name in symbol_names:
        pattern: re.Pattern[str] = re.compile(rf"^\s*#define\s+{re.escape(symbol_name)}\s+(0x[0-9A-Fa-f]+|\d+)[uUlL]*", re.MULTILINE)
        match: re.Match[str] | None = pattern.search(header_text)

        if match is None:
            raise RuntimeError(f"{symbol_name}: could not read existing #define from header.")

        values[symbol_name] = int(match.group(1), 0)

    return values

def plan_dll_rva_patches(dll_path: Path, old_values: dict[str, int], new_values: dict[str, int]) -> list[DllPatch]:
    pe_image: PeImage = PeImage(dll_path)
    text_sections: list[Section] = [section for section in pe_image.sections if section.name == ".text"]

    if not text_sections:
        raise RuntimeError(f"{dll_path}: DLL has no .text section.")

    patches: list[DllPatch] = []

    for symbol_name, new_value in new_values.items():
        old_value: int = old_values[symbol_name]

        if old_value == new_value:
            continue

        if not (0 <= old_value <= 0xFFFFFFFF and 0 <= new_value <= 0xFFFFFFFF):
            raise RuntimeError(f"{symbol_name}: DLL literal patching only supports 32-bit RVA values.")

        old_bytes: bytes = struct.pack("<I", old_value)
        file_offsets: list[int] = []

        for section in text_sections:
            section_data: bytes = pe_image.get_section_bytes(section)
            search_from: int = 0

            while True:
                relative_offset: int = section_data.find(old_bytes, search_from)

                if relative_offset < 0:
                    break

                file_offsets.append(section.raw_pointer + relative_offset)
                search_from = relative_offset + 1

        if not file_offsets:
            raise RuntimeError(f"{symbol_name}: old RVA 0x{old_value:X} was not found as a 32-bit literal in the DLL .text section.")

        patches.append(DllPatch(symbol_name, old_value, new_value, tuple(file_offsets)))

    return patches

def apply_dll_rva_patches(dll_path: Path, patches: list[DllPatch], create_backup: bool) -> None:
    if not patches:
        print("No DLL RVA changes needed.")
        return

    data: bytearray = bytearray(dll_path.read_bytes())

    for patch in patches:
        replacement: bytes = struct.pack("<I", patch.new_value)

        for file_offset in patch.file_offsets:
            data[file_offset:file_offset + 4] = replacement

        locations: str = ", ".join(f"file+0x{file_offset:X}" for file_offset in patch.file_offsets)
        print(f"Patched DLL {patch.symbol_name}: 0x{patch.old_value:X} -> 0x{patch.new_value:X} ({locations})")

    if create_backup:
        backup_path: Path = dll_path.with_suffix(dll_path.suffix + ".bak")
        shutil.copy2(dll_path, backup_path)
        print(f"DLL backup written: {backup_path}")

    dll_path.write_bytes(data)
    print(f"Updated {len(patches)} RVA literals in {dll_path}")

def load_signatures(signature_path: Path) -> dict[str, Any]:
    with signature_path.open("r", encoding="utf-8") as handle:
        config: dict[str, Any] = json.load(handle)

    if "symbols" not in config or not isinstance(config["symbols"], dict):
        raise ValueError("Signature file must contain a 'symbols' object.")

    return config

def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description="Update MewFurnitureFramework.h RVAs and optionally patch a compiled DLL from Mewgenics byte signatures.")
    parser.add_argument("exe_path", type=Path, help="Path to the updated Mewgenics executable.")
    parser.add_argument("header_path", type=Path, help="Path to MewFurnitureFramework.h.")
    parser.add_argument("--signatures", type=Path, default=Path("mew_furniture_framework_signatures.json"), help="Path to mew_furniture_framework_signatures.json.")
    parser.add_argument("--dll", type=Path, help="Optional compiled MewFurnitureFramework.dll to patch in-place using the header's current RVA values as the old literals.")
    parser.add_argument("--dry-run", action="store_true", help="Resolve symbols and print results without rewriting the header or DLL.")
    parser.add_argument("--no-backup", action="store_true", help="Do not create .bak copies before rewriting the header or DLL.")
    args: argparse.Namespace = parser.parse_args()

    pe_image: PeImage = PeImage(args.exe_path)
    signature_config: dict[str, Any] = load_signatures(args.signatures)
    resolved_values: dict[str, int] = {}

    resolution_errors: list[str] = []

    for symbol_name, symbol_config_any in signature_config["symbols"].items():
        symbol_config: dict[str, Any] = dict(symbol_config_any)

        try:
            result: MatchResult = resolve_symbol(pe_image, symbol_name, symbol_config)
        except Exception as exception:
            resolution_errors.append(str(exception))
            print(f"{symbol_name} = ERROR: {exception}", file=sys.stderr)
            continue

        resolved_values[symbol_name] = result.value
        print(f"{symbol_name} = 0x{result.value:X} ({result.section_name}, file+0x{result.file_offset:X})")

    if resolution_errors:
        print(f"error: {len(resolution_errors)} symbol(s) could not be resolved; header and DLL were not modified.", file=sys.stderr)
        return 1

    original_text: str = args.header_path.read_text(encoding="utf-8")
    original_values: dict[str, int] = read_header_define_values(original_text, list(resolved_values.keys()))
    dll_patches: list[DllPatch] = []

    if args.dll is not None:
        dll_patches = plan_dll_rva_patches(args.dll, original_values, resolved_values)
        print(f"DLL patch plan: {sum(len(patch.file_offsets) for patch in dll_patches)} literal occurrence(s) across {len(dll_patches)} symbol(s).")

    if args.dry_run:
        print("Dry run complete. Header and DLL were not modified.")
        return 0

    updated_text: str
    updated_symbols: list[str]
    updated_text, updated_symbols = update_header_text(original_text, resolved_values)

    if updated_text != original_text:
        if not args.no_backup:
            backup_path: Path = args.header_path.with_suffix(args.header_path.suffix + ".bak")
            shutil.copy2(args.header_path, backup_path)
            print(f"Header backup written: {backup_path}")

        args.header_path.write_text(updated_text, encoding="utf-8", newline="")
        print(f"Updated {len(updated_symbols)} symbols in {args.header_path}")
    else:
        print("No header changes needed.")

    if args.dll is not None:
        apply_dll_rva_patches(args.dll, dll_patches, create_backup=not args.no_backup)

    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"error: {exception}", file=sys.stderr)
        raise SystemExit(1)
