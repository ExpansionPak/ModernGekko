// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/gx_vertex_loader.hpp"

#include <bit>
#include <cmath>
#include <cstring>

namespace moderngekko
{
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

std::uint32_t ColorSize(std::uint32_t format)
{
  constexpr std::array<std::uint32_t, 8> sizes = {2, 3, 4, 2, 3, 4, 0, 0};
  return sizes[format & 7u];
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

std::uint32_t TextureFraction(std::uint32_t g0, std::uint32_t g1, std::uint32_t g2,
                               unsigned index)
{
  switch (index)
  {
  case 0: return Bits(g0, 25, 5);
  case 1: return Bits(g1, 4, 5);
  case 2: return Bits(g1, 13, 5);
  case 3: return Bits(g1, 22, 5);
  case 4: return Bits(g2, 0, 5);
  case 5: return Bits(g2, 9, 5);
  case 6: return Bits(g2, 18, 5);
  case 7: return Bits(g2, 27, 5);
  default: return 0;
  }
}

class Reader
{
public:
  explicit Reader(std::span<const std::uint8_t> data) : m_data(data) {}

  bool ReadU8(std::uint8_t* value)
  {
    if (m_offset >= m_data.size())
      return false;
    *value = m_data[m_offset++];
    return true;
  }

  bool ReadIndex(std::uint32_t descriptor, std::uint16_t* index)
  {
    std::uint8_t first = 0;
    if (!ReadU8(&first))
      return false;
    if (descriptor == 2u)
    {
      *index = first;
      return true;
    }
    std::uint8_t second = 0;
    if (!ReadU8(&second))
      return false;
    *index = static_cast<std::uint16_t>((first << 8) | second);
    return true;
  }

  const std::uint8_t* Take(std::size_t size)
  {
    if (size > m_data.size() - m_offset)
      return nullptr;
    const std::uint8_t* result = m_data.data() + m_offset;
    m_offset += size;
    return result;
  }

  bool AtEnd() const { return m_offset == m_data.size(); }

private:
  std::span<const std::uint8_t> m_data;
  std::size_t m_offset = 0;
};

const std::uint8_t* AttributeData(Reader& reader, const AddressSpace& memory,
                                  std::span<const std::uint32_t> cp, unsigned array,
                                  std::uint32_t descriptor, std::size_t direct_size)
{
  if (descriptor == 1u)
    return reader.Take(direct_size);
  if (descriptor != 2u && descriptor != 3u)
    return nullptr;
  std::uint16_t index = 0;
  if (!reader.ReadIndex(descriptor, &index))
    return nullptr;
  const std::uint64_t address = static_cast<std::uint64_t>(cp[0xA0u + array]) +
                                static_cast<std::uint64_t>(cp[0xB0u + array] & 0xFFu) * index;
  return address <= 0xFFFFFFFFu ? memory.Resolve(static_cast<std::uint32_t>(address), direct_size) :
                                 nullptr;
}

float ReadComponent(const std::uint8_t* data, std::uint32_t format, std::uint32_t fraction)
{
  const float scale = std::ldexp(1.0f, -static_cast<int>(fraction));
  switch (format)
  {
  case 0: return data[0] * scale;
  case 1: return static_cast<std::int8_t>(data[0]) * scale;
  case 2: return static_cast<float>((data[0] << 8) | data[1]) * scale;
  case 3:
    return static_cast<std::int16_t>((data[0] << 8) | data[1]) * scale;
  default:
  {
    const std::uint32_t bits = (static_cast<std::uint32_t>(data[0]) << 24) |
                               (static_cast<std::uint32_t>(data[1]) << 16) |
                               (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
    return std::bit_cast<float>(bits);
  }
  }
}

std::uint32_t Expand(std::uint32_t value, unsigned bits)
{
  const std::uint32_t maximum = (1u << bits) - 1u;
  return (value * 255u + maximum / 2u) / maximum;
}

std::uint32_t DecodeColor(const std::uint8_t* data, std::uint32_t format)
{
  std::uint32_t r = 255, g = 255, b = 255, a = 255;
  if (format == 0u)
  {
    const std::uint16_t value = static_cast<std::uint16_t>((data[0] << 8) | data[1]);
    r = Expand(value >> 11, 5); g = Expand((value >> 5) & 63u, 6); b = Expand(value & 31u, 5);
  }
  else if (format == 1u || format == 2u)
  {
    r = data[0]; g = data[1]; b = data[2];
  }
  else if (format == 3u)
  {
    const std::uint16_t value = static_cast<std::uint16_t>((data[0] << 8) | data[1]);
    r = Expand(value >> 12, 4); g = Expand((value >> 8) & 15u, 4);
    b = Expand((value >> 4) & 15u, 4); a = Expand(value & 15u, 4);
  }
  else if (format == 4u)
  {
    const std::uint32_t value = (data[0] << 16) | (data[1] << 8) | data[2];
    r = Expand(value >> 18, 6); g = Expand((value >> 12) & 63u, 6);
    b = Expand((value >> 6) & 63u, 6); a = Expand(value & 63u, 6);
  }
  else if (format == 5u)
  {
    r = data[0]; g = data[1]; b = data[2]; a = data[3];
  }
  return (r << 24) | (g << 16) | (b << 8) | a;
}

void AddIndices(GxPrimitive primitive, std::uint32_t count, GxDecodedDraw* output)
{
  if (primitive == GxPrimitive::Points)
  {
    output->topology = GxTopology::Points;
    for (std::uint32_t i = 0; i < count; ++i) output->indices.push_back(i);
  }
  else if (primitive == GxPrimitive::Lines || primitive == GxPrimitive::LineStrip)
  {
    output->topology = GxTopology::Lines;
    if (primitive == GxPrimitive::Lines)
      for (std::uint32_t i = 0; i + 1 < count; i += 2) output->indices.insert(output->indices.end(), {i, i + 1});
    else
      for (std::uint32_t i = 0; i + 1 < count; ++i) output->indices.insert(output->indices.end(), {i, i + 1});
  }
  else
  {
    output->topology = GxTopology::Triangles;
    if (primitive == GxPrimitive::Triangles)
      for (std::uint32_t i = 0; i + 2 < count; i += 3) output->indices.insert(output->indices.end(), {i, i + 1, i + 2});
    else if (primitive == GxPrimitive::Quads || primitive == GxPrimitive::Quads2)
    {
      std::uint32_t i = 0;
      for (; i + 3 < count; i += 4)
        output->indices.insert(output->indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
      if (count - i == 3u)
        output->indices.insert(output->indices.end(), {i, i + 1, i + 2});
    }
    else if (primitive == GxPrimitive::TriangleFan)
      for (std::uint32_t i = 1; i + 1 < count; ++i) output->indices.insert(output->indices.end(), {0, i, i + 1});
    else
      for (std::uint32_t i = 0; i + 2 < count; ++i)
        if ((i & 1u) == 0u) output->indices.insert(output->indices.end(), {i, i + 1, i + 2});
        else output->indices.insert(output->indices.end(), {i + 1, i, i + 2});
  }
}
}

GxVertexLoader::GxVertexLoader(const AddressSpace& memory) : m_memory(memory) {}

bool GxVertexLoader::Decode(const GxDrawPacket& packet, std::span<const std::uint32_t> cp,
                            GxDecodedDraw* output) const
{
  if (output == nullptr || cp.size() < 256u)
    return false;
  output->vertices.clear();
  output->indices.clear();
  output->vertices.reserve(packet.vertex_count);
  Reader reader(packet.vertex_data);
  const std::uint32_t vcd_low = cp[0x50u];
  const std::uint32_t vcd_high = cp[0x60u];
  const std::uint32_t g0 = cp[0x70u + packet.vat];
  const std::uint32_t g1 = cp[0x80u + packet.vat];
  const std::uint32_t g2 = cp[0x90u + packet.vat];

  for (std::uint16_t vertex_index = 0; vertex_index < packet.vertex_count; ++vertex_index)
  {
    GxVertex vertex;
    if ((vcd_low & 1u) != 0u)
    {
      if (!reader.ReadU8(&vertex.position_matrix)) return false;
      vertex.position_matrix &= 0x3Fu;
    }
    for (unsigned i = 0; i < 8; ++i)
    {
      if ((vcd_low & (2u << i)) != 0u)
      {
        if (!reader.ReadU8(&vertex.texture_matrix[i])) return false;
        vertex.texture_matrix[i] &= 0x3Fu;
      }
    }

    const std::uint32_t position_desc = Bits(vcd_low, 9, 2);
    const std::uint32_t position_format = Bits(g0, 1, 3);
    const std::uint32_t position_count = Bits(g0, 0, 1) + 2u;
    const std::uint32_t position_size = FormatSize(position_format) * position_count;
    bool skip_vertex = false;
    const std::uint8_t* position = nullptr;
    if (position_desc == 1u)
    {
      position = reader.Take(position_size);
    }
    else if (position_desc == 2u || position_desc == 3u)
    {
      std::uint16_t index = 0;
      if (!reader.ReadIndex(position_desc, &index)) return false;
      skip_vertex = index == (position_desc == 2u ? 0xFFu : 0xFFFFu);
      if (!skip_vertex)
      {
        const std::uint64_t address = static_cast<std::uint64_t>(cp[0xA0u]) +
                                      static_cast<std::uint64_t>(cp[0xB0u] & 0xFFu) * index;
        position = address <= 0xFFFFFFFFu ?
            m_memory.Resolve(static_cast<std::uint32_t>(address), position_size) : nullptr;
      }
    }
    if (position_desc != 0u && position == nullptr && !skip_vertex) return false;
    for (std::uint32_t i = 0; i < position_count && position != nullptr; ++i)
      vertex.position[i] = ReadComponent(position + i * FormatSize(position_format), position_format, Bits(g0, 4, 5));

    const std::uint32_t normal_desc = Bits(vcd_low, 11, 2);
    const std::uint32_t normal_format = Bits(g0, 10, 3);
    const bool has_ntb = Bits(g0, 9, 1) != 0u;
    const bool normal_index3 = Bits(g0, 31, 1) != 0u;
    const std::uint32_t component_size = FormatSize(normal_format);
    const unsigned normal_bits = component_size * 8u;
    const std::uint32_t normal_fraction = normal_format == 1u || normal_format == 3u ?
        normal_bits - 2u : normal_bits - 1u;
    const auto decode_normal = [&](const std::uint8_t* source, std::array<float, 3>& target) {
      for (std::uint32_t i = 0; i < 3u; ++i)
        target[i] = ReadComponent(source + i * component_size, normal_format,
                                  normal_format >= 4u ? 0u : normal_fraction);
    };
    if ((normal_desc == 2u || normal_desc == 3u) && has_ntb && normal_index3)
    {
      std::array<std::array<float, 3>*, 3> targets = {
          &vertex.normal, &vertex.tangent, &vertex.binormal};
      for (std::uint32_t group = 0; group < 3u; ++group)
      {
        std::uint16_t index = 0;
        if (!reader.ReadIndex(normal_desc, &index))
          return false;
        const std::uint64_t address = static_cast<std::uint64_t>(cp[0xA1u]) +
            static_cast<std::uint64_t>(cp[0xB1u] & 0xFFu) * index +
            group * 3u * component_size;
        const std::uint8_t* source = address <= 0xFFFFFFFFu ?
            m_memory.Resolve(static_cast<std::uint32_t>(address), 3u * component_size) : nullptr;
        if (source == nullptr)
          return false;
        decode_normal(source, *targets[group]);
      }
    }
    else
    {
      const std::uint32_t normal_count = has_ntb ? 9u : 3u;
      const std::uint8_t* normal = AttributeData(reader, m_memory, cp, 1u, normal_desc,
                                                  component_size * normal_count);
      if (normal_desc != 0u && normal == nullptr)
        return false;
      if (normal != nullptr)
      {
        decode_normal(normal, vertex.normal);
        if (has_ntb)
        {
          decode_normal(normal + 3u * component_size, vertex.tangent);
          decode_normal(normal + 6u * component_size, vertex.binormal);
        }
      }
    }

    for (unsigned color = 0; color < 2; ++color)
    {
      const std::uint32_t descriptor = Bits(vcd_low, 13u + color * 2u, 2);
      const std::uint32_t format = Bits(g0, 14u + color * 4u, 3);
      const std::uint8_t* source = AttributeData(reader, m_memory, cp, 2u + color, descriptor, ColorSize(format));
      if (descriptor != 0u && source == nullptr) return false;
      if (source != nullptr) vertex.color[color] = DecodeColor(source, format);
    }
    for (unsigned texture = 0; texture < 8; ++texture)
    {
      const std::uint32_t descriptor = Bits(vcd_high, texture * 2u, 2);
      const auto [format, elements] = TextureFormat(g0, g1, g2, texture);
      const std::uint32_t count = elements + 1u;
      const std::uint8_t* source = AttributeData(reader, m_memory, cp, 4u + texture, descriptor, FormatSize(format) * count);
      if (descriptor != 0u && source == nullptr) return false;
      for (std::uint32_t i = 0; i < count && source != nullptr; ++i)
        vertex.texcoord[texture][i] = ReadComponent(source + i * FormatSize(format), format, TextureFraction(g0, g1, g2, texture));
    }
    if (!skip_vertex)
      output->vertices.push_back(vertex);
  }
  if (!reader.AtEnd())
    return false;
  AddIndices(packet.primitive, static_cast<std::uint32_t>(output->vertices.size()), output);
  return true;
}
}
