#pragma once

#include "moderngekko/cpu_state.h"
#include "moderngekko/mmio_bus.hpp"

#include <cstdint>

namespace moderngekko
{
class ProcessorInterface final
{
public:
  enum InterruptCause : std::uint32_t
  {
    Pi = 0x1,
    ResetSwitch = 0x2,
    DiscInterface = 0x4,
    SerialInterface = 0x8,
    ExpansionInterface = 0x10,
    AudioInterface = 0x20,
    DspInterface = 0x40,
    MemoryInterface = 0x80,
    VideoInterface = 0x100,
    PixelEngineToken = 0x200,
    PixelEngineFinish = 0x400,
    CommandProcessor = 0x800,
    WiiIpc = 0x4000,
    ResetButton = 0x10000,
  };

  static constexpr std::uint32_t MmioBase = 0x0C003000u;
  static constexpr std::uint32_t ExternalInterrupt = 0x00000004u;

  explicit ProcessorInterface(CPUState& cpu);

  void RegisterMmio(MmioBus& bus);
  void Reset();
  void SetInterrupt(std::uint32_t cause, bool set = true);
  std::uint32_t GetCause() const;
  std::uint32_t GetMask() const;

private:
  void UpdateException();

  CPUState& m_cpu;
  std::uint32_t m_cause = 0;
  std::uint32_t m_mask = 0;
};
}
