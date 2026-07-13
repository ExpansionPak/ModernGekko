#include "moderngekko/gx_command_processor.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace
{
std::uint32_t Bits(std::uint32_t value, unsigned offset, unsigned count)
{
  return (value >> offset) & ((1u << count) - 1u);
}

std::uint32_t FormatSize(std::uint32_t format)
{
  return format < 2u ? 1u : format < 4u ? 2u : 4u;
}

std::uint32_t AttributeSize(std::uint32_t descriptor, std::uint32_t direct)
{
  constexpr std::array<std::uint32_t, 4> index_sizes = {0, 0, 1, 2};
  return descriptor == 1u ? direct : index_sizes[descriptor];
}

std::pair<std::uint32_t, std::uint32_t> TextureFormat(std::uint32_t g0, std::uint32_t g1,
                                                       std::uint32_t g2, unsigned index)
{
  switch (index)
  {
  case 0: return {Bits(g0, 22, 3), Bits(g0, 21, 1)};
  case 1: return {Bits(g1, 1, 3), Bits(g1, 0, 1)};
  case 2: return {Bits(g1, 10, 3), Bits(g1, 9, 1)};
  case 3: return {Bits(g1, 19, 3), Bits(g1, 18, 1)};
  case 4: return {Bits(g1, 28, 3), Bits(g1, 27, 1)};
  case 5: return {Bits(g2, 6, 3), Bits(g2, 5, 1)};
  case 6: return {Bits(g2, 15, 3), Bits(g2, 14, 1)};
  case 7: return {Bits(g2, 24, 3), Bits(g2, 23, 1)};
  default: return {};
  }
}

std::uint32_t DolphinVertexSize(std::uint32_t low, std::uint32_t high,
                                std::uint32_t g0, std::uint32_t g1, std::uint32_t g2)
{
  constexpr std::array<std::uint32_t, 8> color_sizes = {2, 3, 4, 2, 3, 4, 0, 0};
  std::uint32_t size = std::popcount(low & 0x1FFu);

  const std::uint32_t position = Bits(low, 9, 2);
  size += AttributeSize(position, FormatSize(Bits(g0, 1, 3)) * (Bits(g0, 0, 1) + 2u));

  const std::uint32_t normal = Bits(low, 11, 2);
  if (normal == 1u)
    size += FormatSize(Bits(g0, 10, 3)) * (Bits(g0, 9, 1) != 0u ? 9u : 3u);
  else if (normal == 2u || normal == 3u)
    size += (normal - 1u) * (Bits(g0, 9, 1) != 0u && Bits(g0, 31, 1) != 0u ? 3u : 1u);

  for (unsigned color = 0; color < 2; ++color)
  {
    const std::uint32_t descriptor = Bits(low, 13u + color * 2u, 2);
    size += AttributeSize(descriptor, color_sizes[Bits(g0, 14u + color * 4u, 3)]);
  }
  for (unsigned texture = 0; texture < 8; ++texture)
  {
    const auto [format, elements] = TextureFormat(g0, g1, g2, texture);
    size += AttributeSize(Bits(high, texture * 2u, 2), FormatSize(format) * (elements + 1u));
  }
  return size;
}

void LoadCp(moderngekko::GxCommandProcessor& processor, std::uint8_t command,
            std::uint32_t value)
{
  const std::array<std::uint8_t, 6> bytes = {
      0x08u, command, static_cast<std::uint8_t>(value >> 24),
      static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
      static_cast<std::uint8_t>(value)};
  processor.WriteBytes(bytes);
}

class BoundaryBackend final : public moderngekko::GpuBackend
{
public:
  void LoadXfRegisters(std::uint16_t, std::span<const std::uint32_t>) override { calls++; }
  void Draw(const moderngekko::GxDrawPacket&) override { calls++; }
  std::uint32_t calls = 0;
};
}

int main()
{
  std::mt19937 random(0x47585050u);
  moderngekko::GxCommandProcessor processor;
  for (std::uint32_t iteration = 0; iteration < 100000u; ++iteration)
  {
    const std::uint32_t low = random() & 0x1FFFFu;
    const std::uint32_t high = random() & 0xFFFFu;
    const std::uint32_t g0 = random();
    const std::uint32_t g1 = random();
    const std::uint32_t g2 = random();
    const std::uint8_t vat = static_cast<std::uint8_t>(random() & 7u);
    LoadCp(processor, 0x50u, low);
    LoadCp(processor, 0x60u, high);
    LoadCp(processor, static_cast<std::uint8_t>(0x70u + vat), g0);
    LoadCp(processor, static_cast<std::uint8_t>(0x80u + vat), g1);
    LoadCp(processor, static_cast<std::uint8_t>(0x90u + vat), g2);
    if (processor.GetVertexSize(vat) != DolphinVertexSize(low, high, g0, g1, g2))
      return 1;
  }

  const std::array<std::uint8_t, 13> xf = {
      0x10, 0x00, 0x01, 0x10, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};
  for (std::size_t split = 1; split < xf.size(); ++split)
  {
    BoundaryBackend backend;
    moderngekko::GxCommandProcessor boundary(&backend);
    boundary.WriteBytes(std::span{xf}.first(split));
    if (backend.calls != 0u || boundary.GetBufferedByteCount() != split)
      return 2;
    boundary.WriteBytes(std::span{xf}.subspan(split));
    if (backend.calls != 1u || boundary.GetBufferedByteCount() != 0u)
      return 3;
  }

  moderngekko::GxCommandProcessor nops;
  const std::array<std::uint8_t, 4> nop_stream = {0, 0, 0, 0x44};
  nops.WriteBytes(nop_stream);
  if (nops.GetCommandCount() != 2u)
    return 4;
  return 0;
}
