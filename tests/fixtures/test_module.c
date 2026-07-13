#include "moderngekko/module_abi.h"

static int dispatch(CPUState* state, uint32_t address)
{
    state->gpr[0] = address;
    return 1;
}
static const ModernGekkoRange code_ranges[] = {
    {0x80003100u, 0x80003200u},
};

static const uint64_t chunk_hashes[] = {
    0xCBF29CE484222325ull,
};

static const ModernGekkoModuleDesc descriptor = {
    MODERNGEKKO_MODULE_ABI_VERSION,
    2u,
    (uint32_t)sizeof(CPUState),
    "TEST01",
    0x80003100u,
    dispatch,
    0,
    code_ranges,
    1u,
    0,
    0u,
    code_ranges,
    1u,
    chunk_hashes,
};

MODERNGEKKO_MODULE_EXPORT const ModernGekkoModuleDesc* staticrecomp_get_module(void)
{
    return &descriptor;
}
