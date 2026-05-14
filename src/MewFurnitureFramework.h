#ifndef FURNITURE_INFO_APPENDER_H
#define FURNITURE_INFO_APPENDER_H

#include <stdint.h>
#include <stddef.h>
#include <windows.h>

#define MOD_NAME "MewFurnitureFramework"
#define RVA_PACKED_FILE_LOAD 0x0098E300U // RVA of the packed-file loader, (hooked to intercept furniture_info.data after the game loads it)...
#define PACKED_FILE_LOAD_STOLEN_BYTES 15 // Hook overwrite size at RVA_PACKED_FILE_LOAD, (prologue instructions before the trampoline jump)...
#define RVA_FURNITURE_EDITOR_UPDATE 0x001AC860U // RVA of the furniture editor update function, (hooked so we can capture the selected row)...
#define FURNITURE_EDITOR_UPDATE_STOLEN_BYTES 19 // Hook overwrite size at RVA_FURNITURE_EDITOR_UPDATE, covers prologue instructions before the trampoline jump...
#define RVA_FURNITURE_PAYLOAD_LOOKUP 0x001ADFC0U // RVA of the helper that resolves a furniture name in the payload map and returns the payload bytes...
#define RVA_DEBUG_TEXT_ADD 0x0099F6A0U // RVA of the single-line debug-text display helper used by the furniture editor help overlay...
#define DEBUG_TEXT_ADD_STOLEN_BYTES 16 // Hook overwrite size at RVA_DEBUG_TEXT_ADD, covers complete prologue instructions before the trampoline jump...
#define RVA_DEBUG_TEXT_ADD_PAIR 0x00946490U // RVA of the label/value debug-text display helper used by the current furniture piece line...
#define DEBUG_TEXT_ADD_PAIR_STOLEN_BYTES 16 // Hook overwrite size at RVA_DEBUG_TEXT_ADD_PAIR, covers complete prologue instructions before the trampoline jump...
#define RVA_DEBUG_CONSOLE_VISIBILITY_UPDATE 0x009FFED0U // RVA of the debug-console visibility updater, used to force the overlay visible on editor entry...
#define DEBUG_CONSOLE_VISIBILITY_UPDATE_STOLEN_BYTES 17 // Hook overwrite size at RVA_DEBUG_CONSOLE_VISIBILITY_UPDATE, covers complete prologue instructions before the trampoline jump...
#define RVA_GAME_ALLOCATE 0x00052590U // RVA of the game allocator used by std::string storage...
#define RVA_MSVC_STRING_DESTRUCT 0x000522D0U // RVA of the game std::string destructor used by debug-text calls...
#define FURNITURE_EDITOR_CURRENT_RESOURCE_OFFSET 0x40U // Offset in the furniture editor object to the current resource pointer used by payload editing code...
#define FURNITURE_EDITOR_NAME_VECTOR_OFFSET 0x68U // Offset in the furniture editor object to the selected-name vector begin pointer, (end pointer is at +0x70)...
#define FURNITURE_EDITOR_CURRENT_INDEX_OFFSET 0x80U // Offset in the furniture editor object to the current selected-name index...
#define FURNITURE_RESOURCE_PAYLOAD_MAP_OFFSET 0x38U // Offset in the current resource object to the name-to-payload map passed to the lookup helper...
#define DEBUG_CONSOLE_DEBUG_TEXT_VISIBLE_OFFSET 0xE8U // Offset in the debug-console object to the debug text visibility flag...
#define FURNITURE_APPEND_CAPTURE_HOTKEY VK_F8
#define FURNITURE_OVERWRITE_CAPTURE_HOTKEY VK_F8
#define FURNITURE_VANILLA_NAMES_DUMP_HOTKEY VK_F9
#define FURNITURE_MAGIC_VALUE 1U
#define FURNITURE_RECORD_BASE_BYTES 588U
#define FURNITURE_RECORD_METADATA_BYTES 8U
#define MAX_PATH_BUFFER 4096U
#define MAX_FURNITURE_NAME_BYTES 512U
#define FURNITURE_EDITOR_DEBUG_TEXT_FILE "furniture_editor_debug_text.txt"
#define FURNITURE_DEBUG_TOGGLE_HOTKEY VK_F7
#define FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY 8U
#define FURNITURE_DEBUG_RUNTIME_MESSAGE_TTL_MS 8000ULL
#define FURNITURE_DEBUG_AUTO_SHOW_REENTRY_MS 5000ULL
#define FURNITURE_DEBUG_PENDING_FORCE_VALID_MS 1000ULL

typedef struct MsvcString
{
    union
    {
        char Small[16];
        char* Heap;
    } Data;
    size_t Length;
    size_t Capacity;
} MsvcString;

typedef struct PackedFileData
{
    MsvcString Name;
    uint8_t* Data;
    uint32_t Size;
    uint8_t OwnsData;
    uint8_t Padding[3];
} PackedFileData;

typedef PackedFileData* (__fastcall *fn_packed_file_load)(void* context, MsvcString* path, uint8_t useCache, uint8_t allowFallback);
typedef void (__fastcall *fn_furniture_editor_update)(void* editor);
typedef uint8_t* (__fastcall *fn_furniture_payload_lookup)(void* payloadMap, MsvcString* name);
typedef void (__fastcall *fn_debug_text_add)(MsvcString* text);
typedef void (__fastcall *fn_debug_text_add_pair)(void* context, MsvcString* label, MsvcString* value);
typedef void (__fastcall *fn_debug_console_visibility_update)(void* debugConsole);
typedef void* (__fastcall *fn_game_allocate)(size_t size);
typedef void (__fastcall *fn_msvc_string_destruct)(MsvcString* value);

#define FIA_STATIC_ASSERT(expression, name) typedef char name[(expression) ? 1 : -1]

FIA_STATIC_ASSERT(sizeof(MsvcString) == 32, fia_static_assert_msvc_string_size);
FIA_STATIC_ASSERT(offsetof(PackedFileData, Data) == 0x20, fia_static_assert_packed_file_data_data_offset);
FIA_STATIC_ASSERT(offsetof(PackedFileData, Size) == 0x28, fia_static_assert_packed_file_data_size_offset);
FIA_STATIC_ASSERT(offsetof(PackedFileData, OwnsData) == 0x2C, fia_static_assert_packed_file_data_owns_data_offset);

#undef FIA_STATIC_ASSERT

#endif