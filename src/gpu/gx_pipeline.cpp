#include "moderngekko/gx_pipeline.hpp"

#include <algorithm>

namespace moderngekko
{
namespace
{
void HashWord(std::uint64_t* hash, std::uint32_t value)
{
  for (unsigned shift = 0; shift < 32u; shift += 8u)
  {
    *hash ^= static_cast<std::uint8_t>(value >> shift);
    *hash *= 1099511628211ull;
  }
}
}

std::uint64_t GxPipelineKey::Hash() const
{
  std::uint64_t hash = 14695981039346656037ull;
  HashWord(&hash, static_cast<std::uint32_t>(topology));
  for (const std::uint32_t value : cp) HashWord(&hash, value);
  for (const std::uint32_t value : xf) HashWord(&hash, value);
  for (const std::uint32_t value : bp) HashWord(&hash, value);
  return hash;
}

GxPipelineKey GxPipelineKey::FromState(GxTopology topology, const GxStateView& state)
{
  GxPipelineKey key;
  key.topology = topology;
  if (state.cp.size() >= 0x98u)
  {
    key.cp[0] = state.cp[0x50u];
    key.cp[1] = state.cp[0x60u];
    std::ranges::copy(state.cp.subspan(0x70u, 8u), key.cp.begin() + 2);
    std::ranges::copy(state.cp.subspan(0x80u, 8u), key.cp.begin() + 10);
    std::ranges::copy(state.cp.subspan(0x90u, 8u), key.cp.begin() + 18);
  }
  if (state.xf.size() >= 0x1058u)
    std::ranges::copy(state.xf.subspan(0x1000u, 0x58u), key.xf.begin());
  std::ranges::copy(state.bp.first(std::min(state.bp.size(), key.bp.size())), key.bp.begin());
  return key;
}

GxTextureDescriptor GxTextureDescriptor::FromState(std::uint8_t stage,
                                                    const GxStateView& state)
{
  GxTextureDescriptor descriptor;
  if (stage >= 8u || state.bp.size() < 256u)
    return descriptor;
  const std::uint8_t index = stage & 3u;
  const std::uint8_t bank = stage < 4u ? 0x80u : 0xA0u;
  descriptor.mode0 = state.bp[bank + index];
  descriptor.mode1 = state.bp[bank + 4u + index];
  const std::uint32_t image0 = state.bp[bank + 8u + index];
  descriptor.width = static_cast<std::uint16_t>((image0 & 0x3FFu) + 1u);
  descriptor.height = static_cast<std::uint16_t>(((image0 >> 10) & 0x3FFu) + 1u);
  descriptor.format = static_cast<GxTextureFormat>((image0 >> 20) & 0xFu);
  descriptor.tmem_even = (state.bp[bank + 12u + index] & 0x7FFFu) << 5;
  descriptor.tmem_odd = (state.bp[bank + 16u + index] & 0x7FFFu) << 5;
  descriptor.address = (state.bp[bank + 20u + index] & 0xFFFFFFu) << 5;
  const std::uint32_t tlut = state.bp[bank + 24u + index];
  descriptor.palette_offset = (tlut & 0x3FFu) << 9;
  descriptor.palette_format = static_cast<GxPaletteFormat>((tlut >> 10) & 3u);
  return descriptor;
}
}
