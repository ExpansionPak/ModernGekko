#pragma once

#include "moderngekko/address_space.hpp"
#include "moderngekko/audio_system.hpp"
#include "moderngekko/cpu_state.h"
#include "moderngekko/controller.hpp"
#include "moderngekko/disc_interface.hpp"
#include "moderngekko/event_scheduler.hpp"
#include "moderngekko/gx_command_processor.hpp"
#include "moderngekko/gx_state_backend.hpp"
#include "moderngekko/mmio_bus.hpp"
#include "moderngekko/module_loader.hpp"
#include "moderngekko/processor_interface.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace moderngekko
{
enum class LegacyRunStatus
{
  Completed = 0,
  DispatchLimitReached,
  NoModule,
  UnsupportedInstruction,
  MemoryFault,
};

struct LegacyRunResult
{
  LegacyRunStatus status = LegacyRunStatus::NoModule;
  std::uint64_t dispatches = 0;
  std::uint64_t fallback_instructions = 0;
  std::uint64_t cycles = 0;
  std::uint32_t program_counter = 0;
};

enum class LegacyDolLoadStatus
{
  Ok = 0,
  InvalidImage,
  AddressOutOfRange,
};

struct LegacyDolLoadResult
{
  LegacyDolLoadStatus status = LegacyDolLoadStatus::InvalidImage;
  std::uint32_t entry_point = 0;
  bool is_wii = false;
};

class LegacyRuntime final
{
public:
  explicit LegacyRuntime(bool enable_mem2 = false);

  ModuleLoadResult LoadModule(const std::string& path, const std::string& game_id);
  ModuleLoadResult AttachModule(const ModernGekkoModuleDesc* descriptor,
                                const std::string& game_id);
  LegacyDolLoadResult LoadDol(std::span<const std::uint8_t> image);
  void UnloadModule();
  void Reset();

  LegacyRunResult Run(std::uint64_t dispatch_limit);

  CPUState& GetCpuState();
  const CPUState& GetCpuState() const;
  AddressSpace& GetAddressSpace();
  const AddressSpace& GetAddressSpace() const;
  MmioBus& GetMmioBus();
  EventScheduler& GetEventScheduler();
  ProcessorInterface& GetProcessorInterface();
  DiscInterface& GetDiscInterface();
  AudioSystem& GetAudioSystem();
  GxCommandProcessor& GetGxCommandProcessor();
  void SetGpuBackend(GpuBackend* backend);
  void SetRenderDevice(GxRenderDevice* device);
  void PresentXfb(const GxXfbPresent& present);
  ControllerPort& GetControllerPort(std::size_t index);

private:
  static std::uint64_t ReadExternal(CPUState* cpu, std::uint32_t address, std::uint8_t size);
  static void WriteExternal(CPUState* cpu, std::uint32_t address, std::uint64_t value,
                            std::uint8_t size);
  static void* ResolveExternalPointer(CPUState* cpu, std::uint32_t address,
                                      std::uint32_t size);

  AddressSpace m_address_space;
  ModuleLibrary m_module;
  CPUState m_cpu{};
  MmioBus m_mmio_bus;
  EventScheduler m_event_scheduler;
  ProcessorInterface m_processor_interface;
  DiscInterface m_disc_interface;
  AudioSystem m_audio_system;
  GxStateBackend m_gx_state_backend;
  GxCommandProcessor m_gx_command_processor;
  std::array<ControllerPort, 4> m_controller_ports;
  bool m_is_wii = false;
};
}
