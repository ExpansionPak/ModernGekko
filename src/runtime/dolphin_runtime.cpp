#include "moderngekko/runtime.hpp"

#include "AudioCommon/AudioCommon.h"
#include "Common/Config/Config.h"
#include "Common/HookableEvent.h"
#include "Common/StringUtil.h"
#include "Core/Boot/Boot.h"
#include "Core/Boot/BootManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/HW/GBACore.h"
#include "Core/Host.h"
#include "Core/NetPlay/NetPlayClient.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h"
#include "Core/System.h"
#include "DolphinNoGUI/Platform.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/VideoEvents.h"
#include "VideoCommon/FrameDumper.h"
#include "Core/HW/Memmap.h"
#include "Core/State.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"
#include "automation_protocol.hpp"

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <stop_token>
#include "VideoCommon/VideoConfig.h"
#include "dolphin_runtime_internal.hpp"
#include "moderngekko/cpu_state.h"
#include "moderngekko/mod_loader.hpp"
#include "moderngekko/module_loader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fmt/format.h>
#include <mutex>
#include <thread>
#include <utility>

namespace {
static_assert(sizeof(ModernGekkoModuleDesc) == sizeof(StaticRecompModuleDesc));
static_assert(offsetof(ModernGekkoModuleDesc, chunk_hashes) ==
              offsetof(StaticRecompModuleDesc, chunk_hashes));
std::mutex s_runtime_mutex;
bool s_runtime_active = false;
Platform *s_platform = nullptr;
std::string s_window_title;
bool s_show_fps_in_title = true;
bool s_external_ui_common = false;
std::unique_ptr<BootSessionData> s_boot_session_data;
u64 s_previous_net_wait_ns = 0;
double s_net_wait_ms_per_second = 0.0;
std::chrono::steady_clock::time_point s_previous_net_wait_sample;

std::string FormatWindowTitle(const std::string &title, double fps) {
  if (!std::isfinite(fps) || fps < 0.0)
    fps = 0.0;
  const auto now = std::chrono::steady_clock::now();
  std::string formatted_title = fmt::format("{} | {:.1f} FPS", title, fps);
  const NetPlay::InputWaitTelemetry telemetry =
      NetPlay::NetPlayClient::GetInputWaitTelemetry();
  if (!telemetry.active) {
    s_previous_net_wait_ns = 0;
    s_net_wait_ms_per_second = 0.0;
    s_previous_net_wait_sample = {};
    return formatted_title;
  }
  if (s_previous_net_wait_sample.time_since_epoch().count() == 0) {
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
  } else if (telemetry.total_wait_ns < s_previous_net_wait_ns) {
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
    s_net_wait_ms_per_second = 0.0;
  } else if (now - s_previous_net_wait_sample >=
             std::chrono::milliseconds(500)) {
    const double seconds =
        std::chrono::duration<double>(now - s_previous_net_wait_sample).count();
    s_net_wait_ms_per_second =
        static_cast<double>(telemetry.total_wait_ns - s_previous_net_wait_ns) /
        1000000.0 / seconds;
    s_previous_net_wait_sample = now;
    s_previous_net_wait_ns = telemetry.total_wait_ns;
  }
  return fmt::format("{} | Net wait {:.1f} ms/s | Buffer {}", formatted_title,
                     s_net_wait_ms_per_second, telemetry.buffer_size);
}
} // namespace

std::vector<std::string> Host_GetPreferredLocales() { return {}; }
void Host_PPCSymbolsChanged() {}
void Host_PPCBreakpointsChanged() {}
bool Host_UIBlocksControllerState() { return false; }
void Host_Message(HostMessageID id) {
  if (id == HostMessageID::WMUserStop && s_platform)
    s_platform->Stop();
}
void Host_UpdateTitle(const std::string &) {
  if (!s_platform)
    return;

  std::string title = s_window_title;
  if (s_show_fps_in_title &&
      s_platform->GetWindowSystemInfo().type != WindowSystemType::Headless)
    title = FormatWindowTitle(
        title, Core::System::GetInstance().GetPerfMetrics().GetFPS());
  s_platform->SetTitle(title);
}
void Host_UpdateDisasmDialog() {}
void Host_JitCacheInvalidation() {}
void Host_JitProfileDataWiped() {}
void Host_RequestRenderWindowSize(int, int) {}
bool Host_RendererHasFocus() {
  return !s_platform || s_platform->IsWindowFocused();
}
bool Host_RendererHasFullFocus() { return Host_RendererHasFocus(); }
bool Host_RendererIsFullscreen() {
  return s_platform && s_platform->IsWindowFullscreen();
}
bool Host_TASInputHasFocus() { return false; }
void Host_YieldToUI() {}
void Host_TitleChanged() {}
void Host_UpdateDiscordClientID(const std::string &) {}
bool Host_UpdateDiscordPresenceRaw(const std::string &, const std::string &,
                                   const std::string &, const std::string &,
                                   const std::string &, const std::string &,
                                   std::int64_t, std::int64_t, int, int) {
  return false;
}
std::unique_ptr<GBAHostInterface>
Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core>) {
  return nullptr;
}

namespace moderngekko {
// ---- Automation protocol -----------------------------------------------
// Ported from the benchmark instrumentation that lived only in the Skyward
// Sword ModernGekko checkout. The harness drives runs through it: commands
// are dropped as files under <automation-dir>/commands, and status.txt is
// rewritten every 200ms with fps/speed/frame counts.
namespace {
struct RuntimeAutomationState
{
  mutable std::mutex mutex;
  std::atomic<std::uint64_t> frame_count{0};
  std::atomic<std::uint64_t> present_count{0};
  std::uint64_t processed_commands = 0;
  std::string last_command;
  std::string last_error;
};

void EnsureAutomationDirectories(const std::filesystem::path& root)
{
  if (root.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(root / "commands", ec);
  std::filesystem::create_directories(root / "processed", ec);
  std::filesystem::create_directories(root / "failed", ec);
}

void SetAutomationError(RuntimeAutomationState& state, std::string message)
{
  std::lock_guard lock(state.mutex);
  state.last_error = std::move(message);
}

void MarkAutomationCommand(RuntimeAutomationState& state, std::string command_name)
{
  std::lock_guard lock(state.mutex);
  ++state.processed_commands;
  state.last_command = std::move(command_name);
  state.last_error.clear();
}

void ClearAutomationPad(int port)
{
  // Pin every control to neutral rather than clearing the override.
  // ClearControlState REMOVES the override, and Dolphin then falls back to the
  // real input device -- so the host keyboard reaches the game again. Since
  // ApplyAutomationPad() calls this first, every pad command silently reopened
  // host input mid-run, which is how a stray keypress (Enter = START = pause)
  // wrecked benchmark runs. A neutral override is identical for the guest and
  // keeps automation authoritative for as long as it is registered.
  for (int control = static_cast<int>(ciface::Touch::FIRST_GC_CONTROL);
       control <= static_cast<int>(ciface::Touch::LAST_GC_CONTROL); ++control)
  {
    ciface::Touch::SetControlState(
        port, static_cast<ciface::Touch::ControlID>(control), 0.0);
  }
}

void ApplyAutomationPad(const automation::PadState& pad)
{
  ClearAutomationPad(pad.port);
  for (std::size_t index = 0; index < pad.controls.size(); ++index)
  {
    ciface::Touch::SetControlState(
        pad.port,
        static_cast<ciface::Touch::ControlID>(
            static_cast<int>(ciface::Touch::FIRST_GC_CONTROL) +
            static_cast<int>(index)),
        pad.controls[index]);
  }
}

std::filesystem::path NormalizeScreenshotPath(const std::filesystem::path& path)
{
  if (!path.has_extension())
    return path.string() + ".png";
  return path;
}

void SaveAutomationScreenshot(const std::filesystem::path& path)
{
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const Core::CPUThreadGuard guard(Core::System::GetInstance());
  if (g_frame_dumper)
    g_frame_dumper->SaveScreenshot(path.string());
}

std::optional<RuntimeError> ReadAutomationMemory(const std::filesystem::path& path, u32 address,
                                                 u32 size)
{
  auto& system = Core::System::GetInstance();
  const Core::CPUThreadGuard guard(system);
  const u8* source = system.GetMemory().GetPointerForRange(address, size);
  if (!source)
  {
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        fmt::format("guest range {:#010x}+{} is not readable", address, size)};
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not create memory output directory: " + ec.message()};
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not open memory output file"};
  }
  output.write(reinterpret_cast<const char*>(source), size);
  if (!output)
  {
    return RuntimeError{RuntimeErrorCode::InitializationFailed,
                        "could not write memory output file"};
  }
  return {};
}

std::optional<RuntimeError> WriteAutomationMemory(u32 address,
                                                  const std::vector<std::uint8_t>& data)
{
  auto& system = Core::System::GetInstance();
  const Core::CPUThreadGuard guard(system);
  if (!system.GetMemory().GetPointerForRange(address, data.size()))
  {
    return RuntimeError{
        RuntimeErrorCode::InvalidState,
        fmt::format("guest range {:#010x}+{} is not writable", address, data.size())};
  }
  system.GetMemory().CopyToEmu(address, data.data(), data.size());
  return {};
}

std::optional<RuntimeError> ApplyAutomationCommand(Runtime& runtime,
                                                   RuntimeAutomationState& state,
                                                   const automation::Command& command,
                                                   std::stop_token stop_token)
{
  auto& system = Core::System::GetInstance();
  switch (command.type)
  {
  case automation::CommandType::Pad:
    ApplyAutomationPad(command.pad);
    break;
  case automation::CommandType::PadFrames:
  {
    if (Core::GetState(system) != Core::State::Running)
    {
      return RuntimeError{RuntimeErrorCode::InvalidState,
                          "pad_frames requires a running emulated core"};
    }
    std::uint64_t first_frame = state.frame_count.load(std::memory_order_relaxed);
    ApplyAutomationPad(command.pad);
    while (!stop_token.stop_requested())
    {
      const std::uint64_t current_frame =
          state.frame_count.load(std::memory_order_relaxed);
      if (current_frame < first_frame)
        first_frame = current_frame;
      else if (current_frame - first_frame >= command.frames)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ClearAutomationPad(command.pad.port);
    break;
  }
  case automation::CommandType::ClearPad:
    ClearAutomationPad(command.pad.port);
    break;
  case automation::CommandType::Pause:
    if (auto error = runtime.Pause())
      return error;
    break;
  case automation::CommandType::Resume:
    if (auto error = runtime.Resume())
      return error;
    break;
  case automation::CommandType::SaveState:
    std::filesystem::create_directories(command.path.parent_path());
    State::SaveAs(system, command.path.string());
    break;
  case automation::CommandType::LoadState:
    State::LoadAs(system, command.path.string());
    break;
  case automation::CommandType::Screenshot:
    SaveAutomationScreenshot(NormalizeScreenshotPath(command.path));
    break;
  case automation::CommandType::ReadMemory:
    if (auto error = ReadAutomationMemory(command.path, command.address, command.size))
      return error;
    break;
  case automation::CommandType::WriteMemory:
    if (auto error = WriteAutomationMemory(command.address, command.data))
      return error;
    break;
  case automation::CommandType::Stop:
    runtime.RequestStop();
    break;
  }
  MarkAutomationCommand(state, command.source_name);
  return {};
}

bool AutomationCommandNeedsReadyCore(automation::CommandType type)
{
  switch (type)
  {
  case automation::CommandType::Pause:
  case automation::CommandType::Resume:
  case automation::CommandType::SaveState:
  case automation::CommandType::LoadState:
  case automation::CommandType::Screenshot:
  case automation::CommandType::ReadMemory:
  case automation::CommandType::WriteMemory:
  case automation::CommandType::PadFrames:
    return true;
  case automation::CommandType::Pad:
  case automation::CommandType::ClearPad:
  case automation::CommandType::Stop:
    return false;
  }
  return true;
}

bool AutomationCoreIsReady()
{
  const auto state = Core::GetState(Core::System::GetInstance());
  return state == Core::State::Running || state == Core::State::Paused;
}

void MoveAutomationCommand(const std::filesystem::path& source, const std::filesystem::path& root,
                           const char* destination_directory)
{
  std::error_code ec;
  const std::filesystem::path destination =
      root / destination_directory / source.filename();
  std::filesystem::remove(destination, ec);
  ec.clear();
  std::filesystem::rename(source, destination, ec);
  if (ec)
  {
    ec.clear();
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec)
      std::filesystem::remove(source, ec);
  }
}

template <typename ImplT>
void WriteAutomationStatus(const std::filesystem::path& root, ImplT& impl)
{
  automation::Status status;
  const auto core_state = impl.booted ? Core::GetState(Core::System::GetInstance())
                                      : Core::State::Uninitialized;
  status.state = core_state == Core::State::Paused
                     ? "paused"
                     : (core_state == Core::State::Running ? "running" : "stopped");
  status.booted = impl.booted;
  status.title = impl.title;
  status.game_id = impl.metadata.disc_id;
  status.game_name = impl.metadata.game_name;
  if (impl.booted)
  {
    const auto& perf = Core::System::GetInstance().GetPerfMetrics();
    status.fps = perf.GetFPS();
    status.vps = perf.GetVPS();
    status.speed = perf.GetSpeed();
  }
  status.frame_count =
      impl.automation_state.frame_count.load(std::memory_order_relaxed);
  status.present_count =
      impl.automation_state.present_count.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(impl.automation_state.mutex);
    status.processed_commands = impl.automation_state.processed_commands;
    status.last_command = impl.automation_state.last_command;
    status.last_error = impl.automation_state.last_error;
  }

  const std::filesystem::path status_path = root / "status.txt";
  const std::filesystem::path temp_path = root / "status.tmp";
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  output << automation::FormatStatus(status);
  output.close();
  std::error_code ec;
  std::filesystem::rename(temp_path, status_path, ec);
  if (ec)
  {
    ec.clear();
    std::filesystem::copy_file(temp_path, status_path,
                               std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(temp_path, ec);
  }
}

template <typename ImplT>
void AutomationLoop(Runtime& runtime, ImplT& impl, std::stop_token stop_token)
{
  const std::filesystem::path root = impl.config.automation.directory;
  if (root.empty())
    return;

  EnsureAutomationDirectories(root);
  auto next_status_write = std::chrono::steady_clock::now();
  while (!stop_token.stop_requested())
  {

    for (const auto& command_path : automation::ListCommandFiles(root / "commands"))
    {
      automation::Command command;
      std::string error;
      if (!automation::ParseCommandFile(command_path, &command, &error))
      {
        SetAutomationError(impl.automation_state,
                           command_path.filename().string() + ": " + error);
        MoveAutomationCommand(command_path, root, "failed");
        continue;
      }

      if (AutomationCommandNeedsReadyCore(command.type) && !AutomationCoreIsReady())
        break;

      if (!command.path.empty())
        command.path = automation::ResolveControlPath(root, command.path);

      if (auto runtime_error =
              ApplyAutomationCommand(runtime, impl.automation_state, command, stop_token))
      {
        SetAutomationError(impl.automation_state,
                           command.source_name + ": " + runtime_error->message);
        MoveAutomationCommand(command_path, root, "failed");
        continue;
      }

      MoveAutomationCommand(command_path, root, "processed");
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_status_write)
    {
      WriteAutomationStatus(root, impl);
      next_status_write = now + std::chrono::milliseconds(200);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  WriteAutomationStatus(root, impl);
}

}  // namespace

struct Runtime::Impl {
  RuntimeConfig config;
  GameMetadata metadata;
  std::string title;
  std::unique_ptr<Platform> platform;
  std::unique_ptr<ModManager> mods;
  Common::EventHook state_hook;
  bool ui_initialized = false;
  bool controllers_initialized = false;
  bool booted = false;
  std::atomic<bool> running{false};
  RuntimeAutomationState automation_state;
  Common::EventHook present_hook;
  bool automation_registered = false;
  std::jthread automation_thread;
};

namespace detail {
void SetExternalUICommon(bool external) {
  std::lock_guard lock(s_runtime_mutex);
  s_external_ui_common = external;
}

void SetBootSessionData(std::unique_ptr<BootSessionData> boot_session_data) {
  std::lock_guard lock(s_runtime_mutex);
  s_boot_session_data = std::move(boot_session_data);
}
} // namespace detail

ModuleSource ModuleSource::DynamicPath(std::filesystem::path path) {
  ModuleSource source;
  source.kind = Kind::DynamicPath;
  source.path = std::move(path);
  return source;
}

ModuleSource
ModuleSource::AttachedDescriptor(const ModernGekkoModuleDesc *descriptor) {
  ModuleSource source;
  source.kind = Kind::AttachedDescriptor;
  source.descriptor = descriptor;
  return source;
}

Runtime::Runtime(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

RuntimeCreateResult Runtime::Create(RuntimeConfig config) {
  std::lock_guard lock(s_runtime_mutex);
  if (s_runtime_active)
    return {
        {},
        RuntimeError{RuntimeErrorCode::AlreadyActive,
                     "only one ModernGekko runtime may be active per process"}};

  GameInspectResult inspected = InspectGame(config.game_root);
  if (!inspected)
    return {{}, RuntimeError{RuntimeErrorCode::InvalidGame, inspected.error}};

  const ModernGekkoModuleRequirements requirements = {
      MODERNGEKKO_CPU_ABI_VERSION, static_cast<std::uint32_t>(sizeof(CPUState)),
      inspected.metadata->disc_id.c_str()};
  ModuleLibrary validation_library;
  ModuleLoadResult module_result{};
  if (config.module.kind == ModuleSource::Kind::DynamicPath)
    module_result =
        validation_library.Open(config.module.path.string(), requirements);
  else if (config.module.kind == ModuleSource::Kind::AttachedDescriptor)
    module_result =
        validation_library.Attach(config.module.descriptor, requirements);
  else if (!config.allow_interpreter)
    return {
        {},
        RuntimeError{
            RuntimeErrorCode::ModuleRequired,
            "no native module was supplied; use allow_interpreter explicitly"}};

  if (config.module.kind != ModuleSource::Kind::None &&
      module_result.status != ModuleLoadStatus::Ok) {
    if (!config.allow_interpreter) {
      std::string message = "native module was rejected";
      if (module_result.status == ModuleLoadStatus::DescriptorRejected)
        message += ": " + std::string(moderngekko_module_status_string(
                              module_result.validation_status));
      return {
          {},
          RuntimeError{RuntimeErrorCode::ModuleRejected, std::move(message)}};
    }
    config.module = {};
  }
  validation_library.Close();

  auto impl = std::make_unique<Impl>();
  impl->config = std::move(config);
  impl->metadata = std::move(*inspected.metadata);
  impl->title = impl->config.window_title.value_or(
      "ModernGekko - " + impl->metadata.game_name + " [" +
      impl->metadata.disc_id + "]");
  impl->mods = std::make_unique<ModManager>();
  const ModLoadReport mod_report = impl->mods->LoadDirectories(
      impl->config.mod_directories, impl->metadata.disc_id);
  for (const ModLoadIssue &issue : mod_report.issues)
    std::fprintf(stderr, "mod rejected: %s: %s\n", issue.source.c_str(),
                 issue.message.c_str());
  for (const LoadedModInfo &mod : mod_report.loaded)
    std::fprintf(stderr, "mod loaded: %s %s\n", mod.id.c_str(),
                 mod.version.c_str());

  if (!s_external_ui_common) {
    UICommon::SetUserDirectory(impl->config.user_directory.string());
    UICommon::Init();
    impl->ui_initialized = true;
  }
  Config::SetBase(Config::MAIN_FULLSCREEN, impl->config.fullscreen);

  if (impl->config.headless)
    impl->platform = Platform::CreateHeadlessPlatform();
#ifdef _WIN32
  else
    impl->platform = Platform::CreateWin32Platform();
#endif
#ifdef MODERNGEKKO_HAVE_COCOA
  else impl->platform = Platform::CreateMacOSPlatform();
#endif
#ifdef HAVE_X11
  else if (impl->config.window_system != WindowSystem::Wayland) impl->platform =
      Platform::CreateX11Platform();
#endif
#ifdef HAVE_WAYLAND
  else if (impl->config.window_system != WindowSystem::X11) impl->platform =
      Platform::CreateWaylandPlatform();
#endif
  if (!impl->platform || !impl->platform->Init()) {
    if (impl->ui_initialized)
      UICommon::Shutdown();
    return {{},
            RuntimeError{RuntimeErrorCode::PlatformUnavailable,
                         "the requested Dolphin host platform is unavailable"}};
  }

  const WindowSystemInfo wsi = impl->platform->GetWindowSystemInfo();
  UICommon::InitControllers(wsi);
  impl->controllers_initialized = true;
  // Automation drives the pad through Dolphin's input overrider, which is what
  // lets the harness hold accelerate for the length of a measured run.
  EnsureAutomationDirectories(impl->config.automation.directory);
  if (!impl->config.automation.directory.empty()) {
    for (int port = 0; port < 4; ++port)
    {
      ciface::Touch::RegisterGameCubeInputOverrider(port);
      // The overrider returns nullopt for any control automation is not
      // actively driving, and Dolphin then falls back to the REAL device --
      // so a stray keypress on the host still reaches the game. (Enter is
      // Dolphin's default mapping for START, which opens the pause menu and
      // silently wrecks a benchmark run.) Pin every control to neutral so
      // nothing falls through; automation pad commands override on top.
      for (int id = ciface::Touch::FIRST_GC_CONTROL; id <= ciface::Touch::LAST_GC_CONTROL; ++id)
        ciface::Touch::SetControlState(port, static_cast<ciface::Touch::ControlID>(id), 0.0);
      // Wii titles read the Wiimote/Nunchuk/Classic controls, which the
      // GameCube overrider does not cover -- without this a Wii run still
      // takes host input.
      ciface::Touch::RegisterWiiInputOverrider(port);
      for (int id = ciface::Touch::FIRST_WII_CONTROL; id <= ciface::Touch::LAST_WII_CONTROL; ++id)
        ciface::Touch::SetControlState(port, static_cast<ciface::Touch::ControlID>(id), 0.0);
    }
    impl->automation_registered = true;
  }
  impl->platform->SetTitle(impl->title);

  Config::SetBase(Config::MAIN_CPU_CORE, PowerPC::CPUCore::StaticRecomp);
  if (!impl->config.graphics.backend.empty())
    Config::SetBase(Config::MAIN_GFX_BACKEND, impl->config.graphics.backend);
  else if (impl->config.headless)
    Config::SetBase(Config::MAIN_GFX_BACKEND, std::string("Null"));
  if (impl->config.graphics.internal_resolution_scale)
    Config::SetBase(Config::GFX_EFB_SCALE,
                    *impl->config.graphics.internal_resolution_scale);
  Config::SetBase(Config::GFX_SHADER_CACHE, true);
  Config::SetBase(Config::GFX_SHADER_COMPILATION_MODE,
                  ShaderCompilationMode::AsynchronousUberShaders);
  Config::SetBase(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING, true);
  const std::vector<std::string> audio_backends =
      AudioCommon::GetSoundBackends();
  if (impl->config.headless) {
    impl->config.audio.backend = BACKEND_NULLSOUND;
  } else if (impl->config.audio.backend.empty() ||
             !std::ranges::contains(audio_backends,
                                    impl->config.audio.backend)) {
    constexpr std::array preferred_backends = {
        BACKEND_CUBEB, BACKEND_PULSEAUDIO, BACKEND_ALSA};
    const auto preferred =
        std::ranges::find_if(preferred_backends, [&](const char *backend) {
          return std::ranges::contains(audio_backends, backend);
        });
    impl->config.audio.backend =
        preferred != preferred_backends.end() ? *preferred : BACKEND_NULLSOUND;
  }
  Config::SetBase(Config::MAIN_AUDIO_BACKEND, impl->config.audio.backend);
  if (impl->config.audio.mute)
    Config::SetBase(Config::MAIN_AUDIO_VOLUME, 0);
  Config::SetBase(Config::MAIN_INPUT_BACKGROUND_INPUT,
                  impl->config.input.background_input);

  auto &jit = Core::System::GetInstance().GetJitInterface();
  StaticRecompModuleSource recomp_source;
  if (impl->config.module.kind == ModuleSource::Kind::DynamicPath)
    recomp_source =
        StaticRecompModuleSource::Dynamic(impl->config.module.path.string());
  else if (impl->config.module.kind == ModuleSource::Kind::AttachedDescriptor)
    recomp_source = StaticRecompModuleSource::Attached(
        reinterpret_cast<const StaticRecompModuleDesc *>(
            impl->config.module.descriptor));
  recomp_source.host_call = &ModManager::HostCall;
  recomp_source.host_call_contains = &ModManager::HostCallContains;
  recomp_source.host_call_range_contains =
      &ModManager::HostCallRangeContains;
  recomp_source.host_call_active = &ModManager::HostCallActive;
  recomp_source.host_call_generation = &ModManager::HostCallGeneration;
  recomp_source.host_call_user = impl->mods.get();
  jit.SetStaticRecompModuleSource(std::move(recomp_source));

  s_runtime_active = true;
  s_platform = impl->platform.get();
  s_window_title = impl->title;
  s_show_fps_in_title = impl->config.show_fps_in_title;
  return {std::unique_ptr<Runtime>(new Runtime(std::move(impl))), {}};
}

Runtime::~Runtime() {
  RequestStop();
  if (m_impl->booted) {
    Core::Stop(Core::System::GetInstance());
    Core::Shutdown(Core::System::GetInstance());
  }
  if (m_impl->automation_thread.joinable()) {
    m_impl->automation_thread.request_stop();
    m_impl->automation_thread.join();
  }
  m_impl->present_hook = {};
  if (m_impl->automation_registered) {
    for (int port = 0; port < 4; ++port)
    {
      ciface::Touch::UnregisterGameCubeInputOverrider(port);
      ciface::Touch::UnregisterWiiInputOverrider(port);
    }
    m_impl->automation_registered = false;
  }
  m_impl->state_hook = {};
  if (m_impl->controllers_initialized)
    UICommon::ShutdownControllers();
  if (m_impl->ui_initialized)
    UICommon::Shutdown();
  std::lock_guard lock(s_runtime_mutex);
  s_platform = nullptr;
  s_window_title.clear();
  s_show_fps_in_title = true;
  s_runtime_active = false;
}

RuntimeRunResult Runtime::Run() {
  if (m_impl->running.exchange(true))
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::InvalidState,
                         "runtime is already running"}};

  std::unique_ptr<BootParameters> boot;
  {
    std::lock_guard lock(s_runtime_mutex);
    if (s_boot_session_data)
      boot = BootParameters::GenerateFromFile(
          PathToString(m_impl->metadata.main_dol), std::move(*s_boot_session_data));
    else if (m_impl->config.load_state_path)
      // DeleteSavestateAfterBoot::No: the state is the player's, not a
      // scratch file this run owns.
      boot = BootParameters::GenerateFromFile(
          PathToString(m_impl->metadata.main_dol),
          BootSessionData(PathToString(*m_impl->config.load_state_path),
                          DeleteSavestateAfterBoot::No));
    else
      boot =
          BootParameters::GenerateFromFile(PathToString(m_impl->metadata.main_dol));
    s_boot_session_data.reset();
  }
  if (!boot) {
    m_impl->running = false;
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::BootFailed,
                         "Dolphin rejected the extracted disc"}};
  }
  m_impl->state_hook =
      Core::AddOnStateChangedCallback([this](Core::State state) {
        if (state == Core::State::Uninitialized && m_impl->platform)
          m_impl->platform->Stop();
      });
  if (!BootManager::BootCore(Core::System::GetInstance(), std::move(boot),
                             m_impl->platform->GetWindowSystemInfo())) {
    m_impl->running = false;
    return {RuntimeExitReason::BootFailed,
            RuntimeError{RuntimeErrorCode::BootFailed,
                         "Dolphin could not boot sys/main.dol"}};
  }
  m_impl->booted = true;
  // Frame/present counters the harness reads out of status.txt.
  m_impl->present_hook =
      GetVideoEvents().after_present_event.Register([this](const PresentInfo &info) {
        m_impl->automation_state.frame_count.store(info.frame_count,
                                                   std::memory_order_relaxed);
        m_impl->automation_state.present_count.store(info.present_count,
                                                     std::memory_order_relaxed);
      });
  if (!m_impl->config.automation.directory.empty()) {
    m_impl->automation_thread =
        std::jthread([this](std::stop_token stop_token) {
          AutomationLoop(*this, *m_impl, std::move(stop_token));
        });
  }
  std::jthread title_thread;
  if (!m_impl->config.headless && m_impl->config.show_fps_in_title) {
    title_thread = std::jthread([](std::stop_token stop_token) {
      while (!stop_token.stop_requested()) {
        Host_UpdateTitle({});
        for (int i = 0; i < 10 && !stop_token.stop_requested(); ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }
  m_impl->platform->MainLoop();
  title_thread.request_stop();
  if (title_thread.joinable())
    title_thread.join();
  m_impl->platform->SaveWindowGeometry();
  Core::Stop(Core::System::GetInstance());
  Core::Shutdown(Core::System::GetInstance());
  m_impl->booted = false;
  m_impl->running = false;
  return {};
}

void Runtime::RequestStop() {
  if (m_impl && m_impl->platform)
    m_impl->platform->RequestShutdown();
}

std::optional<RuntimeError> Runtime::Pause() {
  if (!m_impl->running)
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        "runtime is not running"};
  Core::SetState(Core::System::GetInstance(), Core::State::Paused);
  return {};
}

std::optional<RuntimeError> Runtime::Resume() {
  if (!m_impl->running)
    return RuntimeError{RuntimeErrorCode::InvalidState,
                        "runtime is not running"};
  Core::SetState(Core::System::GetInstance(), Core::State::Running);
  return {};
}

const RuntimeConfig &Runtime::GetConfig() const { return m_impl->config; }
const GameMetadata &Runtime::GetGameMetadata() const {
  return m_impl->metadata;
}
const std::string &Runtime::GetWindowTitle() const { return m_impl->title; }
} // namespace moderngekko
