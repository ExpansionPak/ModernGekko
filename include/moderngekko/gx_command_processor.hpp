#pragma once

#include "moderngekko/gpu_backend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace moderngekko
{
class AddressSpace;

class GxCommandProcessor final
{
public:
  explicit GxCommandProcessor(GpuBackend* backend = nullptr, AddressSpace* memory = nullptr);

  void SetBackend(GpuBackend* backend);
  void SetAddressSpace(AddressSpace* memory);
  void Reset();
  void Write(std::uint64_t value, std::uint8_t size);
  void WriteBytes(std::span<const std::uint8_t> bytes);

  std::size_t GetBufferedByteCount() const;
  std::uint64_t GetCommandCount() const;
  std::uint64_t GetUnknownOpcodeCount() const;
  std::uint32_t GetVertexSize(std::uint8_t vat) const;

private:
  std::size_t DecodeOne(std::span<const std::uint8_t> data);
  void ExecuteDisplayList(std::uint32_t address, std::uint32_t size);
  void LoadCp(std::uint8_t command, std::uint32_t value);

  GpuBackend* m_backend = nullptr;
  AddressSpace* m_memory = nullptr;
  std::vector<std::uint8_t> m_buffer;
  std::uint32_t m_vcd_low = 0;
  std::uint32_t m_vcd_high = 0;
  std::array<std::array<std::uint32_t, 3>, 8> m_vat{};
  std::array<std::uint32_t, 16> m_array_bases{};
  std::array<std::uint8_t, 16> m_array_strides{};
  std::uint64_t m_command_count = 0;
  std::uint64_t m_unknown_opcode_count = 0;
  std::uint8_t m_display_list_depth = 0;
};
}
