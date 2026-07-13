#include "moderngekko/event_scheduler.hpp"
#include "moderngekko/mmio_bus.hpp"
#include "moderngekko/processor_interface.hpp"

#include <cstdint>
#include <vector>

int main()
{
  moderngekko::EventScheduler scheduler;
  std::vector<std::uint32_t> order;
  const auto cancelled = scheduler.ScheduleAfter(4u, [&](std::uint64_t) { order.push_back(9u); });
  scheduler.ScheduleAfter(5u, [&](std::uint64_t late) { order.push_back(1u + late); });
  scheduler.ScheduleAfter(5u, [&](std::uint64_t) { order.push_back(2u); });
  if (!scheduler.Cancel(cancelled))
    return 1;
  scheduler.Advance(7u);
  if (order != std::vector<std::uint32_t>{3u, 2u} || scheduler.GetTicks() != 7u)
    return 2;

  moderngekko::MmioBus bus;
  std::uint64_t storage = 0;
  if (!bus.Register(0x1000u, 8u,
                    [&](std::uint32_t, std::uint8_t) { return storage; },
                    [&](std::uint32_t, std::uint64_t value, std::uint8_t) { storage = value; }) ||
      bus.Register(0x1004u, 4u, {}, {}))
  {
    return 3;
  }
  if (!bus.Write(0x1000u, 0x12345678u, 4u))
    return 4;
  std::uint64_t read = 0;
  if (!bus.Read(0x1000u, 4u, &read) || read != 0x12345678u || bus.Read(0x2000u, 4u, &read))
    return 5;

  CPUState cpu{};
  moderngekko::MmioBus interrupt_bus;
  moderngekko::ProcessorInterface processor_interface(cpu);
  processor_interface.RegisterMmio(interrupt_bus);
  processor_interface.SetInterrupt(moderngekko::ProcessorInterface::AudioInterface);
  if (!interrupt_bus.Write(moderngekko::ProcessorInterface::MmioBase + 4u,
                           moderngekko::ProcessorInterface::AudioInterface, 4u) ||
      (cpu.exception & moderngekko::ProcessorInterface::ExternalInterrupt) == 0u)
  {
    return 6;
  }
  if (!interrupt_bus.Write(moderngekko::ProcessorInterface::MmioBase,
                           moderngekko::ProcessorInterface::AudioInterface, 4u) ||
      (cpu.exception & moderngekko::ProcessorInterface::ExternalInterrupt) != 0u)
  {
    return 7;
  }

  return 0;
}
