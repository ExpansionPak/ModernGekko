#pragma once

#include "moderngekko/address_space.hpp"
#include "moderngekko/event_scheduler.hpp"
#include "moderngekko/mmio_bus.hpp"
#include "moderngekko/processor_interface.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace moderngekko
{
class AudioSystem final
{
public:
  using DmaSink = std::function<void(std::span<const std::int16_t> interleaved_stereo)>;

  static constexpr std::uint32_t DspMmioBase = 0x0C005000u;
  static constexpr std::uint32_t AiMmioBase = 0x0C006C00u;
  static constexpr std::uint64_t AudioDmaPeriod = 121500u;

  AudioSystem(AddressSpace& memory, EventScheduler& scheduler,
              ProcessorInterface& processor_interface);

  void RegisterMmio(MmioBus& bus, bool is_wii);
  void Reset();
  void SetDmaSink(DmaSink sink);

private:
  std::uint32_t ReadAi(std::uint32_t offset);
  void WriteAi(std::uint32_t offset, std::uint32_t value);
  std::uint16_t ReadDsp(std::uint32_t offset) const;
  void WriteDsp(std::uint32_t offset, std::uint16_t value, bool is_wii);
  void ScheduleAiInterrupt();
  void ScheduleDmaBlock(std::uint64_t delay);
  void TransferDmaBlock();
  void UpdateAiCounter();
  void UpdateAiInterrupt();
  void UpdateDspInterrupt();

  AddressSpace& m_memory;
  EventScheduler& m_scheduler;
  ProcessorInterface& m_processor_interface;
  DmaSink m_dma_sink;
  EventScheduler::EventId m_ai_event = 0;
  EventScheduler::EventId m_dma_event = 0;
  std::uint32_t m_ai_control = 0;
  std::uint32_t m_ai_volume = 0;
  std::uint32_t m_sample_counter = 0;
  std::uint32_t m_interrupt_timing = 0;
  std::uint64_t m_last_ai_tick = 0;
  std::uint16_t m_dsp_control = 0;
  std::uint32_t m_dma_source = 0;
  std::uint32_t m_current_source = 0;
  std::uint16_t m_dma_control = 0;
  std::uint16_t m_remaining_blocks = 0;
};
}
