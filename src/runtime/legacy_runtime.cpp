#include "moderngekko/legacy_runtime.hpp"

#include "Core/Boot/BootMemory.h"
#include "Core/Boot/DolReader.h"
#include "moderngekko/interpreter.hpp"

#include <limits>

namespace moderngekko
{
LegacyRuntime::LegacyRuntime(bool enable_mem2)
    : m_address_space(enable_mem2), m_processor_interface(m_cpu),
      m_disc_interface(m_address_space, m_event_scheduler, m_processor_interface),
      m_audio_system(m_address_space, m_event_scheduler, m_processor_interface),
      m_gx_state_backend(m_address_space),
      m_gx_command_processor(nullptr, &m_address_space),
      m_is_wii(enable_mem2)
{
  Reset();
}

ModuleLoadResult LegacyRuntime::LoadModule(const std::string& path, const std::string& game_id)
{
  const ModernGekkoModuleRequirements requirements = {
      MODERNGEKKO_CPU_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(CPUState)),
      game_id.c_str(),
  };
  const ModuleLoadResult result = m_module.Open(path, requirements);
  if (result.status != ModuleLoadStatus::Ok)
    return result;

  m_cpu.pc = m_module.GetDescriptor()->entry_point;
  if (m_module.GetDescriptor()->on_state_loaded != nullptr)
    m_module.GetDescriptor()->on_state_loaded(&m_cpu);
  return result;
}

ModuleLoadResult LegacyRuntime::AttachModule(const ModernGekkoModuleDesc* descriptor,
                                       const std::string& game_id)
{
  const ModernGekkoModuleRequirements requirements = {
      MODERNGEKKO_CPU_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(CPUState)),
      game_id.c_str(),
  };
  const ModuleLoadResult result = m_module.Attach(descriptor, requirements);
  if (result.status != ModuleLoadStatus::Ok)
    return result;

  m_cpu.pc = descriptor->entry_point;
  if (descriptor->on_state_loaded != nullptr)
    descriptor->on_state_loaded(&m_cpu);
  return result;
}

LegacyDolLoadResult LegacyRuntime::LoadDol(std::span<const std::uint8_t> image)
{
  DolReader reader(image);
  if (!reader.IsValid())
    return {};
  if (!reader.LoadIntoMemory(m_address_space))
    return {LegacyDolLoadStatus::AddressOutOfRange, reader.GetEntryPoint(), reader.IsWii()};

  m_cpu.pc = reader.GetEntryPoint();
  return {LegacyDolLoadStatus::Ok, reader.GetEntryPoint(), reader.IsWii()};
}

void LegacyRuntime::UnloadModule()
{
  m_module.Close();
}

void LegacyRuntime::Reset()
{
  m_address_space.Clear();
  m_cpu = {};
  m_cpu.ram = m_address_space.GetMem1().data();
  m_cpu.ram_size = static_cast<std::uint32_t>(m_address_space.GetMem1().size());
  m_cpu.exram = m_address_space.GetMem2().empty() ? nullptr : m_address_space.GetMem2().data();
  m_cpu.exram_size = static_cast<std::uint32_t>(m_address_space.GetMem2().size());
  m_cpu.external_read = ReadExternal;
  m_cpu.external_write = WriteExternal;
  m_cpu.external_pointer = ResolveExternalPointer;
  m_cpu.external_user_data = this;
  m_mmio_bus.Clear();
  m_event_scheduler.Reset();
  m_processor_interface.Reset();
  m_processor_interface.RegisterMmio(m_mmio_bus);
  m_disc_interface.Reset();
  m_disc_interface.RegisterMmio(m_mmio_bus, m_is_wii);
  m_audio_system.Reset();
  m_audio_system.RegisterMmio(m_mmio_bus, m_is_wii);
  m_gx_command_processor.Reset();
  m_gx_state_backend.Reset();
  DolphinBoot::SetupMemory(m_address_space, m_cpu, m_is_wii);
}

LegacyRunResult LegacyRuntime::Run(std::uint64_t dispatch_limit)
{
  LegacyRunResult result{};
  result.program_counter = m_cpu.pc;

  const ModernGekkoModuleDesc* descriptor = m_module.GetDescriptor();
  if (descriptor == nullptr)
    return result;

  std::uint64_t steps = 0;
  while (steps < dispatch_limit)
  {
    if (m_cpu.pc == 0u)
    {
      result.status = LegacyRunStatus::Completed;
      result.program_counter = m_cpu.pc;
      return result;
    }

    m_cpu.downcount = 0;
    if (!descriptor->dispatch(&m_cpu, m_cpu.pc))
    {
      const InterpreterResult fallback = Interpreter::Step(m_cpu, m_address_space);
      if (fallback.status != InterpreterStatus::Executed)
      {
        result.status = fallback.status == InterpreterStatus::MemoryFault ?
            LegacyRunStatus::MemoryFault : LegacyRunStatus::UnsupportedInstruction;
        result.program_counter = m_cpu.pc;
        return result;
      }

      ++result.fallback_instructions;
      result.cycles += fallback.cycles;
      m_cpu.timebase += fallback.cycles;
      m_event_scheduler.Advance(fallback.cycles);
      ++steps;
      continue;
    }

    ++result.dispatches;
    ++steps;
    if (m_cpu.downcount < 0)
    {
      const std::uint64_t charged = static_cast<std::uint64_t>(-(m_cpu.downcount + 1)) + 1u;
      if (charged > std::numeric_limits<std::uint64_t>::max() - result.cycles)
        result.cycles = std::numeric_limits<std::uint64_t>::max();
      else
        result.cycles += charged;
      m_cpu.timebase += charged;
      m_event_scheduler.Advance(charged);
    }
  }

  result.status = m_cpu.pc == 0u ? LegacyRunStatus::Completed : LegacyRunStatus::DispatchLimitReached;
  result.program_counter = m_cpu.pc;
  return result;
}

CPUState& LegacyRuntime::GetCpuState()
{
  return m_cpu;
}

const CPUState& LegacyRuntime::GetCpuState() const
{
  return m_cpu;
}

AddressSpace& LegacyRuntime::GetAddressSpace()
{
  return m_address_space;
}

const AddressSpace& LegacyRuntime::GetAddressSpace() const
{
  return m_address_space;
}

MmioBus& LegacyRuntime::GetMmioBus()
{
  return m_mmio_bus;
}

EventScheduler& LegacyRuntime::GetEventScheduler()
{
  return m_event_scheduler;
}

ProcessorInterface& LegacyRuntime::GetProcessorInterface()
{
  return m_processor_interface;
}

DiscInterface& LegacyRuntime::GetDiscInterface()
{
  return m_disc_interface;
}

AudioSystem& LegacyRuntime::GetAudioSystem()
{
  return m_audio_system;
}

GxCommandProcessor& LegacyRuntime::GetGxCommandProcessor()
{
  return m_gx_command_processor;
}

void LegacyRuntime::SetGpuBackend(GpuBackend* backend)
{
  m_gx_command_processor.SetBackend(backend);
}

void LegacyRuntime::SetRenderDevice(GxRenderDevice* device)
{
  m_gx_state_backend.SetRenderDevice(device);
  m_gx_command_processor.SetBackend(&m_gx_state_backend);
}

void LegacyRuntime::PresentXfb(const GxXfbPresent& present)
{
  m_gx_state_backend.PresentXfb(present);
}

ControllerPort& LegacyRuntime::GetControllerPort(std::size_t index)
{
  return m_controller_ports.at(index);
}

std::uint64_t LegacyRuntime::ReadExternal(CPUState* cpu, std::uint32_t address, std::uint8_t size)
{
  auto* runtime = static_cast<LegacyRuntime*>(cpu->external_user_data);
  std::uint64_t value = 0;
  if (runtime != nullptr)
    runtime->m_mmio_bus.Read(address & 0x3FFFFFFFu, size, &value);
  return value;
}

void LegacyRuntime::WriteExternal(CPUState* cpu, std::uint32_t address, std::uint64_t value,
                            std::uint8_t size)
{
  auto* runtime = static_cast<LegacyRuntime*>(cpu->external_user_data);
  if (runtime == nullptr)
    return;
  const std::uint32_t physical = address & 0x3FFFFFFFu;
  if ((physical & ~0x1Fu) == 0x0C008000u)
    runtime->m_gx_command_processor.Write(value, size);
  else
    runtime->m_mmio_bus.Write(physical, value, size);
}

void* LegacyRuntime::ResolveExternalPointer(CPUState* cpu, std::uint32_t address, std::uint32_t size)
{
  auto* runtime = static_cast<LegacyRuntime*>(cpu->external_user_data);
  return runtime == nullptr ? nullptr : runtime->m_address_space.Resolve(address, size);
}
}
