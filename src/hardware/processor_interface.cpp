// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/processor_interface.hpp"

namespace moderngekko
{
ProcessorInterface::ProcessorInterface(CPUState& cpu) : m_cpu(cpu)
{
  Reset();
}

void ProcessorInterface::RegisterMmio(MmioBus& bus)
{
  bus.Register(MmioBase, 4u,
               [this](std::uint32_t, std::uint8_t) { return m_cause; },
               [this](std::uint32_t, std::uint64_t value, std::uint8_t) {
                 m_cause &= ~static_cast<std::uint32_t>(value);
                 UpdateException();
               });
  bus.Register(MmioBase + 4u, 4u,
               [this](std::uint32_t, std::uint8_t) { return m_mask; },
               [this](std::uint32_t, std::uint64_t value, std::uint8_t) {
                 m_mask = static_cast<std::uint32_t>(value);
                 UpdateException();
               });
}

void ProcessorInterface::Reset()
{
  m_cause = ResetButton | VideoInterface;
  m_mask = 0;
  UpdateException();
}

void ProcessorInterface::SetInterrupt(std::uint32_t cause, bool set)
{
  m_cause = set ? m_cause | cause : m_cause & ~cause;
  UpdateException();
}

std::uint32_t ProcessorInterface::GetCause() const
{
  return m_cause;
}

std::uint32_t ProcessorInterface::GetMask() const
{
  return m_mask;
}

void ProcessorInterface::UpdateException()
{
  if ((m_cause & m_mask) != 0u)
    m_cpu.exception |= ExternalInterrupt;
  else
    m_cpu.exception &= ~ExternalInterrupt;
}
}
