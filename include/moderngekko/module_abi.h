#ifndef MODERNGEKKO_MODULE_ABI_H
#define MODERNGEKKO_MODULE_ABI_H

#include "moderngekko/cpu_state.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODERNGEKKO_MODULE_ABI_VERSION 2u
#define MODERNGEKKO_GET_MODULE_SYMBOL "staticrecomp_get_module"

#if defined(_WIN32)
#define MODERNGEKKO_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define MODERNGEKKO_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define MODERNGEKKO_MODULE_EXPORT
#endif

typedef struct ModernGekkoRange
{
    uint32_t start;
    uint32_t end;
} ModernGekkoRange;

typedef struct ModernGekkoModuleDesc
{
    uint32_t abi_version;
    uint32_t cpu_abi_version;
    uint32_t cpu_state_size;
    char game_id[8];
    uint32_t entry_point;

    int (*dispatch)(CPUState* state, uint32_t address);
    void (*on_state_loaded)(CPUState* state);

    const ModernGekkoRange* code_ranges;
    uint32_t num_code_ranges;
    const ModernGekkoRange* smc_ranges;
    uint32_t num_smc_ranges;
    const ModernGekkoRange* chunk_ranges;
    uint32_t num_chunk_ranges;
    const uint64_t* chunk_hashes;
} ModernGekkoModuleDesc;

typedef const ModernGekkoModuleDesc* (*ModernGekkoGetModuleFn)(void);

typedef ModernGekkoRange StaticRecompRange;
typedef ModernGekkoModuleDesc StaticRecompModuleDesc;
typedef ModernGekkoGetModuleFn StaticRecompGetModuleFn;

#define STATICRECOMP_ABI_VERSION MODERNGEKKO_MODULE_ABI_VERSION
#define STATICRECOMP_GET_MODULE_SYMBOL MODERNGEKKO_GET_MODULE_SYMBOL

#ifdef __cplusplus
}
#endif

#endif
