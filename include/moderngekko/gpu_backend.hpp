#pragma once

#include <cstdint>
#include <span>

namespace moderngekko
{
enum class GxPrimitive : std::uint8_t
{
  Quads,
  Quads2,
  Triangles,
  TriangleStrip,
  TriangleFan,
  Lines,
  LineStrip,
  Points,
};

struct GxDrawPacket
{
  GxPrimitive primitive = GxPrimitive::Triangles;
  std::uint8_t vat = 0;
  std::uint32_t vertex_size = 0;
  std::uint16_t vertex_count = 0;
  std::span<const std::uint8_t> vertex_data;
};

class GpuBackend
{
public:
  virtual ~GpuBackend() = default;
  virtual void LoadCpRegister(std::uint8_t, std::uint32_t) {}
  virtual void LoadXfRegisters(std::uint16_t, std::span<const std::uint32_t>) {}
  virtual void LoadBpRegister(std::uint8_t, std::uint32_t) {}
  virtual void LoadIndexedXf(std::uint8_t, std::uint16_t, std::uint16_t, std::uint8_t) {}
  virtual void Draw(const GxDrawPacket&) {}
  virtual void CallDisplayList(std::uint32_t, std::uint32_t) {}
  virtual void InvalidateVertexCache() {}
};
}
