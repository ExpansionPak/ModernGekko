// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/event_scheduler.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace moderngekko
{
EventScheduler::EventId EventScheduler::ScheduleAfter(std::uint64_t cycles, Callback callback)
{
  const EventId id = m_next_id++;
  const std::uint64_t due = cycles > std::numeric_limits<std::uint64_t>::max() - m_ticks ?
      std::numeric_limits<std::uint64_t>::max() : m_ticks + cycles;
  m_events.push_back({due, id, std::move(callback)});
  return id;
}

bool EventScheduler::Cancel(EventId id)
{
  const auto event = std::ranges::find(m_events, id, &Event::id);
  if (event == m_events.end())
    return false;
  m_events.erase(event);
  return true;
}

void EventScheduler::Advance(std::uint64_t cycles)
{
  m_ticks = cycles > std::numeric_limits<std::uint64_t>::max() - m_ticks ?
      std::numeric_limits<std::uint64_t>::max() : m_ticks + cycles;

  while (true)
  {
    const auto event = std::ranges::min_element(m_events, {}, [](const Event& value) {
      return std::pair{value.due, value.id};
    });
    if (event == m_events.end() || event->due > m_ticks)
      return;

    Event ready = std::move(*event);
    m_events.erase(event);
    if (ready.callback)
      ready.callback(m_ticks - ready.due);
  }
}

void EventScheduler::Reset()
{
  m_events.clear();
  m_ticks = 0;
  m_next_id = 1;
}

std::uint64_t EventScheduler::GetTicks() const
{
  return m_ticks;
}

std::size_t EventScheduler::GetPendingEventCount() const
{
  return m_events.size();
}
}
