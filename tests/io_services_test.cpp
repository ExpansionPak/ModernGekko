#include "moderngekko/controller.hpp"
#include "moderngekko/disc_interface.hpp"

#include <algorithm>
#include <array>

namespace
{
class TestDisc final : public moderngekko::DiscSource
{
public:
  TestDisc()
  {
    for (std::size_t i = 0; i < m_data.size(); ++i)
      m_data[i] = static_cast<std::uint8_t>(i);
  }

  std::uint64_t GetSize() const override { return m_data.size(); }
  bool Read(std::uint64_t offset, std::span<std::uint8_t> output) override
  {
    std::ranges::copy(std::span{m_data}.subspan(static_cast<std::size_t>(offset), output.size()),
                      output.begin());
    return true;
  }

private:
  std::array<std::uint8_t, 0x1000> m_data{};
};
}

int main()
{
  moderngekko::ControllerPort controller;
  controller.SetProvider([] {
    moderngekko::ControllerState state;
    state.connected = true;
    state.buttons = 0x0100u;
    state.stick_x = 129u;
    state.stick_y = 127u;
    state.substick_x = 130u;
    state.substick_y = 126u;
    state.trigger_left = 3u;
    state.trigger_right = 4u;
    return state;
  });
  std::uint32_t high = 0;
  std::uint32_t low = 0;
  if (!controller.Read(&high, &low) || high != 0x0180817Fu || low != 0x827E0304u)
    return 1;

  moderngekko::AddressSpace memory;
  CPUState cpu{};
  moderngekko::EventScheduler scheduler;
  moderngekko::ProcessorInterface processor_interface(cpu);
  moderngekko::MmioBus bus;
  processor_interface.RegisterMmio(bus);
  moderngekko::DiscInterface disc(memory, scheduler, processor_interface);
  TestDisc source;
  disc.SetSource(&source);
  disc.RegisterMmio(bus, false);

  bus.Write(moderngekko::ProcessorInterface::MmioBase + 4u,
            moderngekko::ProcessorInterface::DiscInterface, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase, 1u << 3, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase + 8u, 0xA8000000u, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase + 12u, 0x100u, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase + 20u, 0x00000200u, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase + 24u, 0x20u, 4u);
  bus.Write(moderngekko::DiscInterface::GameCubeMmioBase + 28u, 1u, 4u);
  scheduler.Advance(1u);

  std::uint8_t first = 0;
  std::uint8_t last = 0;
  if (!memory.Read8(0x200u, &first) || !memory.Read8(0x21Fu, &last) ||
      first != 0u || last != 31u ||
      (cpu.exception & moderngekko::ProcessorInterface::ExternalInterrupt) == 0u)
  {
    return 2;
  }

  return 0;
}
