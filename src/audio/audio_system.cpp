// Copyright 2003 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/audio_system.hpp"

#include <array>
#include <utility>

namespace moderngekko
{
namespace
{
constexpr std::uint64_t AiCyclesPerSample = 10116u;
constexpr std::uint16_t DspAidInterrupt = 0x08u;
constexpr std::uint16_t DspAidInterruptMask = 0x10u;
}

AudioSystem::AudioSystem(AddressSpace& memory, EventScheduler& scheduler,
                         ProcessorInterface& processor_interface)
    : m_memory(memory), m_scheduler(scheduler), m_processor_interface(processor_interface)
{
}

void AudioSystem::RegisterMmio(MmioBus& bus, bool is_wii)
{
  for (std::uint32_t offset = 0; offset <= 0x0Cu; offset += 4u)
  {
    bus.Register(AiMmioBase + offset, 4u,
                 [this, offset](std::uint32_t, std::uint8_t) { return ReadAi(offset); },
                 [this, offset](std::uint32_t, std::uint64_t value, std::uint8_t) {
                   WriteAi(offset, static_cast<std::uint32_t>(value));
                 });
  }

  for (const std::uint32_t offset : {0x0Au, 0x30u, 0x32u, 0x36u, 0x3Au})
  {
    bus.Register(DspMmioBase + offset, 2u,
                 [this, offset](std::uint32_t, std::uint8_t) { return ReadDsp(offset); },
                 [this, offset, is_wii](std::uint32_t, std::uint64_t value, std::uint8_t) {
                   WriteDsp(offset, static_cast<std::uint16_t>(value), is_wii);
                 });
  }
}

void AudioSystem::Reset()
{
  if (m_ai_event != 0)
    m_scheduler.Cancel(m_ai_event);
  if (m_dma_event != 0)
    m_scheduler.Cancel(m_dma_event);
  m_ai_event = 0;
  m_dma_event = 0;
  m_ai_control = 0;
  m_ai_volume = 0;
  m_sample_counter = 0;
  m_interrupt_timing = 0;
  m_last_ai_tick = m_scheduler.GetTicks();
  m_dsp_control = 0;
  m_dma_source = 0;
  m_current_source = 0;
  m_dma_control = 0;
  m_remaining_blocks = 0;
  UpdateAiInterrupt();
  UpdateDspInterrupt();
}

void AudioSystem::SetDmaSink(DmaSink sink)
{
  m_dma_sink = std::move(sink);
}

std::uint32_t AudioSystem::ReadAi(std::uint32_t offset)
{
  UpdateAiCounter();
  switch (offset)
  {
  case 0x00: return m_ai_control;
  case 0x04: return m_ai_volume;
  case 0x08: return m_sample_counter;
  case 0x0C: return m_interrupt_timing;
  default: return 0;
  }
}

void AudioSystem::WriteAi(std::uint32_t offset, std::uint32_t value)
{
  UpdateAiCounter();
  switch (offset)
  {
  case 0x00:
  {
    const bool was_playing = (m_ai_control & 1u) != 0u;
    const std::uint32_t interrupt = (value & 8u) != 0u ? 0u : m_ai_control & 8u;
    m_ai_control = (value & 0x57u) | interrupt;
    if ((value & 0x20u) != 0u)
      m_sample_counter = 0;
    m_ai_control &= ~0x20u;
    m_last_ai_tick = m_scheduler.GetTicks();
    if (was_playing != ((m_ai_control & 1u) != 0u) || (m_ai_control & 1u) != 0u)
      ScheduleAiInterrupt();
    UpdateAiInterrupt();
    break;
  }
  case 0x04: m_ai_volume = value & 0xFFFFu; break;
  case 0x08:
    m_sample_counter = value;
    m_last_ai_tick = m_scheduler.GetTicks();
    ScheduleAiInterrupt();
    break;
  case 0x0C:
    m_interrupt_timing = value;
    ScheduleAiInterrupt();
    break;
  default: break;
  }
}

std::uint16_t AudioSystem::ReadDsp(std::uint32_t offset) const
{
  switch (offset)
  {
  case 0x0A: return m_dsp_control;
  case 0x30: return static_cast<std::uint16_t>(m_dma_source >> 16);
  case 0x32: return static_cast<std::uint16_t>(m_dma_source);
  case 0x36: return m_dma_control;
  case 0x3A: return m_remaining_blocks > 0 ? m_remaining_blocks - 1u : 0u;
  default: return 0;
  }
}

void AudioSystem::WriteDsp(std::uint32_t offset, std::uint16_t value, bool is_wii)
{
  switch (offset)
  {
  case 0x0A:
    if ((value & DspAidInterrupt) != 0u)
      m_dsp_control &= ~DspAidInterrupt;
    m_dsp_control = (m_dsp_control & DspAidInterrupt) |
                    (value & static_cast<std::uint16_t>(~DspAidInterrupt));
    UpdateDspInterrupt();
    break;
  case 0x30:
    m_dma_source = (m_dma_source & 0xFFFFu) |
                   (static_cast<std::uint32_t>(value & (is_wii ? 0x1FFFu : 0x03FFu)) << 16);
    break;
  case 0x32:
    m_dma_source = (m_dma_source & 0xFFFF0000u) | (value & 0xFFE0u);
    break;
  case 0x36:
  {
    const bool was_enabled = (m_dma_control & 0x8000u) != 0u;
    m_dma_control = value;
    if (!was_enabled && (m_dma_control & 0x8000u) != 0u)
    {
      m_current_source = m_dma_source;
      m_remaining_blocks = m_dma_control & 0x7FFFu;
      ScheduleDmaBlock(200u);
    }
    break;
  }
  default: break;
  }
}

void AudioSystem::ScheduleAiInterrupt()
{
  if (m_ai_event != 0)
    m_scheduler.Cancel(m_ai_event);
  m_ai_event = 0;
  if ((m_ai_control & 1u) == 0u)
    return;
  const std::uint32_t samples = m_interrupt_timing - m_sample_counter;
  const std::uint64_t delay = samples == 0u ? AiCyclesPerSample : samples * AiCyclesPerSample;
  m_ai_event = m_scheduler.ScheduleAfter(delay, [this](std::uint64_t) {
    UpdateAiCounter();
    m_ai_control |= 8u;
    UpdateAiInterrupt();
    m_ai_event = 0;
  });
}

void AudioSystem::ScheduleDmaBlock(std::uint64_t delay)
{
  if (m_dma_event != 0)
    m_scheduler.Cancel(m_dma_event);
  m_dma_event = m_scheduler.ScheduleAfter(delay, [this](std::uint64_t) {
    m_dma_event = 0;
    TransferDmaBlock();
  });
}

void AudioSystem::TransferDmaBlock()
{
  if ((m_dma_control & 0x8000u) == 0u)
    return;

  std::array<std::int16_t, 16> samples{};
  const std::uint8_t* input = m_memory.Resolve(m_current_source, 32u);
  if (input != nullptr)
  {
    for (std::size_t i = 0; i < samples.size(); ++i)
      samples[i] = static_cast<std::int16_t>((input[i * 2] << 8) | input[i * 2 + 1]);
  }
  if (m_dma_sink)
    m_dma_sink(samples);

  if (m_remaining_blocks > 0u)
  {
    --m_remaining_blocks;
    m_current_source += 32u;
  }
  if (m_remaining_blocks == 0u)
  {
    m_current_source = m_dma_source;
    m_remaining_blocks = m_dma_control & 0x7FFFu;
    m_dsp_control |= DspAidInterrupt;
    UpdateDspInterrupt();
  }
  ScheduleDmaBlock(AudioDmaPeriod);
}

void AudioSystem::UpdateAiCounter()
{
  if ((m_ai_control & 1u) == 0u)
    return;
  const std::uint64_t elapsed = m_scheduler.GetTicks() - m_last_ai_tick;
  const std::uint32_t samples = static_cast<std::uint32_t>(elapsed / AiCyclesPerSample);
  m_sample_counter += samples;
  m_last_ai_tick += static_cast<std::uint64_t>(samples) * AiCyclesPerSample;
}

void AudioSystem::UpdateAiInterrupt()
{
  m_processor_interface.SetInterrupt(ProcessorInterface::AudioInterface,
                                     (m_ai_control & 0x0Cu) == 0x0Cu);
}

void AudioSystem::UpdateDspInterrupt()
{
  m_processor_interface.SetInterrupt(ProcessorInterface::DspInterface,
                                     (m_dsp_control & DspAidInterrupt) != 0u &&
                                     (m_dsp_control & DspAidInterruptMask) != 0u);
}
}
