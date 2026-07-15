#include "moderngekko/game.hpp"
#include "moderngekko/module_abi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr std::string_view RECOMPCORE_REVISION = "6ed835397d984f2ac8cccb89589ef592add68d71";
constexpr std::string_view DOLRECOMP_REVISION =
    "a2b02e5a515fc8971cc551ad51c9e26a9815daad-dispatch-port";

struct BuildOptions
{
  std::string toolchain = "auto";
  fs::path output;
  std::vector<std::string> runner_arguments;
};

fs::path DefaultOutput()
{
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
    return fs::path(xdg) / "moderngekko" / "modules";
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / ".cache" / "moderngekko" / "modules";
  return "moderngekko-modules";
}

std::string Suffix()
{
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::string Quote(const fs::path& value)
{
#if defined(_WIN32)
  std::string text = value.string();
  return '"' + text + '"';
#else
  std::string text = value.string();
  std::string result = "'";
  for (char c : text)
    result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
#endif
}

std::uint64_t Fnv1a(std::string_view value)
{
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (unsigned char c : value)
    hash = (hash ^ c) * 0x100000001b3ULL;
  return hash;
}

std::string ReadCommand(const std::string& command)
{
#if defined(_WIN32)
  FILE* pipe = _popen(command.c_str(), "r");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (!pipe)
    return {};
  std::string output;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), pipe))
    output += buffer;
#if defined(_WIN32)
  _pclose(pipe);
#else
  pclose(pipe);
#endif
  return output;
}

bool RunCommand(const std::string& command)
{
  std::cout << "+ " << command << '\n';
  return std::system(command.c_str()) == 0;
}

fs::path SiblingExecutable(const char* argv0, std::string name)
{
  std::error_code ec;
  fs::path self = fs::weakly_canonical(argv0, ec);
#if defined(_WIN32)
  name += ".exe";
#endif
  const fs::path sibling = self.parent_path() / name;
  return fs::is_regular_file(sibling) ? sibling : fs::path(std::move(name));
}

std::string PlatformName(moderngekko::GamePlatform platform)
{
  return platform == moderngekko::GamePlatform::Wii ? "Wii (Broadway)" : "GameCube (Gekko)";
}

std::string ActiveModule(const fs::path& output, std::string_view id)
{
  std::ifstream file(output / id / "active-module.txt");
  std::string value;
  std::getline(file, value);
  return value;
}

std::string CachedModuleStatus(const fs::path& output,
                               const moderngekko::GameMetadata& game)
{
  const std::string active = ActiveModule(output, game.disc_id);
  if (active.empty())
    return "none";

  const fs::path module = active;
  if (!fs::is_regular_file(module))
    return "missing: " + module.string();

  std::ifstream manifest(module.parent_path() / "manifest.txt");
  std::string line;
  while (std::getline(manifest, line))
  {
    constexpr std::string_view prefix = "dol_sha256=";
    if (line.starts_with(prefix))
    {
      const bool current = line.substr(prefix.size()) == game.dol_sha256;
      return std::string(current ? "current: " : "stale: ") + module.string();
    }
  }
  return "unverified: " + module.string();
}

int Inspect(const fs::path& root, const fs::path& output)
{
  const auto result = moderngekko::InspectGame(root);
  if (!result)
  {
    std::cerr << "invalid extracted game: " << result.error << '\n';
    return 1;
  }
  const auto& game = *result.metadata;
  std::cout << "Game name: " << game.game_name << '\n'
            << "Disc ID:   " << game.disc_id << '\n'
            << "Platform:  " << PlatformName(game.platform) << '\n'
            << "Entry:     0x" << std::hex << std::setw(8) << std::setfill('0')
            << game.entry_point << std::dec << '\n'
            << "DOL SHA-256: " << game.dol_sha256 << '\n'
            << "Cached module: " << CachedModuleStatus(output, game) << '\n';
  return 0;
}

std::optional<fs::path> Build(const char* argv0, const fs::path& root,
                              BuildOptions options)
{
  const auto inspected = moderngekko::InspectGame(root);
  if (!inspected)
  {
    std::cerr << "invalid extracted game: " << inspected.error << '\n';
    return std::nullopt;
  }
  const auto& game = *inspected.metadata;
  if (options.output.empty())
    options.output = DefaultOutput();

  std::string compiler;
  if (options.toolchain == "auto")
#if defined(_WIN32)
    compiler = "cl";
#else
    compiler = ReadCommand("clang --version 2>&1").empty() ? "gcc" : "clang";
#endif
  else if (options.toolchain == "clang")
    compiler = "clang";
  else if (options.toolchain == "gcc")
    compiler = "gcc";
  else if (options.toolchain == "msvc")
  {
#if defined(_WIN32)
    compiler = "cl";
#else
    std::cerr << "MSVC modules can only be built on Windows\n";
    return std::nullopt;
#endif
  }
  else
  {
    std::cerr << "unknown toolchain: " << options.toolchain << '\n';
    return std::nullopt;
  }

  const std::string compiler_identity = ReadCommand(compiler + " --version 2>&1");
  if (compiler_identity.empty())
  {
    std::cerr << "compiler is unavailable: " << compiler << '\n';
    return std::nullopt;
  }
#if defined(__x86_64__) || defined(_M_X64)
  constexpr std::string_view architecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  constexpr std::string_view architecture = "aarch64";
#else
  constexpr std::string_view architecture = "unsupported";
#endif
  std::string flags;
  if (compiler == "clang")
  {
    flags = "compile:-O2 -flto=thin -fvisibility=hidden -ffp-contract=off -fno-fast-math "
            "link:-flto=thin";
#if defined(__linux__)
    flags += " -fuse-ld=lld";
#endif
  }
  else if (compiler == "gcc")
  {
    flags = "compile:-O2 -fvisibility=hidden -ffp-contract=off -fno-fast-math link:no-lto";
  }
  else
  {
    flags = "compile:/O2 /fp:strict";
  }
  const std::string identity = std::string(RECOMPCORE_REVISION) + "|dolrecomp=" +
      std::string(DOLRECOMP_REVISION) + "|module-abi=" +
      std::to_string(MODERNGEKKO_MODULE_ABI_VERSION) + "|cpu-abi=" +
      std::to_string(MODERNGEKKO_CPU_ABI_VERSION) + "|" + compiler_identity + "|" +
      std::string(architecture) + "|" + flags;
  std::ostringstream key_tail;
  key_tail << std::hex << std::setfill('0') << std::setw(16) << Fnv1a(identity);
  const std::string cache_key = game.dol_sha256 + "-" + key_tail.str();
  const fs::path artifact = options.output / game.disc_id / cache_key;
  const fs::path module = artifact / ("g" + game.disc_id + "_recomp" + Suffix());
  const fs::path module_build = artifact / "module-build";
  const fs::path built = module_build / ("g" + game.disc_id + "_recomp" + Suffix());
  if (fs::is_regular_file(module))
  {
    fs::create_directories(options.output / game.disc_id);
    std::ofstream active(options.output / game.disc_id / "active-module.txt");
    active << module.string() << '\n';
    std::cout << "cache hit: " << module << '\n';
    return module;
  }

  const auto publish_module = [&]() -> std::optional<fs::path> {
    fs::create_directories(artifact);
    fs::copy_file(built, module, fs::copy_options::overwrite_existing);
    std::ofstream manifest(artifact / "manifest.txt");
    manifest << "disc_id=" << game.disc_id << '\n' << "dol_sha256=" << game.dol_sha256 << '\n'
             << "recompcore_revision=" << RECOMPCORE_REVISION << '\n'
             << "dolrecomp_revision=" << DOLRECOMP_REVISION << '\n'
             << "module_abi=" << MODERNGEKKO_MODULE_ABI_VERSION << '\n'
             << "cpu_abi=" << MODERNGEKKO_CPU_ABI_VERSION << '\n'
             << "compiler=" << compiler_identity << '\n'
             << "architecture=" << architecture << '\n'
             << "flags=" << flags << '\n';
    fs::create_directories(options.output / game.disc_id);
    std::ofstream active(options.output / game.disc_id / "active-module.txt");
    active << module.string() << '\n';
    std::cout << "built module: " << module << '\n';
    return module;
  };
  if (fs::is_regular_file(built))
    return publish_module();

  fs::create_directories(artifact);
  const fs::path generated_parent = artifact / "dolrecomp-output";
  const fs::path dolrecomp = SiblingExecutable(argv0, "dolrecomp");
  std::string generate = Quote(dolrecomp) + " -j" +
                         std::to_string(std::max(1u, std::thread::hardware_concurrency())) + " ";
  if (game.platform == moderngekko::GamePlatform::GameCube)
    generate += "--cpu gekko --gamecube " + Quote(game.main_dol) + " " + Quote(generated_parent);
  else
    generate += "--cpu broadway " + Quote(game.main_dol) + " " + game.disc_id + " " +
                Quote(generated_parent);
  if (!RunCommand(generate))
    return std::nullopt;

  fs::path generated = game.platform == moderngekko::GamePlatform::Wii ?
      generated_parent / (game.disc_id + "_generated") : generated_parent / "generated";
  std::string generated_stem =
      game.platform == moderngekko::GamePlatform::Wii ? game.disc_id : "generated";
  // DolRecomp's optional title database affects output naming only. An
  // explicit --cpu broadway keeps Wii semantics even when that database is absent.
  if (!fs::is_regular_file(generated / (generated_stem + ".h")) &&
      fs::is_regular_file(generated_parent / "generated" / "generated.h"))
  {
    generated = generated_parent / "generated";
    generated_stem = "generated";
  }
  const fs::path emitted_header = generated / (generated_stem + ".h");
  if (!fs::is_regular_file(emitted_header))
  {
    std::cerr << "DolRecomp did not produce " << emitted_header << '\n';
    return std::nullopt;
  }
  if (emitted_header.filename() != "generated.h")
    fs::copy_file(emitted_header, generated / "generated.h", fs::copy_options::overwrite_existing);
  fs::copy_file(game.main_dol, generated / "main.dol", fs::copy_options::overwrite_existing);
  const fs::path emitted_smc = generated / (generated_stem + "_smc.txt");
  const fs::path normalized_smc = generated / "generated_smc.txt";
  if (fs::is_regular_file(emitted_smc))
  {
    if (emitted_smc != normalized_smc)
      fs::copy_file(emitted_smc, normalized_smc, fs::copy_options::overwrite_existing);
  }
  else
    std::ofstream{normalized_smc};

  const fs::path source_root = fs::path(MODERNGEKKO_SOURCE_DIR);
  const unsigned compile_jobs =
      std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
  std::string configure = "cmake -E env CMAKE_NINJA_FORCE_RESPONSE_FILE=1 cmake -S " +
      Quote(source_root / "vendor/dolphin/module-template") +
      " -B " + Quote(module_build) + " -G Ninja -DCMAKE_BUILD_TYPE=Release" +
      " -DCMAKE_C_COMPILER=" + compiler + " -DGAME_ID=" + game.disc_id +
      " -DGENERATED_DIR=" + Quote(generated) +
      " -DGXRUNTIME_DIR=" + Quote(source_root / "vendor/dolphin/GXRuntime") +
      " -DCHASSIS_ABI_DIR=" +
      Quote(source_root / "vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp");
  if (!RunCommand(configure) ||
      !RunCommand("cmake --build " + Quote(module_build) + " -j" +
                  std::to_string(compile_jobs)))
    return std::nullopt;

  if (!fs::is_regular_file(built))
  {
    std::cerr << "module build completed but did not produce " << built << '\n';
    return std::nullopt;
  }
  return publish_module();
}

void Usage()
{
  std::cerr << "usage: moderngekko-port inspect <game-root>\n"
               "       moderngekko-port build <game-root> [--toolchain auto|clang|gcc|msvc] [--output path]\n"
               "       moderngekko-port run <game-root> [build options] [-- runner options]\n";
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    Usage();
    return 2;
  }
  const std::string command = argv[1];
  const fs::path root = argv[2];
  BuildOptions options;
  bool runner_args = false;
  for (int i = 3; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (runner_args)
      options.runner_arguments.push_back(arg);
    else if (arg == "--")
      runner_args = true;
    else if (arg == "--toolchain" && i + 1 < argc)
      options.toolchain = argv[++i];
    else if (arg == "--output" && i + 1 < argc)
      options.output = argv[++i];
    else if (command == "run")
      options.runner_arguments.push_back(arg);
    else
    {
      std::cerr << "unknown or incomplete option: " << arg << '\n';
      return 2;
    }
  }
  if (options.output.empty())
    options.output = DefaultOutput();
  if (command == "inspect")
    return Inspect(root, options.output);
  if (command != "build" && command != "run")
  {
    Usage();
    return 2;
  }
  const auto module = Build(argv[0], root, options);
  if (!module)
    return 1;
  if (command == "build")
    return 0;
  std::string run = Quote(SiblingExecutable(argv[0], "moderngekko-run")) + " --game " +
                    Quote(root) + " --module " + Quote(*module);
  for (const std::string& arg : options.runner_arguments)
    run += " " + Quote(arg);
  return std::system(run.c_str()) == 0 ? 0 : 1;
}
