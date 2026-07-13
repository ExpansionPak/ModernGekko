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
volatile std::sig_atomic_t s_stop_requested = 0;

void HandleStopSignal(int)
{
  s_stop_requested = 1;
}

void Usage()
{
  std::cerr << "usage: moderngekko-run [--game <extracted-root>] [--module <path>]\n"
               "                       [--user-dir <path>] [--title <text>]\n"
               "                       [--graphics <backend>] [--audio <backend>]\n"
               "                       [-X11] [--headless] [--allow-interpreter]\n"
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
    return std::filesystem::path(xdg) / "moderngekko";
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home) / ".local" / "share" / "moderngekko";
  return "moderngekko-user";
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
}  // namespace

int main(int argc, char** argv)
{
  moderngekko::RuntimeConfig config;
  config.user_directory = DefaultUserDirectory();
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

  std::string controller_message;
  if (!moderngekko::frontend::ImportDolphinController(config.user_directory,
                                                       &controller_message))
  {
    std::cerr << "controller configuration: " << controller_message << '\n';
  }
  else
  {
    std::cout << "controller configuration: " << controller_message << '\n';
  }

  const auto inspected = moderngekko::InspectGame(config.game_root);
  if (!inspected)
  {
    std::cerr << "invalid game: " << inspected.error << '\n';
    return 2;
  }

  // Compatibility discovery belongs to the runner, never the runtime library.
  if (module_path.empty())
  {
    if (const char* env = std::getenv("STATICRECOMP_MODULE"))
      module_path = env;
    else
    {
      const auto candidate = config.user_directory / "StaticRecompModules" /
                             ("g" + inspected.metadata->disc_id + "_recomp" + LibrarySuffix());
      if (std::filesystem::is_regular_file(candidate))
        module_path = candidate;
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
