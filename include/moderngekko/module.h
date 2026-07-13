#ifndef MODERNGEKKO_MODULE_H
#define MODERNGEKKO_MODULE_H

#include "moderngekko/module_abi.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum ModernGekkoModuleStatus
{
    MODERNGEKKO_MODULE_OK = 0,
    MODERNGEKKO_MODULE_NULL_DESCRIPTOR,
    MODERNGEKKO_MODULE_ABI_MISMATCH,
    MODERNGEKKO_MODULE_CPU_ABI_MISMATCH,
    MODERNGEKKO_MODULE_CPU_STATE_SIZE_MISMATCH,
    MODERNGEKKO_MODULE_INVALID_GAME_ID,
    MODERNGEKKO_MODULE_GAME_ID_MISMATCH,
    MODERNGEKKO_MODULE_MISSING_DISPATCH,
    MODERNGEKKO_MODULE_INVALID_CODE_RANGES,
    MODERNGEKKO_MODULE_INVALID_SMC_RANGES,
    MODERNGEKKO_MODULE_INVALID_CHUNKS,
    MODERNGEKKO_MODULE_ENTRY_POINT_UNCOVERED
} ModernGekkoModuleStatus;

typedef struct ModernGekkoModuleRequirements
{
    uint32_t cpu_abi_version;
    uint32_t cpu_state_size;
    const char* game_id;
} ModernGekkoModuleRequirements;

ModernGekkoModuleStatus moderngekko_validate_module(
    const ModernGekkoModuleDesc* descriptor,
    const ModernGekkoModuleRequirements* requirements);

const char* moderngekko_module_status_string(ModernGekkoModuleStatus status);

#ifdef __cplusplus
}
#endif

#endif
