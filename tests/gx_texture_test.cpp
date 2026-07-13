#include "moderngekko/gx_texture_decoder.hpp"

#include <array>
#include <vector>

int main()
{
  moderngekko::GxDecodedTexture decoded;
  std::array<std::uint8_t, 32> i4{};
  i4[0] = 0xF1u;
  if (!moderngekko::GxTextureDecoder::Decode(i4, 8, 8, moderngekko::GxTextureFormat::I4,
                                              {}, moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0xFFFFFFFFu || decoded.rgba8[1] != 0x11111111u)
  {
    return 1;
  }

  std::array<std::uint8_t, 64> rgba8{};
  rgba8[0] = 0x44u;
  rgba8[1] = 0x11u;
  rgba8[32] = 0x22u;
  rgba8[33] = 0x33u;
  if (!moderngekko::GxTextureDecoder::Decode(rgba8, 4, 4,
                                              moderngekko::GxTextureFormat::RGBA8, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0x11223344u)
  {
    return 2;
  }

  std::array<std::uint8_t, 32> c4{};
  c4[0] = 0x10u;
  std::array<std::uint8_t, 32> palette{};
  palette[0] = 0xFFu; palette[1] = 0x20u;
  palette[2] = 0x80u; palette[3] = 0x40u;
  if (!moderngekko::GxTextureDecoder::Decode(c4, 8, 8, moderngekko::GxTextureFormat::C4,
                                              palette, moderngekko::GxPaletteFormat::IA8,
                                              &decoded) ||
      decoded.rgba8[0] != 0x40404080u || decoded.rgba8[1] != 0x202020FFu)
  {
    return 3;
  }

  std::array<std::uint8_t, 32> cmpr{};
  cmpr[0] = 0xF8u; cmpr[1] = 0x00u;
  cmpr[2] = 0x00u; cmpr[3] = 0x1Fu;
  cmpr[4] = 0x1Bu;
  if (!moderngekko::GxTextureDecoder::Decode(cmpr, 8, 8,
                                              moderngekko::GxTextureFormat::CMPR, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0xFF0000FFu || decoded.rgba8[1] != 0x0000FFFFu)
  {
    return 4;
  }

  if (moderngekko::GxTextureDecoder::EncodedSize(9, 9,
                                                  moderngekko::GxTextureFormat::I4) != 128u ||
      moderngekko::GxTextureDecoder::PaletteEntries(moderngekko::GxTextureFormat::C14X2) != 16384u)
  {
    return 5;
  }

  std::array<std::uint8_t, 32> block32{};
  block32[0] = 0x12u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 8, 4,
                                              moderngekko::GxTextureFormat::I8, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0x12121212u)
  {
    return 6;
  }
  block32[0] = 0xA5u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 8, 4,
                                              moderngekko::GxTextureFormat::IA4, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0x555555AAu)
  {
    return 7;
  }
  block32 = {};
  block32[0] = 0x0Au; block32[1] = 0x05u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 4, 4,
                                              moderngekko::GxTextureFormat::IA8, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0x0505050Au)
  {
    return 8;
  }
  block32[0] = 0xF8u; block32[1] = 0x00u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 4, 4,
                                              moderngekko::GxTextureFormat::RGB565, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0xFF0000FFu)
  {
    return 9;
  }
  block32[0] = 0x0Fu; block32[1] = 0x00u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 4, 4,
                                              moderngekko::GxTextureFormat::RGB5A3, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0xFF000000u)
  {
    return 10;
  }

  std::array<std::uint8_t, 512> palette256{};
  palette256[2] = 0xF8u; palette256[3] = 0x00u;
  block32 = {}; block32[0] = 1u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 8, 4,
                                              moderngekko::GxTextureFormat::C8, palette256,
                                              moderngekko::GxPaletteFormat::RGB565, &decoded) ||
      decoded.rgba8[0] != 0xFF0000FFu)
  {
    return 11;
  }
  std::vector<std::uint8_t> palette14(32768u);
  palette14[2] = 0xF8u; palette14[3] = 0x00u;
  block32 = {}; block32[1] = 1u;
  if (!moderngekko::GxTextureDecoder::Decode(block32, 4, 4,
                                              moderngekko::GxTextureFormat::C14X2, palette14,
                                              moderngekko::GxPaletteFormat::RGB565, &decoded) ||
      decoded.rgba8[0] != 0xFF0000FFu)
  {
    return 12;
  }

  std::array<std::uint8_t, 128> tiled{};
  tiled[0] = 0x11u; tiled[32] = 0x22u; tiled[64] = 0x33u;
  if (!moderngekko::GxTextureDecoder::Decode(tiled, 9, 5,
                                              moderngekko::GxTextureFormat::I8, {},
                                              moderngekko::GxPaletteFormat::IA8, &decoded) ||
      decoded.rgba8[0] != 0x11111111u || decoded.rgba8[8] != 0x22222222u ||
      decoded.rgba8[4u * 9u] != 0x33333333u)
  {
    return 13;
  }
  return 0;
}
