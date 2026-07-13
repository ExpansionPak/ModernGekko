#include "moderngekko/legacy_runtime.hpp"

#include <cstdint>

int main()
{
  static_assert(sizeof(CPUState) == 3504u);

  moderngekko::LegacyRuntime runtime(true);
  const moderngekko::ModuleLoadResult loaded =
      runtime.LoadModule(MODERNGEKKO_RUNTIME_TEST_MODULE_PATH, "TEST01");
  if (loaded.status != moderngekko::ModuleLoadStatus::Ok)
    return 1;

  runtime.GetAddressSpace().Write32(0x80004000u, 0x38A00007u);
  runtime.GetAddressSpace().Write32(0x80004004u, 0x4BFFF11Cu);
  bool event_fired = false;
  runtime.GetEventScheduler().ScheduleAfter(6u, [&](std::uint64_t late) {
    event_fired = late == 1u;
  });

  const moderngekko::LegacyRunResult first = runtime.Run(4u);
  if (first.status != moderngekko::LegacyRunStatus::Completed || first.dispatches != 2u ||
      first.fallback_instructions != 2u || first.cycles != 7u || first.program_counter != 0u ||
      !event_fired || runtime.GetCpuState().timebase != 7u)
  {
    return 2;
  }

  runtime.GetCpuState().pc = 0x80005000u;
  runtime.GetAddressSpace().Write32(0x80005000u, 0u);
  const moderngekko::LegacyRunResult second = runtime.Run(1u);
  if (second.status != moderngekko::LegacyRunStatus::UnsupportedInstruction ||
      second.dispatches != 0u || second.fallback_instructions != 0u)
  {
    return 3;
  }

  const CPUState& cpu = runtime.GetCpuState();
  if (cpu.gpr[3] != 0x13579BDFu || cpu.gpr[4] != 0x12345678u || cpu.gpr[5] != 7u)
    return 4;

  cpu.external_write(const_cast<CPUState*>(&cpu), 0xCC003004u,
                     moderngekko::ProcessorInterface::AudioInterface, 4u);
  runtime.GetProcessorInterface().SetInterrupt(moderngekko::ProcessorInterface::AudioInterface);
  if ((cpu.exception & moderngekko::ProcessorInterface::ExternalInterrupt) == 0u ||
      cpu.external_read(const_cast<CPUState*>(&cpu), 0xCC003000u, 4u) == 0u)
  {
    return 5;
  }

  std::uint32_t memory_value = 0u;
  if (!runtime.GetAddressSpace().Read32(0x80000100u, &memory_value) ||
      memory_value != 0x12345678u)
  {
    return 6;
  }

  runtime.UnloadModule();
  if (runtime.Run(1u).status != moderngekko::LegacyRunStatus::NoModule)
    return 7;

  return 0;
}
