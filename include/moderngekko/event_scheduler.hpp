#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace moderngekko
{
class EventScheduler final
{
public:
  using EventId = std::uint64_t;
  using Callback = std::function<void(std::uint64_t cycles_late)>;

  EventId ScheduleAfter(std::uint64_t cycles, Callback callback);
  bool Cancel(EventId id);
  void Advance(std::uint64_t cycles);
  void Reset();

  std::uint64_t GetTicks() const;
  std::size_t GetPendingEventCount() const;

private:
  struct Event
  {
    std::uint64_t due = 0;
    EventId id = 0;
    Callback callback;
  };

  std::vector<Event> m_events;
  std::uint64_t m_ticks = 0;
  EventId m_next_id = 1;
};
}
