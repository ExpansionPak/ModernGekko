// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/disc_interface.hpp"

#include <algorithm>

namespace moderngekko
{
namespace
{
constexpr std::uint32_t TransferCompleteMask = 1u << 3;
constexpr std::uint32_t TransferComplete = 1u << 4;
}

DiscInterface::DiscInterface(AddressSpace& memory, EventScheduler& scheduler,
                             ProcessorInterface& processor_interface)
    : m_memory(memory), m_scheduler(scheduler), m_processor_interface(processor_interface)
{
}

void DiscInterface::SetSource(DiscSource* source)
{
  m_source = source;
  m_cover = source == nullptr ? 1u : 0u;
}

void DiscInterface::RegisterMmio(MmioBus& bus, bool is_wii)
{
  const std::uint32_t base = is_wii ? WiiMmioBase : GameCubeMmioBase;
  for (std::uint32_t offset = 0; offset <= 0x24u; offset += 4u)
  {
    bus.Register(base + offset, 4u,
                 [this, offset](std::uint32_t, std::uint8_t) { return ReadRegister(offset); },
                 [this, offset](std::uint32_t, std::uint64_t value, std::uint8_t) {
                   WriteRegister(offset, static_cast<std::uint32_t>(value));
                 });
  }
}

void DiscInterface::Reset()
{
  m_status = 0;
  m_cover = m_source == nullptr ? 1u : 0u;
  std::ranges::fill(m_command, 0u);
  m_dma_address = 0;
  m_dma_length = 0;
  m_dma_control = 0;
  m_immediate = 0;
  UpdateInterrupt();
}

std::uint32_t DiscInterface::ReadRegister(std::uint32_t offset) const
{
  switch (offset)
  {
  case 0x00: return m_status;
  case 0x04: return m_cover;
  case 0x08: return m_command[0];
  case 0x0C: return m_command[1];
  case 0x10: return m_command[2];
  case 0x14: return m_dma_address;
  case 0x18: return m_dma_length;
  case 0x1C: return m_dma_control;
  case 0x20: return m_immediate;
  case 0x24: return 1u;
  default: return 0;
  }
}

void DiscInterface::WriteRegister(std::uint32_t offset, std::uint32_t value)
{
  switch (offset)
  {
  case 0x00:
    m_status = (m_status & ~(value & 0x54u)) | (value & 0x2Au);
    UpdateInterrupt();
    break;
  case 0x08: m_command[0] = value; break;
  case 0x0C: m_command[1] = value; break;
  case 0x10: m_command[2] = value; break;
  case 0x14: m_dma_address = value & 0x03FFFFE0u; break;
  case 0x18: m_dma_length = value & ~0x1Fu; break;
  case 0x1C:
    m_dma_control = value & 7u;
    if ((m_dma_control & 1u) != 0u)
      StartTransfer();
    break;
  case 0x20: m_immediate = value; break;
  default: break;
  }
}

void DiscInterface::StartTransfer()
{
  m_scheduler.ScheduleAfter(1u, [this](std::uint64_t) { FinishTransfer(); });
}

void DiscInterface::FinishTransfer()
{
  const std::uint8_t command = static_cast<std::uint8_t>(m_command[0] >> 24);
  bool success = false;
  if (command == 0xA8u && m_source != nullptr)
  {
    const std::uint64_t offset = static_cast<std::uint64_t>(m_command[1]) << 2;
    std::uint8_t* output = m_memory.Resolve(m_dma_address, m_dma_length);
    success = output != nullptr && offset <= m_source->GetSize() &&
              m_dma_length <= m_source->GetSize() - offset &&
              m_source->Read(offset, {output, m_dma_length});
  }

  m_dma_control &= ~1u;
  m_dma_length = 0;
  m_status |= success ? TransferComplete : 0x4u;
  UpdateInterrupt();
}

void DiscInterface::UpdateInterrupt()
{
  m_processor_interface.SetInterrupt(ProcessorInterface::DiscInterface,
                                     (m_status & TransferCompleteMask) != 0u &&
                                     (m_status & TransferComplete) != 0u);
}
}
