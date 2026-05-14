#ifndef MEWJECTOR_H
#define MEWJECTOR_H

#include <windows.h>
#include <string.h>

#define MJ_API_VERSION 3

typedef int (__cdecl *MJ_fn_InstallHook)(UINT_PTR rva, int stolenBytes, void* hookFn, void** outTrampoline, int priority, const char* owner);
typedef int (__cdecl *MJ_fn_QueryHook)(UINT_PTR rva);
typedef UINT_PTR (__cdecl *MJ_fn_AllocTypeIdPair)(const char* owner);
typedef int (__cdecl *MJ_fn_RegisterName)(const char* category, const char* name, const char* owner);
typedef const char* (__cdecl *MJ_fn_LookupName)(const char* category, const char* name);
typedef UINT_PTR (__cdecl *MJ_fn_GetGameBase)(void);
typedef void (__cdecl *MJ_fn_Log)(const char* owner, const char* fmt, ...);
typedef int (__cdecl *MJ_fn_VerifyHooks)(void);
typedef int (__cdecl *MJ_fn_GetVersion)(void);

typedef struct MewjectorAPI
{
    MJ_fn_InstallHook InstallHook;
    MJ_fn_QueryHook QueryHook;
    MJ_fn_AllocTypeIdPair AllocTypeIdPair;
    MJ_fn_RegisterName RegisterName;
    MJ_fn_LookupName LookupName;
    MJ_fn_GetGameBase GetGameBase;
    MJ_fn_Log Log;
    MJ_fn_VerifyHooks VerifyHooks;
    MJ_fn_GetVersion GetVersion;
} MewjectorAPI;

static __inline int MJ_Resolve(MewjectorAPI* api)
{
    HMODULE moduleHandle;

    if (api == NULL)
    {
        return 0;
    }

    memset(api, 0, sizeof(MewjectorAPI));

    moduleHandle = GetModuleHandleA("version.dll");

    if (moduleHandle == NULL)
    {
        return 0;
    }

    api->GetVersion = (MJ_fn_GetVersion)GetProcAddress(moduleHandle, "MJ_GetVersion");

    if (api->GetVersion == NULL)
    {
        return 0;
    }

    if (api->GetVersion() < MJ_API_VERSION)
    {
        return 0;
    }

    api->InstallHook = (MJ_fn_InstallHook)GetProcAddress(moduleHandle, "MJ_InstallHook");
    api->QueryHook = (MJ_fn_QueryHook)GetProcAddress(moduleHandle, "MJ_QueryHook");
    api->AllocTypeIdPair = (MJ_fn_AllocTypeIdPair)GetProcAddress(moduleHandle, "MJ_AllocTypeIdPair");
    api->RegisterName = (MJ_fn_RegisterName)GetProcAddress(moduleHandle, "MJ_RegisterName");
    api->LookupName = (MJ_fn_LookupName)GetProcAddress(moduleHandle, "MJ_LookupName");
    api->GetGameBase = (MJ_fn_GetGameBase)GetProcAddress(moduleHandle, "MJ_GetGameBase");
    api->Log = (MJ_fn_Log)GetProcAddress(moduleHandle, "MJ_Log");
    api->VerifyHooks = (MJ_fn_VerifyHooks)GetProcAddress(moduleHandle, "MJ_VerifyHooks");

    if (api->InstallHook == NULL || api->QueryHook == NULL || api->AllocTypeIdPair == NULL || api->RegisterName == NULL || api->LookupName == NULL || api->GetGameBase == NULL || api->Log == NULL || api->VerifyHooks == NULL)
    {
        memset(api, 0, sizeof(MewjectorAPI));
        return 0;
    }

    return 1;
}

#endif
