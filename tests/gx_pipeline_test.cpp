#include "moderngekko/gx_pipeline.hpp"

#include <array>

int main()
{
  std::array<std::uint32_t, 256> cp{};
  std::array<std::uint32_t, 0x1058> xf{};
  std::array<std::uint32_t, 256> bp{};
  const moderngekko::GxStateView state{cp, xf, bp};
  const moderngekko::GxPipelineKey first =
      moderngekko::GxPipelineKey::FromState(moderngekko::GxTopology::Triangles, state);
  bp[0xC0u] = 1u;
  const moderngekko::GxPipelineKey second =
      moderngekko::GxPipelineKey::FromState(moderngekko::GxTopology::Triangles, state);
  if (first == second || first.Hash() == second.Hash())
    return 1;

  bp = {};
  xf[0] = 1u;
  const moderngekko::GxPipelineKey matrix_only =
      moderngekko::GxPipelineKey::FromState(moderngekko::GxTopology::Triangles, state);
  if (!(first == matrix_only))
    return 2;
  xf[0x1008u] = 1u;
  const moderngekko::GxPipelineKey xf_config =
      moderngekko::GxPipelineKey::FromState(moderngekko::GxTopology::Triangles, state);
  if (first == xf_config)
    return 3;

  xf = {};
  bp = {};
  bp[0x80u] = 0x12345u;
  bp[0x84u] = 0x6789u;
  bp[0x88u] = (5u << 20) | (31u << 10) | 63u;
  bp[0x8Cu] = 0x12u;
  bp[0x90u] = 0x34u;
  bp[0x94u] = 0x1234u;
  bp[0x98u] = (2u << 10) | 5u;
  const moderngekko::GxTextureDescriptor texture =
      moderngekko::GxTextureDescriptor::FromState(0u, state);
  if (texture.width != 64u || texture.height != 32u ||
      texture.format != moderngekko::GxTextureFormat::RGB5A3 ||
      texture.address != 0x24680u || texture.tmem_even != 0x240u ||
      texture.tmem_odd != 0x680u || texture.palette_offset != 0xA00u ||
      texture.palette_format != moderngekko::GxPaletteFormat::RGB5A3)
  {
    return 4;
  }
  return 0;
}
