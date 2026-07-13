#include "moderngekko/gx_vertex_loader.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <vector>

int main()
{
  moderngekko::AddressSpace memory;
  moderngekko::GxVertexLoader loader(memory);
  std::array<std::uint32_t, 256> cp{};
  cp[0x50] = 1u << 9;
  cp[0x70] = 9u;

  std::array<std::uint8_t, 48> data{};
  const std::array<float, 12> positions = {
      0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  for (std::size_t i = 0; i < positions.size(); ++i)
  {
    const std::uint32_t value = std::bit_cast<std::uint32_t>(positions[i]);
    data[i * 4] = static_cast<std::uint8_t>(value >> 24);
    data[i * 4 + 1] = static_cast<std::uint8_t>(value >> 16);
    data[i * 4 + 2] = static_cast<std::uint8_t>(value >> 8);
    data[i * 4 + 3] = static_cast<std::uint8_t>(value);
  }

  moderngekko::GxDecodedDraw decoded;
  const moderngekko::GxDrawPacket packet = {
      moderngekko::GxPrimitive::Quads, 0u, 12u, 4u, data};
  if (!loader.Decode(packet, cp, &decoded) || decoded.vertices.size() != 4u ||
      decoded.indices != std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3}) ||
      std::abs(decoded.vertices[2].position[1] - 1.0f) > 0.0001f)
  {
    return 1;
  }

  cp[0x50] = 2u << 9;
  cp[0xA0] = 0x80001000u;
  cp[0xB0] = 12u;
  for (std::size_t i = 0; i < data.size(); ++i)
    memory.Write8(0x80001000u + static_cast<std::uint32_t>(i), data[i]);
  const std::array<std::uint8_t, 3> indices = {0u, 2u, 3u};
  const moderngekko::GxDrawPacket indexed = {
      moderngekko::GxPrimitive::Triangles, 0u, 1u, 3u, indices};
  if (!loader.Decode(indexed, cp, &decoded) || decoded.vertices.size() != 3u ||
      std::abs(decoded.vertices[1].position[0] - 1.0f) > 0.0001f ||
      decoded.indices != std::vector<std::uint32_t>({0, 1, 2}))
  {
    return 2;
  }

  cp = {};
  cp[0x50] = 2u << 11;
  cp[0x70] = (1u << 9) | (1u << 10) | (1u << 31);
  cp[0xA1] = 0x80002000u;
  cp[0xB1] = 9u;
  const std::array<std::uint8_t, 9> ntb = {64u, 0u, 0u, 0u, 64u, 0u, 0u, 0u, 64u};
  for (std::size_t i = 0; i < ntb.size(); ++i)
    memory.Write8(0x80002000u + static_cast<std::uint32_t>(i), ntb[i]);
  const std::array<std::uint8_t, 3> normal_indices{};
  const moderngekko::GxDrawPacket indexed_normals = {
      moderngekko::GxPrimitive::Points, 0u, 3u, 1u, normal_indices};
  if (!loader.Decode(indexed_normals, cp, &decoded) || decoded.vertices.size() != 1u ||
      std::abs(decoded.vertices[0].normal[0] - 1.0f) > 0.0001f ||
      std::abs(decoded.vertices[0].tangent[1] - 1.0f) > 0.0001f ||
      std::abs(decoded.vertices[0].binormal[2] - 1.0f) > 0.0001f)
  {
    return 3;
  }

  cp = {};
  cp[0x50] = (1u << 0) | (2u << 9);
  cp[0x70] = 9u;
  cp[0xA0] = 0x80001000u;
  cp[0xB0] = 12u;
  const std::array<std::uint8_t, 8> skipped = {
      0xFFu, 0u, 0x7Fu, 0xFFu, 0xFFu, 2u, 0xFFu, 3u};
  const moderngekko::GxDrawPacket skipped_packet = {
      moderngekko::GxPrimitive::Quads, 0u, 2u, 4u, skipped};
  if (!loader.Decode(skipped_packet, cp, &decoded) || decoded.vertices.size() != 3u ||
      decoded.vertices[0].position_matrix != 0x3Fu ||
      decoded.indices != std::vector<std::uint32_t>({0, 1, 2}))
  {
    return 4;
  }

  struct ColorCase
  {
    std::uint32_t format;
    std::vector<std::uint8_t> bytes;
    std::uint32_t expected;
  };
  const std::array<ColorCase, 6> colors = {{
      {0u, {0xF8u, 0x00u}, 0xFF0000FFu},
      {1u, {0x01u, 0x02u, 0x03u}, 0x010203FFu},
      {2u, {0x01u, 0x02u, 0x03u, 0x99u}, 0x010203FFu},
      {3u, {0xF1u, 0x2Eu}, 0xFF1122EEu},
      {4u, {0xFFu, 0xFFu, 0xFFu}, 0xFFFFFFFFu},
      {5u, {0x01u, 0x02u, 0x03u, 0x04u}, 0x01020304u},
  }};
  for (const ColorCase& color : colors)
  {
    cp = {};
    cp[0x50] = 1u << 13;
    cp[0x70] = color.format << 14;
    const moderngekko::GxDrawPacket color_packet = {
        moderngekko::GxPrimitive::Points, 0u,
        static_cast<std::uint32_t>(color.bytes.size()), 1u, color.bytes};
    if (!loader.Decode(color_packet, cp, &decoded) || decoded.vertices.size() != 1u ||
        decoded.vertices[0].color[0] != color.expected)
    {
      return 5;
    }
  }

  cp = {};
  cp[0x50] = 1u << 11;
  cp[0x70] = 1u << 10;
  const std::array<std::uint8_t, 3> signed_normal = {0xC0u, 0x00u, 0x40u};
  const moderngekko::GxDrawPacket normal_packet = {
      moderngekko::GxPrimitive::Points, 0u, 3u, 1u, signed_normal};
  if (!loader.Decode(normal_packet, cp, &decoded) ||
      std::abs(decoded.vertices[0].normal[0] + 1.0f) > 0.0001f ||
      std::abs(decoded.vertices[0].normal[2] - 1.0f) > 0.0001f)
  {
    return 6;
  }

  cp = {};
  cp[0x60] = 1u;
  cp[0x70] = (1u << 21) | (3u << 22) | (1u << 25);
  const std::array<std::uint8_t, 4> texture = {0x00u, 0x02u, 0xFFu, 0xFEu};
  const moderngekko::GxDrawPacket texture_packet = {
      moderngekko::GxPrimitive::Points, 0u, 4u, 1u, texture};
  if (!loader.Decode(texture_packet, cp, &decoded) ||
      std::abs(decoded.vertices[0].texcoord[0][0] - 1.0f) > 0.0001f ||
      std::abs(decoded.vertices[0].texcoord[0][1] + 1.0f) > 0.0001f)
  {
    return 7;
  }
  return 0;
}
