// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/controller.hpp"

#include <utility>

namespace moderngekko
{
void ControllerPort::SetProvider(Provider provider)
{
  m_provider = std::move(provider);
}

void ControllerPort::SetMode(std::uint8_t mode)
{
  m_mode = mode & 7u;
}

bool ControllerPort::Read(std::uint32_t* high, std::uint32_t* low) const
{
  if (!m_provider || high == nullptr || low == nullptr)
    return false;
  const ControllerState state = m_provider();
  if (!state.connected)
    return false;

  *high = state.stick_y | (static_cast<std::uint32_t>(state.stick_x) << 8) |
          (static_cast<std::uint32_t>(state.buttons | 0x0080u) << 16);
  if (m_mode == 3u)
  {
    *low = state.trigger_right | (static_cast<std::uint32_t>(state.trigger_left) << 8) |
           (static_cast<std::uint32_t>(state.substick_y) << 16) |
           (static_cast<std::uint32_t>(state.substick_x) << 24);
  }
  else
  {
    *low = (state.analog_b >> 4) | (static_cast<std::uint32_t>(state.analog_a >> 4) << 4) |
           (static_cast<std::uint32_t>(state.trigger_right >> 4) << 8) |
           (static_cast<std::uint32_t>(state.trigger_left >> 4) << 12) |
           (static_cast<std::uint32_t>(state.substick_y) << 16) |
           (static_cast<std::uint32_t>(state.substick_x) << 24);
  }
  return true;
}
}
