#pragma once

// The parts of `moderngekko-port pgo-run` that are worth testing on their own.
//
// The workflow itself -- build instrumented, play, merge, build again -- can
// only be exercised with a game and a display. Everything it can get wrong
// quietly cannot: a cache key that lets a plain module answer a PGO build, a
// profile summary that is parsed as valid when it is empty, a raw-profile scan
// that misses a file because the path has a space in it. Those live here, take
// values rather than reading the filesystem or spawning processes where that is
// possible, and are covered by tests that need neither a game nor Dolphin.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::pgo
{
namespace fs = std::filesystem;

enum class PgoMode
{
  Off,
  Generate,
  Use,
};

std::string_view PgoModeName(PgoMode mode);

struct PgoBuildOptions
{
  PgoMode mode = PgoMode::Off;
  fs::path merged_profile;
  // The digest of merged_profile's *contents*. The path is deliberately not
  // part of the build's identity: the same profile copied elsewhere is the same
  // profile, and two different profiles written to the same scratch filename
  // are not.
  std::string merged_profile_sha256;
  bool reject_stale_profile = true;
};

// Where a run keeps everything it produces. Separate directories rather than
// one, because the instrumented module and the final module must never be able
// to be confused for each other.
struct Workspace
{
  fs::path root;
  fs::path raw;               // <root>/raw   -- .profraw from the training run
  // <root>/gen -- the instrumented module's own private cache root. Named that
  // tersely on purpose: it prefixes a module build path that is already close
  // to Windows' limit. See LongestModuleObjectPathLength.
  fs::path generate_modules;
  fs::path merged_profile;    // <root>/merged.profdata
  fs::path manifest;          // <root>/pgo-manifest.txt
};

// `profile_dir` empty derives a deterministic location under the module cache,
// so a repeat run reuses the same workspace instead of scattering profiles.
Workspace DeriveWorkspace(const fs::path& profile_dir, const fs::path& output,
                          std::string_view disc_id);

// Windows refuses to create a file whose full path exceeds this, and ninja does
// not use the \\?\ extended-length prefix, so the module build hits it as
// "Unable to create file. No such file or directory" with nothing said about
// paths.
inline constexpr std::size_t WINDOWS_PATH_LIMIT = 260;

// The longest path the module build will try to create under `cache_root`:
//
//   <cache_root>/<disc>/<64-hex dol>-<16-hex key>/module-build/CMakeFiles/
//       g<disc>_recomp.dir/<32-hex>/<longest source name>.c.obj.rsp
//
// A plain build already sits close to the limit; pgo-run adds a workspace
// directory in front of it, which is enough to push a normal cache location
// over. Checking up front turns a twenty-minute build that dies with a
// confusing ninja error into an immediate message naming --profile-dir.
std::size_t LongestModuleObjectPathLength(const fs::path& cache_root, std::string_view disc_id);

// UTF-8. std::filesystem::path::string() narrows through the active code page
// on Windows, which mangles any path the user's code page cannot represent.
std::string PathText(const fs::path& path);

// Forward slashes on every platform. A Windows path reaches Clang inside
// -fprofile-use=..., and reaches CMake inside a -D argument; backslashes are an
// escape character to enough of that chain to be worth never emitting.
std::string ClangPathText(const fs::path& path);

struct ProfdataSummary
{
  std::uint64_t total_functions = 0;
  std::uint64_t maximum_function_count = 0;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Parses `llvm-profdata show`. A profile that merged cleanly but holds nothing
// -- the training process died before the runtime flushed, or never ran the
// module at all -- is a successful command and a useless profile, so this is
// what tells those apart. No minimum count is imposed beyond nonzero: a small
// program with one hot function is a legitimate profile.
ProfdataSummary ParseProfdataSummary(std::string_view text);

// The leading integer of an LLVM version string, from either `clang --version`
// or `llvm-profdata --version`. Used only to refuse a known mismatch; an
// unparseable version yields nullopt and the check is skipped rather than
// guessed at.
std::optional<int> ParseLlvmMajorVersion(std::string_view text);

// Every .profraw under `raw_directory`, recursively, sorted so a run is
// reproducible. Anything that is not a regular .profraw file is ignored.
std::vector<fs::path> DiscoverRawProfiles(const fs::path& raw_directory);

// The candidates that could be llvm-profdata, in the order they should be
// tried. Kept separate from actually probing them so the order is testable.
struct LlvmProfdataSources
{
  fs::path explicit_path;     // --llvm-profdata
  fs::path environment_path;  // $LLVM_PROFDATA
  fs::path clang_path;        // the resolved clang, for its own directory
  fs::path print_prog_name;   // clang --print-prog-name=llvm-profdata
  fs::path xcrun_path;        // xcrun --find llvm-profdata
};

std::vector<fs::path> LlvmProfdataCandidates(const LlvmProfdataSources& sources);

// The PGO contribution to the module cache key. Off is spelled out rather than
// left empty: "no PGO" is a real answer that has to be distinguishable from
// "instrumented", or a plain module satisfies a PGO build.
std::string PgoCacheIdentity(const PgoBuildOptions& options);

struct ManifestFacts
{
  std::string backend;
  std::string clang_version;
  std::string llvm_profdata_version;
  std::size_t raw_profile_count = 0;
  std::uint64_t total_functions = 0;
  std::uint64_t maximum_function_count = 0;
};

// The `pgo_*` block appended to a module's manifest.txt, and written whole as
// the workspace's pgo-manifest.txt. Enough to audit how a module was produced
// without having the run's console output.
std::string FormatPgoManifest(const PgoBuildOptions& options, const ManifestFacts& facts);
}  // namespace moderngekko::pgo
