#include "moderngekko/gx_command_processor.hpp"
#include "moderngekko/legacy_runtime.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace
{
class RecordingBackend final : public moderngekko::GpuBackend
{
public:
  void LoadCpRegister(std::uint8_t command, std::uint32_t value) override
  {
    cp_command = command;
    cp_value = value;
  }
  void LoadXfRegisters(std::uint16_t address, std::span<const std::uint32_t> values) override
  {
    xf_address = address;
    xf_values.assign(values.begin(), values.end());
  }
  void LoadBpRegister(std::uint8_t command, std::uint32_t value) override
  {
    bp_command = command;
    bp_value = value;
  }
  void Draw(const moderngekko::GxDrawPacket& packet) override
  {
    draw_count = packet.vertex_count;
    draw_size = packet.vertex_size;
    draw_data.assign(packet.vertex_data.begin(), packet.vertex_data.end());
  }
  void CallDisplayList(std::uint32_t address, std::uint32_t size) override
  {
    display_list_address = address;
    display_list_size = size;
  }

  std::uint8_t cp_command = 0;
  std::uint32_t cp_value = 0;
  std::uint16_t xf_address = 0;
  std::vector<std::uint32_t> xf_values;
  std::uint8_t bp_command = 0;
  std::uint32_t bp_value = 0;
  std::uint16_t draw_count = 0;
  std::uint32_t draw_size = 0;
  std::vector<std::uint8_t> draw_data;
  std::uint32_t display_list_address = 0;
  std::uint32_t display_list_size = 0;
};
}

int main()
{
  RecordingBackend backend;
  moderngekko::GxCommandProcessor fifo(&backend);

  const std::array<std::uint8_t, 6> vcd = {0x08, 0x50, 0x00, 0x00, 0x02, 0x00};
  const std::array<std::uint8_t, 6> vat = {0x08, 0x70, 0x00, 0x00, 0x00, 0x09};
  fifo.WriteBytes(vcd);
  fifo.WriteBytes(vat);
  if (backend.cp_command != 0x70u || backend.cp_value != 9u || fifo.GetVertexSize(0) != 12u)
    return 1;

  const std::array<std::uint8_t, 13> xf = {
      0x10, 0x00, 0x01, 0x10, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};
  fifo.WriteBytes(std::span{xf}.first(7));
  if (fifo.GetBufferedByteCount() != 7u || !backend.xf_values.empty())
    return 2;
  fifo.WriteBytes(std::span{xf}.subspan(7));
  if (backend.xf_address != 0x1000u || backend.xf_values.size() != 2u ||
      backend.xf_values[1] != 0x40000000u)
  {
    return 3;
  }

  const std::array<std::uint8_t, 5> bp = {0x61, 0xFE, 0x12, 0x34, 0x56};
  fifo.WriteBytes(bp);
  if (backend.bp_command != 0xFEu || backend.bp_value != 0x123456u)
    return 4;

  std::vector<std::uint8_t> draw = {0x90, 0x00, 0x02};
  for (std::uint8_t i = 0; i < 24u; ++i)
    draw.push_back(i);
  fifo.WriteBytes(draw);
  if (backend.draw_count != 2u || backend.draw_size != 12u || backend.draw_data.size() != 24u)
    return 5;

  fifo.Write(0xFFu, 1u);
  if (fifo.GetUnknownOpcodeCount() != 1u || fifo.GetCommandCount() != 6u)
    return 6;

  moderngekko::LegacyRuntime runtime;
  RecordingBackend runtime_backend;
  runtime.SetGpuBackend(&runtime_backend);
  const std::array<std::uint8_t, 5> list_bp = {0x61, 0x42, 0x65, 0x43, 0x21};
  for (std::size_t i = 0; i < list_bp.size(); ++i)
    runtime.GetAddressSpace().Write8(0x80002000u + static_cast<std::uint32_t>(i), list_bp[i]);
  CPUState& cpu = runtime.GetCpuState();
  cpu.external_write(&cpu, 0xCC008000u, 0x40000020u, 4u);
  cpu.external_write(&cpu, 0xCC008000u, 0x00000000u, 4u);
  cpu.external_write(&cpu, 0xCC008000u, 0x20u, 1u);
  if (runtime.GetGxCommandProcessor().GetBufferedByteCount() != 0u ||
      runtime_backend.display_list_address != 0x2000u || runtime_backend.display_list_size != 0x20u ||
      runtime_backend.bp_command != 0x42u || runtime_backend.bp_value != 0x654321u)
  {
    return 7;
  }
  return 0;
}
