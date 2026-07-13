#pragma once

#include "moderngekko/gx_state_backend.hpp"
#include "moderngekko/gx_texture_decoder.hpp"

#include <array>
#include <cstdint>

namespace moderngekko
{
struct GxPipelineKey
{
  GxTopology topology = GxTopology::Triangles;
  std::array<std::uint32_t, 26> cp{};
  std::array<std::uint32_t, 0x58> xf{};
  std::array<std::uint32_t, 256> bp{};

  bool operator==(const GxPipelineKey&) const = default;
  std::uint64_t Hash() const;
  static GxPipelineKey FromState(GxTopology topology, const GxStateView& state);
};

struct GxTextureDescriptor
{
  std::uint32_t address = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  GxTextureFormat format = GxTextureFormat::I4;
  std::uint32_t mode0 = 0;
  std::uint32_t mode1 = 0;
  std::uint32_t tmem_even = 0;
  std::uint32_t tmem_odd = 0;
  std::uint32_t palette_offset = 0;
  GxPaletteFormat palette_format = GxPaletteFormat::IA8;

  bool operator==(const GxTextureDescriptor&) const = default;
  static GxTextureDescriptor FromState(std::uint8_t stage, const GxStateView& state);
};
}
