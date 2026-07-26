#ifndef MODERNGEKKO_MOD_ABI_H
#define MODERNGEKKO_MOD_ABI_H

#include "moderngekko/cpu_state.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODERNGEKKO_MOD_ABI_VERSION 1u
#define MODERNGEKKO_MOD_HOST_ABI_VERSION 1u
#define MODERNGEKKO_GET_MOD_SYMBOL "moderngekko_get_mod"

#if defined(_WIN32)
#define MODERNGEKKO_MOD_VISIBILITY __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define MODERNGEKKO_MOD_VISIBILITY __attribute__((visibility("default")))
#else
#define MODERNGEKKO_MOD_VISIBILITY
#endif

#if defined(__cplusplus)
#define MODERNGEKKO_MOD_EXPORT extern "C" MODERNGEKKO_MOD_VISIBILITY
#else
#define MODERNGEKKO_MOD_EXPORT MODERNGEKKO_MOD_VISIBILITY
#endif

typedef void (*ModernGekkoModFunction)(CPUState* state);

typedef enum ModernGekkoModPatchFlags
{
    MODERNGEKKO_MOD_PATCH_NONE = 0,
    MODERNGEKKO_MOD_PATCH_FORCE = 1
} ModernGekkoModPatchFlags;

typedef enum ModernGekkoModHookKind
{
    MODERNGEKKO_MOD_HOOK_ENTRY = 0,
    MODERNGEKKO_MOD_HOOK_RETURN = 1
} ModernGekkoModHookKind;

typedef struct ModernGekkoModDependency
{
    const char* id;
    const char* minimum_version;
    uint32_t optional;
} ModernGekkoModDependency;

typedef struct ModernGekkoModPatch
{
    uint32_t address;
    ModernGekkoModFunction function;
    uint32_t flags;
} ModernGekkoModPatch;

typedef struct ModernGekkoModHook
{
    uint32_t address;
    ModernGekkoModFunction function;
    uint32_t kind;
} ModernGekkoModHook;

typedef struct ModernGekkoModExportEntry
{
    const char* name;
    ModernGekkoModFunction function;
} ModernGekkoModExportEntry;

typedef struct ModernGekkoModImportEntry
{
    const char* dependency_id;
    const char* name;
    ModernGekkoModFunction* slot;
} ModernGekkoModImportEntry;

typedef struct ModernGekkoModEvent
{
    const char* name;
} ModernGekkoModEvent;

typedef struct ModernGekkoModCallback
{
    const char* dependency_id;
    const char* event_name;
    ModernGekkoModFunction function;
} ModernGekkoModCallback;

typedef struct ModernGekkoModHostApi
{
    uint32_t abi_version;
    void* user_data;
    int (*trigger_event)(void* user_data, const char* provider_id,
                         const char* event_name, CPUState* state);
    ModernGekkoModFunction (*find_export)(void* user_data,
                                          const char* provider_id,
                                          const char* export_name);
} ModernGekkoModHostApi;

typedef struct ModernGekkoModDesc
{
    uint32_t abi_version;
    uint32_t cpu_abi_version;
    uint32_t cpu_state_size;
    char game_id[8];
    const char* id;
    const char* version;
    const char* display_name;
    const ModernGekkoModDependency* dependencies;
    uint32_t num_dependencies;
    const ModernGekkoModPatch* patches;
    uint32_t num_patches;
    const ModernGekkoModHook* hooks;
    uint32_t num_hooks;
    const ModernGekkoModExportEntry* exports;
    uint32_t num_exports;
    const ModernGekkoModImportEntry* imports;
    uint32_t num_imports;
    const ModernGekkoModEvent* events;
    uint32_t num_events;
    const ModernGekkoModCallback* callbacks;
    uint32_t num_callbacks;
    void (*on_load)(const ModernGekkoModHostApi* api);
    void (*on_unload)(void);
} ModernGekkoModDesc;

typedef const ModernGekkoModDesc* (*ModernGekkoGetModFn)(void);

#define RECOMP_PATCH(address, function) \
    { (address), (function), MODERNGEKKO_MOD_PATCH_NONE }
#define RECOMP_FORCE_PATCH(address, function) \
    { (address), (function), MODERNGEKKO_MOD_PATCH_FORCE }
#define RECOMP_HOOK(address, function) \
    { (address), (function), MODERNGEKKO_MOD_HOOK_ENTRY }
#define RECOMP_HOOK_RETURN(address, function) \
    { (address), (function), MODERNGEKKO_MOD_HOOK_RETURN }
#define RECOMP_EXPORT(name, function) { (name), (function) }
#define RECOMP_IMPORT(mod, name, slot) { (mod), (name), (slot) }
#define RECOMP_DECLARE_EVENT(name) { (name) }
#define RECOMP_CALLBACK(mod, event, function) { (mod), (event), (function) }

static inline uint32_t moderngekko_mod_arg_u32(const CPUState* state,
                                                uint32_t index)
{
    return index < 8u ? state->gpr[3u + index] : 0u;
}

static inline void moderngekko_mod_return_u32(CPUState* state, uint32_t value)
{
    state->gpr[3] = value;
    state->pc = state->lr;
}

static inline uint64_t moderngekko_mod_read(CPUState* state, uint32_t address,
                                            uint8_t size)
{
    return state->external_read ? state->external_read(state, address, size) : 0u;
}

static inline void moderngekko_mod_write(CPUState* state, uint32_t address,
                                         uint64_t value, uint8_t size)
{
    if (state->external_write)
        state->external_write(state, address, value, size);
}

#ifdef __cplusplus
}
#endif

#endif
