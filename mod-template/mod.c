#include "moderngekko/mod_abi.h"

static void replacement(CPUState* state)
{
    moderngekko_mod_return_u32(state, 1u);
}

static const ModernGekkoModPatch patches[] = {
    RECOMP_PATCH(MOD_PATCH_ADDRESS, replacement),
};

static const ModernGekkoModDesc descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    MOD_GAME_ID,
    MOD_ID,
    MOD_VERSION,
    MOD_DISPLAY_NAME,
    0,
    0u,
    patches,
    1u,
    0,
    0u,
    0,
    0u,
    0,
    0u,
    0,
    0u,
    0,
    0u,
    0,
    0,
};

MODERNGEKKO_MOD_EXPORT const ModernGekkoModDesc* moderngekko_get_mod(void)
{
    return &descriptor;
}
