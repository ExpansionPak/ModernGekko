#include "moderngekko/mod_abi.h"

static void patch(CPUState* state)
{
    moderngekko_mod_return_u32(state, 999u);
}

static const ModernGekkoModPatch patches[] = {
    RECOMP_PATCH(0x80001000u, patch),
};

static const ModernGekkoModDesc descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    "dynamic_fixture",
    "1.0.0",
    "Dynamic fixture",
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
