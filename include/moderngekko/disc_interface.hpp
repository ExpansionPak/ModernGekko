#pragma once

#include "moderngekko/address_space.hpp"
#include "moderngekko/event_scheduler.hpp"
#include "moderngekko/mmio_bus.hpp"
#include "moderngekko/processor_interface.hpp"

#include <cstdint>
#include <span>

namespace moderngekko
{
class DiscSource
{
public:
  virtual ~DiscSource() = default;
  virtual std::uint64_t GetSize() const = 0;
  virtual bool Read(std::uint64_t offset, std::span<std::uint8_t> output) = 0;
};

class DiscInterface final
{
public:
  static constexpr std::uint32_t GameCubeMmioBase = 0x0C006000u;
  static constexpr std::uint32_t WiiMmioBase = 0x0D006000u;

  DiscInterface(AddressSpace& memory, EventScheduler& scheduler,
                ProcessorInterface& processor_interface);

  void SetSource(DiscSource* source);
  void RegisterMmio(MmioBus& bus, bool is_wii);
  void Reset();

private:
  std::uint32_t ReadRegister(std::uint32_t offset) const;
  void WriteRegister(std::uint32_t offset, std::uint32_t value);
  void StartTransfer();
  void FinishTransfer();
  void UpdateInterrupt();

  AddressSpace& m_memory;
  EventScheduler& m_scheduler;
  ProcessorInterface& m_processor_interface;
  DiscSource* m_source = nullptr;
  std::uint32_t m_status = 0;
  std::uint32_t m_cover = 1;
  std::uint32_t m_command[3]{};
  std::uint32_t m_dma_address = 0;
  std::uint32_t m_dma_length = 0;
  std::uint32_t m_dma_control = 0;
  std::uint32_t m_immediate = 0;
};
}
