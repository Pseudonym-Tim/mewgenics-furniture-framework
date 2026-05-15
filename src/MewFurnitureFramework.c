#include "MewFurnitureFramework.h"
#include "mewjector.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MewjectorAPI g_mj;
static HMODULE g_moduleHandle = NULL;
static fn_packed_file_load g_originalPackedFileLoad = NULL;
static fn_furniture_editor_update g_originalFurnitureEditorUpdate = NULL;
static fn_shop_init g_originalShopInit = NULL;
static fn_shop_update g_originalShopUpdate = NULL;
static fn_shop_init_shop g_shopInitShop = NULL;
static fn_shop_refresh_shop_item g_shopRefreshShopItem = NULL;
static fn_furniture_price_lookup g_furniturePriceLookup = NULL;
static void** g_furnitureEffectsGlobal = NULL;
static fn_furniture_payload_lookup g_furniturePayloadLookup = NULL;
static fn_debug_text_add g_originalDebugTextAdd = NULL;
static fn_debug_text_add_pair g_originalDebugTextAddPair = NULL;
static fn_debug_console_visibility_update g_originalDebugConsoleVisibilityUpdate = NULL;
static fn_game_allocate g_gameAllocate = NULL;
static fn_msvc_string_destruct g_msvcStringDestruct = NULL;
static UINT_PTR g_gameBase = 0U;
static volatile LONG g_dumpInProgress = 0;
static uint8_t g_lastAppendCaptureChordDown = 0U;
static uint8_t g_lastOverwriteCaptureChordDown = 0U;
static uint8_t g_lastVanillaDumpChordDown = 0U;
static uint8_t g_lastDebugToggleChordDown = 0U;
static uint8_t g_lastJackShopRefreshChordDown = 0U;
static uint8_t g_lastJackShopGuaranteeChordDown = 0U;
static uint8_t* g_vanillaFurnitureData = NULL;
static uint32_t g_vanillaFurnitureDataSize = 0U;
static uint8_t* g_currentFurnitureData = NULL;
static uint32_t g_currentFurnitureDataSize = 0U;
static uint32_t g_jackShopRandomState = 0U;
static volatile LONG g_installed = 0;
static volatile LONG g_patchStarted = 0;
static volatile LONG g_patchFinished = 0;
static volatile LONG g_inFurnitureEditorUpdate = 0;
static volatile LONG g_furnitureEditorInstructionsPrintedThisUpdate = 0;
static volatile LONG g_pendingDebugOverlayForce = 0;
static void* g_lastDebugConsole = NULL;
static void* g_lastFurnitureEditor = NULL;
static void* g_lastJackShop = NULL;
static ULONGLONG g_lastFurnitureEditorUpdateTick = 0ULL;

/*
    furniture_info.data format:
    u32 magic/version = 1
    u32 rowCount
    row[rowCount]: u32 nameByteCount, u32 zeroOrFlags, char name[nameByteCount], byte payload[580]
    (Whole row blob is nameByteCount + 588 bytes, including the 8-byte name header)
*/

typedef struct BufferBuilder
{
    uint8_t* Data;
    size_t Size;
    size_t Capacity;
} BufferBuilder;

typedef struct NameEntry
{
    char* Name;
    uint32_t Length;
} NameEntry;

typedef struct NameSet
{
    NameEntry* Entries;
    uint32_t Count;
    uint32_t Capacity;
} NameSet;

typedef struct PathSet
{
    char** Paths;
    uint32_t Count;
    uint32_t Capacity;
} PathSet;

typedef struct FileBytes
{
    uint8_t* Data;
    uint32_t Size;
} FileBytes;

typedef struct ParsedFurnitureFile
{
    const uint8_t* Data;
    uint32_t Size;
    uint32_t Magic;
    uint32_t RowCount;
} ParsedFurnitureFile;

typedef struct FurnitureDebugTextLine
{
    char* Text;
    size_t Length;
} FurnitureDebugTextLine;

typedef enum FurnitureDebugMessageId
{
    FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN = 0,
    FURNITURE_DEBUG_MESSAGE_OVERLAY_HIDDEN,
    FURNITURE_DEBUG_MESSAGE_CAPTURE_SAVED,
    FURNITURE_DEBUG_MESSAGE_CAPTURE_OVERWRITTEN,
    FURNITURE_DEBUG_MESSAGE_CAPTURE_READ_FAILED,
    FURNITURE_DEBUG_MESSAGE_CAPTURE_PATH_FAILED,
    FURNITURE_DEBUG_MESSAGE_CAPTURE_WRITE_FAILED,
    FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_SUCCESS,
    FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_MISSING,
    FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_INVALID,
    FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_WRITE_FAILED,
    FURNITURE_DEBUG_MESSAGE_JACK_SHOP_REFRESHED,
    FURNITURE_DEBUG_MESSAGE_JACK_SHOP_INJECTED,
    FURNITURE_DEBUG_MESSAGE_COUNT
} FurnitureDebugMessageId;

typedef struct FurnitureDebugTextState
{
    char* TextBuffer;
    uint32_t TextBufferSize;
    FurnitureDebugTextLine* Lines;
    uint32_t LineCount;
    uint32_t LineCapacity;
    char* InstructionsHeader;
    size_t InstructionsHeaderLength;
    char* LogsHeader;
    size_t LogsHeaderLength;
    char* CurrentPieceText;
    size_t CurrentPieceTextLength;
    char* UnsavedChangesText;
    size_t UnsavedChangesTextLength;
    char* MessageTemplates[FURNITURE_DEBUG_MESSAGE_COUNT];
    size_t MessageTemplateLengths[FURNITURE_DEBUG_MESSAGE_COUNT];
    int HideInstructionsHeader;
    int HideLogsHeader;
    int HideCurrentPiece;
    int HideUnsavedChanges;
    int HideCtrlSSaveHint;
    FILETIME LastWriteTime;
    DWORD LastFileSizeLow;
    DWORD LastFileSizeHigh;
    ULONGLONG LastCheckTick;
    int Loaded;
    int HasFile;
} FurnitureDebugTextState;

typedef struct FurnitureRuntimeDebugMessage
{
    char* Text;
    ULONGLONG ExpireTick;
    FurnitureDebugMessageId MessageId;
} FurnitureRuntimeDebugMessage;

typedef struct FurnitureDebugMessageContext
{
    const char* FurnitureName;
    uint32_t RowCount;
    const char* Path;
    uint32_t NameCount;
    int SlotIndex;
    uint32_t ItemCount;
    int Price;
    int Rare;
    uint32_t ReplacedCount;
} FurnitureDebugMessageContext;

typedef struct JackShopTestConfig
{
    char FurnitureName[MAX_FURNITURE_NAME_BYTES + 1U];
    int SlotIndex;
    int Rare;
    int RefreshBeforeReplace;
} JackShopTestConfig;

static FurnitureDebugTextState g_furnitureDebugTextState;
static FurnitureRuntimeDebugMessage g_runtimeDebugMessages[FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY];

static const char* GetConfiguredFurnitureDebugTextValue(const char* value, size_t length, const char* fallback, size_t* outputLength);
static const char* GetConfiguredFurnitureMessageTemplate(FurnitureDebugMessageId messageId, size_t* outputLength);
static void QueueFurnitureDebugMessage(FurnitureDebugMessageId messageId, const char* furnitureName, uint32_t rowCount, const char* path, uint32_t nameCount);
static void QueueFurnitureDebugMessageEx(FurnitureDebugMessageId messageId, const FurnitureDebugMessageContext* context);
static int InitializeMsvcStringForDebugText(MsvcString* value, const char* text, size_t length);
static void AddFurnitureDebugTextLine(const char* text, size_t length);
static int IsFurnitureDebugTextWhitespace(char value);

static void LogMessage(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;

    if (g_mj.Log == NULL)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    g_mj.Log(MOD_NAME, "%s", buffer);
}

static uint32_t ReadU32Le(const uint8_t* data)
{
    uint32_t value;

    value = ((uint32_t)data[0]) | (((uint32_t)data[1]) << 8) | (((uint32_t)data[2]) << 16) | (((uint32_t)data[3]) << 24);
    return value;
}

static void WriteU32Le(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 16) & 0xFFU);
    data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static int IsSlash(char value)
{
    if (value == '/' || value == '\\')
    {
        return 1;
    }

    return 0;
}

static char ToLowerAscii(char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return (char)(value + ('a' - 'A'));
    }

    return value;
}

static const char* GetMsvcStringChars(const MsvcString* value)
{
    if (value == NULL)
    {
        return NULL;
    }

    if (value->Capacity <= 15U)
    {
        return value->Data.Small;
    }

    return value->Data.Heap;
}

static int CopyMsvcString(char* output, size_t outputSize, const MsvcString* value)
{
    const char* input;
    size_t copySize;

    if (output == NULL || outputSize == 0U || value == NULL)
    {
        return 0;
    }

    input = GetMsvcStringChars(value);

    if (input == NULL)
    {
        output[0] = 0;
        return 0;
    }

    copySize = value->Length;

    if (copySize >= outputSize)
    {
        copySize = outputSize - 1U;
    }

    memcpy(output, input, copySize);
    output[copySize] = 0;
    return 1;
}

static int IsFurnitureInfoPath(const MsvcString* path)
{
    char buffer[MAX_PATH_BUFFER];
    char normalized[MAX_PATH_BUFFER];
    size_t sourceIndex;
    size_t outputIndex;
    const char* suffix;
    size_t suffixLength;
    size_t normalizedLength;

    if (!CopyMsvcString(buffer, sizeof(buffer), path))
    {
        return 0;
    }

    sourceIndex = 0U;
    outputIndex = 0U;

    while (buffer[sourceIndex] != 0 && outputIndex + 1U < sizeof(normalized))
    {
        if (IsSlash(buffer[sourceIndex]))
        {
            normalized[outputIndex] = '/';
        }
        else
        {
            normalized[outputIndex] = ToLowerAscii(buffer[sourceIndex]);
        }

        outputIndex++;
        sourceIndex++;
    }

    normalized[outputIndex] = 0;
    suffix = "data/furniture_info.data";
    suffixLength = strlen(suffix);
    normalizedLength = strlen(normalized);

    if (normalizedLength < suffixLength)
    {
        return 0;
    }

    if (strcmp(normalized + normalizedLength - suffixLength, suffix) == 0)
    {
        return 1;
    }

    if (strcmp(normalized, "furniture_info.data") == 0)
    {
        return 1;
    }

    return 0;
}

static void TrimToDirectory(char* path)
{
    size_t length;

    if (path == NULL)
    {
        return;
    }

    length = strlen(path);

    while (length > 0U)
    {
        length--;

        if (IsSlash(path[length]))
        {
            path[length] = 0;
            return;
        }
    }

    path[0] = 0;
}

static int GetParentDirectory(const char* path, char* output, size_t outputSize)
{
    size_t length;

    if (path == NULL || output == NULL || outputSize == 0U)
    {
        return 0;
    }

    strncpy(output, path, outputSize - 1U);
    output[outputSize - 1U] = 0;
    length = strlen(output);

    while (length > 0U && IsSlash(output[length - 1U]))
    {
        output[length - 1U] = 0;
        length--;
    }

    TrimToDirectory(output);
    return output[0] != 0;
}

static void JoinPath(char* output, size_t outputSize, const char* left, const char* right)
{
    size_t leftLength;

    if (output == NULL || outputSize == 0U)
    {
        return;
    }

    output[0] = 0;

    if (left == NULL || right == NULL)
    {
        return;
    }

    snprintf(output, outputSize, "%s", left);
    leftLength = strlen(output);

    if (leftLength > 0U && !IsSlash(output[leftLength - 1U]) && leftLength + 1U < outputSize)
    {
        output[leftLength] = '\\';
        output[leftLength + 1U] = 0;
    }

    strncat(output, right, outputSize - strlen(output) - 1U);
}

static int FileExists(const char* path)
{
    DWORD attributes;

    if (path == NULL || path[0] == 0)
    {
        return 0;
    }

    attributes = GetFileAttributesA(path);

    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return 0;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
    {
        return 0;
    }

    return 1;
}

static int DirectoryExists(const char* path)
{
    DWORD attributes;

    if (path == NULL || path[0] == 0)
    {
        return 0;
    }

    attributes = GetFileAttributesA(path);

    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return 0;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
    {
        return 0;
    }

    return 1;
}

static char* DuplicateString(const char* value)
{
    size_t length;
    char* copy;

    if (value == NULL)
    {
        return NULL;
    }

    length = strlen(value);
    copy = (char*)HeapAlloc(GetProcessHeap(), 0, length + 1U);

    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, value, length + 1U);
    return copy;
}

static int PathSetContains(const PathSet* set, const char* path)
{
    uint32_t index;

    if (set == NULL || path == NULL)
    {
        return 0;
    }

    for (index = 0U; index < set->Count; index++)
    {
        if (_stricmp(set->Paths[index], path) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int PathSetAdd(PathSet* set, const char* path)
{
    char** resizedPaths;
    uint32_t newCapacity;
    char* copiedPath;

    if (set == NULL || path == NULL || path[0] == 0)
    {
        return 0;
    }

    if (PathSetContains(set, path))
    {
        return 1;
    }

    if (set->Count == set->Capacity)
    {
        newCapacity = set->Capacity == 0U ? 16U : set->Capacity * 2U;

        if (set->Paths == NULL)
        {
            resizedPaths = (char**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(char*) * newCapacity);
        }
        else
        {
            resizedPaths = (char**)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, set->Paths, sizeof(char*) * newCapacity);
        }

        if (resizedPaths == NULL)
        {
            return 0;
        }

        set->Paths = resizedPaths;
        set->Capacity = newCapacity;
    }

    copiedPath = DuplicateString(path);

    if (copiedPath == NULL)
    {
        return 0;
    }

    set->Paths[set->Count] = copiedPath;
    set->Count++;
    return 1;
}

static void PathSetFree(PathSet* set)
{
    uint32_t index;

    if (set == NULL)
    {
        return;
    }

    for (index = 0U; index < set->Count; index++)
    {
        HeapFree(GetProcessHeap(), 0, set->Paths[index]);
    }

    HeapFree(GetProcessHeap(), 0, set->Paths);
    memset(set, 0, sizeof(PathSet));
}

static int NameEquals(const char* left, uint32_t leftLength, const char* right, uint32_t rightLength)
{
    uint32_t index;

    if (leftLength != rightLength)
    {
        return 0;
    }

    for (index = 0U; index < leftLength; index++)
    {
        if (ToLowerAscii(left[index]) != ToLowerAscii(right[index]))
        {
            return 0;
        }
    }

    return 1;
}

static int NameSetContains(const NameSet* set, const char* name, uint32_t nameLength)
{
    uint32_t index;

    if (set == NULL || name == NULL)
    {
        return 0;
    }

    for (index = 0U; index < set->Count; index++)
    {
        if (NameEquals(set->Entries[index].Name, set->Entries[index].Length, name, nameLength))
        {
            return 1;
        }
    }

    return 0;
}

static int NameSetAdd(NameSet* set, const char* name, uint32_t nameLength)
{
    NameEntry* resizedEntries;
    uint32_t newCapacity;
    char* copiedName;

    if (set == NULL || name == NULL || nameLength == 0U)
    {
        return 0;
    }

    if (NameSetContains(set, name, nameLength))
    {
        return 1;
    }

    if (set->Count == set->Capacity)
    {
        newCapacity = set->Capacity == 0U ? 1024U : set->Capacity * 2U;

        if (set->Entries == NULL)
        {
            resizedEntries = (NameEntry*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(NameEntry) * newCapacity);
        }
        else
        {
            resizedEntries = (NameEntry*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, set->Entries, sizeof(NameEntry) * newCapacity);
        }

        if (resizedEntries == NULL)
        {
            return 0;
        }

        set->Entries = resizedEntries;
        set->Capacity = newCapacity;
    }

    copiedName = (char*)HeapAlloc(GetProcessHeap(), 0, (size_t)nameLength + 1U);

    if (copiedName == NULL)
    {
        return 0;
    }

    memcpy(copiedName, name, nameLength);
    copiedName[nameLength] = 0;
    set->Entries[set->Count].Name = copiedName;
    set->Entries[set->Count].Length = nameLength;
    set->Count++;
    return 1;
}

static void NameSetFree(NameSet* set)
{
    uint32_t index;

    if (set == NULL)
    {
        return;
    }

    for (index = 0U; index < set->Count; index++)
    {
        HeapFree(GetProcessHeap(), 0, set->Entries[index].Name);
    }

    HeapFree(GetProcessHeap(), 0, set->Entries);
    memset(set, 0, sizeof(NameSet));
}

static int BufferEnsureCapacity(BufferBuilder* builder, size_t neededSize)
{
    uint8_t* resizedData;
    size_t newCapacity;

    if (builder == NULL)
    {
        return 0;
    }

    if (neededSize <= builder->Capacity)
    {
        return 1;
    }

    newCapacity = builder->Capacity == 0U ? 4096U : builder->Capacity;

    while (newCapacity < neededSize)
    {
        newCapacity *= 2U;
    }

    if (builder->Data == NULL)
    {
        resizedData = (uint8_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newCapacity);
    }
    else
    {
        resizedData = (uint8_t*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, builder->Data, newCapacity);
    }

    if (resizedData == NULL)
    {
        return 0;
    }

    builder->Data = resizedData;
    builder->Capacity = newCapacity;
    return 1;
}

static int BufferAppend(BufferBuilder* builder, const void* data, size_t size)
{
    if (builder == NULL || data == NULL)
    {
        return 0;
    }

    if (size == 0U)
    {
        return 1;
    }

    if (!BufferEnsureCapacity(builder, builder->Size + size))
    {
        return 0;
    }

    memcpy(builder->Data + builder->Size, data, size);
    builder->Size += size;
    return 1;
}

static void BufferFree(BufferBuilder* builder)
{
    if (builder == NULL)
    {
        return;
    }

    HeapFree(GetProcessHeap(), 0, builder->Data);
    memset(builder, 0, sizeof(BufferBuilder));
}

static int ParseFurnitureFile(const uint8_t* data, uint32_t size, ParsedFurnitureFile* parsed)
{
    uint32_t rowIndex;
    uint32_t rowCount;
    uint32_t magic;
    uint32_t nameLength;
    size_t offset;
    size_t rowSize;

    if (data == NULL || parsed == NULL || size < 8U)
    {
        return 0;
    }

    magic = ReadU32Le(data);
    rowCount = ReadU32Le(data + 4U);

    if (magic != FURNITURE_MAGIC_VALUE)
    {
        return 0;
    }

    offset = 8U;

    for (rowIndex = 0U; rowIndex < rowCount; rowIndex++)
    {
        if (offset + FURNITURE_RECORD_METADATA_BYTES > size)
        {
            return 0;
        }

        nameLength = ReadU32Le(data + offset);

        if (nameLength == 0U || nameLength > MAX_FURNITURE_NAME_BYTES)
        {
            return 0;
        }

        rowSize = (size_t)nameLength + FURNITURE_RECORD_BASE_BYTES;

        if (offset + rowSize > size)
        {
            return 0;
        }

        offset += rowSize;
    }

    if (offset != size)
    {
        return 0;
    }

    parsed->Data = data;
    parsed->Size = size;
    parsed->Magic = magic;
    parsed->RowCount = rowCount;
    return 1;
}

static int CollectNamesFromFurnitureFile(const ParsedFurnitureFile* file, NameSet* names)
{
    uint32_t rowIndex;
    uint32_t nameLength;
    const char* name;
    size_t offset;
    size_t rowSize;

    if (file == NULL || names == NULL)
    {
        return 0;
    }

    offset = 8U;

    for (rowIndex = 0U; rowIndex < file->RowCount; rowIndex++)
    {
        nameLength = ReadU32Le(file->Data + offset);
        name = (const char*)(file->Data + offset + FURNITURE_RECORD_METADATA_BYTES);

        if (!NameSetAdd(names, name, nameLength))
        {
            return 0;
        }

        rowSize = (size_t)nameLength + FURNITURE_RECORD_BASE_BYTES;
        offset += rowSize;
    }

    return 1;
}

static int AppendNewRowsFromFurnitureFile(const ParsedFurnitureFile* file, NameSet* names, BufferBuilder* output, uint32_t* addedRows)
{
    uint32_t rowIndex;
    uint32_t nameLength;
    const char* name;
    const uint8_t* rowData;
    size_t offset;
    size_t rowSize;

    if (file == NULL || names == NULL || output == NULL || addedRows == NULL)
    {
        return 0;
    }

    offset = 8U;

    for (rowIndex = 0U; rowIndex < file->RowCount; rowIndex++)
    {
        nameLength = ReadU32Le(file->Data + offset);
        name = (const char*)(file->Data + offset + FURNITURE_RECORD_METADATA_BYTES);
        rowData = file->Data + offset;
        rowSize = (size_t)nameLength + FURNITURE_RECORD_BASE_BYTES;

        if (!NameSetContains(names, name, nameLength))
        {
            if (!BufferAppend(output, rowData, rowSize))
            {
                return 0;
            }

            if (!NameSetAdd(names, name, nameLength))
            {
                return 0;
            }

            *addedRows += 1U;
            LogMessage("Queued furniture_info row: %.*s", (int)nameLength, name);
        }

        offset += rowSize;
    }

    return 1;
}

static int ReadWholeFile(const char* path, FileBytes* output)
{
    HANDLE fileHandle;
    LARGE_INTEGER fileSize;
    DWORD bytesRead;
    uint8_t* data;

    if (path == NULL || output == NULL)
    {
        return 0;
    }

    memset(output, 0, sizeof(FileBytes));
    fileHandle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 0x7FFFFFFFLL)
    {
        CloseHandle(fileHandle);
        return 0;
    }

    data = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, (size_t)fileSize.QuadPart + 1U);

    if (data == NULL)
    {
        CloseHandle(fileHandle);
        return 0;
    }

    bytesRead = 0U;

    if (!ReadFile(fileHandle, data, (DWORD)fileSize.QuadPart, &bytesRead, NULL) || bytesRead != (DWORD)fileSize.QuadPart)
    {
        HeapFree(GetProcessHeap(), 0, data);
        CloseHandle(fileHandle);
        return 0;
    }

    CloseHandle(fileHandle);
    data[bytesRead] = 0;
    output->Data = data;
    output->Size = (uint32_t)bytesRead;
    return 1;
}

static void FreeFileBytes(FileBytes* file)
{
    if (file == NULL)
    {
        return;
    }

    HeapFree(GetProcessHeap(), 0, file->Data);
    memset(file, 0, sizeof(FileBytes));
}

static int StoreVanillaFurnitureDataSnapshot(const uint8_t* data, uint32_t size)
{
    uint8_t* copiedData;

    if (data == NULL || size == 0U)
    {
        return 0;
    }

    copiedData = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, size);

    if (copiedData == NULL)
    {
        return 0;
    }

    memcpy(copiedData, data, size);

    if (g_vanillaFurnitureData != NULL)
    {
        HeapFree(GetProcessHeap(), 0, g_vanillaFurnitureData);
    }

    g_vanillaFurnitureData = copiedData;
    g_vanillaFurnitureDataSize = size;
    return 1;
}

static int StoreCurrentFurnitureDataSnapshot(const uint8_t* data, uint32_t size)
{
    uint8_t* copiedData;

    if (data == NULL || size == 0U)
    {
        return 0;
    }

    copiedData = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, size);

    if (copiedData == NULL)
    {
        return 0;
    }

    memcpy(copiedData, data, size);

    if (g_currentFurnitureData != NULL)
    {
        HeapFree(GetProcessHeap(), 0, g_currentFurnitureData);
    }

    g_currentFurnitureData = copiedData;
    g_currentFurnitureDataSize = size;
    return 1;
}

static int WriteWholeFileAtomic(const char* path, const uint8_t* data, uint32_t size)
{
    char tempPath[MAX_PATH_BUFFER];
    HANDLE fileHandle;
    DWORD bytesWritten;
    DWORD moveFlags;

    if (path == NULL || data == NULL || size == 0U)
    {
        return 0;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    fileHandle = CreateFileA(tempPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    bytesWritten = 0U;

    if (!WriteFile(fileHandle, data, size, &bytesWritten, NULL) || bytesWritten != size)
    {
        CloseHandle(fileHandle);
        DeleteFileA(tempPath);
        return 0;
    }

    FlushFileBuffers(fileHandle);
    CloseHandle(fileHandle);

    moveFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;

    if (!MoveFileExA(tempPath, path, moveFlags))
    {
        DeleteFileA(tempPath);
        return 0;
    }

    return 1;
}

static int EnsureDirectoryExists(const char* path)
{
    if (path == NULL || path[0] == 0)
    {
        return 0;
    }

    if (DirectoryExists(path))
    {
        return 1;
    }

    if (CreateDirectoryA(path, NULL))
    {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS && DirectoryExists(path))
    {
        return 1;
    }

    return 0;
}

static int GetFrameworkRootDirectory(char* output, size_t outputSize)
{
    char dllPath[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;
    dllPath[0] = 0;

    if (GetModuleFileNameA(g_moduleHandle, dllPath, sizeof(dllPath)) == 0U)
    {
        return 0;
    }

    strncpy(output, dllPath, outputSize - 1U);
    output[outputSize - 1U] = 0;
    TrimToDirectory(output);
    return output[0] != 0;
}

static int GetDllOutputDirectory(char* output, size_t outputSize)
{
    char dllDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetFrameworkRootDirectory(dllDirectory, sizeof(dllDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, dllDirectory, "output");

    if (!EnsureDirectoryExists(output))
    {
        output[0] = 0;
        return 0;
    }

    return output[0] != 0;
}

static int GetCaptureOutputPath(char* output, size_t outputSize)
{
    char outputDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetDllOutputDirectory(outputDirectory, sizeof(outputDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, outputDirectory, "furniture_info.data.append");
    return output[0] != 0;
}

static int GetVanillaFurnitureNamesLoadPath(char* output, size_t outputSize)
{
    char dllDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetFrameworkRootDirectory(dllDirectory, sizeof(dllDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, dllDirectory, "vanilla_furniture_names.txt");
    return output[0] != 0;
}

static int GetFurnitureDebugTextLoadPath(char* output, size_t outputSize)
{
    char dllDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetFrameworkRootDirectory(dllDirectory, sizeof(dllDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, dllDirectory, FURNITURE_EDITOR_DEBUG_TEXT_FILE);
    return output[0] != 0;
}

static int GetVanillaFurnitureNamesDumpOutputPath(char* output, size_t outputSize)
{
    char dataDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetDllOutputDirectory(dataDirectory, sizeof(dataDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, dataDirectory, "vanilla_furniture_names_dump.txt");
    return output[0] != 0;
}

static int IsNameTextWhitespace(char value)
{
    if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
    {
        return 1;
    }

    return 0;
}

static int AddFurnitureNameLineToSet(NameSet* names, const char* line, uint32_t lineLength, uint32_t* loadedNames)
{
    uint32_t beginIndex;
    uint32_t endIndex;
    const char* name;
    uint32_t nameLength;

    if (names == NULL || line == NULL || loadedNames == NULL)
    {
        return 0;
    }

    beginIndex = 0U;
    endIndex = lineLength;

    while (beginIndex < endIndex && IsNameTextWhitespace(line[beginIndex]))
    {
        beginIndex++;
    }

    while (endIndex > beginIndex && IsNameTextWhitespace(line[endIndex - 1U]))
    {
        endIndex--;
    }

    if (beginIndex == endIndex)
    {
        return 1;
    }

    if (line[beginIndex] == '#')
    {
        return 1;
    }

    name = line + beginIndex;
    nameLength = endIndex - beginIndex;

    if (nameLength > MAX_FURNITURE_NAME_BYTES)
    {
        LogMessage("Ignored super long vanilla furniture name from text file: %u bytes", nameLength);
        return 1;
    }

    if (!NameSetContains(names, name, nameLength))
    {
        if (!NameSetAdd(names, name, nameLength))
        {
            return 0;
        }

        *loadedNames += 1U;
    }

    return 1;
}

static int LoadVanillaFurnitureNamesIfExists(NameSet* names)
{
    FileBytes fileBytes;
    char inputPath[MAX_PATH_BUFFER];
    uint32_t lineStart;
    uint32_t index;
    uint32_t loadedNames;

    if (names == NULL)
    {
        return 0;
    }

    memset(&fileBytes, 0, sizeof(fileBytes));
    inputPath[0] = 0;
    loadedNames = 0U;

    if (!GetVanillaFurnitureNamesLoadPath(inputPath, sizeof(inputPath)))
    {
        LogMessage("Could not resolve vanilla furniture name text path, continuing without it!");
        return 1;
    }

    if (!FileExists(inputPath))
    {
        LogMessage("No vanilla furniture name text file found, continuing without it: %s", inputPath);
        return 1;
    }

    if (!ReadWholeFile(inputPath, &fileBytes))
    {
        LogMessage("Could not read vanilla furniture name text file, continuing without it: %s", inputPath);
        return 1;
    }

    lineStart = 0U;

    for (index = 0U; index < fileBytes.Size; index++)
    {
        if (fileBytes.Data[index] == '\n')
        {
            if (!AddFurnitureNameLineToSet(names, (const char*)fileBytes.Data + lineStart, index - lineStart, &loadedNames))
            {
                FreeFileBytes(&fileBytes);
                return 0;
            }

            lineStart = index + 1U;
        }
    }

    if (lineStart < fileBytes.Size)
    {
        if (!AddFurnitureNameLineToSet(names, (const char*)fileBytes.Data + lineStart, fileBytes.Size - lineStart, &loadedNames))
        {
            FreeFileBytes(&fileBytes);
            return 0;
        }
    }

    LogMessage("Loaded vanilla furniture name text file: %s (%u additional names)", inputPath, loadedNames);
    FreeFileBytes(&fileBytes);
    return 1;
}

static int WriteVanillaFurnitureNameDump(const ParsedFurnitureFile* file, char* writtenPath, size_t writtenPathSize)
{
    BufferBuilder output;
    char outputPath[MAX_PATH_BUFFER];
    const char* header;
    const char* newline;
    uint32_t rowIndex;
    uint32_t nameLength;
    const char* name;
    size_t offset;
    size_t rowSize;

    if (file == NULL || file->Data == NULL)
    {
        return 0;
    }

    memset(&output, 0, sizeof(output));
    outputPath[0] = 0;
    header = "# Dump generated by MewFurnitureFramework, from the game's packed/loaded furniture_info.data!\r\n\r\n";
    newline = "\r\n";

    if (!GetVanillaFurnitureNamesDumpOutputPath(outputPath, sizeof(outputPath)))
    {
        LogMessage("Could not resolve vanilla furniture name dump path!");
        return 0;
    }

    if (!BufferAppend(&output, header, strlen(header)))
    {
        BufferFree(&output);
        return 0;
    }

    offset = 8U;

    for (rowIndex = 0U; rowIndex < file->RowCount; rowIndex++)
    {
        nameLength = ReadU32Le(file->Data + offset);
        name = (const char*)(file->Data + offset + FURNITURE_RECORD_METADATA_BYTES);
        rowSize = (size_t)nameLength + FURNITURE_RECORD_BASE_BYTES;

        if (!BufferAppend(&output, name, nameLength) || !BufferAppend(&output, newline, strlen(newline)))
        {
            BufferFree(&output);
            return 0;
        }

        offset += rowSize;
    }

    if (output.Size > 0xFFFFFFFFULL)
    {
        BufferFree(&output);
        return 0;
    }

    if (!WriteWholeFileAtomic(outputPath, output.Data, (uint32_t)output.Size))
    {
        LogMessage("Could not write vanilla furniture name dump: %s", outputPath);
        BufferFree(&output);
        return 0;
    }

    if (writtenPath != NULL && writtenPathSize != 0U)
    {
        strncpy(writtenPath, outputPath, writtenPathSize - 1U);
        writtenPath[writtenPathSize - 1U] = 0;
    }

    LogMessage("Wrote vanilla furniture name dump: %s (%u names)", outputPath, file->RowCount);
    BufferFree(&output);
    return 1;
}

static int SaveCapturedRowToAppendFile(const uint8_t* capturedRow, uint32_t capturedRowSize, const char* capturedName, uint32_t capturedNameLength, const char* outputPath, uint32_t* finalRows)
{
    BufferBuilder output;
    FileBytes existingBytes;
    ParsedFurnitureFile existingFile;
    uint8_t header[8];
    uint32_t existingIndex;
    uint32_t existingNameLength;
    const char* existingName;
    const uint8_t* existingRow;
    size_t existingOffset;
    size_t existingRowSize;
    uint32_t rowCount;
    int hasExistingFile;

    if (capturedRow == NULL || capturedRowSize == 0U || capturedName == NULL || capturedNameLength == 0U || outputPath == NULL || finalRows == NULL)
    {
        return 0;
    }

    memset(&output, 0, sizeof(output));
    memset(&existingBytes, 0, sizeof(existingBytes));
    memset(header, 0, sizeof(header));
    WriteU32Le(header, FURNITURE_MAGIC_VALUE);
    WriteU32Le(header + 4U, 0U);

    if (!BufferAppend(&output, header, sizeof(header)))
    {
        BufferFree(&output);
        return 0;
    }

    rowCount = 0U;
    hasExistingFile = 0;

    if (FileExists(outputPath) && ReadWholeFile(outputPath, &existingBytes))
    {
        if (ParseFurnitureFile(existingBytes.Data, existingBytes.Size, &existingFile))
        {
            hasExistingFile = 1;
            existingOffset = 8U;

            for (existingIndex = 0U; existingIndex < existingFile.RowCount; existingIndex++)
            {
                existingNameLength = ReadU32Le(existingFile.Data + existingOffset);
                existingName = (const char*)(existingFile.Data + existingOffset + FURNITURE_RECORD_METADATA_BYTES);
                existingRow = existingFile.Data + existingOffset;
                existingRowSize = (size_t)existingNameLength + FURNITURE_RECORD_BASE_BYTES;

                if (!NameEquals(existingName, existingNameLength, capturedName, capturedNameLength))
                {
                    if (!BufferAppend(&output, existingRow, existingRowSize))
                    {
                        FreeFileBytes(&existingBytes);
                        BufferFree(&output);
                        return 0;
                    }

                    rowCount++;
                }

                existingOffset += existingRowSize;
            }
        }
        else
        {
            LogMessage("Existing append file is invalid, replacing it: %s", outputPath);
        }
    }

    if (!BufferAppend(&output, capturedRow, capturedRowSize))
    {
        FreeFileBytes(&existingBytes);
        BufferFree(&output);
        return 0;
    }

    rowCount++;
    WriteU32Le(output.Data + 4U, rowCount);

    if (!WriteWholeFileAtomic(outputPath, output.Data, (uint32_t)output.Size))
    {
        FreeFileBytes(&existingBytes);
        BufferFree(&output);
        return 0;
    }

    if (hasExistingFile)
    {
        LogMessage("Updated append file: %s", outputPath);
    }
    else
    {
        LogMessage("Created append file: %s", outputPath);
    }

    *finalRows = rowCount;
    FreeFileBytes(&existingBytes);
    BufferFree(&output);
    return 1;
}

static int OverwriteCapturedRowAppendFile(const uint8_t* capturedRow, uint32_t capturedRowSize, const char* outputPath, uint32_t* finalRows)
{
    BufferBuilder output;
    uint8_t header[8];

    if (capturedRow == NULL || capturedRowSize == 0U || outputPath == NULL || finalRows == NULL)
    {
        return 0;
    }

    memset(&output, 0, sizeof(output));
    memset(header, 0, sizeof(header));
    WriteU32Le(header, FURNITURE_MAGIC_VALUE);
    WriteU32Le(header + 4U, 1U);

    if (!BufferAppend(&output, header, sizeof(header)))
    {
        BufferFree(&output);
        return 0;
    }

    if (!BufferAppend(&output, capturedRow, capturedRowSize))
    {
        BufferFree(&output);
        return 0;
    }

    if (!WriteWholeFileAtomic(outputPath, output.Data, (uint32_t)output.Size))
    {
        BufferFree(&output);
        return 0;
    }

    *finalRows = 1U;
    LogMessage("Overwrote append file with one row: %s", outputPath);
    BufferFree(&output);
    return 1;
}

static int GetCurrentFurnitureNameFromEditor(void* editor, MsvcString** nameString, char* nameBuffer, size_t nameBufferSize, uint32_t* nameLength)
{
    uint8_t* editorBytes;
    MsvcString* begin;
    MsvcString* end;
    MsvcString* currentName;
    ptrdiff_t count;
    int currentIndex;

    if (editor == NULL || nameString == NULL || nameBuffer == NULL || nameBufferSize == 0U || nameLength == NULL)
    {
        return 0;
    }

    editorBytes = (uint8_t*)editor;
    begin = *(MsvcString**)(editorBytes + FURNITURE_EDITOR_NAME_VECTOR_OFFSET);
    end = *(MsvcString**)(editorBytes + FURNITURE_EDITOR_NAME_VECTOR_OFFSET + sizeof(void*));
    currentIndex = *(int*)(editorBytes + FURNITURE_EDITOR_CURRENT_INDEX_OFFSET);

    if (begin == NULL || end == NULL || end < begin || currentIndex < 0)
    {
        return 0;
    }

    count = end - begin;

    if ((ptrdiff_t)currentIndex >= count)
    {
        return 0;
    }

    currentName = begin + currentIndex;

    if (currentName->Length == 0U || currentName->Length > MAX_FURNITURE_NAME_BYTES || currentName->Length >= nameBufferSize)
    {
        return 0;
    }

    if (!CopyMsvcString(nameBuffer, nameBufferSize, currentName))
    {
        return 0;
    }

    *nameString = currentName;
    *nameLength = (uint32_t)currentName->Length;
    return 1;
}

static int IsFurnitureEditorReadyForDebugInfo(void* editor)
{
    MsvcString* nameString;
    char nameBuffer[MAX_FURNITURE_NAME_BYTES + 1U];
    uint32_t nameLength;
    void* currentResource;

    if (editor == NULL)
    {
        return 0;
    }

    nameString = NULL;
    nameBuffer[0] = 0;
    nameLength = 0U;

    if (!GetCurrentFurnitureNameFromEditor(editor, &nameString, nameBuffer, sizeof(nameBuffer), &nameLength))
    {
        return 0;
    }

    currentResource = *(void**)((uint8_t*)editor + FURNITURE_EDITOR_CURRENT_RESOURCE_OFFSET);

    if (currentResource == NULL)
    {
        return 0;
    }

    return 1;
}

static int BuildCapturedRowFromEditor(void* editor, uint8_t** capturedRow, uint32_t* capturedRowSize, char* capturedName, size_t capturedNameSize, uint32_t* capturedNameLength)
{
    uint8_t* editorBytes;
    void* currentResource;
    void* payloadMap;
    MsvcString* nameString;
    uint8_t* payload;
    uint8_t* row;
    uint32_t rowSize;

    if (editor == NULL || capturedRow == NULL || capturedRowSize == NULL || capturedName == NULL || capturedNameLength == NULL)
    {
        return 0;
    }

    *capturedRow = NULL;
    *capturedRowSize = 0U;
    *capturedNameLength = 0U;

    if (g_furniturePayloadLookup == NULL)
    {
        return 0;
    }

    if (!GetCurrentFurnitureNameFromEditor(editor, &nameString, capturedName, capturedNameSize, capturedNameLength))
    {
        return 0;
    }

    editorBytes = (uint8_t*)editor;
    currentResource = *(void**)(editorBytes + FURNITURE_EDITOR_CURRENT_RESOURCE_OFFSET);

    if (currentResource == NULL)
    {
        return 0;
    }

    payloadMap = (void*)((uint8_t*)currentResource + FURNITURE_RESOURCE_PAYLOAD_MAP_OFFSET);
    payload = g_furniturePayloadLookup(payloadMap, nameString);

    if (payload == NULL)
    {
        return 0;
    }

    rowSize = *capturedNameLength + FURNITURE_RECORD_BASE_BYTES;
    row = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, rowSize);

    if (row == NULL)
    {
        return 0;
    }

    WriteU32Le(row, *capturedNameLength);
    WriteU32Le(row + 4U, 0U);
    memcpy(row + FURNITURE_RECORD_METADATA_BYTES, capturedName, *capturedNameLength);
    memcpy(row + FURNITURE_RECORD_METADATA_BYTES + *capturedNameLength, payload, FURNITURE_RECORD_BASE_BYTES - FURNITURE_RECORD_METADATA_BYTES);

    *capturedRow = row;
    *capturedRowSize = rowSize;
    return 1;
}

static int IsKeyDown(int virtualKey)
{
    if ((GetAsyncKeyState(virtualKey) & 0x8000) != 0)
    {
        return 1;
    }

    return 0;
}

static int IsControlDown(void)
{
    if (IsKeyDown(VK_CONTROL) || IsKeyDown(VK_LCONTROL) || IsKeyDown(VK_RCONTROL))
    {
        return 1;
    }

    return 0;
}

static int IsShiftDown(void)
{
    if (IsKeyDown(VK_SHIFT) || IsKeyDown(VK_LSHIFT) || IsKeyDown(VK_RSHIFT))
    {
        return 1;
    }

    return 0;
}

static int IsAppendCaptureHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)(IsKeyDown(FURNITURE_APPEND_CAPTURE_HOTKEY) && IsControlDown() && !IsShiftDown());
    pressed = chordDown != 0U && g_lastAppendCaptureChordDown == 0U;
    g_lastAppendCaptureChordDown = chordDown;
    return pressed;
}

static int IsOverwriteCaptureHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)(IsKeyDown(FURNITURE_OVERWRITE_CAPTURE_HOTKEY) && IsControlDown() && IsShiftDown());
    pressed = chordDown != 0U && g_lastOverwriteCaptureChordDown == 0U;
    g_lastOverwriteCaptureChordDown = chordDown;
    return pressed;
}

static int IsVanillaDumpHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)(IsKeyDown(FURNITURE_VANILLA_NAMES_DUMP_HOTKEY) && IsControlDown() && !IsShiftDown());
    pressed = chordDown != 0U && g_lastVanillaDumpChordDown == 0U;
    g_lastVanillaDumpChordDown = chordDown;
    return pressed;
}

static int IsDebugToggleHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)IsKeyDown(FURNITURE_DEBUG_TOGGLE_HOTKEY);
    pressed = chordDown != 0U && g_lastDebugToggleChordDown == 0U;
    g_lastDebugToggleChordDown = chordDown;
    return pressed;
}

static void TryCaptureCurrentFurniture(void* editor, int overwriteAppendFile)
{
    uint8_t* capturedRow;
    uint32_t capturedRowSize;
    char capturedName[MAX_FURNITURE_NAME_BYTES + 1U];
    uint32_t capturedNameLength;
    char outputPath[MAX_PATH_BUFFER];
    uint32_t finalRows;
    int saveSucceeded;

    if (editor == NULL)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_dumpInProgress, 1, 0) != 0)
    {
        return;
    }

    capturedRow = NULL;
    capturedRowSize = 0U;
    capturedName[0] = 0;
    capturedNameLength = 0U;
    outputPath[0] = 0;
    finalRows = 0U;
    saveSucceeded = 0;

    if (!BuildCapturedRowFromEditor(editor, &capturedRow, &capturedRowSize, capturedName, sizeof(capturedName), &capturedNameLength))
    {
        LogMessage("Capture failed: Could not read the current furniture editor row!");
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_CAPTURE_READ_FAILED, NULL, 0U, NULL, 0U);
        InterlockedExchange(&g_dumpInProgress, 0);
        return;
    }

    if (!GetCaptureOutputPath(outputPath, sizeof(outputPath)))
    {
        LogMessage("Capture failed: Could not resolve the output path!");
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_CAPTURE_PATH_FAILED, capturedName, 0U, NULL, 0U);
        HeapFree(GetProcessHeap(), 0, capturedRow);
        InterlockedExchange(&g_dumpInProgress, 0);
        return;
    }

    if (overwriteAppendFile)
    {
        saveSucceeded = OverwriteCapturedRowAppendFile(capturedRow, capturedRowSize, outputPath, &finalRows);
    }
    else
    {
        saveSucceeded = SaveCapturedRowToAppendFile(capturedRow, capturedRowSize, capturedName, capturedNameLength, outputPath, &finalRows);
    }

    if (saveSucceeded)
    {
        if (overwriteAppendFile)
        {
            LogMessage("Overwrote append file with current furniture row: %s", capturedName);
            QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_CAPTURE_OVERWRITTEN, capturedName, finalRows, outputPath, 0U);
        }
        else
        {
            LogMessage("Captured current furniture row: %s (%u rows in append file)!", capturedName, finalRows);
            QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_CAPTURE_SAVED, capturedName, finalRows, outputPath, 0U);
        }
    }
    else
    {
        LogMessage("Capture failed while writing: %s", outputPath);
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_CAPTURE_WRITE_FAILED, capturedName, 0U, outputPath, 0U);
    }

    HeapFree(GetProcessHeap(), 0, capturedRow);
    InterlockedExchange(&g_dumpInProgress, 0);
}

static void TryDumpVanillaFurnitureNames(void)
{
    ParsedFurnitureFile file;
    char outputPath[MAX_PATH_BUFFER];

    if (InterlockedCompareExchange(&g_dumpInProgress, 1, 0) != 0)
    {
        return;
    }

    outputPath[0] = 0;

    if (g_vanillaFurnitureData == NULL || g_vanillaFurnitureDataSize == 0U)
    {
        LogMessage("Vanilla furniture name dump failed: Vanilla furniture_info.data has not been loaded yet!");
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_MISSING, NULL, 0U, NULL, 0U);
        InterlockedExchange(&g_dumpInProgress, 0);
        return;
    }

    if (!ParseFurnitureFile(g_vanillaFurnitureData, g_vanillaFurnitureDataSize, &file))
    {
        LogMessage("Vanilla furniture name dump failed: Cached vanilla data is invalid!");
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_INVALID, NULL, 0U, NULL, 0U);
        InterlockedExchange(&g_dumpInProgress, 0);
        return;
    }

    if (WriteVanillaFurnitureNameDump(&file, outputPath, sizeof(outputPath)))
    {
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_SUCCESS, NULL, 0U, outputPath, file.RowCount);
    }
    else
    {
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_WRITE_FAILED, NULL, 0U, NULL, 0U);
    }

    InterlockedExchange(&g_dumpInProgress, 0);
}

static const char* GetDefaultFurnitureDebugText(void)
{
    return
        "# Lines beginning with @ configure the furniture debug overlay. Non-@ lines become instruction text.\r\n"
        "@instructions_header=--- Mew Furniture Instructions ---\r\n"
        "@logs_header=--- Mew Furniture Logs ---\r\n"
        "@current_piece_text=Current Piece\r\n"
        "@unsaved_changes_text=*UNSAVED CHANGES*\r\n"
        "@hide_current_piece=0\r\n"
        "@hide_unsaved_changes=0\r\n"
        "@hide_instructions_header=0\r\n"
        "@hide_logs_header=0\r\n"
        "@hide_ctrl_s_save_hint=1\r\n"
        "@message_overlay_shown=Debug overlay shown. Press F7 to hide it.\r\n"
        "@message_overlay_hidden=Debug overlay hidden. Press F7 to show it.\r\n"
        "@message_capture_saved=Saved furniture data!\\nFurniture: {furniture}\\nRows in append file: {rows}\\nSaved to:\\n{path}\r\n"
        "@message_capture_overwritten=Overwrote furniture append data!\\nFurniture: {furniture}\\nRows in append file: {rows}\\nSaved to:\\n{path}\r\n"
        "@message_capture_read_failed=Furniture capture failed!\\nCould not read the current furniture editor row. Make sure the furniture editor is open and a furniture entry is selected.\r\n"
        "@message_capture_path_failed=Furniture capture failed!\\nCould not resolve or create the output folder.\r\n"
        "@message_capture_write_failed=Furniture capture failed!\\nCould not write the append file.\\nAttempted path:\\n{path}\r\n"
        "@message_vanilla_dump_success=Dumped vanilla furniture names!\\nNames: {names}\\nSaved to:\\n{path}\r\n"
        "@message_vanilla_dump_missing=Vanilla furniture name dump failed!\\nThe vanilla furniture_info.data has not been loaded yet.\r\n"
        "@message_vanilla_dump_invalid=Vanilla furniture name dump failed!\\nThe cached vanilla furniture data is invalid.\r\n"
        "@message_vanilla_dump_write_failed=Vanilla furniture name dump failed!\\nCould not write vanilla_furniture_names_dump.txt.\r\n"
        "@message_jack_shop_refreshed=Jack shop refreshed!\\nVisible slots: {item_count}\\nFurniture slots rerolled: {replaced}\r\n"
        "@message_jack_shop_injected=Jack shop test furniture injected!\\nFurniture: {furniture}\\nSlot: {slot} of {item_count}\\nCost: {price}\\nRare price path: {rare}\r\n"
        "\r\n"
        "Click / left-click\tDraws black tiles: solid/blocking furniture collision.\r\n"
        "Ctrl + Click\tDraws red tiles: support tiles, required to overlap with a surface tile. Ctrl has priority over the other draw modifiers.\r\n"
        "Shift + Click\tDraws blue tiles: surface tiles, meaning can place stuff on.\r\n"
        "P + Click\tDraws brown tiles: poop-specific placement/logic tiles.\r\n"
        "Tab + Click\tDraws yellow tiles: extra clickable/hitbox areas.\r\n"
        "Right-click\tErases a tile, setting it back to empty.\r\n"
        "Arrow keys\tPrevious / next furniture piece.\r\n"
        "Shift + Arrow\tJump through pieces until an empty tile grid is found.\r\n"
        "Ctrl + Z\tUndo the previous furniture-grid snapshot.\r\n"
        "Exit / close with unsaved edits\tPrompts: Save changes to furniture_info.data?\r\n"
        "F7\tToggle the furniture debug overlay.\r\n"
        "Ctrl + F8\tCapture/update the selected furniture row into the append file.\r\n"
        "Ctrl + Shift + F8\tOverwrite the append file with only the selected furniture row.\r\n"
        "Ctrl + F9\tDump vanilla furniture names.\r\n"
        "Ctrl + F10 / Ctrl + 0\tRefresh Jack's visible shop furniture while Jack's shop is open.\r\n"
        "Ctrl + Shift + F10 / Ctrl + Shift + 0\tReplace a configured Jack shop slot with the furniture in jack_shop_test.txt.\r\n";
}

static int FileTimesEqual(const FILETIME* left, const FILETIME* right)
{
    if (left == NULL || right == NULL)
    {
        return 0;
    }

    if (left->dwLowDateTime == right->dwLowDateTime && left->dwHighDateTime == right->dwHighDateTime)
    {
        return 1;
    }

    return 0;
}

static void FreeFurnitureDebugTextState(FurnitureDebugTextState* state)
{
    if (state == NULL)
    {
        return;
    }

    HeapFree(GetProcessHeap(), 0, state->TextBuffer);
    HeapFree(GetProcessHeap(), 0, state->Lines);
    memset(state, 0, sizeof(FurnitureDebugTextState));
}

static int EnsureFurnitureDebugTextLineCapacity(FurnitureDebugTextState* state, uint32_t neededCapacity)
{
    FurnitureDebugTextLine* resizedLines;
    uint32_t newCapacity;

    if (state == NULL)
    {
        return 0;
    }

    if (neededCapacity <= state->LineCapacity)
    {
        return 1;
    }

    newCapacity = state->LineCapacity == 0U ? 16U : state->LineCapacity;

    while (newCapacity < neededCapacity)
    {
        if (newCapacity > 0x80000000U)
        {
            return 0;
        }

        newCapacity *= 2U;
    }

    if (state->Lines == NULL)
    {
        resizedLines = (FurnitureDebugTextLine*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FurnitureDebugTextLine) * newCapacity);
    }
    else
    {
        resizedLines = (FurnitureDebugTextLine*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, state->Lines, sizeof(FurnitureDebugTextLine) * newCapacity);
    }

    if (resizedLines == NULL)
    {
        return 0;
    }

    state->Lines = resizedLines;
    state->LineCapacity = newCapacity;
    return 1;
}

static int IsFurnitureDebugHotkeySeparator(char value)
{
    if (value == 0 || value == ' ' || value == '\t' || value == '-' || value == ':' || value == '=')
    {
        return 1;
    }

    return 0;
}

static int FurnitureDebugLineStartsWithHotkey(const char* text, size_t length, const char* prefix)
{
    size_t prefixLength;
    size_t index;

    if (text == NULL || prefix == NULL)
    {
        return 0;
    }

    prefixLength = strlen(prefix);

    if (length < prefixLength)
    {
        return 0;
    }

    for (index = 0U; index < prefixLength; index++)
    {
        if (ToLowerAscii(text[index]) != ToLowerAscii(prefix[index]))
        {
            return 0;
        }
    }

    if (length == prefixLength)
    {
        return 1;
    }

    return IsFurnitureDebugHotkeySeparator(text[prefixLength]);
}

static int IsFurnitureCtrlSSaveHintLine(const char* text, size_t length)
{
    size_t beginIndex;

    if (text == NULL)
    {
        return 0;
    }

    beginIndex = 0U;

    while (beginIndex < length && IsFurnitureDebugTextWhitespace(text[beginIndex]))
    {
        beginIndex++;
    }

    if (beginIndex >= length)
    {
        return 0;
    }

    text += beginIndex;
    length -= beginIndex;

    if (FurnitureDebugLineStartsWithHotkey(text, length, "Ctrl + S"))
    {
        return 1;
    }

    if (FurnitureDebugLineStartsWithHotkey(text, length, "Ctrl+S"))
    {
        return 1;
    }

    if (FurnitureDebugLineStartsWithHotkey(text, length, "Ctrl-S"))
    {
        return 1;
    }

    return 0;
}

static int AddFurnitureDebugTextFileLine(FurnitureDebugTextState* state, char* text, size_t length)
{
    if (state == NULL || text == NULL)
    {
        return 0;
    }

    if (!EnsureFurnitureDebugTextLineCapacity(state, state->LineCount + 1U))
    {
        return 0;
    }

    state->Lines[state->LineCount].Text = text;
    state->Lines[state->LineCount].Length = length;
    state->LineCount++;
    return 1;
}

static int IsFurnitureDebugTextWhitespace(char value)
{
    if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
    {
        return 1;
    }

    return 0;
}

static void TrimFurnitureDebugConfigValue(char** text, size_t* length)
{
    char* value;
    size_t valueLength;

    if (text == NULL || length == NULL || *text == NULL)
    {
        return;
    }

    value = *text;
    valueLength = *length;

    while (valueLength > 0U && IsFurnitureDebugTextWhitespace(value[0]))
    {
        value++;
        valueLength--;
    }

    while (valueLength > 0U && IsFurnitureDebugTextWhitespace(value[valueLength - 1U]))
    {
        value[valueLength - 1U] = 0;
        valueLength--;
    }

    *text = value;
    *length = valueLength;
}

static int FurnitureDebugConfigBoolValue(const char* text)
{
    if (text == NULL)
    {
        return 0;
    }

    if (_stricmp(text, "1") == 0 || _stricmp(text, "true") == 0 || _stricmp(text, "yes") == 0 || _stricmp(text, "on") == 0)
    {
        return 1;
    }

    return 0;
}

static int FurnitureDebugDirectiveMessageId(const char* key, FurnitureDebugMessageId* messageId)
{
    if (key == NULL || messageId == NULL)
    {
        return 0;
    }

    if (_stricmp(key, "message_overlay_shown") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN;
        return 1;
    }

    if (_stricmp(key, "message_overlay_hidden") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_OVERLAY_HIDDEN;
        return 1;
    }

    if (_stricmp(key, "message_capture_saved") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_CAPTURE_SAVED;
        return 1;
    }

    if (_stricmp(key, "message_capture_overwritten") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_CAPTURE_OVERWRITTEN;
        return 1;
    }

    if (_stricmp(key, "message_capture_read_failed") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_CAPTURE_READ_FAILED;
        return 1;
    }

    if (_stricmp(key, "message_capture_path_failed") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_CAPTURE_PATH_FAILED;
        return 1;
    }

    if (_stricmp(key, "message_capture_write_failed") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_CAPTURE_WRITE_FAILED;
        return 1;
    }

    if (_stricmp(key, "message_vanilla_dump_success") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_SUCCESS;
        return 1;
    }

    if (_stricmp(key, "message_vanilla_dump_missing") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_MISSING;
        return 1;
    }

    if (_stricmp(key, "message_vanilla_dump_invalid") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_INVALID;
        return 1;
    }

    if (_stricmp(key, "message_vanilla_dump_write_failed") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_WRITE_FAILED;
        return 1;
    }

    if (_stricmp(key, "message_jack_shop_refreshed") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_JACK_SHOP_REFRESHED;
        return 1;
    }

    if (_stricmp(key, "message_jack_shop_injected") == 0)
    {
        *messageId = FURNITURE_DEBUG_MESSAGE_JACK_SHOP_INJECTED;
        return 1;
    }

    return 0;
}

static void ApplyFurnitureDebugDirective(FurnitureDebugTextState* state, char* line, size_t length)
{
    char* key;
    char* value;
    char* equals;
    size_t keyLength;
    size_t valueLength;
    FurnitureDebugMessageId messageId;

    if (state == NULL || line == NULL || length < 2U || line[0] != '@')
    {
        return;
    }

    key = line + 1;
    equals = strchr(key, '=');

    if (equals == NULL)
    {
        return;
    }

    *equals = 0;
    value = equals + 1;
    keyLength = strlen(key);
    valueLength = length - (size_t)(value - line);
    TrimFurnitureDebugConfigValue(&key, &keyLength);
    TrimFurnitureDebugConfigValue(&value, &valueLength);

    if (_stricmp(key, "instructions_header") == 0)
    {
        state->InstructionsHeader = value;
        state->InstructionsHeaderLength = valueLength;
        return;
    }

    if (_stricmp(key, "logs_header") == 0)
    {
        state->LogsHeader = value;
        state->LogsHeaderLength = valueLength;
        return;
    }

    if (_stricmp(key, "current_piece_text") == 0)
    {
        state->CurrentPieceText = value;
        state->CurrentPieceTextLength = valueLength;
        return;
    }

    if (_stricmp(key, "unsaved_changes_text") == 0)
    {
        state->UnsavedChangesText = value;
        state->UnsavedChangesTextLength = valueLength;
        return;
    }

    if (_stricmp(key, "hide_current_piece") == 0)
    {
        state->HideCurrentPiece = FurnitureDebugConfigBoolValue(value);
        return;
    }

    if (_stricmp(key, "hide_unsaved_changes") == 0)
    {
        state->HideUnsavedChanges = FurnitureDebugConfigBoolValue(value);
        return;
    }

    if (_stricmp(key, "hide_instructions_header") == 0)
    {
        state->HideInstructionsHeader = FurnitureDebugConfigBoolValue(value);
        return;
    }

    if (_stricmp(key, "hide_logs_header") == 0)
    {
        state->HideLogsHeader = FurnitureDebugConfigBoolValue(value);
        return;
    }

    if (_stricmp(key, "hide_ctrl_s_save_hint") == 0)
    {
        state->HideCtrlSSaveHint = FurnitureDebugConfigBoolValue(value);
        return;
    }

    if (FurnitureDebugDirectiveMessageId(key, &messageId))
    {
        state->MessageTemplates[messageId] = value;
        state->MessageTemplateLengths[messageId] = valueLength;
        return;
    }
}

static int ParseFurnitureDebugTextBuffer(FurnitureDebugTextState* state)
{
    uint32_t lineStart;
    uint32_t index;
    uint32_t lineEnd;
    char* line;
    size_t lineLength;

    if (state == NULL)
    {
        return 0;
    }

    state->LineCount = 0U;

    if (state->TextBuffer == NULL)
    {
        return 1;
    }

    lineStart = 0U;

    for (index = 0U; index < state->TextBufferSize; index++)
    {
        if (state->TextBuffer[index] == '\n')
        {
            lineEnd = index;

            if (lineEnd > lineStart && state->TextBuffer[lineEnd - 1U] == '\r')
            {
                lineEnd--;
            }

            state->TextBuffer[index] = 0;

            if (lineEnd < index)
            {
                state->TextBuffer[lineEnd] = 0;
            }

            line = state->TextBuffer + lineStart;
            lineLength = (size_t)(lineEnd - lineStart);

            if (lineLength != 0U && line[0] == '@')
            {
                ApplyFurnitureDebugDirective(state, line, lineLength);
            }
            else if (lineLength != 0U && line[0] == '#')
            {

            }
            else if (lineLength == 0U && state->LineCount == 0U)
            {

            }
            else if (!AddFurnitureDebugTextFileLine(state, line, lineLength))
            {
                return 0;
            }

            lineStart = index + 1U;
        }
    }

    if (lineStart < state->TextBufferSize)
    {
        lineEnd = state->TextBufferSize;

        if (lineEnd > lineStart && state->TextBuffer[lineEnd - 1U] == '\r')
        {
            lineEnd--;
            state->TextBuffer[lineEnd] = 0;
        }

        line = state->TextBuffer + lineStart;
        lineLength = (size_t)(lineEnd - lineStart);

        if (lineLength != 0U && line[0] == '@')
        {
            ApplyFurnitureDebugDirective(state, line, lineLength);
        }
        else if (lineLength != 0U && line[0] == '#')
        {

        }
        else if (lineLength == 0U && state->LineCount == 0U)
        {

        }
        else if (!AddFurnitureDebugTextFileLine(state, line, lineLength))
        {
            return 0;
        }
    }

    return 1;
}

static int EnsureDefaultFurnitureDebugTextFile(const char* path)
{
    const char* defaultText;
    size_t defaultTextLength;

    if (path == NULL || path[0] == 0)
    {
        return 0;
    }

    if (FileExists(path))
    {
        return 1;
    }

    defaultText = GetDefaultFurnitureDebugText();
    defaultTextLength = strlen(defaultText);

    if (defaultTextLength == 0U || defaultTextLength > 0xFFFFFFFFULL)
    {
        return 0;
    }

    if (!WriteWholeFileAtomic(path, (const uint8_t*)defaultText, (uint32_t)defaultTextLength))
    {
        LogMessage("Could not create default furniture editor debug text file: %s", path);
        return 0;
    }

    LogMessage("Created default furniture editor debug text file: %s", path);
    return 1;
}

static int ReloadFurnitureDebugTextFile(const char* path, const WIN32_FILE_ATTRIBUTE_DATA* attributes)
{
    FileBytes fileBytes;
    FurnitureDebugTextState nextState;

    if (path == NULL || attributes == NULL)
    {
        return 0;
    }

    memset(&fileBytes, 0, sizeof(fileBytes));
    memset(&nextState, 0, sizeof(nextState));

    if (!ReadWholeFile(path, &fileBytes))
    {
        LogMessage("Could not read furniture editor debug text file, using built-in defaults: %s", path);
        return 0;
    }

    if (fileBytes.Size != 0U)
    {
        nextState.TextBuffer = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)fileBytes.Size + 1U);

        if (nextState.TextBuffer == NULL)
        {
            FreeFileBytes(&fileBytes);
            return 0;
        }

        memcpy(nextState.TextBuffer, fileBytes.Data, fileBytes.Size);
        nextState.TextBufferSize = fileBytes.Size;
    }

    nextState.HideCtrlSSaveHint = 1;
    nextState.LastWriteTime = attributes->ftLastWriteTime;
    nextState.LastFileSizeLow = attributes->nFileSizeLow;
    nextState.LastFileSizeHigh = attributes->nFileSizeHigh;
    nextState.Loaded = 1;
    nextState.HasFile = 1;

    if (!ParseFurnitureDebugTextBuffer(&nextState))
    {
        FreeFurnitureDebugTextState(&nextState);
        FreeFileBytes(&fileBytes);
        return 0;
    }

    FreeFurnitureDebugTextState(&g_furnitureDebugTextState);
    g_furnitureDebugTextState = nextState;
    memset(&nextState, 0, sizeof(nextState));
    FreeFileBytes(&fileBytes);
    LogMessage("Loaded furniture editor debug text file: %s (%u lines)", path, g_furnitureDebugTextState.LineCount);
    return 1;
}

static void MaybeReloadFurnitureDebugTextFile(void)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    char inputPath[MAX_PATH_BUFFER];
    ULONGLONG tick;
    int hasAttributes;
    int shouldReload;

    tick = GetTickCount64();

    if (g_furnitureDebugTextState.Loaded && tick - g_furnitureDebugTextState.LastCheckTick < 500ULL)
    {
        return;
    }

    g_furnitureDebugTextState.LastCheckTick = tick;
    inputPath[0] = 0;

    if (!GetFurnitureDebugTextLoadPath(inputPath, sizeof(inputPath)))
    {
        return;
    }

    EnsureDefaultFurnitureDebugTextFile(inputPath);
    memset(&attributes, 0, sizeof(attributes));
    hasAttributes = GetFileAttributesExA(inputPath, GetFileExInfoStandard, &attributes) != 0;

    if (!hasAttributes || (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
    {
        if (!g_furnitureDebugTextState.Loaded || g_furnitureDebugTextState.HasFile)
        {
            FreeFurnitureDebugTextState(&g_furnitureDebugTextState);
            g_furnitureDebugTextState.Loaded = 1;
            g_furnitureDebugTextState.HasFile = 0;
        }

        return;
    }

    shouldReload = 0;

    if (!g_furnitureDebugTextState.Loaded || !g_furnitureDebugTextState.HasFile)
    {
        shouldReload = 1;
    }
    else if (!FileTimesEqual(&g_furnitureDebugTextState.LastWriteTime, &attributes.ftLastWriteTime))
    {
        shouldReload = 1;
    }
    else if (g_furnitureDebugTextState.LastFileSizeLow != attributes.nFileSizeLow || g_furnitureDebugTextState.LastFileSizeHigh != attributes.nFileSizeHigh)
    {
        shouldReload = 1;
    }

    if (shouldReload)
    {
        ReloadFurnitureDebugTextFile(inputPath, &attributes);
    }
}

static const char* GetDefaultFurnitureDebugValueForKey(const char* key)
{
    if (key == NULL)
    {
        return "";
    }

    if (_stricmp(key, "instructions_header") == 0)
    {
        return "--- Mew Furniture Instructions ---";
    }

    if (_stricmp(key, "logs_header") == 0)
    {
        return "--- Mew Furniture Logs ---";
    }

    if (_stricmp(key, "current_piece_text") == 0)
    {
        return "Current Piece";
    }

    if (_stricmp(key, "unsaved_changes_text") == 0)
    {
        return "*UNSAVED CHANGES*";
    }

    return "";
}

static const char* GetDefaultFurnitureMessageTemplate(FurnitureDebugMessageId messageId)
{
    switch (messageId)
    {
        case FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN:
        {
            return "Debug overlay shown. Press F7 to hide it.";
        }
        case FURNITURE_DEBUG_MESSAGE_OVERLAY_HIDDEN:
        {
            return "Debug overlay hidden. Press F7 to show it.";
        }
        case FURNITURE_DEBUG_MESSAGE_CAPTURE_SAVED:
        {
            return "Saved furniture data!\\nFurniture: {furniture}\\nRows in append file: {rows}\\nSaved to:\\n{path}";
        }
        case FURNITURE_DEBUG_MESSAGE_CAPTURE_OVERWRITTEN:
        {
            return "Overwrote furniture append data!\\nFurniture: {furniture}\\nRows in append file: {rows}\\nSaved to:\\n{path}";
        }
        case FURNITURE_DEBUG_MESSAGE_CAPTURE_READ_FAILED:
        {
            return "Furniture capture failed!\\nCould not read the current furniture editor row. Make sure the furniture editor is open and a furniture entry is selected.";
        }
        case FURNITURE_DEBUG_MESSAGE_CAPTURE_PATH_FAILED:
        {
            return "Furniture capture failed!\\nCould not resolve or create the output folder.";
        }
        case FURNITURE_DEBUG_MESSAGE_CAPTURE_WRITE_FAILED:
        {
            return "Furniture capture failed!\\nCould not write the append file.\\nAttempted path:\\n{path}";
        }
        case FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_SUCCESS:
        {
            return "Dumped vanilla furniture names!\\nNames: {names}\\nSaved to:\\n{path}";
        }
        case FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_MISSING:
        {
            return "Vanilla furniture name dump failed!\\nThe vanilla furniture_info.data has not been loaded yet.";
        }
        case FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_INVALID:
        {
            return "Vanilla furniture name dump failed!\\nThe cached vanilla furniture data is invalid.";
        }
        case FURNITURE_DEBUG_MESSAGE_VANILLA_DUMP_WRITE_FAILED:
        {
            return "Vanilla furniture name dump failed!\\nCould not write vanilla_furniture_names_dump.txt.";
        }
        case FURNITURE_DEBUG_MESSAGE_JACK_SHOP_REFRESHED:
        {
            return "Jack shop refreshed!\\nVisible slots: {item_count}\\nFurniture slots rerolled: {replaced}";
        }
        case FURNITURE_DEBUG_MESSAGE_JACK_SHOP_INJECTED:
        {
            return "Jack shop test furniture injected!\\nFurniture: {furniture}\\nSlot: {slot} of {item_count}\\nCost: {price}\\nRare price path: {rare}";
        }
        default:
        {
            return "";
        }
    }
}

static const char* GetConfiguredFurnitureDebugTextValue(const char* value, size_t length, const char* fallback, size_t* outputLength)
{
    const char* result;

    result = value;

    if (result == NULL)
    {
        result = fallback;
        length = strlen(result);
    }

    if (outputLength != NULL)
    {
        *outputLength = length;
    }

    return result;
}

static const char* GetConfiguredFurnitureMessageTemplate(FurnitureDebugMessageId messageId, size_t* outputLength)
{
    const char* result;
    size_t length;

    result = NULL;
    length = 0U;

    if (messageId >= 0 && messageId < FURNITURE_DEBUG_MESSAGE_COUNT)
    {
        result = g_furnitureDebugTextState.MessageTemplates[messageId];
        length = g_furnitureDebugTextState.MessageTemplateLengths[messageId];
    }

    if (result == NULL)
    {
        result = GetDefaultFurnitureMessageTemplate(messageId);
        length = strlen(result);
    }

    if (outputLength != NULL)
    {
        *outputLength = length;
    }

    return result;
}

static int BufferAppendChar(BufferBuilder* builder, char value)
{
    return BufferAppend(builder, &value, 1U);
}

static int BufferAppendCString(BufferBuilder* builder, const char* value)
{
    if (value == NULL)
    {
        value = "";
    }

    return BufferAppend(builder, value, strlen(value));
}

static int BufferAppendU32Decimal(BufferBuilder* builder, uint32_t value)
{
    char buffer[32];

    _snprintf(buffer, sizeof(buffer), "%u", value);
    buffer[sizeof(buffer) - 1U] = 0;
    return BufferAppendCString(builder, buffer);
}

static int BufferAppendIntDecimal(BufferBuilder* builder, int value)
{
    char buffer[32];

    _snprintf(buffer, sizeof(buffer), "%d", value);
    buffer[sizeof(buffer) - 1U] = 0;
    return BufferAppendCString(builder, buffer);
}

static int AppendFurnitureDebugEscapedTemplateChar(BufferBuilder* builder, char value)
{
    if (value == 'n')
    {
        return BufferAppendChar(builder, '\n');
    }

    if (value == 'r')
    {
        return BufferAppendChar(builder, '\r');
    }

    if (value == 't')
    {
        return BufferAppendChar(builder, '\t');
    }

    if (value == '\\')
    {
        return BufferAppendChar(builder, '\\');
    }

    if (!BufferAppendChar(builder, '\\'))
    {
        return 0;
    }

    return BufferAppendChar(builder, value);
}

static int AppendFurnitureDebugPlaceholderValue(BufferBuilder* builder, const char* name, size_t nameLength, const FurnitureDebugMessageContext* context)
{
    if (context == NULL)
    {
        return 0;
    }

    if (nameLength == 9U && memcmp(name, "furniture", 9U) == 0)
    {
        return BufferAppendCString(builder, context->FurnitureName);
    }

    if (nameLength == 4U && memcmp(name, "rows", 4U) == 0)
    {
        return BufferAppendU32Decimal(builder, context->RowCount);
    }

    if (nameLength == 4U && memcmp(name, "path", 4U) == 0)
    {
        return BufferAppendCString(builder, context->Path);
    }

    if (nameLength == 5U && memcmp(name, "names", 5U) == 0)
    {
        return BufferAppendU32Decimal(builder, context->NameCount);
    }

    if (nameLength == 4U && memcmp(name, "slot", 4U) == 0)
    {
        return BufferAppendIntDecimal(builder, context->SlotIndex);
    }

    if (nameLength == 10U && memcmp(name, "item_count", 10U) == 0)
    {
        return BufferAppendU32Decimal(builder, context->ItemCount);
    }

    if (nameLength == 5U && memcmp(name, "slots", 5U) == 0)
    {
        return BufferAppendU32Decimal(builder, context->ItemCount);
    }

    if (nameLength == 5U && memcmp(name, "price", 5U) == 0)
    {
        return BufferAppendIntDecimal(builder, context->Price);
    }

    if (nameLength == 4U && memcmp(name, "cost", 4U) == 0)
    {
        return BufferAppendIntDecimal(builder, context->Price);
    }

    if (nameLength == 4U && memcmp(name, "rare", 4U) == 0)
    {
        return BufferAppendCString(builder, context->Rare ? "yes" : "no");
    }

    if (nameLength == 8U && memcmp(name, "replaced", 8U) == 0)
    {
        return BufferAppendU32Decimal(builder, context->ReplacedCount);
    }

    if (!BufferAppendChar(builder, '{'))
    {
        return 0;
    }

    if (!BufferAppend(builder, name, nameLength))
    {
        return 0;
    }

    return BufferAppendChar(builder, '}');
}

static char* BuildFurnitureDebugMessageTextEx(FurnitureDebugMessageId messageId, const FurnitureDebugMessageContext* context)
{
    BufferBuilder output;
    const char* templateText;
    size_t templateLength;
    size_t index;
    size_t placeholderStart;
    char zero;

    memset(&output, 0, sizeof(output));
    templateText = GetConfiguredFurnitureMessageTemplate(messageId, &templateLength);
    index = 0U;

    while (index < templateLength)
    {
        if (templateText[index] == '\\' && index + 1U < templateLength)
        {
            if (!AppendFurnitureDebugEscapedTemplateChar(&output, templateText[index + 1U]))
            {
                BufferFree(&output);
                return NULL;
            }

            index += 2U;
            continue;
        }

        if (templateText[index] == '{')
        {
            placeholderStart = index + 1U;

            while (index < templateLength && templateText[index] != '}')
            {
                index++;
            }

            if (index < templateLength && templateText[index] == '}')
            {
                if (!AppendFurnitureDebugPlaceholderValue(&output, templateText + placeholderStart, index - placeholderStart, context))
                {
                    BufferFree(&output);
                    return NULL;
                }

                index++;
                continue;
            }

            if (!BufferAppendChar(&output, '{'))
            {
                BufferFree(&output);
                return NULL;
            }

            index = placeholderStart;
            continue;
        }

        if (!BufferAppendChar(&output, templateText[index]))
        {
            BufferFree(&output);
            return NULL;
        }

        index++;
    }

    zero = 0;

    if (!BufferAppendChar(&output, zero))
    {
        BufferFree(&output);
        return NULL;
    }

    return (char*)output.Data;
}

static void FreeRuntimeDebugMessage(FurnitureRuntimeDebugMessage* message)
{
    if (message == NULL)
    {
        return;
    }

    HeapFree(GetProcessHeap(), 0, message->Text);
    message->Text = NULL;
    message->ExpireTick = 0ULL;
    message->MessageId = FURNITURE_DEBUG_MESSAGE_COUNT;
}

static void CompactRuntimeDebugMessages(void)
{
    uint32_t readIndex;
    uint32_t writeIndex;
    ULONGLONG tick;

    tick = GetTickCount64();
    writeIndex = 0U;

    for (readIndex = 0U; readIndex < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; readIndex++)
    {
        if (g_runtimeDebugMessages[readIndex].Text != NULL && g_runtimeDebugMessages[readIndex].ExpireTick > tick)
        {
            if (writeIndex != readIndex)
            {
                g_runtimeDebugMessages[writeIndex] = g_runtimeDebugMessages[readIndex];
                g_runtimeDebugMessages[readIndex].Text = NULL;
                g_runtimeDebugMessages[readIndex].ExpireTick = 0ULL;
            }

            writeIndex++;
        }
        else
        {
            FreeRuntimeDebugMessage(&g_runtimeDebugMessages[readIndex]);
        }
    }
}

static void QueueFurnitureDebugMessageEx(FurnitureDebugMessageId messageId, const FurnitureDebugMessageContext* context)
{
    char* messageText;
    uint32_t index;

    if (context == NULL)
    {
        return;
    }

    MaybeReloadFurnitureDebugTextFile();
    messageText = BuildFurnitureDebugMessageTextEx(messageId, context);

    if (messageText == NULL)
    {
        return;
    }

    CompactRuntimeDebugMessages();

    for (index = 0U; index < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; index++)
    {
        if (g_runtimeDebugMessages[index].Text == NULL)
        {
            g_runtimeDebugMessages[index].Text = messageText;
            g_runtimeDebugMessages[index].ExpireTick = GetTickCount64() + FURNITURE_DEBUG_RUNTIME_MESSAGE_TTL_MS;
            g_runtimeDebugMessages[index].MessageId = messageId;
            return;
        }
    }

    FreeRuntimeDebugMessage(&g_runtimeDebugMessages[0]);

    for (index = 1U; index < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; index++)
    {
        g_runtimeDebugMessages[index - 1U] = g_runtimeDebugMessages[index];
    }

    g_runtimeDebugMessages[FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY - 1U].Text = messageText;
    g_runtimeDebugMessages[FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY - 1U].ExpireTick = GetTickCount64() + FURNITURE_DEBUG_RUNTIME_MESSAGE_TTL_MS;
    g_runtimeDebugMessages[FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY - 1U].MessageId = messageId;
}

static void QueueFurnitureDebugMessage(FurnitureDebugMessageId messageId, const char* furnitureName, uint32_t rowCount, const char* path, uint32_t nameCount)
{
    FurnitureDebugMessageContext context;

    memset(&context, 0, sizeof(context));
    context.FurnitureName = furnitureName;
    context.RowCount = rowCount;
    context.Path = path;
    context.NameCount = nameCount;
    QueueFurnitureDebugMessageEx(messageId, &context);
}

static int HasRuntimeDebugMessages(void)
{
    uint32_t index;

    CompactRuntimeDebugMessages();

    for (index = 0U; index < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; index++)
    {
        if (g_runtimeDebugMessages[index].Text != NULL)
        {
            return 1;
        }
    }

    return 0;
}

static int MsvcStringEqualsLiteral(const MsvcString* value, const char* literal)
{
    const char* text;
    size_t literalLength;

    if (value == NULL || literal == NULL)
    {
        return 0;
    }

    text = GetMsvcStringChars(value);

    if (text == NULL)
    {
        return 0;
    }

    literalLength = strlen(literal);

    if (value->Length != literalLength)
    {
        return 0;
    }

    if (memcmp(text, literal, literalLength) == 0)
    {
        return 1;
    }

    return 0;
}

static int IsVanillaFurnitureEditorHelpText(const MsvcString* value)
{
    if (MsvcStringEqualsLiteral(value, "Click: draw Black tiles (solid)"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "Ctrl+Click: draw Red tiles (supports, required to overlap with a surface tile)"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "Shift+Click: draw Blue tiles (surface, can place stuff on)"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "P+Click: draw Brown tiles (for poop specifically)"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "Tab+Click: draw Yellow tiles (extra clickable areas)"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "Arrows: next/previous furniture piece"))
    {
        return 1;
    }

    return 0;
}

static int IsFurnitureCurrentPieceStatusText(const MsvcString* value)
{
    if (MsvcStringEqualsLiteral(value, "Current Piece"))
    {
        return 1;
    }

    if (MsvcStringEqualsLiteral(value, "Current Piece:"))
    {
        return 1;
    }

    return 0;
}

static int IsFurnitureUnsavedChangesStatusText(const MsvcString* value)
{
    if (MsvcStringEqualsLiteral(value, "*UNSAVED CHANGES*"))
    {
        return 1;
    }

    return 0;
}

static int IsFurnitureCtrlSSaveHintMsvcString(const MsvcString* value)
{
    const char* text;

    if (value == NULL)
    {
        return 0;
    }

    text = GetMsvcStringChars(value);

    if (text == NULL)
    {
        return 0;
    }

    return IsFurnitureCtrlSSaveHintLine(text, value->Length);
}

static void DestroyMsvcStringIfNeeded(MsvcString* value)
{
    if (value == NULL)
    {
        return;
    }

    if (g_msvcStringDestruct != NULL)
    {
        g_msvcStringDestruct(value);
    }
}


static const char* GetDefaultJackShopTestConfigText(void)
{
    return
        "# MewFurnitureFramework Jack shop test hotkeys\r\n"
        "# Ctrl + F10 or Ctrl + 0 rerolls Jack's live shop slots in-place.\r\n"
        "# Ctrl + Shift + F10 or Ctrl + Shift + 0 optionally rerolls Jack's live slots, then replaces one visible slot.\r\n"
        "# slot is zero-based and maps to the live stock vector order. In Jack's six-slot layout, 0 is the first live item.\r\n"
        "# rare=1 asks the vanilla furniture price helper for the rare furniture price path.\r\n"
        "@furniture=" JACK_SHOP_DEFAULT_GUARANTEED_FURNITURE "\r\n"
        "@slot=0\r\n"
        "@rare=0\r\n"
        "@refresh_before_replace=1\r\n";
}

static int GetJackShopTestConfigLoadPath(char* output, size_t outputSize)
{
    char dllDirectory[MAX_PATH_BUFFER];

    if (output == NULL || outputSize == 0U)
    {
        return 0;
    }

    output[0] = 0;

    if (!GetFrameworkRootDirectory(dllDirectory, sizeof(dllDirectory)))
    {
        return 0;
    }

    JoinPath(output, outputSize, dllDirectory, JACK_SHOP_TEST_CONFIG_FILE);
    return output[0] != 0;
}

static void InitializeDefaultJackShopTestConfig(JackShopTestConfig* config)
{
    if (config == NULL)
    {
        return;
    }

    memset(config, 0, sizeof(JackShopTestConfig));
    strncpy(config->FurnitureName, JACK_SHOP_DEFAULT_GUARANTEED_FURNITURE, sizeof(config->FurnitureName) - 1U);
    config->SlotIndex = 0;
    config->Rare = 0;
    config->RefreshBeforeReplace = 1;
}

static void EnsureDefaultJackShopTestConfigFile(const char* path)
{
    const char* defaultText;
    size_t defaultTextLength;

    if (path == NULL || path[0] == 0 || FileExists(path))
    {
        return;
    }

    defaultText = GetDefaultJackShopTestConfigText();
    defaultTextLength = strlen(defaultText);

    if (!WriteWholeFileAtomic(path, (const uint8_t*)defaultText, (uint32_t)defaultTextLength))
    {
        LogMessage("Could not create default Jack shop test config: %s", path);
    }
}

static void TrimJackShopConfigValue(char** text, size_t* length)
{
    char* value;
    size_t valueLength;

    if (text == NULL || length == NULL || *text == NULL)
    {
        return;
    }

    value = *text;
    valueLength = *length;

    while (valueLength > 0U && IsNameTextWhitespace(value[0]))
    {
        value++;
        valueLength--;
    }

    while (valueLength > 0U && IsNameTextWhitespace(value[valueLength - 1U]))
    {
        value[valueLength - 1U] = 0;
        valueLength--;
    }

    *text = value;
    *length = valueLength;
}

static int ParseJackShopConfigBool(const char* text)
{
    if (text == NULL)
    {
        return 0;
    }

    if (_stricmp(text, "1") == 0 || _stricmp(text, "true") == 0 || _stricmp(text, "yes") == 0 || _stricmp(text, "on") == 0)
    {
        return 1;
    }

    return 0;
}

static void ApplyJackShopConfigLine(JackShopTestConfig* config, char* line, size_t lineLength)
{
    char* key;
    char* value;
    char* equals;
    size_t keyLength;
    size_t valueLength;
    long numberValue;

    if (config == NULL || line == NULL || lineLength == 0U)
    {
        return;
    }

    TrimJackShopConfigValue(&line, &lineLength);

    if (lineLength == 0U || line[0] == '#')
    {
        return;
    }

    if (line[0] == '@')
    {
        line++;
        lineLength--;
    }

    equals = strchr(line, '=');

    if (equals == NULL)
    {
        return;
    }

    *equals = 0;
    key = line;
    value = equals + 1;
    keyLength = strlen(key);
    valueLength = lineLength - (size_t)(value - line);
    TrimJackShopConfigValue(&key, &keyLength);
    TrimJackShopConfigValue(&value, &valueLength);

    if (_stricmp(key, "furniture") == 0 || _stricmp(key, "furniture_name") == 0 || _stricmp(key, "name") == 0)
    {
        if (valueLength > 0U && valueLength <= MAX_FURNITURE_NAME_BYTES)
        {
            memcpy(config->FurnitureName, value, valueLength);
            config->FurnitureName[valueLength] = 0;
        }

        return;
    }

    if (_stricmp(key, "slot") == 0 || _stricmp(key, "slot_index") == 0)
    {
        numberValue = strtol(value, NULL, 10);

        if (numberValue < 0L)
        {
            numberValue = 0L;
        }

        if (numberValue > 64L)
        {
            numberValue = 64L;
        }

        config->SlotIndex = (int)numberValue;
        return;
    }

    if (_stricmp(key, "rare") == 0 || _stricmp(key, "is_rare") == 0)
    {
        config->Rare = ParseJackShopConfigBool(value);
        return;
    }

    if (_stricmp(key, "refresh_before_replace") == 0 || _stricmp(key, "refresh_first") == 0)
    {
        config->RefreshBeforeReplace = ParseJackShopConfigBool(value);
        return;
    }

}

static void ParseJackShopConfigBuffer(JackShopTestConfig* config, char* textBuffer, uint32_t textBufferSize)
{
    uint32_t lineStart;
    uint32_t index;
    uint32_t lineEnd;
    char* line;
    size_t lineLength;

    if (config == NULL || textBuffer == NULL)
    {
        return;
    }

    lineStart = 0U;

    for (index = 0U; index < textBufferSize; index++)
    {
        if (textBuffer[index] == '\n')
        {
            lineEnd = index;

            if (lineEnd > lineStart && textBuffer[lineEnd - 1U] == '\r')
            {
                lineEnd--;
            }

            textBuffer[index] = 0;

            if (lineEnd < index)
            {
                textBuffer[lineEnd] = 0;
            }

            line = textBuffer + lineStart;
            lineLength = (size_t)(lineEnd - lineStart);
            ApplyJackShopConfigLine(config, line, lineLength);
            lineStart = index + 1U;
        }
    }

    if (lineStart < textBufferSize)
    {
        lineEnd = textBufferSize;

        if (lineEnd > lineStart && textBuffer[lineEnd - 1U] == '\r')
        {
            lineEnd--;
            textBuffer[lineEnd] = 0;
        }

        line = textBuffer + lineStart;
        lineLength = (size_t)(lineEnd - lineStart);
        ApplyJackShopConfigLine(config, line, lineLength);
    }
}

static int LoadJackShopTestConfig(JackShopTestConfig* config, char* configPath, size_t configPathSize)
{
    FileBytes fileBytes;

    if (config == NULL)
    {
        return 0;
    }

    InitializeDefaultJackShopTestConfig(config);
    memset(&fileBytes, 0, sizeof(fileBytes));

    if (configPath != NULL && configPathSize != 0U)
    {
        configPath[0] = 0;
    }

    if (!GetJackShopTestConfigLoadPath(configPath, configPathSize))
    {
        LogMessage("Could not resolve Jack shop test config path, using defaults.");
        return 1;
    }

    EnsureDefaultJackShopTestConfigFile(configPath);

    if (!FileExists(configPath))
    {
        return 1;
    }

    if (!ReadWholeFile(configPath, &fileBytes))
    {
        LogMessage("Could not read Jack shop test config, using defaults: %s", configPath);
        return 1;
    }

    ParseJackShopConfigBuffer(config, (char*)fileBytes.Data, fileBytes.Size);
    FreeFileBytes(&fileBytes);
    return 1;
}

static int IsJackShopName(const char* value)
{
    if (value == NULL || value[0] == 0)
    {
        return 0;
    }

    if (_stricmp(value, "JackShop") == 0 || _stricmp(value, "jackshop") == 0 || _stricmp(value, "jack_shop") == 0)
    {
        return 1;
    }

    if (strstr(value, "JackShop") != NULL || strstr(value, "jackshop") != NULL || strstr(value, "jack_shop") != NULL)
    {
        return 1;
    }

    return 0;
}

static int IsJackShopRefreshHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)((IsKeyDown(JACK_SHOP_REFRESH_HOTKEY) || IsKeyDown(JACK_SHOP_REFRESH_ALT_HOTKEY)) && IsControlDown() && !IsShiftDown());
    pressed = chordDown != 0U && g_lastJackShopRefreshChordDown == 0U;
    g_lastJackShopRefreshChordDown = chordDown;
    return pressed;
}

static int IsJackShopGuaranteeHotkeyPressedEdge(void)
{
    uint8_t chordDown;
    int pressed;

    chordDown = (uint8_t)((IsKeyDown(JACK_SHOP_GUARANTEE_HOTKEY) || IsKeyDown(JACK_SHOP_GUARANTEE_ALT_HOTKEY)) && IsControlDown() && IsShiftDown());
    pressed = chordDown != 0U && g_lastJackShopGuaranteeChordDown == 0U;
    g_lastJackShopGuaranteeChordDown = chordDown;
    return pressed;
}

static uint32_t GetShopItemCount(void* shop, uint8_t** itemBegin)
{
    uint8_t* vectorBytes;
    uint8_t* begin;
    uint8_t* end;
    ptrdiff_t byteCount;

    if (itemBegin != NULL)
    {
        *itemBegin = NULL;
    }

    if (shop == NULL)
    {
        return 0U;
    }

    vectorBytes = (uint8_t*)shop + SHOP_ITEMS_VECTOR_OFFSET;
    begin = *(uint8_t**)vectorBytes;
    end = *(uint8_t**)(vectorBytes + sizeof(void*));

    if (begin == NULL || end == NULL || end < begin)
    {
        return 0U;
    }

    byteCount = end - begin;

    if (byteCount <= 0 || (byteCount % (ptrdiff_t)SHOP_ITEM_STRIDE_BYTES) != 0)
    {
        return 0U;
    }

    if (itemBegin != NULL)
    {
        *itemBegin = begin;
    }

    return (uint32_t)(byteCount / (ptrdiff_t)SHOP_ITEM_STRIDE_BYTES);
}

static void* GetShopItemByConfigSlot(void* shop, const JackShopTestConfig* config, uint32_t* itemCount)
{
    uint8_t* begin;
    uint32_t count;
    uint32_t slotIndex;

    if (itemCount != NULL)
    {
        *itemCount = 0U;
    }

    if (shop == NULL || config == NULL)
    {
        return NULL;
    }

    begin = NULL;
    count = GetShopItemCount(shop, &begin);

    if (itemCount != NULL)
    {
        *itemCount = count;
    }

    if (begin == NULL || count == 0U)
    {
        return NULL;
    }

    slotIndex = (uint32_t)config->SlotIndex;

    if (slotIndex >= count)
    {
        slotIndex = count - 1U;
    }

    return begin + ((size_t)slotIndex * SHOP_ITEM_STRIDE_BYTES);
}

static int CalculateJackShopFurniturePrice(const char* furnitureName, int rare, int* price)
{
    MsvcString nameString;
    void* furnitureEffects;
    int calculatedPrice;

    if (furnitureName == NULL || furnitureName[0] == 0 || price == NULL)
    {
        return 0;
    }

    if (g_furniturePriceLookup == NULL || g_furnitureEffectsGlobal == NULL)
    {
        return 0;
    }

    furnitureEffects = *g_furnitureEffectsGlobal;

    if (furnitureEffects == NULL)
    {
        return 0;
    }

    memset(&nameString, 0, sizeof(nameString));

    if (!InitializeMsvcStringForDebugText(&nameString, furnitureName, strlen(furnitureName)))
    {
        return 0;
    }

    calculatedPrice = g_furniturePriceLookup(furnitureEffects, &nameString, rare ? 1U : 0U);
    DestroyMsvcStringIfNeeded(&nameString);
    *price = calculatedPrice;
    return 1;
}

static int SetShopItemFurnitureName(void* shopItem, const char* furnitureName)
{
    MsvcString* itemName;

    if (shopItem == NULL || furnitureName == NULL || furnitureName[0] == 0)
    {
        return 0;
    }

    itemName = (MsvcString*)((uint8_t*)shopItem + SHOP_ITEM_NAME_OFFSET);
    DestroyMsvcStringIfNeeded(itemName);

    if (!InitializeMsvcStringForDebugText(itemName, furnitureName, strlen(furnitureName)))
    {
        memset(itemName, 0, sizeof(MsvcString));
        itemName->Capacity = 15U;
        return 0;
    }

    return 1;
}

static int ReplaceShopItemWithFurnitureName(void* shop, void* shopItem, const char* furnitureName, int rare, int* outputPrice)
{
    int price;

    if (shop == NULL || shopItem == NULL || furnitureName == NULL || furnitureName[0] == 0)
    {
        return 0;
    }

    price = 0;

    if (!CalculateJackShopFurniturePrice(furnitureName, rare, &price))
    {
        LogMessage("Could not calculate furniture price for Jack shop item: %s", furnitureName);
        return 0;
    }

    if (!SetShopItemFurnitureName(shopItem, furnitureName))
    {
        LogMessage("Could not set Jack shop furniture name: %s", furnitureName);
        return 0;
    }

    *(int*)((uint8_t*)shopItem + SHOP_ITEM_TYPE_OFFSET) = SHOP_ITEM_TYPE_FURNITURE;
    *(int*)((uint8_t*)shopItem + SHOP_ITEM_PRICE_OFFSET) = price;
    *(int*)((uint8_t*)shopItem + SHOP_ITEM_RARE_OFFSET) = rare ? 1 : 0;

    if (g_shopRefreshShopItem != NULL)
    {
        g_shopRefreshShopItem(shop, shopItem, 1U);
    }

    if (outputPrice != NULL)
    {
        *outputPrice = price;
    }

    return 1;
}

static int ReplaceShopItemWithFurniture(void* shop, void* shopItem, const JackShopTestConfig* config, int* outputPrice)
{
    if (config == NULL)
    {
        return 0;
    }

    return ReplaceShopItemWithFurnitureName(shop, shopItem, config->FurnitureName, config->Rare, outputPrice);
}

static uint32_t NextJackShopRandomU32(void)
{
    uint32_t state;

    state = g_jackShopRandomState;

    if (state == 0U)
    {
        state = (uint32_t)GetTickCount64();
        state ^= (uint32_t)(uintptr_t)&state;
        state ^= (uint32_t)(uintptr_t)g_lastJackShop;

        if (state == 0U)
        {
            state = 0xA341316CU;
        }
    }

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    g_jackShopRandomState = state;
    return state;
}

static int GetFurnitureNameByRowIndex(const ParsedFurnitureFile* file, uint32_t wantedIndex, char* outputName, size_t outputNameSize)
{
    uint32_t rowIndex;
    uint32_t nameLength;
    const char* name;
    size_t offset;
    size_t rowSize;

    if (file == NULL || outputName == NULL || outputNameSize == 0U || wantedIndex >= file->RowCount)
    {
        return 0;
    }

    offset = 8U;

    for (rowIndex = 0U; rowIndex < file->RowCount; rowIndex++)
    {
        nameLength = ReadU32Le(file->Data + offset);
        name = (const char*)(file->Data + offset + FURNITURE_RECORD_METADATA_BYTES);
        rowSize = (size_t)nameLength + FURNITURE_RECORD_BASE_BYTES;

        if (rowIndex == wantedIndex)
        {
            if (nameLength == 0U || nameLength >= outputNameSize)
            {
                return 0;
            }

            memcpy(outputName, name, nameLength);
            outputName[nameLength] = 0;
            return 1;
        }

        offset += rowSize;
    }

    return 0;
}

static int PickRandomCurrentFurnitureName(char* outputName, size_t outputNameSize)
{
    ParsedFurnitureFile file;
    uint32_t rowIndex;

    if (outputName == NULL || outputNameSize == 0U)
    {
        return 0;
    }

    outputName[0] = 0;

    if (g_currentFurnitureData == NULL || g_currentFurnitureDataSize == 0U)
    {
        LogMessage("Jack shop refresh failed: current furniture_info.data has not been captured yet.");
        return 0;
    }

    if (!ParseFurnitureFile(g_currentFurnitureData, g_currentFurnitureDataSize, &file) || file.RowCount == 0U)
    {
        LogMessage("Jack shop refresh failed: current furniture_info.data snapshot is invalid.");
        return 0;
    }

    rowIndex = NextJackShopRandomU32() % file.RowCount;
    return GetFurnitureNameByRowIndex(&file, rowIndex, outputName, outputNameSize);
}

static void RefreshAllJackShopItemButtons(void* shop)
{
    uint8_t* begin;
    uint32_t count;
    uint32_t index;

    if (shop == NULL || g_shopRefreshShopItem == NULL)
    {
        return;
    }

    begin = NULL;
    count = GetShopItemCount(shop, &begin);

    if (begin == NULL || count == 0U)
    {
        return;
    }

    for (index = 0U; index < count; index++)
    {
        g_shopRefreshShopItem(shop, begin + ((size_t)index * SHOP_ITEM_STRIDE_BYTES), 1U);
    }
}

static int TryRefreshJackShop(void* shop)
{
    uint8_t* begin;
    uint32_t count;
    uint32_t index;
    uint32_t replacedCount;
    char furnitureName[MAX_FURNITURE_NAME_BYTES + 1U];

    if (shop == NULL)
    {
        return 0;
    }

    begin = NULL;
    count = GetShopItemCount(shop, &begin);

    if (begin == NULL || count == 0U)
    {
        LogMessage("Jack shop refresh failed: no live shop slots were available. Close and reopen Jack's shop to rebuild the game's stock vector.");
        return 0;
    }

    replacedCount = 0U;

    for (index = 0U; index < count; index++)
    {
        furnitureName[0] = 0;

        if (!PickRandomCurrentFurnitureName(furnitureName, sizeof(furnitureName)))
        {
            continue;
        }

        if (ReplaceShopItemWithFurnitureName(shop, begin + ((size_t)index * SHOP_ITEM_STRIDE_BYTES), furnitureName, 0, NULL))
        {
            replacedCount++;
        }
    }

    RefreshAllJackShopItemButtons(shop);
    LogMessage("Rerolled Jack shop stock in-place: slots=%u replaced=%u", count, replacedCount);

    if (replacedCount != 0U)
    {
        FurnitureDebugMessageContext context;

        memset(&context, 0, sizeof(context));
        context.ItemCount = count;
        context.ReplacedCount = replacedCount;
        QueueFurnitureDebugMessageEx(FURNITURE_DEBUG_MESSAGE_JACK_SHOP_REFRESHED, &context);
    }

    return replacedCount != 0U;
}

static void TryGuaranteeJackShopFurniture(void* shop)
{
    JackShopTestConfig config;
    char configPath[MAX_PATH_BUFFER];
    void* shopItem;
    uint32_t itemCount;
    int price;

    if (shop == NULL)
    {
        return;
    }

    memset(&config, 0, sizeof(config));
    configPath[0] = 0;
    price = 0;

    if (!LoadJackShopTestConfig(&config, configPath, sizeof(configPath)))
    {
        LogMessage("Jack shop test failed: could not load config.");
        return;
    }

    if (config.FurnitureName[0] == 0)
    {
        LogMessage("Jack shop test failed: config furniture name is empty. Config: %s", configPath);
        return;
    }

    if (config.RefreshBeforeReplace)
    {
        if (!TryRefreshJackShop(shop))
        {
            LogMessage("Jack shop test warning: refresh-before-replace did not complete, attempting slot replacement against current stock.");
        }
    }

    itemCount = 0U;
    shopItem = GetShopItemByConfigSlot(shop, &config, &itemCount);

    if (shopItem == NULL)
    {
        LogMessage("Jack shop test failed: no live shop item slot was available.");
        return;
    }

    if (!ReplaceShopItemWithFurniture(shop, shopItem, &config, &price))
    {
        LogMessage("Jack shop test failed while replacing slot %d with %s.", config.SlotIndex, config.FurnitureName);
        return;
    }

    LogMessage("Jack shop test item installed: slot=%d count=%u furniture=%s rare=%d price=%d", config.SlotIndex, itemCount, config.FurnitureName, config.Rare, price);

    {
        FurnitureDebugMessageContext context;

        memset(&context, 0, sizeof(context));
        context.FurnitureName = config.FurnitureName;
        context.SlotIndex = config.SlotIndex;
        context.ItemCount = itemCount;
        context.Price = price;
        context.Rare = config.Rare;
        QueueFurnitureDebugMessageEx(FURNITURE_DEBUG_MESSAGE_JACK_SHOP_INJECTED, &context);
    }
}

static void __fastcall HookDebugTextAdd(MsvcString* text)
{
    const char* replacementText;
    size_t replacementLength;

    if (InterlockedCompareExchange(&g_inFurnitureEditorUpdate, 0, 0) != 0)
    {
        MaybeReloadFurnitureDebugTextFile();

        if (IsFurnitureCurrentPieceStatusText(text))
        {
            DestroyMsvcStringIfNeeded(text);

            if (!g_furnitureDebugTextState.HideCurrentPiece)
            {
                replacementText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.CurrentPieceText, g_furnitureDebugTextState.CurrentPieceTextLength, GetDefaultFurnitureDebugValueForKey("current_piece_text"), &replacementLength);
                AddFurnitureDebugTextLine(replacementText, replacementLength);
            }

            return;
        }

        if (IsFurnitureUnsavedChangesStatusText(text))
        {
            DestroyMsvcStringIfNeeded(text);

            if (!g_furnitureDebugTextState.HideUnsavedChanges)
            {
                replacementText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.UnsavedChangesText, g_furnitureDebugTextState.UnsavedChangesTextLength, GetDefaultFurnitureDebugValueForKey("unsaved_changes_text"), &replacementLength);
                AddFurnitureDebugTextLine(replacementText, replacementLength);
            }

            return;
        }

        if (g_furnitureDebugTextState.HideCtrlSSaveHint && IsFurnitureCtrlSSaveHintMsvcString(text))
        {
            DestroyMsvcStringIfNeeded(text);
            return;
        }

        if (IsVanillaFurnitureEditorHelpText(text))
        {
            InterlockedExchange(&g_furnitureEditorInstructionsPrintedThisUpdate, 1);
            DestroyMsvcStringIfNeeded(text);
            return;
        }
    }

    if (g_originalDebugTextAdd != NULL)
    {
        g_originalDebugTextAdd(text);
    }
    else
    {
        DestroyMsvcStringIfNeeded(text);
    }
}

static void __fastcall HookDebugTextAddPair(void* context, MsvcString* label, MsvcString* value)
{
    MsvcString replacementLabel;
    const char* replacementText;
    size_t replacementLength;

    if (InterlockedCompareExchange(&g_inFurnitureEditorUpdate, 0, 0) != 0)
    {
        MaybeReloadFurnitureDebugTextFile();

        if (IsFurnitureCurrentPieceStatusText(label))
        {
            if (g_furnitureDebugTextState.HideCurrentPiece)
            {
                return;
            }

            replacementText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.CurrentPieceText, g_furnitureDebugTextState.CurrentPieceTextLength, GetDefaultFurnitureDebugValueForKey("current_piece_text"), &replacementLength);

            if (replacementText == NULL || replacementLength == 0U)
            {
                return;
            }

            memset(&replacementLabel, 0, sizeof(replacementLabel));

            if (!InitializeMsvcStringForDebugText(&replacementLabel, replacementText, replacementLength))
            {
                return;
            }

            if (g_originalDebugTextAddPair != NULL)
            {
                g_originalDebugTextAddPair(context, &replacementLabel, value);
            }

            DestroyMsvcStringIfNeeded(&replacementLabel);
            return;
        }
    }

    if (g_originalDebugTextAddPair != NULL)
    {
        g_originalDebugTextAddPair(context, label, value);
    }
}

static void SetDebugOverlayVisibleOnConsole(void* debugConsole, int visible)
{
    if (debugConsole == NULL)
    {
        return;
    }

    *((uint8_t*)debugConsole + DEBUG_CONSOLE_DEBUG_TEXT_VISIBLE_OFFSET) = visible ? 1U : 0U;
}

static int IsDebugOverlayVisibleOnConsole(void* debugConsole)
{
    if (debugConsole == NULL)
    {
        return 0;
    }

    if (*((uint8_t*)debugConsole + DEBUG_CONSOLE_DEBUG_TEXT_VISIBLE_OFFSET) != 0U)
    {
        return 1;
    }

    return 0;
}

static int IsRecentFurnitureInfoDebugContext(void)
{
    ULONGLONG tick;

    if (g_lastFurnitureEditor == NULL || g_lastFurnitureEditorUpdateTick == 0ULL)
    {
        return 0;
    }

    tick = GetTickCount64();

    if (tick - g_lastFurnitureEditorUpdateTick <= FURNITURE_DEBUG_PENDING_FORCE_VALID_MS)
    {
        return 1;
    }

    return 0;
}

static void __fastcall HookDebugConsoleVisibilityUpdate(void* debugConsole)
{
    int shouldForceVisible;

    shouldForceVisible = InterlockedExchange(&g_pendingDebugOverlayForce, 0) != 0;

    if (shouldForceVisible && !IsRecentFurnitureInfoDebugContext())
    {
        shouldForceVisible = 0;
    }

    if (debugConsole != NULL)
    {
        g_lastDebugConsole = debugConsole;
    }

    if (shouldForceVisible)
    {
        SetDebugOverlayVisibleOnConsole(debugConsole, 1);
    }

    if (g_originalDebugConsoleVisibilityUpdate != NULL)
    {
        g_originalDebugConsoleVisibilityUpdate(debugConsole);
    }

    if (shouldForceVisible)
    {
        SetDebugOverlayVisibleOnConsole(debugConsole, 1);
    }
}

static int PrepareFurnitureDebugOverlayForFurnitureInstructions(void* editor, int instructionsWerePrinted)
{
    ULONGLONG tick;
    ULONGLONG previousTick;
    int shouldForce;

    if (!instructionsWerePrinted)
    {
        return 0;
    }

    if (!IsFurnitureEditorReadyForDebugInfo(editor))
    {
        return 0;
    }

    tick = GetTickCount64();
    previousTick = g_lastFurnitureEditorUpdateTick;
    shouldForce = 0;

    if (editor != g_lastFurnitureEditor)
    {
        shouldForce = 1;
    }
    else if (previousTick == 0ULL || tick - previousTick > FURNITURE_DEBUG_AUTO_SHOW_REENTRY_MS)
    {
        shouldForce = 1;
    }

    g_lastFurnitureEditor = editor;
    g_lastFurnitureEditorUpdateTick = tick;

    if (!shouldForce)
    {
        return 1;
    }

    if (g_lastDebugConsole != NULL)
    {
        SetDebugOverlayVisibleOnConsole(g_lastDebugConsole, 1);
    }

    InterlockedExchange(&g_pendingDebugOverlayForce, 1);
    QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN, NULL, 0U, NULL, 0U);
    return 1;
}

static void ToggleFurnitureDebugOverlay(void)
{
    int nextVisible;

    if (g_lastDebugConsole == NULL)
    {
        InterlockedExchange(&g_pendingDebugOverlayForce, 1);
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN, NULL, 0U, NULL, 0U);
        return;
    }

    nextVisible = IsDebugOverlayVisibleOnConsole(g_lastDebugConsole) ? 0 : 1;
    SetDebugOverlayVisibleOnConsole(g_lastDebugConsole, nextVisible);

    if (nextVisible)
    {
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_OVERLAY_SHOWN, NULL, 0U, NULL, 0U);
    }
    else
    {
        QueueFurnitureDebugMessage(FURNITURE_DEBUG_MESSAGE_OVERLAY_HIDDEN, NULL, 0U, NULL, 0U);
    }
}

static void HideFurnitureDebugOverlayOnDetach(void)
{
    InterlockedExchange(&g_pendingDebugOverlayForce, 0);

    if (g_lastDebugConsole != NULL)
    {
        SetDebugOverlayVisibleOnConsole(g_lastDebugConsole, 0);
    }

    g_lastDebugConsole = NULL;
    g_lastFurnitureEditor = NULL;
    g_lastJackShop = NULL;
    g_lastFurnitureEditorUpdateTick = 0ULL;
    g_lastDebugToggleChordDown = 0U;
    g_lastJackShopRefreshChordDown = 0U;
    g_lastJackShopGuaranteeChordDown = 0U;
}

static int InitializeMsvcStringForDebugText(MsvcString* value, const char* text, size_t length)
{
    size_t capacity;
    char* heapText;

    if (value == NULL || text == NULL)
    {
        return 0;
    }

    memset(value, 0, sizeof(MsvcString));
    value->Length = length;

    if (length <= 15U)
    {
        value->Capacity = 15U;

        if (length != 0U)
        {
            memcpy(value->Data.Small, text, length);
        }

        value->Data.Small[length] = 0;
        return 1;
    }

    if (g_gameAllocate == NULL)
    {
        return 0;
    }

    capacity = length | 15U;

    if (capacity < length || capacity == (size_t)-1)
    {
        return 0;
    }

    heapText = (char*)g_gameAllocate(capacity + 1U);

    if (heapText == NULL)
    {
        return 0;
    }

    memcpy(heapText, text, length);
    heapText[length] = 0;
    value->Data.Heap = heapText;
    value->Capacity = capacity;
    return 1;
}

static void AddFurnitureDebugTextLine(const char* text, size_t length)
{
    MsvcString value;

    if (g_originalDebugTextAdd == NULL || text == NULL)
    {
        return;
    }

    if (!InitializeMsvcStringForDebugText(&value, text, length))
    {
        return;
    }

    g_originalDebugTextAdd(&value);
}

static int AppendFurnitureDebugHtmlEscapedText(BufferBuilder* builder, const char* text, size_t length)
{
    size_t index;

    if (builder == NULL || text == NULL)
    {
        return 0;
    }

    for (index = 0U; index < length; index++)
    {
        if (text[index] == '&')
        {
            if (!BufferAppendCString(builder, "&amp;"))
            {
                return 0;
            }
        }
        else if (text[index] == '<')
        {
            if (!BufferAppendCString(builder, "&lt;"))
            {
                return 0;
            }
        }
        else if (text[index] == '>')
        {
            if (!BufferAppendCString(builder, "&gt;"))
            {
                return 0;
            }
        }
        else if (text[index] == '"')
        {
            if (!BufferAppendCString(builder, "&quot;"))
            {
                return 0;
            }
        }
        else
        {
            if (!BufferAppendChar(builder, text[index]))
            {
                return 0;
            }
        }
    }

    return 1;
}

static void AddFurnitureDebugTextLiteral(const char* text)
{
    if (text == NULL)
    {
        return;
    }

    AddFurnitureDebugTextLine(text, strlen(text));
}

static void AddDefaultFurnitureDebugTextLines(void)
{
    const char* text;
    const char* lineStart;
    const char* cursor;
    const char* lineEnd;
    size_t lineLength;

    text = GetDefaultFurnitureDebugText();
    lineStart = text;
    cursor = text;

    while (*cursor != 0)
    {
        if (*cursor == '\n')
        {
            lineEnd = cursor;

            if (lineEnd > lineStart && lineEnd[-1] == '\r')
            {
                lineEnd--;
            }

            lineLength = (size_t)(lineEnd - lineStart);

            if (lineLength != 0U && lineStart[0] != '@' && lineStart[0] != '#' && !IsFurnitureCtrlSSaveHintLine(lineStart, lineLength))
            {
                AddFurnitureDebugTextLine(lineStart, lineLength);
            }

            lineStart = cursor + 1;
        }

        cursor++;
    }

    if (cursor > lineStart)
    {
        lineLength = (size_t)(cursor - lineStart);

        if (lineLength != 0U && lineStart[0] != '@' && lineStart[0] != '#')
        {
            AddFurnitureDebugTextLine(lineStart, lineLength);
        }
    }
}

static void AddFurnitureRuntimeDebugMessageText(const char* text)
{
    const char* lineStart;
    const char* cursor;
    const char* lineEnd;

    if (text == NULL)
    {
        return;
    }

    lineStart = text;
    cursor = text;

    while (*cursor != 0)
    {
        if (*cursor == '\n')
        {
            lineEnd = cursor;

            if (lineEnd > lineStart && lineEnd[-1] == '\r')
            {
                lineEnd--;
            }

            AddFurnitureDebugTextLine(lineStart, (size_t)(lineEnd - lineStart));
            lineStart = cursor + 1;
        }

        cursor++;
    }

    if (cursor >= lineStart)
    {
        AddFurnitureDebugTextLine(lineStart, (size_t)(cursor - lineStart));
    }
}

static void AddRuntimeFurnitureDebugMessages(void)
{
    uint32_t index;

    for (index = 0U; index < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; index++)
    {
        if (g_runtimeDebugMessages[index].Text != NULL)
        {
            AddFurnitureRuntimeDebugMessageText(g_runtimeDebugMessages[index].Text);
        }
    }
}

static void ShowFurnitureEditorDebugText(void)
{
    uint32_t lineIndex;
    const char* headerText;
    size_t headerLength;
    int hasRuntimeMessages;

    MaybeReloadFurnitureDebugTextFile();
    hasRuntimeMessages = HasRuntimeDebugMessages();

    if (!g_furnitureDebugTextState.HideInstructionsHeader)
    {
        headerText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.InstructionsHeader, g_furnitureDebugTextState.InstructionsHeaderLength, GetDefaultFurnitureDebugValueForKey("instructions_header"), &headerLength);

        if (headerLength != 0U)
        {
            AddFurnitureDebugTextLiteral("");
            AddFurnitureDebugTextLine(headerText, headerLength);
        }
    }

    if (!g_furnitureDebugTextState.Loaded || !g_furnitureDebugTextState.HasFile)
    {
        AddDefaultFurnitureDebugTextLines();
    }
    else
    {
        for (lineIndex = 0U; lineIndex < g_furnitureDebugTextState.LineCount; lineIndex++)
        {
            if (g_furnitureDebugTextState.HideCtrlSSaveHint && IsFurnitureCtrlSSaveHintLine(g_furnitureDebugTextState.Lines[lineIndex].Text, g_furnitureDebugTextState.Lines[lineIndex].Length))
            {
                continue;
            }

            AddFurnitureDebugTextLine(g_furnitureDebugTextState.Lines[lineIndex].Text, g_furnitureDebugTextState.Lines[lineIndex].Length);
        }
    }

    if (hasRuntimeMessages)
    {
        AddFurnitureDebugTextLiteral("");

        if (!g_furnitureDebugTextState.HideLogsHeader)
        {
            headerText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.LogsHeader, g_furnitureDebugTextState.LogsHeaderLength, GetDefaultFurnitureDebugValueForKey("logs_header"), &headerLength);

            if (headerLength != 0U)
            {
                AddFurnitureDebugTextLine(headerText, headerLength);
            }
        }

        AddRuntimeFurnitureDebugMessages();
    }
}

static void ShowFurnitureRuntimeDebugLogTextOnly(void)
{
    const char* headerText;
    size_t headerLength;

    MaybeReloadFurnitureDebugTextFile();

    if (!HasRuntimeDebugMessages())
    {
        return;
    }

    AddFurnitureDebugTextLiteral("");

    if (!g_furnitureDebugTextState.HideLogsHeader)
    {
        headerText = GetConfiguredFurnitureDebugTextValue(g_furnitureDebugTextState.LogsHeader, g_furnitureDebugTextState.LogsHeaderLength, GetDefaultFurnitureDebugValueForKey("logs_header"), &headerLength);

        if (headerLength != 0U)
        {
            AddFurnitureDebugTextLine(headerText, headerLength);
        }
    }

    AddRuntimeFurnitureDebugMessages();
}

static void AddCandidateFile(PathSet* paths, const char* baseDirectory, const char* relativePath)
{
    char candidate[MAX_PATH_BUFFER];

    if (paths == NULL || baseDirectory == NULL || relativePath == NULL)
    {
        return;
    }

    JoinPath(candidate, sizeof(candidate), baseDirectory, relativePath);

    if (FileExists(candidate))
    {
        PathSetAdd(paths, candidate);
    }
}

static void AddCandidateFilesInModDirectory(PathSet* paths, const char* modDirectory)
{
    if (paths == NULL || modDirectory == NULL || !DirectoryExists(modDirectory))
    {
        return;
    }

    AddCandidateFile(paths, modDirectory, "data\\furniture_info.append.data");
    AddCandidateFile(paths, modDirectory, "data\\furniture_info.data.append");
    AddCandidateFile(paths, modDirectory, "data\\furniture_info.data");

    AddCandidateFile(paths, modDirectory, "output\\furniture_info.append.data");
    AddCandidateFile(paths, modDirectory, "output\\furniture_info.data.append");
    AddCandidateFile(paths, modDirectory, "output\\furniture_info.data");

    AddCandidateFile(paths, modDirectory, "furniture_info.append.data");
    AddCandidateFile(paths, modDirectory, "furniture_info.data.append");
    AddCandidateFile(paths, modDirectory, "furniture_info.data");
}

static void ScanModRoot(PathSet* paths, const char* modRoot)
{
    char searchPath[MAX_PATH_BUFFER];
    char childPath[MAX_PATH_BUFFER];
    WIN32_FIND_DATAA findData;
    HANDLE findHandle;

    if (paths == NULL || modRoot == NULL || !DirectoryExists(modRoot))
    {
        return;
    }

    AddCandidateFilesInModDirectory(paths, modRoot);
    JoinPath(searchPath, sizeof(searchPath), modRoot, "*");
    findHandle = FindFirstFileA(searchPath, &findData);

    if (findHandle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U && strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0)
        {
            JoinPath(childPath, sizeof(childPath), modRoot, findData.cFileName);
            AddCandidateFilesInModDirectory(paths, childPath);
        }
    }
    while (FindNextFileA(findHandle, &findData));

    FindClose(findHandle);
}

static void DiscoverFurnitureFiles(PathSet* paths)
{
    char dllPath[MAX_PATH_BUFFER];
    char dllDirectory[MAX_PATH_BUFFER];
    char parentDirectory[MAX_PATH_BUFFER];
    char gamePath[MAX_PATH_BUFFER];
    char gameDirectory[MAX_PATH_BUFFER];
    char gameModsDirectory[MAX_PATH_BUFFER];

    if (paths == NULL)
    {
        return;
    }

    dllPath[0] = 0;
    GetModuleFileNameA(g_moduleHandle, dllPath, sizeof(dllPath));
    strncpy(dllDirectory, dllPath, sizeof(dllDirectory) - 1U);
    dllDirectory[sizeof(dllDirectory) - 1U] = 0;
    TrimToDirectory(dllDirectory);
    AddCandidateFilesInModDirectory(paths, dllDirectory);

    if (GetParentDirectory(dllDirectory, parentDirectory, sizeof(parentDirectory)))
    {
        ScanModRoot(paths, parentDirectory);
    }

    gamePath[0] = 0;
    GetModuleFileNameA(NULL, gamePath, sizeof(gamePath));
    strncpy(gameDirectory, gamePath, sizeof(gameDirectory) - 1U);
    gameDirectory[sizeof(gameDirectory) - 1U] = 0;
    TrimToDirectory(gameDirectory);
    JoinPath(gameModsDirectory, sizeof(gameModsDirectory), gameDirectory, "mods");
    ScanModRoot(paths, gameModsDirectory);
}

static int BuildMergedFurnitureData(const uint8_t* baseData, uint32_t baseSize, uint8_t** mergedData, uint32_t* mergedSize, uint32_t* mergedRows)
{
    ParsedFurnitureFile baseFile;
    ParsedFurnitureFile modFile;
    NameSet names;
    PathSet paths;
    BufferBuilder output;
    FileBytes fileBytes;
    uint32_t pathIndex;
    uint32_t addedRows;
    uint32_t finalRows;

    if (baseData == NULL || mergedData == NULL || mergedSize == NULL || mergedRows == NULL)
    {
        return 0;
    }

    *mergedData = NULL;
    *mergedSize = 0U;
    *mergedRows = 0U;
    memset(&names, 0, sizeof(names));
    memset(&paths, 0, sizeof(paths));
    memset(&output, 0, sizeof(output));
    memset(&fileBytes, 0, sizeof(fileBytes));

    if (!ParseFurnitureFile(baseData, baseSize, &baseFile))
    {
        LogMessage("Base furniture_info.data failed validation, not patching!");
        return 0;
    }

    if (!StoreVanillaFurnitureDataSnapshot(baseData, baseSize))
    {
        LogMessage("Could not cache vanilla furniture_info.data, name dump will be unavailable!");
    }

    if (!BufferAppend(&output, baseData, baseSize))
    {
        NameSetFree(&names);
        BufferFree(&output);
        return 0;
    }

    if (!CollectNamesFromFurnitureFile(&baseFile, &names))
    {
        NameSetFree(&names);
        BufferFree(&output);
        return 0;
    }

    if (!LoadVanillaFurnitureNamesIfExists(&names))
    {
        NameSetFree(&names);
        BufferFree(&output);
        return 0;
    }

    DiscoverFurnitureFiles(&paths);
    addedRows = 0U;

    for (pathIndex = 0U; pathIndex < paths.Count; pathIndex++)
    {
        if (!ReadWholeFile(paths.Paths[pathIndex], &fileBytes))
        {
            LogMessage("Could not read furniture append file: %s", paths.Paths[pathIndex]);
            continue;
        }

        if (ParseFurnitureFile(fileBytes.Data, fileBytes.Size, &modFile))
        {
            LogMessage("Scanning furniture append file: %s (%u rows)", paths.Paths[pathIndex], modFile.RowCount);
            AppendNewRowsFromFurnitureFile(&modFile, &names, &output, &addedRows);
        }
        else
        {
            LogMessage("Ignored invalid furniture append file: %s", paths.Paths[pathIndex]);
        }

        FreeFileBytes(&fileBytes);
    }

    if (addedRows == 0U)
    {
        LogMessage("No new furniture_info rows found! Base rows: %u", baseFile.RowCount);
        PathSetFree(&paths);
        NameSetFree(&names);
        BufferFree(&output);
        return 1;
    }

    finalRows = baseFile.RowCount + addedRows;
    WriteU32Le(output.Data + 4U, finalRows);

    if (output.Size > 0xFFFFFFFFULL)
    {
        PathSetFree(&paths);
        NameSetFree(&names);
        BufferFree(&output);
        return 0;
    }

    *mergedData = output.Data;
    *mergedSize = (uint32_t)output.Size;
    *mergedRows = finalRows;
    memset(&output, 0, sizeof(output));
    LogMessage("Merged furniture_info.data: base rows=%u, added rows=%u, final rows=%u, final bytes=%u", baseFile.RowCount, addedRows, finalRows, *mergedSize);
    PathSetFree(&paths);
    NameSetFree(&names);
    BufferFree(&output);
    return 1;
}

static void PatchFurnitureResource(PackedFileData* resource)
{
    uint8_t* mergedData;
    uint32_t mergedSize;
    uint32_t mergedRows;

    if (resource == NULL || resource->Data == NULL || resource->Size == 0U)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_patchFinished, 0, 0) != 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_patchStarted, 1, 0) != 0)
    {
        return;
    }

    mergedData = NULL;
    mergedSize = 0U;
    mergedRows = 0U;

    if (BuildMergedFurnitureData(resource->Data, resource->Size, &mergedData, &mergedSize, &mergedRows))
    {
        if (mergedData != NULL && mergedSize != 0U)
        {
            resource->Data = mergedData;
            resource->Size = mergedSize;
            resource->OwnsData = 0U;
            StoreCurrentFurnitureDataSnapshot(resource->Data, resource->Size);
            LogMessage("Patched loaded furniture_info.data in memory! Rows=%u Size=%u", mergedRows, mergedSize);
        }
        else
        {
            StoreCurrentFurnitureDataSnapshot(resource->Data, resource->Size);
        }

        InterlockedExchange(&g_patchFinished, 1);
    }
    else
    {
        LogMessage("Furniture append patch failed, leaving original data untouched!");
    }

    InterlockedExchange(&g_patchStarted, 0);
}


static void __fastcall HookShopInit(void* shop, MsvcString* shopName, void* shopItems, uint8_t houseShop)
{
    char shopNameBuffer[MAX_PATH_BUFFER];
    int isJackShop;

    shopNameBuffer[0] = 0;
    isJackShop = 0;

    if (CopyMsvcString(shopNameBuffer, sizeof(shopNameBuffer), shopName))
    {
        isJackShop = IsJackShopName(shopNameBuffer);
    }

    if (g_originalShopInit != NULL)
    {
        g_originalShopInit(shop, shopName, shopItems, houseShop);
    }

    if (isJackShop)
    {
        g_lastJackShop = shop;
        LogMessage("Remembered Jack shop instance for test hotkeys: %s", shopNameBuffer);
    }
}

static void __fastcall HookShopUpdate(void* shop)
{
    if (g_originalShopUpdate != NULL)
    {
        g_originalShopUpdate(shop);
    }

    if (shop == NULL || shop != g_lastJackShop)
    {
        return;
    }

    if (IsJackShopGuaranteeHotkeyPressedEdge())
    {
        TryGuaranteeJackShopFurniture(shop);
    }

    if (IsJackShopRefreshHotkeyPressedEdge())
    {
        TryRefreshJackShop(shop);
    }

    if (HasRuntimeDebugMessages())
    {
        if (g_lastDebugConsole != NULL)
        {
            SetDebugOverlayVisibleOnConsole(g_lastDebugConsole, 1);
        }

        ShowFurnitureRuntimeDebugLogTextOnly();
    }
}

static PackedFileData* __fastcall HookPackedFileLoad(void* context, MsvcString* path, uint8_t useCache, uint8_t allowFallback)
{
    int isFurnitureInfo;
    PackedFileData* resource;

    isFurnitureInfo = IsFurnitureInfoPath(path);
    resource = NULL;

    if (g_originalPackedFileLoad != NULL)
    {
        resource = g_originalPackedFileLoad(context, path, useCache, allowFallback);
    }

    if (isFurnitureInfo && resource != NULL)
    {
        PatchFurnitureResource(resource);
    }

    return resource;
}

static void __fastcall HookFurnitureEditorUpdate(void* editor)
{
    int furnitureInfoReady;
    int instructionsWerePrinted;

    InterlockedExchange(&g_furnitureEditorInstructionsPrintedThisUpdate, 0);

    if (g_originalFurnitureEditorUpdate != NULL)
    {
        InterlockedExchange(&g_inFurnitureEditorUpdate, 1);
        g_originalFurnitureEditorUpdate(editor);
        InterlockedExchange(&g_inFurnitureEditorUpdate, 0);
    }

    instructionsWerePrinted = InterlockedExchange(&g_furnitureEditorInstructionsPrintedThisUpdate, 0) != 0;
    furnitureInfoReady = PrepareFurnitureDebugOverlayForFurnitureInstructions(editor, instructionsWerePrinted);

    if (furnitureInfoReady && IsDebugToggleHotkeyPressedEdge())
    {
        ToggleFurnitureDebugOverlay();
    }

    if (IsVanillaDumpHotkeyPressedEdge())
    {
        TryDumpVanillaFurnitureNames();
    }

    if (IsOverwriteCaptureHotkeyPressedEdge())
    {
        TryCaptureCurrentFurniture(editor, 1);
    }

    if (IsAppendCaptureHotkeyPressedEdge())
    {
        TryCaptureCurrentFurniture(editor, 0);
    }

    if (furnitureInfoReady)
    {
        ShowFurnitureEditorDebugText();
    }
}

static void Initialize(void)
{
    void* trampoline;
    int installedHookCount;

    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
    {
        return;
    }

    if (!MJ_Resolve(&g_mj))
    {
        InterlockedExchange(&g_installed, 0);
        return;
    }

    g_gameBase = g_mj.GetGameBase();

    if (g_gameBase == 0U)
    {
        g_gameBase = (UINT_PTR)GetModuleHandleA(NULL);
    }

    if (g_gameBase != 0U)
    {
        g_furniturePayloadLookup = (fn_furniture_payload_lookup)(g_gameBase + RVA_FURNITURE_PAYLOAD_LOOKUP);
        g_gameAllocate = (fn_game_allocate)(g_gameBase + RVA_GAME_ALLOCATE);
        g_msvcStringDestruct = (fn_msvc_string_destruct)(g_gameBase + RVA_MSVC_STRING_DESTRUCT);
        g_shopInitShop = (fn_shop_init_shop)(g_gameBase + RVA_SHOP_INIT_SHOP);
        g_shopRefreshShopItem = (fn_shop_refresh_shop_item)(g_gameBase + RVA_SHOP_REFRESH_SHOP_ITEM);
        g_furniturePriceLookup = (fn_furniture_price_lookup)(g_gameBase + RVA_FURNITURE_PRICE_LOOKUP);
        g_furnitureEffectsGlobal = (void**)(g_gameBase + RVA_GLOBAL_FURNITURE_EFFECTS_PTR);
    }

    installedHookCount = 0;
    trampoline = NULL;

    if (g_mj.InstallHook(RVA_PACKED_FILE_LOAD, PACKED_FILE_LOAD_STOLEN_BYTES, (void*)HookPackedFileLoad, &trampoline, 5, MOD_NAME))
    {
        g_originalPackedFileLoad = (fn_packed_file_load)trampoline;
        installedHookCount++;
        LogMessage("Installed packed file hook at RVA 0x%08X!", RVA_PACKED_FILE_LOAD);
    }
    else
    {
        LogMessage("Failed to install packed file hook at RVA 0x%08X!", RVA_PACKED_FILE_LOAD);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_FURNITURE_EDITOR_UPDATE, FURNITURE_EDITOR_UPDATE_STOLEN_BYTES, (void*)HookFurnitureEditorUpdate, &trampoline, 5, MOD_NAME))
    {
        g_originalFurnitureEditorUpdate = (fn_furniture_editor_update)trampoline;
        installedHookCount++;
        LogMessage("Installed furniture editor hotkey hook at RVA 0x%08X!", RVA_FURNITURE_EDITOR_UPDATE);
    }
    else
    {
        LogMessage("Failed to install furniture editor hotkey hook at RVA 0x%08X!", RVA_FURNITURE_EDITOR_UPDATE);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_SHOP_INIT, SHOP_INIT_STOLEN_BYTES, (void*)HookShopInit, &trampoline, 5, MOD_NAME))
    {
        g_originalShopInit = (fn_shop_init)trampoline;
        installedHookCount++;
        LogMessage("Installed Jack shop init hook at RVA 0x%08X!", RVA_SHOP_INIT);
    }
    else
    {
        LogMessage("Failed to install Jack shop init hook at RVA 0x%08X!", RVA_SHOP_INIT);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_SHOP_UPDATE, SHOP_UPDATE_STOLEN_BYTES, (void*)HookShopUpdate, &trampoline, 5, MOD_NAME))
    {
        g_originalShopUpdate = (fn_shop_update)trampoline;
        installedHookCount++;
        LogMessage("Installed Jack shop hotkey hook at RVA 0x%08X!", RVA_SHOP_UPDATE);
    }
    else
    {
        LogMessage("Failed to install Jack shop hotkey hook at RVA 0x%08X!", RVA_SHOP_UPDATE);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_DEBUG_TEXT_ADD, DEBUG_TEXT_ADD_STOLEN_BYTES, (void*)HookDebugTextAdd, &trampoline, 5, MOD_NAME))
    {
        g_originalDebugTextAdd = (fn_debug_text_add)trampoline;
        installedHookCount++;
        LogMessage("Installed debug text replacement hook at RVA 0x%08X!", RVA_DEBUG_TEXT_ADD);
    }
    else
    {
        if (g_gameBase != 0U)
        {
            g_originalDebugTextAdd = (fn_debug_text_add)(g_gameBase + RVA_DEBUG_TEXT_ADD);
        }

        LogMessage("Failed to install debug text replacement hook at RVA 0x%08X!", RVA_DEBUG_TEXT_ADD);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_DEBUG_TEXT_ADD_PAIR, DEBUG_TEXT_ADD_PAIR_STOLEN_BYTES, (void*)HookDebugTextAddPair, &trampoline, 5, MOD_NAME))
    {
        g_originalDebugTextAddPair = (fn_debug_text_add_pair)trampoline;
        installedHookCount++;
        LogMessage("Installed debug text label/value replacement hook at RVA 0x%08X!", RVA_DEBUG_TEXT_ADD_PAIR);
    }
    else
    {
        if (g_gameBase != 0U)
        {
            g_originalDebugTextAddPair = (fn_debug_text_add_pair)(g_gameBase + RVA_DEBUG_TEXT_ADD_PAIR);
        }

        LogMessage("Failed to install debug text label/value replacement hook at RVA 0x%08X!", RVA_DEBUG_TEXT_ADD_PAIR);
    }

    trampoline = NULL;

    if (g_mj.InstallHook(RVA_DEBUG_CONSOLE_VISIBILITY_UPDATE, DEBUG_CONSOLE_VISIBILITY_UPDATE_STOLEN_BYTES, (void*)HookDebugConsoleVisibilityUpdate, &trampoline, 5, MOD_NAME))
    {
        g_originalDebugConsoleVisibilityUpdate = (fn_debug_console_visibility_update)trampoline;
        installedHookCount++;
        LogMessage("Installed debug overlay visibility hook at RVA 0x%08X!", RVA_DEBUG_CONSOLE_VISIBILITY_UPDATE);
    }
    else
    {
        LogMessage("Failed to install debug overlay visibility hook at RVA 0x%08X!", RVA_DEBUG_CONSOLE_VISIBILITY_UPDATE);
    }

    if (installedHookCount == 0 || g_furniturePayloadLookup == NULL)
    {
        LogMessage("MewFurnitureFramework did not install all required hooks/functions!");

        if (installedHookCount == 0)
        {
            InterlockedExchange(&g_installed, 0);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE moduleHandle, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleHandle = moduleHandle;
        DisableThreadLibraryCalls(moduleHandle);
        Initialize();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        HideFurnitureDebugOverlayOnDetach();

        if (g_vanillaFurnitureData != NULL)
        {
            HeapFree(GetProcessHeap(), 0, g_vanillaFurnitureData);
            g_vanillaFurnitureData = NULL;
            g_vanillaFurnitureDataSize = 0U;
        }

        for (uint32_t messageIndex = 0U; messageIndex < FURNITURE_DEBUG_RUNTIME_MESSAGE_CAPACITY; messageIndex++)
        {
            FreeRuntimeDebugMessage(&g_runtimeDebugMessages[messageIndex]);
        }

        FreeFurnitureDebugTextState(&g_furnitureDebugTextState);
    }

    return TRUE;
}