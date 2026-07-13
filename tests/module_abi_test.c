#include "moderngekko/module.h"

#include <stddef.h>
#include <string.h>

#define COMPILE_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]

static int dispatch(CPUState* state, uint32_t address)
{
    state->gpr[0] = address;
    return 1;
}

static void on_state_loaded(CPUState* state)
{
    state->gpr[0] = 0;
}

int main(void)
{
    static const ModernGekkoRange code_ranges[] = {{0x80003100u, 0x80003200u}};
    static const uint64_t chunk_hashes[] = {0xCBF29CE484222325ull};
    StaticRecompModuleDesc descriptor = {
        MODERNGEKKO_MODULE_ABI_VERSION,
        MODERNGEKKO_CPU_ABI_VERSION,
        (uint32_t)sizeof(CPUState),
        "TEST01",
        0x80003100u,
        dispatch,
        on_state_loaded,
        code_ranges,
        1u,
        NULL,
        0u,
        code_ranges,
        1u,
        chunk_hashes,
    };
    const ModernGekkoModuleRequirements requirements = {
        MODERNGEKKO_CPU_ABI_VERSION,
        (uint32_t)sizeof(CPUState),
        "TEST01",
    };
    CPUState state = {0};

    COMPILE_ASSERT(module_abi_version_is_two,
                   MODERNGEKKO_MODULE_ABI_VERSION == 2u);
    COMPILE_ASSERT(game_id_storage_is_eight_bytes,
                   sizeof(descriptor.game_id) == 8u);
    COMPILE_ASSERT(dispatch_follows_entry_point,
                   offsetof(ModernGekkoModuleDesc, dispatch) >
                       offsetof(ModernGekkoModuleDesc, entry_point));

    if (descriptor.abi_version != STATICRECOMP_ABI_VERSION)
        return 1;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_OK)
        return 2;
    if (!descriptor.dispatch(&state, descriptor.entry_point))
        return 3;
    if (state.gpr[0] != descriptor.entry_point)
        return 4;
    descriptor.on_state_loaded(&state);
    if (state.gpr[0] != 0u)
        return 5;

    descriptor.abi_version++;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_ABI_MISMATCH)
    {
        return 6;
    }
    descriptor.abi_version--;

    descriptor.cpu_abi_version++;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_CPU_ABI_MISMATCH)
        return 7;
    descriptor.cpu_abi_version--;

    descriptor.cpu_state_size++;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_CPU_STATE_SIZE_MISMATCH)
        return 8;
    descriptor.cpu_state_size--;

    descriptor.entry_point = 0x80004000u;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_ENTRY_POINT_UNCOVERED)
        return 9;
    descriptor.entry_point = 0x80003100u;

    descriptor.chunk_ranges = NULL;
    if (moderngekko_validate_module(&descriptor, &requirements) !=
        MODERNGEKKO_MODULE_INVALID_CHUNKS)
    {
        return 10;
    }

    return 0;
}
