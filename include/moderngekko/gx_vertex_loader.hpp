#pragma once

#include "moderngekko/address_space.hpp"
#include "moderngekko/gpu_backend.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace moderngekko
{
enum class GxTopology
{
  Triangles,
  Lines,
  Points,
};

struct GxVertex
{
  std::array<float, 3> position{};
  std::array<float, 3> normal{};
  std::array<float, 3> tangent{};
  std::array<float, 3> binormal{};
  std::array<std::uint32_t, 2> color{0xFFFFFFFFu, 0xFFFFFFFFu};
  std::array<std::array<float, 2>, 8> texcoord{};
  std::uint8_t position_matrix = 0;
  std::array<std::uint8_t, 8> texture_matrix{};
};

struct GxDecodedDraw
{
  GxTopology topology = GxTopology::Triangles;
  std::vector<GxVertex> vertices;
  std::vector<std::uint32_t> indices;
};

class GxVertexLoader final
{
public:
  explicit GxVertexLoader(const AddressSpace& memory);
  bool Decode(const GxDrawPacket& packet, std::span<const std::uint32_t> cp,
              GxDecodedDraw* output) const;

private:
  const AddressSpace& m_memory;
};
}
