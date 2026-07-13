#include "moderngekko/module.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool ranges_are_valid(const ModernGekkoRange* ranges, uint32_t count)
{
    uint32_t i;

    if (ranges == NULL || count == 0u)
        return false;

    for (i = 0; i < count; ++i)
    {
        if (ranges[i].start >= ranges[i].end)
            return false;
        if (i != 0u && ranges[i - 1u].end > ranges[i].start)
            return false;
    }

    return true;
}
static bool chunks_tile_code(const ModernGekkoModuleDesc* descriptor)
{
    uint32_t code_index;
    uint32_t chunk_index = 0u;

    if (!ranges_are_valid(descriptor->chunk_ranges, descriptor->num_chunk_ranges) ||
        descriptor->chunk_hashes == NULL)
    {
        return false;
    }

    for (code_index = 0; code_index < descriptor->num_code_ranges; ++code_index)
    {
        const ModernGekkoRange code = descriptor->code_ranges[code_index];
        uint32_t cursor = code.start;

        while (chunk_index < descriptor->num_chunk_ranges &&
               descriptor->chunk_ranges[chunk_index].start < code.end)
        {
            const ModernGekkoRange chunk = descriptor->chunk_ranges[chunk_index];

            if (chunk.start != cursor || chunk.end > code.end)
                return false;

            cursor = chunk.end;
            ++chunk_index;
        }

        if (cursor != code.end)
            return false;
    }

    return chunk_index == descriptor->num_chunk_ranges;
}

static bool address_is_covered(const ModernGekkoRange* ranges, uint32_t count, uint32_t address)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
    {
        if (address >= ranges[i].start && address < ranges[i].end)
            return true;
    }
    return false;
}

ModernGekkoModuleStatus moderngekko_validate_module(
    const ModernGekkoModuleDesc* descriptor,
    const ModernGekkoModuleRequirements* requirements)
{
    if (descriptor == NULL || requirements == NULL)
        return MODERNGEKKO_MODULE_NULL_DESCRIPTOR;
    if (descriptor->abi_version != MODERNGEKKO_MODULE_ABI_VERSION)
        return MODERNGEKKO_MODULE_ABI_MISMATCH;
    if (descriptor->cpu_abi_version != requirements->cpu_abi_version)
        return MODERNGEKKO_MODULE_CPU_ABI_MISMATCH;
    if (descriptor->cpu_state_size != requirements->cpu_state_size)
        return MODERNGEKKO_MODULE_CPU_STATE_SIZE_MISMATCH;
    if (memchr(descriptor->game_id, '\0', sizeof(descriptor->game_id)) == NULL ||
        descriptor->game_id[0] == '\0')
    {
        return MODERNGEKKO_MODULE_INVALID_GAME_ID;
    }
    if (requirements->game_id != NULL &&
        strcmp(descriptor->game_id, requirements->game_id) != 0)
    {
        return MODERNGEKKO_MODULE_GAME_ID_MISMATCH;
    }
    if (descriptor->dispatch == NULL)
        return MODERNGEKKO_MODULE_MISSING_DISPATCH;
    if (!ranges_are_valid(descriptor->code_ranges, descriptor->num_code_ranges))
        return MODERNGEKKO_MODULE_INVALID_CODE_RANGES;
    if (descriptor->num_smc_ranges != 0u &&
        !ranges_are_valid(descriptor->smc_ranges, descriptor->num_smc_ranges))
    {
        return MODERNGEKKO_MODULE_INVALID_SMC_RANGES;
    }
    if (!chunks_tile_code(descriptor))
        return MODERNGEKKO_MODULE_INVALID_CHUNKS;
    if (!address_is_covered(descriptor->code_ranges, descriptor->num_code_ranges,
                            descriptor->entry_point))
        return MODERNGEKKO_MODULE_ENTRY_POINT_UNCOVERED;

    return MODERNGEKKO_MODULE_OK;
}

const char* moderngekko_module_status_string(ModernGekkoModuleStatus status)
{
    static const char* const messages[] = {
        "ok",
        "null descriptor or requirements",
        "module ABI mismatch",
        "CPU ABI mismatch",
        "CPU state size mismatch",
        "invalid game ID",
        "game ID mismatch",
        "missing dispatch function",
        "invalid code ranges",
        "invalid SMC ranges",
        "invalid chunks",
        "entry point is not covered by code ranges",
    };

    if ((unsigned)status >= sizeof(messages) / sizeof(messages[0]))
        return "unknown module status";

    return messages[status];
}
