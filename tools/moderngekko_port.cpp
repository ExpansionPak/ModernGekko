#include "pgo_support.hpp"
#include "port_command_line.hpp"

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
#include <map>
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

using moderngekko::port::BuildOptions;
using moderngekko::port::PgoRunOptions;

#if defined(MODERNGEKKO_DOLRECOMP_LLVM)
constexpr std::string_view DEFAULT_BACKEND = "llvm";
#else
constexpr std::string_view DEFAULT_BACKEND = "c";
#endif

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

std::string QuoteText(const std::string& text)
{
#if defined(_WIN32)
  return '"' + text + '"';
#else
  std::string result = "'";
  for (char c : text)
    result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
#endif
}

// UTF-8 throughout. path::string() narrows through the active code page on
// Windows, so a module cache under a path the code page cannot represent used
// to reach the compiler as question marks.
std::string Quote(const fs::path& value)
{
  return QuoteText(moderngekko::pgo::PathText(value));
}

std::string FirstLine(std::string_view text)
{
  const std::size_t end = text.find('\n');
  std::string line(end == std::string_view::npos ? text : text.substr(0, end));
  while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
    line.pop_back();
  return line;
}

std::string LineContaining(std::string_view text, std::string_view needle)
{
  std::size_t start = 0;
  while (start <= text.size())
  {
    const std::size_t end = text.find('\n', start);
    const std::string_view line =
        text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (line.find(needle) != std::string_view::npos)
      return FirstLine(line);
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return {};
}

#if defined(_WIN32)
std::wstring Widen(std::string_view utf8)
{
  if (utf8.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
  return wide;
}

std::string Narrow(std::wstring_view wide)
{
  if (wide.empty())
    return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                      nullptr, nullptr);
  return utf8;
}
#endif

// Child processes inherit the process environment, so setting a variable here
// is how DOLRECOMP_LLVM_PGO and LLVM_PROFILE_FILE reach DolRecomp and the
// runner without an environment block being threaded through every call. The
// restore is in the destructor rather than at the end of the happy path
// because a failed stage returns early, and a leaked DOLRECOMP_LLVM_PGO=gen
// would instrument every later build in the same shell.
class ScopedEnvironment
{
public:
  ScopedEnvironment() = default;
  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

  ~ScopedEnvironment()
  {
    for (const auto& [name, value] : m_saved)
      Write(name, value.value_or(std::string{}));
  }

  void Set(const std::string& name, const std::string& value)
  {
    if (!m_saved.contains(name))
      m_saved.emplace(name, Read(name));
    Write(name, value);
  }

private:
  static std::optional<std::string> Read(const std::string& name)
  {
#if defined(_WIN32)
    if (const wchar_t* value = _wgetenv(Widen(name).c_str()))
      return Narrow(value);
#else
    if (const char* value = std::getenv(name.c_str()))
      return std::string(value);
#endif
    return std::nullopt;
  }

  // An empty value removes the variable on both platforms, which is what the
  // restore of a previously unset variable needs.
  static void Write(const std::string& name, const std::string& value)
  {
#if defined(_WIN32)
    _wputenv_s(Widen(name).c_str(), Widen(value).c_str());
#else
    if (value.empty())
      unsetenv(name.c_str());
    else
      setenv(name.c_str(), value.c_str(), 1);
#endif
  }

  std::map<std::string, std::optional<std::string>> m_saved;
};

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
  FILE* pipe = _wpopen(Widen(command).c_str(), L"r");
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
  std::wstring wide = Widen(command);
  std::vector<wchar_t> command_line(wide.begin(), wide.end());
  command_line.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
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

// DolRecomp reads these from the ambient environment and they change the code
// it generates, but none of them were in the module cache key: a module built
// with DOLRECOMP_LLVM_CPU=znver3 answered a later build with the variable
// unset. The names are the pinned recompiler's own getenv calls -- the PGO
// three are excluded because this tool sets them itself, and the profile
// belongs in the key by content rather than by path.
//
// Nothing is contributed when none are set, so a default environment keeps the
// cache entries it already has.
std::string DolRecompCodegenIdentity()
{
  constexpr std::array<const char*, 7> names = {
      "DOLRECOMP_C_CHUNK_INSTRUCTIONS", "DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS",
      "DOLRECOMP_LLVM_CPU",             "DOLRECOMP_LLVM_FEATURES",
      "DOLRECOMP_LLVM_TARGET",          "DOLRECOMP_DISPATCH_LOOKUP",
      "DOLRECOMP_UNSAFE_DIRECT_CALLS"};
  std::string identity;
  for (const char* name : names)
  {
    const char* value = std::getenv(name);
    if (!value)
      continue;
    identity += identity.empty() ? "|dolrecomp_env=" : ",";
    identity += std::string(name) + "=" + value;
  }
  return identity;
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
  // The module template appends -O<level> after CMake's own
  // CMAKE_C_FLAGS_RELEASE, so this is the level that actually reaches the
  // per-translation-unit compiles.
  const std::string opt = options.opt_level.empty() ? std::string("2") : options.opt_level;
  std::string flags;
  if (compiler == "clang")
  {
    flags = "compile:-O" + opt +
            " -flto=thin -fvisibility=hidden -ffp-contract=off -fno-fast-math "
            "link:-flto=thin";
#if defined(__linux__)
    flags += " -fuse-ld=lld";
#endif
  }
  else if (compiler == "gcc")
  {
    flags = "compile:-O" + opt +
            " -fvisibility=hidden -ffp-contract=off -fno-fast-math link:no-lto";
  }
  else
  {
    flags = opt == "0" ? "compile:/Od /fp:strict" : "compile:/O2 /fp:strict";
  }

  // -fprofile-generate has to reach the link and not only the compiles: the
  // profiling runtime that writes the .profraw comes from the link line, and an
  // instrumented module that never linked it produces no profile at all. For
  // the LLVM backend the chunks arrive as objects DolRecomp already
  // instrumented, so the link is the only place these flags matter to them.
  std::string pgo_compile_flags;
  if (options.pgo.mode == moderngekko::pgo::PgoMode::Generate)
  {
    pgo_compile_flags = "-fprofile-generate";
  }
  else if (options.pgo.mode == moderngekko::pgo::PgoMode::Use)
  {
    pgo_compile_flags =
        "-fprofile-use=" + moderngekko::pgo::ClangPathText(options.pgo.merged_profile);
    // A profile that no longer describes the code is the exact failure this
    // workflow exists to avoid measuring through, so it is an error.
    // -Wprofile-instr-unprofiled is not the same thing: a training run that
    // never entered a function is normal, and there are thousands of them.
    pgo_compile_flags += " -Wno-profile-instr-unprofiled";
    if (options.pgo.reject_stale_profile)
      pgo_compile_flags += " -Werror=profile-instr-out-of-date";
  }

  const std::string identity = std::string(RECOMPCORE_REVISION) + "|dolrecomp=" +
      std::string(DOLRECOMP_REVISION) + "|module-abi=" +
      std::to_string(MODERNGEKKO_MODULE_ABI_VERSION) + "|cpu-abi=" +
      std::to_string(MODERNGEKKO_CPU_ABI_VERSION) + "|" + compiler_identity + "|" +
      std::string(architecture) + "|" + flags + "|backend=" + options.backend +
      "|patches=" + patches.fingerprint + "|dolrecomp_binary=" +
      *dolrecomp_hash + "|" + moderngekko::pgo::PgoCacheIdentity(options.pgo) +
      DolRecompCodegenIdentity();
  // Deliberately NOT pgo_compile_flags: those carry the workspace path the
  // profile happens to sit at, and the same profile copied elsewhere is the
  // same build. PgoCacheIdentity carries the profile's content digest instead.
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
    if (options.pgo.mode != moderngekko::pgo::PgoMode::Off)
      manifest << moderngekko::pgo::FormatPgoManifest(options.pgo, options.pgo_facts);
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
  // The LLVM backend emits and codegens IR in-process, so no CFLAGS reach its
  // chunks; its instrumentation is two passes selected by these variables. The
  // C backend needs none of this -- its chunks are C, and -fprofile-generate
  // in CMAKE_C_FLAGS below already covers them.
  ScopedEnvironment recompiler_environment;
  if (options.backend == "llvm")
  {
    if (options.pgo.mode == moderngekko::pgo::PgoMode::Generate)
    {
      recompiler_environment.Set("DOLRECOMP_LLVM_PGO", "gen");
    }
    else if (options.pgo.mode == moderngekko::pgo::PgoMode::Use)
    {
      recompiler_environment.Set("DOLRECOMP_LLVM_PGO", "use");
      recompiler_environment.Set("DOLRECOMP_LLVM_PROFILE",
                                 moderngekko::pgo::PathText(options.pgo.merged_profile));
      recompiler_environment.Set("DOLRECOMP_LLVM_PGO_STALE",
                                 options.pgo.reject_stale_profile ? "error" : "warn");
    }
  }
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
      (options.opt_level.empty()
           ? std::string()
           : " -DRECOMPCORE_MODULE_OPT_LEVEL=" + options.opt_level) +
      " -DCHASSIS_ABI_DIR=" +
      Quote(source_root / "vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp");
  // The module template has no hook of its own for extra flags, and CMake's
  // shared-library link rule expands CMAKE_C_FLAGS as well as the linker
  // flags, so setting both is what puts -fprofile-generate on the compile
  // lines and on the link line. The linker variable is set explicitly rather
  // than relied on implicitly, because "it happens to be on the link line
  // too" is not a property worth depending on for the one flag that has to be
  // there.
  if (!pgo_compile_flags.empty())
    configure += " -DCMAKE_C_FLAGS=" + QuoteText(pgo_compile_flags) +
                 " -DCMAKE_SHARED_LINKER_FLAGS=" + QuoteText(pgo_compile_flags);
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

fs::path ResolveExecutable(const std::string& name)
{
#if defined(_WIN32)
  const std::string located = FirstLine(ReadCommand("where " + name + " 2>NUL"));
#else
  const std::string located = FirstLine(ReadCommand("command -v " + name + " 2>/dev/null"));
#endif
  return located.empty() ? fs::path(name) : fs::path(located);
}

// Every failure exit goes through here, so the two things that must be true of
// a failed run -- a nonzero status, and an active module that is still the
// user's -- are stated once instead of at eight return sites.
int PgoFailure(std::string_view stage, const moderngekko::pgo::Workspace& workspace)
{
  std::cerr << "\nPGO run failed during: " << stage << '\n'
            << "The previously active module is unchanged; no instrumented module was "
               "published.\n"
            << "Work files kept for diagnosis: " << workspace.root << '\n';
  return 1;
}

int PgoRun(const char* argv0, const fs::path& root, BuildOptions options,
           const PgoRunOptions& pgo_options)
{
  const auto inspected = moderngekko::InspectGame(root);
  if (!inspected)
  {
    std::cerr << "invalid extracted game: " << inspected.error << '\n';
    return 1;
  }
  const auto& game = *inspected.metadata;

  // Instrumentation PGO is Clang's. GCC writes .gcda that llvm-profdata cannot
  // merge, MSVC's PGO is a different mechanism again, and DolRecomp's LLVM
  // backend applies an LLVM .profdata or nothing. Falling back would produce a
  // module that builds, runs, and is not profiled.
  if (options.toolchain == "auto")
    options.toolchain = "clang";
  if (options.toolchain != "clang")
  {
    std::cerr << "pgo-run requires Clang and a matching llvm-profdata; --toolchain "
              << options.toolchain << " cannot produce an instrumented module.\n"
              << "Use --toolchain clang, or `build` for a non-PGO module.\n";
    return 2;
  }

  const std::string clang_identity = ReadCommand("clang --version 2>&1");
  const std::string clang_version = FirstLine(clang_identity);
  if (clang_version.empty())
  {
    std::cerr << "clang is unavailable, and pgo-run requires it\n";
    return 1;
  }
  const fs::path clang_path = ResolveExecutable("clang");

  moderngekko::pgo::LlvmProfdataSources sources;
  sources.explicit_path = pgo_options.llvm_profdata;
  if (const char* from_environment = std::getenv("LLVM_PROFDATA"))
    sources.environment_path = from_environment;
  sources.clang_path = clang_path;
  sources.print_prog_name = FirstLine(ReadCommand("clang --print-prog-name=llvm-profdata"));
#if defined(__APPLE__)
  sources.xcrun_path = FirstLine(ReadCommand("xcrun --find llvm-profdata 2>/dev/null"));
#endif

  fs::path llvm_profdata;
  std::string llvm_profdata_version;
  for (const fs::path& candidate : moderngekko::pgo::LlvmProfdataCandidates(sources))
  {
    // A candidate that does not exist still produces output -- the shell's own
    // "not found" -- so a nonempty result is not proof. Only a version banner
    // LLVM itself printed is.
    const std::string probe = ReadCommand(Quote(candidate) + " --version 2>&1");
    const std::string banner = LineContaining(probe, "LLVM version");
    if (banner.empty())
      continue;
    llvm_profdata = candidate;
    llvm_profdata_version = banner;
    break;
  }
  if (llvm_profdata.empty())
  {
    std::cerr << "could not find a usable llvm-profdata.\n"
                 "Pass --llvm-profdata <path>, set LLVM_PROFDATA, or put it on PATH.\n";
    return 1;
  }

  const auto clang_major = moderngekko::pgo::ParseLlvmMajorVersion(clang_identity);
  const auto profdata_major = moderngekko::pgo::ParseLlvmMajorVersion(llvm_profdata_version);
  // Only a version that both tools reported and that disagrees is refused. An
  // unparseable banner means the check does not apply, not that it failed.
  if (clang_major && profdata_major && *clang_major != *profdata_major)
  {
    std::cerr << "llvm-profdata is LLVM " << *profdata_major << " but clang is LLVM "
              << *clang_major << ".\n"
              << "  clang:         " << clang_version << '\n'
              << "  llvm-profdata: " << llvm_profdata_version << " (" << llvm_profdata << ")\n"
              << "The profile formats are not guaranteed compatible across major versions.\n"
                 "Pass --llvm-profdata with the matching binary.\n";
    return 1;
  }

  if (options.output.empty())
    options.output = DefaultOutput();
  const moderngekko::pgo::Workspace workspace =
      moderngekko::pgo::DeriveWorkspace(pgo_options.profile_dir, options.output, game.disc_id);

#if defined(_WIN32)
  // The generation build nests a second module cache under the workspace, which
  // is enough to push an otherwise fine cache location past Windows' limit. The
  // build that would fail takes twenty minutes to get there and then reports
  // only "Unable to create file", so it is worth one second here instead.
  for (const auto& [label, cache_root] :
       {std::pair<std::string_view, const fs::path&>{"--profile-dir", workspace.generate_modules},
        std::pair<std::string_view, const fs::path&>{"--output", options.output}})
  {
    const std::size_t longest =
        moderngekko::pgo::LongestModuleObjectPathLength(cache_root, game.disc_id);
    if (longest <= moderngekko::pgo::WINDOWS_PATH_LIMIT)
      continue;
    std::cerr << "the module build under " << cache_root << " would need paths of up to "
              << longest << " characters, and Windows refuses to create a file past "
              << moderngekko::pgo::WINDOWS_PATH_LIMIT << ".\n"
              << "Pass a shorter " << label << " (for example " << label << " C:\\mg-pgo).\n";
    return 1;
  }
#endif

  std::error_code ec;
  // A .profraw left by an earlier run describes an earlier module. Merged in,
  // it trains this one on code that may no longer exist, and the result still
  // looks like a successful run -- so the raw directory starts empty every
  // time rather than being appended to.
  fs::remove_all(workspace.raw, ec);
  fs::create_directories(workspace.raw, ec);
  if (ec)
  {
    std::cerr << "could not prepare " << workspace.raw << ": " << ec.message() << '\n';
    return 1;
  }
  fs::create_directories(workspace.generate_modules, ec);

  std::cout << "PGO workspace: " << workspace.root << "\n"
            << "clang:         " << clang_version << "\n"
            << "llvm-profdata: " << llvm_profdata_version << " (" << llvm_profdata << ")\n";

  std::cout << "\n[1/6] Building the instrumented module\n";
  BuildOptions generate_options = options;
  // A private cache root. The generation build must not be able to write the
  // real active-module.txt, or a failure after this point leaves the user
  // pointed at an instrumented module.
  generate_options.output = workspace.generate_modules;
  generate_options.pgo.mode = moderngekko::pgo::PgoMode::Generate;
  const auto instrumented = Build(argv0, root, generate_options);
  if (!instrumented)
    return PgoFailure("instrumented module build", workspace);

  std::cout << "\n[2/6] PGO training has started.\n"
               "Exercise representative gameplay, demanding scenes, menus, and transitions.\n"
               "Close the game normally when training is complete.\n\n";
  {
    ScopedEnvironment training_environment;
    // %p is the training process's pid. Without it a second process -- or a
    // relaunch inside one session -- overwrites the first one's profile
    // instead of adding to it.
    training_environment.Set(
        "LLVM_PROFILE_FILE",
        moderngekko::pgo::PathText(workspace.raw / (game.disc_id + "-%p.profraw")));
    std::string run = Quote(SiblingExecutable(argv0, "moderngekko-run")) + " --game " +
                      Quote(root) + " --module " + Quote(*instrumented);
    for (const std::string& arg : options.runner_arguments)
      run += " " + Quote(arg);
    if (!RunCommand(run))
    {
      std::cerr << "\nthe training run exited with a failure status.\n"
                   "The profiling runtime flushes on a normal shutdown, so a crashed or "
                   "killed run has not written a usable profile.\n";
      return PgoFailure("training run", workspace);
    }
  }

  std::cout << "\n[3/6] Collecting raw profiles\n";
  const std::vector<fs::path> raw_profiles =
      moderngekko::pgo::DiscoverRawProfiles(workspace.raw);
  std::uintmax_t raw_bytes = 0;
  std::size_t nonempty = 0;
  for (const fs::path& profile : raw_profiles)
  {
    const std::uintmax_t size = fs::file_size(profile, ec);
    if (ec)
      continue;
    raw_bytes += size;
    if (size > 0)
      ++nonempty;
  }
  if (nonempty == 0)
  {
    std::cerr << "no nonempty .profraw was written to " << workspace.raw << ".\n"
              << "The instrumented module did not flush a profile. Close the game through "
                 "its own quit path rather than killing it.\n";
    return PgoFailure("raw-profile discovery", workspace);
  }

  std::cout << "\n[4/6] Merging profiles\n";
  std::string merge =
      Quote(llvm_profdata) + " merge -output=" + Quote(workspace.merged_profile);
  for (const fs::path& profile : raw_profiles)
    merge += " " + Quote(profile);
  if (!RunCommand(merge))
    return PgoFailure("profile merge", workspace);
  const std::uintmax_t merged_size = fs::file_size(workspace.merged_profile, ec);
  if (ec || merged_size == 0)
  {
    std::cerr << "llvm-profdata reported success but produced no usable "
              << workspace.merged_profile << '\n';
    return PgoFailure("profile merge", workspace);
  }

  std::cout << "\n[5/6] Validating the merged profile\n";
  const std::string shown =
      ReadCommand(Quote(llvm_profdata) + " show " + Quote(workspace.merged_profile) + " 2>&1");
  const moderngekko::pgo::ProfdataSummary summary =
      moderngekko::pgo::ParseProfdataSummary(shown);
  if (!summary)
  {
    std::cerr << summary.error << "\nllvm-profdata show said:\n" << shown << '\n';
    return PgoFailure("profile validation", workspace);
  }
  std::cout << "Raw profiles:              " << raw_profiles.size() << '\n'
            << "Total raw-profile bytes:   " << raw_bytes << '\n'
            << "Total functions:           " << summary.total_functions << '\n'
            << "Maximum function count:    " << summary.maximum_function_count << '\n'
            << "Merged profile:            " << workspace.merged_profile << '\n';

  const auto profile_hash = moderngekko::HashFileSha256(workspace.merged_profile);
  if (!profile_hash)
    return PgoFailure("profile validation", workspace);

  std::cout << "\n[6/6] Building the PGO module\n";
  BuildOptions use_options = options;
  use_options.pgo.mode = moderngekko::pgo::PgoMode::Use;
  // Absolute: this path is handed to a compiler that runs in the module build
  // directory, not in the caller's working directory.
  use_options.pgo.merged_profile = fs::absolute(workspace.merged_profile, ec);
  if (ec)
    use_options.pgo.merged_profile = workspace.merged_profile;
  use_options.pgo.merged_profile_sha256 = *profile_hash;
  use_options.pgo.reject_stale_profile = true;
  use_options.pgo_facts.backend = options.backend;
  use_options.pgo_facts.clang_version = clang_version;
  use_options.pgo_facts.llvm_profdata_version = llvm_profdata_version;
  use_options.pgo_facts.raw_profile_count = raw_profiles.size();
  use_options.pgo_facts.total_functions = summary.total_functions;
  use_options.pgo_facts.maximum_function_count = summary.maximum_function_count;
  const auto module = Build(argv0, root, use_options);
  if (!module)
    return PgoFailure("PGO module build", workspace);

  const auto module_hash = moderngekko::HashFileSha256(*module);
  if (!module_hash)
  {
    std::cerr << "the PGO module is missing after a successful build: " << *module << '\n';
    return PgoFailure("final module validation", workspace);
  }

  {
    std::ofstream manifest(workspace.manifest);
    manifest << "disc_id=" << game.disc_id << '\n'
             << "dol_sha256=" << game.dol_sha256 << '\n'
             << "module_path=" << moderngekko::pgo::PathText(*module) << '\n'
             << "module_sha256=" << *module_hash << '\n'
             << moderngekko::pgo::FormatPgoManifest(use_options.pgo, use_options.pgo_facts);
  }

  // Only now, with a published module and a written manifest, is it safe to
  // drop the bulky intermediates. The merged profile and this manifest stay
  // either way: they are what a later run, or a bug report, needs.
  if (!pgo_options.keep_work)
  {
    fs::remove_all(workspace.raw, ec);
    fs::remove_all(workspace.generate_modules, ec);
  }

  const fs::path active = options.output / game.disc_id / "active-module.txt";
  std::cout << "\nPGO build complete\n"
            << "Game:                  " << game.game_name << '\n'
            << "Disc ID:               " << game.disc_id << '\n'
            << "Backend:               " << options.backend << '\n'
            << "Training run:          completed normally\n"
            << "Raw profiles:          " << raw_profiles.size() << '\n'
            << "Merged profile:        " << workspace.merged_profile << '\n'
            << "Merged profile SHA256: " << *profile_hash << '\n'
            << "Final module:          " << *module << '\n'
            << "Final module SHA256:   " << *module_hash << '\n'
            << "Active module updated: yes (" << active << ")\n"
            << "PGO manifest:          " << workspace.manifest << '\n';
  if (pgo_options.keep_work)
    std::cout << "Work files kept:       " << workspace.root << '\n';
  return 0;
}

void Usage()
{
  std::cerr << "usage: moderngekko-port inspect <game-root>\n"
               "       moderngekko-port build <game-root> [--backend c|llvm] [--toolchain auto|clang|gcc|msvc] [--opt-level 0-3] [--output path]\n"
               "       moderngekko-port run <game-root> [build options] [-- runner options]\n"
               "       moderngekko-port pgo-run <game-root> [--backend c|llvm] [--toolchain auto|clang] [--opt-level 0-3]\n"
               "               [--output path] [--profile-dir path] [--llvm-profdata path] [--keep-work]\n"
               "               [-- runner options]\n";
}
}  // namespace

int main(int argc, char** argv)
{
  moderngekko::port::CommandLine parsed =
      moderngekko::port::ParseCommandLine(argc, argv, DEFAULT_BACKEND);
  if (parsed.usage)
  {
    Usage();
    return 2;
  }
  if (!parsed)
  {
    std::cerr << parsed.error << '\n';
    return 2;
  }
  const std::string& command = parsed.command;
  const fs::path& root = parsed.root;
  BuildOptions options = std::move(parsed.build);
  if (options.output.empty())
    options.output = DefaultOutput();
#if !defined(MODERNGEKKO_DOLRECOMP_LLVM)
  if (options.backend == "llvm")
  {
    std::cerr << "LLVM backend is unavailable in this build\n";
    return 2;
  }
#endif
  if (command == "inspect")
    return Inspect(root, options.output);
  if (command == "pgo-run")
    return PgoRun(argv[0], root, options, parsed.pgo_run);
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
