#include "moderngekko/game.hpp"
#include "moderngekko/module_abi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
constexpr std::string_view RECOMPCORE_REVISION = "6ed835397d984f2ac8cccb89589ef592add68d71";
constexpr std::string_view DOLRECOMP_REVISION =
    "native-llvm-v4";

struct BuildOptions
{
  std::string toolchain = "auto";
#if defined(MODERNGEKKO_DOLRECOMP_LLVM)
  std::string backend = "llvm";
#else
  std::string backend = "c";
#endif
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

std::string Trim(std::string value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value;
}

std::uint32_t ReadBE32(const std::uint8_t* data)
{
  return (std::uint32_t{data[0]} << 24) | (std::uint32_t{data[1]} << 16) |
         (std::uint32_t{data[2]} << 8) | data[3];
}

void WriteBE32(std::uint8_t* data, std::uint32_t value)
{
  data[0] = static_cast<std::uint8_t>(value >> 24);
  data[1] = static_cast<std::uint8_t>(value >> 16);
  data[2] = static_cast<std::uint8_t>(value >> 8);
  data[3] = static_cast<std::uint8_t>(value);
}

bool ParseHex32(std::string_view value, std::uint32_t* parsed)
{
  if (value.starts_with("0x") || value.starts_with("0X"))
    value.remove_prefix(2);
  const auto result = std::from_chars(value.data(), value.data() + value.size(), *parsed, 16);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

struct DolPatch
{
  std::uint32_t address;
  std::uint32_t value;
};

struct DolPatchSet
{
  std::vector<DolPatch> entries;
  std::string fingerprint = "none";
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

DolPatchSet LoadDefaultDolPatches(const fs::path& path)
{
  std::ifstream input(path);
  if (!input)
    return {};

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line))
    lines.push_back(Trim(std::move(line)));

  std::unordered_set<std::string> enabled;
  std::string section;
  for (const std::string& current : lines)
  {
    if (current.starts_with('[') && current.ends_with(']'))
      section = current.substr(1, current.size() - 2);
    else if (section == "OnFrame_Enabled" && current.starts_with('$'))
      enabled.insert(current.substr(1));
  }
  if (enabled.empty())
    return {};

  DolPatchSet patches;
  std::string patch_name;
  for (const std::string& current : lines)
  {
    if (current.starts_with('[') && current.ends_with(']'))
    {
      section = current.substr(1, current.size() - 2);
      patch_name.clear();
      continue;
    }
    if (section != "OnFrame")
      continue;
    if (current.starts_with('$'))
    {
      patch_name = current.substr(1);
      continue;
    }
    if (current.empty() || current.starts_with('#') || current.starts_with(';') ||
        !enabled.contains(patch_name))
      continue;

    const std::size_t first = current.find(':');
    const std::size_t second = current.find(':', first == std::string::npos ? first : first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        current.find(':', second + 1) != std::string::npos ||
        current.substr(first + 1, second - first - 1) != "dword")
    {
      patches.error = "unsupported enabled DOL patch line in " + path.string();
      return patches;
    }
    DolPatch patch{};
    if (!ParseHex32(std::string_view(current).substr(0, first), &patch.address) ||
        !ParseHex32(std::string_view(current).substr(second + 1), &patch.value))
    {
      patches.error = "malformed enabled DOL patch line in " + path.string();
      return patches;
    }
    patches.entries.push_back(patch);
  }

  std::ostringstream identity;
  identity << std::hex << std::setfill('0');
  for (const DolPatch& patch : patches.entries)
    identity << std::setw(8) << patch.address << std::setw(8) << patch.value;
  std::ostringstream fingerprint;
  fingerprint << std::hex << std::setfill('0') << std::setw(16) << Fnv1a(identity.str());
  patches.fingerprint = fingerprint.str();
  return patches;
}

bool PatchDol(const fs::path& input_path, const fs::path& output_path,
              const DolPatchSet& patches, std::string* error)
{
  std::ifstream input(input_path, std::ios::binary | std::ios::ate);
  if (!input)
  {
    *error = "can't open " + input_path.string();
    return false;
  }
  const std::streamoff input_size = input.tellg();
  if (input_size < 0x100)
  {
    *error = "malformed DOL " + input_path.string();
    return false;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input_size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), input_size))
  {
    *error = "can't read " + input_path.string();
    return false;
  }

  for (const DolPatch& patch : patches.entries)
  {
    bool applied = false;
    for (std::size_t section_index = 0; section_index < 18; ++section_index)
    {
      const std::uint32_t offset = ReadBE32(bytes.data() + section_index * 4);
      const std::uint32_t address = ReadBE32(bytes.data() + 0x48 + section_index * 4);
      const std::uint32_t size = ReadBE32(bytes.data() + 0x90 + section_index * 4);
      if (patch.address < address ||
          static_cast<std::uint64_t>(patch.address) + 4 >
              static_cast<std::uint64_t>(address) + size)
        continue;
      const std::uint64_t patch_offset =
          static_cast<std::uint64_t>(offset) + patch.address - address;
      if (patch_offset + 4 > bytes.size())
      {
        *error = "DOL patch points outside the file";
        return false;
      }
      WriteBE32(bytes.data() + patch_offset, patch.value);
      applied = true;
      break;
    }
    if (!applied)
    {
      std::ostringstream message;
      message << "DOL patch address 0x" << std::hex << patch.address
              << " is outside every section";
      *error = message.str();
      return false;
    }
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
  {
    *error = "can't write " + output_path.string();
    return false;
  }
  return true;
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
#if defined(_WIN32)
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                      &startup, &process))
  {
    std::cerr << "failed to launch command: Windows error " << GetLastError() << '\n';
    return false;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 1;
  const bool got_exit_code = GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return got_exit_code && exit_code == 0;
#else
  return std::system(command.c_str()) == 0;
#endif
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
  const fs::path source_root = fs::path(MODERNGEKKO_SOURCE_DIR);
  const DolPatchSet patches = LoadDefaultDolPatches(
      source_root / "vendor/dolphin/Data/Sys/GameSettings" / (game.disc_id + ".ini"));
  if (!patches)
  {
    std::cerr << patches.error << '\n';
    return std::nullopt;
  }

  std::string compiler;
  if (options.toolchain == "auto")
#if defined(_MSC_VER)
    compiler = "cl";
#elif defined(__clang__)
    compiler = "clang";
#elif defined(__GNUC__)
    compiler = "gcc";
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
  const fs::path dolrecomp = SiblingExecutable(argv0, "dolrecomp");
  const auto dolrecomp_hash = moderngekko::HashFileSha256(dolrecomp);
  if (!dolrecomp_hash)
  {
    std::cerr << "DolRecomp compiler is unavailable: " << dolrecomp << '\n';
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
      std::string(architecture) + "|" + flags + "|backend=" + options.backend +
      "|patches=" + patches.fingerprint + "|dolrecomp_binary=" +
      *dolrecomp_hash;
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
             << "dolrecomp_binary_sha256=" << *dolrecomp_hash << '\n'
             << "module_abi=" << MODERNGEKKO_MODULE_ABI_VERSION << '\n'
             << "cpu_abi=" << MODERNGEKKO_CPU_ABI_VERSION << '\n'
             << "compiler=" << compiler_identity << '\n'
             << "architecture=" << architecture << '\n'
             << "flags=" << flags << '\n'
             << "backend=" << options.backend << '\n'
             << "patches=" << patches.fingerprint << '\n';
    fs::create_directories(options.output / game.disc_id);
    std::ofstream active(options.output / game.disc_id / "active-module.txt");
    active << module.string() << '\n';
    std::cout << "built module: " << module << '\n';
    return module;
  };
  if (fs::is_regular_file(built))
    return publish_module();

  fs::create_directories(artifact);
  fs::path recomp_dol = game.main_dol;
  if (!patches.entries.empty())
  {
    recomp_dol = artifact / "patched-main.dol";
    std::string patch_error;
    if (!PatchDol(game.main_dol, recomp_dol, patches, &patch_error))
    {
      std::cerr << patch_error << '\n';
      return std::nullopt;
    }
    std::cout << "applied " << patches.entries.size() << " default DOL patches\n";
  }
  const fs::path generated_parent = artifact / "dolrecomp-output";
  std::string generate = Quote(dolrecomp) + " -j" +
                         std::to_string(std::max(1u, std::thread::hardware_concurrency())) +
                         " --backend=" + options.backend + " ";
  if (game.platform == moderngekko::GamePlatform::GameCube)
    generate += "--cpu gekko --gamecube " + Quote(recomp_dol) + " " + Quote(generated_parent);
  else
    generate += "--cpu broadway " + Quote(recomp_dol) + " " + game.disc_id + " " +
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
  fs::copy_file(recomp_dol, generated / "main.dol", fs::copy_options::overwrite_existing);
  const fs::path emitted_smc = generated / (generated_stem + "_smc.txt");
  const fs::path normalized_smc = generated / "generated_smc.txt";
  if (fs::is_regular_file(emitted_smc))
  {
    if (emitted_smc != normalized_smc)
      fs::copy_file(emitted_smc, normalized_smc, fs::copy_options::overwrite_existing);
  }
  else
    std::ofstream{normalized_smc};

  const unsigned compile_jobs = std::max(1u, std::thread::hardware_concurrency());
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
               "       moderngekko-port build <game-root> [--backend c|llvm] [--toolchain auto|clang|gcc|msvc] [--output path]\n"
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
    else if (arg == "--backend" && i + 1 < argc)
      options.backend = argv[++i];
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
  if (options.backend != "c" && options.backend != "llvm")
  {
    std::cerr << "unknown backend: " << options.backend << '\n';
    return 2;
  }
#if !defined(MODERNGEKKO_DOLRECOMP_LLVM)
  if (options.backend == "llvm")
  {
    std::cerr << "LLVM backend is unavailable in this build\n";
    return 2;
  }
#endif
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
  return RunCommand(run) ? 0 : 1;
}
