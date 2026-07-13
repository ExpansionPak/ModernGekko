// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/gx_texture_decoder.hpp"

#include <array>
#include <limits>

namespace moderngekko
{
namespace
{
struct BlockInfo
{
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t bytes;
};

BlockInfo GetBlockInfo(GxTextureFormat format)
{
  switch (format)
  {
  case GxTextureFormat::I4:
  case GxTextureFormat::C4: return {8, 8, 32};
  case GxTextureFormat::I8:
  case GxTextureFormat::IA4:
  case GxTextureFormat::C8: return {8, 4, 32};
  case GxTextureFormat::IA8:
  case GxTextureFormat::RGB565:
  case GxTextureFormat::RGB5A3:
  case GxTextureFormat::C14X2: return {4, 4, 32};
  case GxTextureFormat::RGBA8: return {4, 4, 64};
  case GxTextureFormat::CMPR: return {8, 8, 32};
  default: return {};
  }
}

std::uint16_t Read16(const std::uint8_t* data)
{
  return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
}

std::uint8_t Convert3(std::uint32_t value) { return static_cast<std::uint8_t>((value << 5) | (value << 2) | (value >> 1)); }
std::uint8_t Convert4(std::uint32_t value) { return static_cast<std::uint8_t>((value << 4) | value); }
std::uint8_t Convert5(std::uint32_t value) { return static_cast<std::uint8_t>((value << 3) | (value >> 2)); }
std::uint8_t Convert6(std::uint32_t value) { return static_cast<std::uint8_t>((value << 2) | (value >> 4)); }

std::uint32_t Pack(std::uint32_t red, std::uint32_t green, std::uint32_t blue,
                   std::uint32_t alpha)
{
  return (red << 24) | (green << 16) | (blue << 8) | alpha;
}

std::uint32_t DecodeRgb565(std::uint16_t value)
{
  return Pack(Convert5(value >> 11), Convert6((value >> 5) & 63u), Convert5(value & 31u), 255u);
}

std::uint32_t DecodeRgb5a3(std::uint16_t value)
{
  if ((value & 0x8000u) != 0u)
    return Pack(Convert5((value >> 10) & 31u), Convert5((value >> 5) & 31u),
                Convert5(value & 31u), 255u);
  return Pack(Convert4((value >> 8) & 15u), Convert4((value >> 4) & 15u),
              Convert4(value & 15u), Convert3((value >> 12) & 7u));
}

std::uint32_t DecodePalette(std::span<const std::uint8_t> palette, std::uint32_t index,
                            GxPaletteFormat format)
{
  const std::uint8_t* entry = palette.data() + index * 2u;
  if (format == GxPaletteFormat::IA8)
    return Pack(entry[1], entry[1], entry[1], entry[0]);
  const std::uint16_t value = Read16(entry);
  return format == GxPaletteFormat::RGB565 ? DecodeRgb565(value) : DecodeRgb5a3(value);
}

void Store(GxDecodedTexture* output, std::uint32_t x, std::uint32_t y, std::uint32_t color)
{
  if (x < output->width && y < output->height)
    output->rgba8[static_cast<std::size_t>(y) * output->width + x] = color;
}

std::uint32_t DxtBlend(std::uint32_t first, std::uint32_t second)
{
  return (first * 3u + second * 5u) >> 3;
}

void DecodeCmprSubBlock(const std::uint8_t* source, std::uint32_t base_x,
                        std::uint32_t base_y, GxDecodedTexture* output)
{
  const std::uint16_t first = Read16(source);
  const std::uint16_t second = Read16(source + 2);
  const std::array<std::uint32_t, 2> endpoints = {
      DecodeRgb565(first), DecodeRgb565(second)};
  std::array<std::uint32_t, 4> colors = {endpoints[0], endpoints[1], 0, 0};
  const auto channel = [](std::uint32_t color, unsigned shift) { return (color >> shift) & 255u; };
  if (first > second)
  {
    colors[2] = Pack(DxtBlend(channel(colors[1], 24), channel(colors[0], 24)),
                     DxtBlend(channel(colors[1], 16), channel(colors[0], 16)),
                     DxtBlend(channel(colors[1], 8), channel(colors[0], 8)), 255u);
    colors[3] = Pack(DxtBlend(channel(colors[0], 24), channel(colors[1], 24)),
                     DxtBlend(channel(colors[0], 16), channel(colors[1], 16)),
                     DxtBlend(channel(colors[0], 8), channel(colors[1], 8)), 255u);
  }
  else
  {
    const std::uint32_t red = (channel(colors[0], 24) + channel(colors[1], 24)) / 2u;
    const std::uint32_t green = (channel(colors[0], 16) + channel(colors[1], 16)) / 2u;
    const std::uint32_t blue = (channel(colors[0], 8) + channel(colors[1], 8)) / 2u;
    colors[2] = Pack(red, green, blue, 255u);
    colors[3] = Pack(red, green, blue, 0u);
  }
  for (std::uint32_t y = 0; y < 4u; ++y)
  {
    std::uint8_t selectors = source[4u + y];
    for (std::uint32_t x = 0; x < 4u; ++x)
    {
      Store(output, base_x + x, base_y + y, colors[selectors >> 6]);
      selectors <<= 2;
    }
  }
}
}

std::size_t GxTextureDecoder::EncodedSize(std::uint32_t width, std::uint32_t height,
                                          GxTextureFormat format)
{
  const BlockInfo block = GetBlockInfo(format);
  if (width == 0u || height == 0u || block.bytes == 0u)
    return 0u;
  const std::uint64_t blocks_x = (static_cast<std::uint64_t>(width) + block.width - 1u) / block.width;
  const std::uint64_t blocks_y = (static_cast<std::uint64_t>(height) + block.height - 1u) / block.height;
  const std::uint64_t size = blocks_x * blocks_y * block.bytes;
  return size <= std::numeric_limits<std::size_t>::max() ? static_cast<std::size_t>(size) : 0u;
}

std::size_t GxTextureDecoder::PaletteEntries(GxTextureFormat format)
{
  switch (format)
  {
  case GxTextureFormat::C4: return 16u;
  case GxTextureFormat::C8: return 256u;
  case GxTextureFormat::C14X2: return 16384u;
  default: return 0u;
  }
}

bool GxTextureDecoder::Decode(std::span<const std::uint8_t> encoded, std::uint32_t width,
                              std::uint32_t height, GxTextureFormat format,
                              std::span<const std::uint8_t> palette,
                              GxPaletteFormat palette_format, GxDecodedTexture* output)
{
  const BlockInfo block = GetBlockInfo(format);
  const std::size_t encoded_size = EncodedSize(width, height, format);
  const std::size_t palette_entries = PaletteEntries(format);
  if (output == nullptr || encoded_size == 0u || encoded.size() < encoded_size ||
      height > std::numeric_limits<std::size_t>::max() / width ||
      (palette_entries != 0u && palette.size() < palette_entries * 2u))
  {
    return false;
  }
  output->width = width;
  output->height = height;
  output->rgba8.assign(static_cast<std::size_t>(width) * height, 0u);

  std::size_t offset = 0;
  for (std::uint32_t block_y = 0; block_y < height; block_y += block.height)
  {
    for (std::uint32_t block_x = 0; block_x < width; block_x += block.width)
    {
      const std::uint8_t* source = encoded.data() + offset;
      if (format == GxTextureFormat::CMPR)
      {
        DecodeCmprSubBlock(source, block_x, block_y, output);
        DecodeCmprSubBlock(source + 8, block_x + 4, block_y, output);
        DecodeCmprSubBlock(source + 16, block_x, block_y + 4, output);
        DecodeCmprSubBlock(source + 24, block_x + 4, block_y + 4, output);
      }
      else
      {
        for (std::uint32_t y = 0; y < block.height; ++y)
        {
          for (std::uint32_t x = 0; x < block.width; ++x)
          {
            const std::uint32_t pixel = y * block.width + x;
            std::uint32_t color = 0;
            switch (format)
            {
            case GxTextureFormat::I4:
            {
              const std::uint8_t value = source[pixel / 2u];
              const std::uint8_t intensity = Convert4((pixel & 1u) == 0u ? value >> 4 : value & 15u);
              color = Pack(intensity, intensity, intensity, intensity);
              break;
            }
            case GxTextureFormat::I8:
            {
              const std::uint8_t intensity = source[pixel];
              color = Pack(intensity, intensity, intensity, intensity);
              break;
            }
            case GxTextureFormat::IA4:
            {
              const std::uint8_t value = source[pixel];
              const std::uint8_t alpha = Convert4(value >> 4);
              const std::uint8_t intensity = Convert4(value & 15u);
              color = Pack(intensity, intensity, intensity, alpha);
              break;
            }
            case GxTextureFormat::IA8:
              color = Pack(source[pixel * 2u + 1u], source[pixel * 2u + 1u],
                           source[pixel * 2u + 1u], source[pixel * 2u]);
              break;
            case GxTextureFormat::RGB565: color = DecodeRgb565(Read16(source + pixel * 2u)); break;
            case GxTextureFormat::RGB5A3: color = DecodeRgb5a3(Read16(source + pixel * 2u)); break;
            case GxTextureFormat::RGBA8:
              color = Pack(source[pixel * 2u + 1u], source[32u + pixel * 2u],
                           source[32u + pixel * 2u + 1u], source[pixel * 2u]);
              break;
            case GxTextureFormat::C4:
            {
              const std::uint8_t value = source[pixel / 2u];
              color = DecodePalette(palette, (pixel & 1u) == 0u ? value >> 4 : value & 15u,
                                    palette_format);
              break;
            }
            case GxTextureFormat::C8:
              color = DecodePalette(palette, source[pixel], palette_format);
              break;
            case GxTextureFormat::C14X2:
              color = DecodePalette(palette, Read16(source + pixel * 2u) & 0x3FFFu,
                                    palette_format);
              break;
            default: break;
            }
            Store(output, block_x + x, block_y + y, color);
          }
        }
      }
      offset += block.bytes;
    }
  }
  return true;
}
}
