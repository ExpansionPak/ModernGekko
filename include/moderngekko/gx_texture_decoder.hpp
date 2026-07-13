#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace moderngekko
{
enum class GxTextureFormat : std::uint8_t
{
  I4 = 0x0,
  I8 = 0x1,
  IA4 = 0x2,
  IA8 = 0x3,
  RGB565 = 0x4,
  RGB5A3 = 0x5,
  RGBA8 = 0x6,
  C4 = 0x8,
  C8 = 0x9,
  C14X2 = 0xA,
  CMPR = 0xE,
};

enum class GxPaletteFormat : std::uint8_t
{
  IA8 = 0,
  RGB565 = 1,
  RGB5A3 = 2,
};

struct GxDecodedTexture
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint32_t> rgba8;
};

class GxTextureDecoder final
{
public:
  static std::size_t EncodedSize(std::uint32_t width, std::uint32_t height,
                                 GxTextureFormat format);
  static std::size_t PaletteEntries(GxTextureFormat format);
  static bool Decode(std::span<const std::uint8_t> encoded, std::uint32_t width,
                     std::uint32_t height, GxTextureFormat format,
                     std::span<const std::uint8_t> palette, GxPaletteFormat palette_format,
                     GxDecodedTexture* output);
};
}
