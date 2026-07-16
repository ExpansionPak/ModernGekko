#include "moderngekko/game.hpp"
#include "moderngekko/runtime.hpp"
#include "frontend_config.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
#ifndef MODERNGEKKO_RUNNER_NAME
#define MODERNGEKKO_RUNNER_NAME "moderngekko-run"
#endif

#ifndef MODERNGEKKO_USER_DIRECTORY_NAME
#define MODERNGEKKO_USER_DIRECTORY_NAME "moderngekko"
#endif

volatile std::sig_atomic_t s_stop_requested = 0;

void HandleStopSignal(int)
{
  s_stop_requested = 1;
}

void Usage()
{
  std::cerr << "usage: " MODERNGEKKO_RUNNER_NAME
               " [--game <extracted-root>] [--module <path>]\n"
               "       [--user-dir <path>] [--title <text>]\n"
               "       [--graphics <backend>] [--audio <backend>]\n"
               "       [--wayland] [-X11] [--headless] [--allow-interpreter]\n"
               "       With no --game, boots the path in <user-dir>/default-game.txt.\n";
}

std::filesystem::path ReadDefaultGame(const std::filesystem::path& user_directory)
{
  std::ifstream file(user_directory / "default-game.txt");
  std::string path;
  std::getline(file, path);
  if (!path.empty() && path.back() == '\r')
    path.pop_back();
  return path;
}

std::filesystem::path DefaultUserDirectory()
{
  if (const char* xdg = std::getenv("XDG_DATA_HOME"))
    return std::filesystem::path(xdg) / MODERNGEKKO_USER_DIRECTORY_NAME;
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home) / ".local" / "share" /
           MODERNGEKKO_USER_DIRECTORY_NAME;
  return std::string(MODERNGEKKO_USER_DIRECTORY_NAME) + "-user";
}

std::string LibrarySuffix()
{
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::filesystem::path ExecutableDirectory(const char* argv0)
{
  std::error_code ec;
#if defined(__linux__)
  const std::filesystem::path proc_executable =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec)
    return proc_executable.parent_path();
  ec.clear();
#endif
  const std::filesystem::path executable = std::filesystem::weakly_canonical(argv0, ec);
  return ec ? std::filesystem::current_path() : executable.parent_path();
}
}  // namespace

int main(int argc, char** argv)
{
  moderngekko::RuntimeConfig config;
  config.user_directory = DefaultUserDirectory();
#ifdef MODERNGEKKO_DEFAULT_WINDOW_TITLE
  config.window_title = MODERNGEKKO_DEFAULT_WINDOW_TITLE;
#endif
  std::filesystem::path module_path;
  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    const auto value = [&](const char* option) -> const char* {
      if (i + 1 >= argc)
      {
        std::cerr << option << " requires a value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--game")
      config.game_root = value("--game");
    else if (arg == "--module")
      module_path = value("--module");
    else if (arg == "--user-dir")
      config.user_directory = value("--user-dir");
    else if (arg == "--title")
      config.window_title = value("--title");
    else if (arg == "--graphics")
      config.graphics.backend = value("--graphics");
    else if (arg == "--audio")
      config.audio.backend = value("--audio");
    else if (arg == "-X11" || arg == "--x11")
      config.window_system = moderngekko::WindowSystem::X11;
    else if (arg == "--wayland")
      config.window_system = moderngekko::WindowSystem::Wayland;
    else if (arg == "--headless")
      config.headless = true;
    else if (arg == "--allow-interpreter")
      config.allow_interpreter = true;
    else if (arg == "--help" || arg == "-h")
    {
      Usage();
      return 0;
    }
    else
    {
      std::cerr << "unknown option: " << arg << '\n';
      Usage();
      return 2;
    }
  }
  if (config.game_root.empty())
    config.game_root = ReadDefaultGame(config.user_directory);
  if (config.game_root.empty())
  {
    std::cerr << "no game configured; use --game once or create "
              << (config.user_directory / "default-game.txt") << '\n';
    Usage();
    return 2;
  }

  const auto frontend_config =
      moderngekko::frontend::LoadConfig(config.user_directory, true);
  if (!frontend_config)
  {
    std::cerr << "invalid config.ini: " << frontend_config.error << '\n';
    return 2;
  }
  config.graphics.internal_resolution_scale = frontend_config.dolphin_scale;
  config.show_fps_in_title = frontend_config.show_fps_in_title;

  if (!frontend_config.controller.empty())
  {
    std::string controller_message;
    if (!moderngekko::frontend::GenerateControllerConfig(
            config.user_directory, frontend_config.controller, &controller_message))
    {
      std::cerr << "controller configuration: " << controller_message << '\n';
      return 2;
    }
    std::cout << "controller configuration: " << controller_message << '\n';
  }

  const auto inspected = moderngekko::InspectGame(config.game_root);
  if (!inspected)
  {
    std::cerr << "invalid game: " << inspected.error << '\n';
    return 2;
  }

#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (inspected.metadata->disc_id != MODERNGEKKO_REQUIRED_DISC_ID)
  {
    std::cerr << "unsupported disc ID: expected " << MODERNGEKKO_REQUIRED_DISC_ID << ", got "
              << inspected.metadata->disc_id << '\n';
    return 2;
  }
#endif

  // Compatibility discovery belongs to the runner, never the runtime library.
  if (module_path.empty())
  {
    if (const char* env = std::getenv("STATICRECOMP_MODULE"))
      module_path = env;
    else
    {
      const std::string module_name =
          "g" + inspected.metadata->disc_id + "_recomp" + LibrarySuffix();
      const auto bundled = ExecutableDirectory(argv[0]) / module_name;
      const auto user_module = config.user_directory / "StaticRecompModules" / module_name;
      if (std::filesystem::is_regular_file(bundled))
        module_path = bundled;
      else if (std::filesystem::is_regular_file(user_module))
        module_path = user_module;
    }
  }
  if (!module_path.empty())
    config.module = moderngekko::ModuleSource::DynamicPath(std::move(module_path));

#if defined(__linux__)
  if (!config.headless && config.graphics.backend.empty())
    config.graphics.backend = "Vulkan";
#endif

  auto created = moderngekko::Runtime::Create(std::move(config));
  if (!created)
  {
    std::cerr << "initialization failed: " << created.error->message << '\n';
    return 1;
  }
  std::cout << "audio backend: " << created.runtime->GetConfig().audio.backend << '\n';

  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);
  std::jthread signal_watcher([&](std::stop_token stop_token) {
    while (!stop_token.stop_requested())
    {
      if (s_stop_requested)
      {
        s_stop_requested = 0;
        created.runtime->RequestStop();
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  const moderngekko::RuntimeRunResult result = created.runtime->Run();
  signal_watcher.request_stop();
  if (result.error)
  {
    std::cerr << "runtime failed: " << result.error->message << '\n';
    return 1;
  }
  return 0;
}
