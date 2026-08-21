// The PGO workflow's failure modes are quiet ones. A cache key that ignores the
// profile hands back a plain module for a PGO build and nothing looks wrong; a
// summary parser that accepts an all-zero profile reports a trained module that
// was never trained; a raw-profile scan that trips over a space in a path
// merges nothing and blames the game. None of that needs a game or a display to
// test, so none of it is left to the end-to-end run.
#include "pgo_support.hpp"

#include "moderngekko/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace pgo = moderngekko::pgo;

namespace
{
int g_failures = 0;

void Check(bool condition, const std::string& what)
{
  if (condition)
    return;
  std::cerr << "FAIL: " << what << '\n';
  ++g_failures;
}

pgo::PgoBuildOptions UseOptions(const fs::path& profile, const std::string& digest)
{
  pgo::PgoBuildOptions options;
  options.mode = pgo::PgoMode::Use;
  options.merged_profile = profile;
  options.merged_profile_sha256 = digest;
  return options;
}

void TestCacheIdentity()
{
  pgo::PgoBuildOptions off;
  pgo::PgoBuildOptions generate;
  generate.mode = pgo::PgoMode::Generate;
  const auto use = UseOptions("/tmp/merged.profdata", "aaaa");

  const std::string off_key = pgo::PgoCacheIdentity(off);
  const std::string generate_key = pgo::PgoCacheIdentity(generate);
  const std::string use_key = pgo::PgoCacheIdentity(use);
  Check(off_key != generate_key, "PGO off and generate share a cache identity");
  Check(off_key != use_key, "PGO off and use share a cache identity");
  Check(generate_key != use_key, "PGO generate and use share a cache identity");
  Check(off_key == "pgo=off", "PGO off identity is not spelled out: " + off_key);

  // Two different profiles that happen to be written to the same scratch
  // filename must not be interchangeable.
  const std::string same_path_a =
      pgo::PgoCacheIdentity(UseOptions("/tmp/merged.profdata", "aaaa"));
  const std::string same_path_b =
      pgo::PgoCacheIdentity(UseOptions("/tmp/merged.profdata", "bbbb"));
  Check(same_path_a != same_path_b, "two different profiles at one path share a cache identity");

  // The same profile moved elsewhere is the same build, so it must reuse.
  const std::string moved =
      pgo::PgoCacheIdentity(UseOptions("D:/somewhere else/merged.profdata", "aaaa"));
  Check(same_path_a == moved, "the same profile at a different path changed the cache identity");

  pgo::PgoBuildOptions lenient = UseOptions("/tmp/merged.profdata", "aaaa");
  lenient.reject_stale_profile = false;
  Check(pgo::PgoCacheIdentity(lenient) != same_path_a,
        "the stale-profile policy is missing from the cache identity");
}

void TestSummaryParsing()
{
  const pgo::ProfdataSummary good = pgo::ParseProfdataSummary(
      "Instrumentation level: IR  entry_first = 0\n"
      "Total functions: 4213\n"
      "Maximum function count: 918273\n"
      "Maximum internal block count: 40551\n");
  Check(static_cast<bool>(good), "a valid summary was rejected: " + good.error);
  Check(good.total_functions == 4213, "total functions misparsed");
  Check(good.maximum_function_count == 918273, "maximum function count misparsed");

  // Instrumentation creates a record for every function whether or not it ran,
  // so records without counts is exactly what an early flush looks like.
  const pgo::ProfdataSummary empty = pgo::ParseProfdataSummary(
      "Instrumentation level: IR\n"
      "Total functions: 4213\n"
      "Maximum function count: 0\n");
  Check(!empty, "a profile with a zero maximum count was accepted");

  const pgo::ProfdataSummary no_records = pgo::ParseProfdataSummary(
      "Total functions: 0\n"
      "Maximum function count: 0\n");
  Check(!no_records, "a profile with no function records was accepted");

  const pgo::ProfdataSummary malformed = pgo::ParseProfdataSummary(
      "Total functions: not-a-number\n"
      "Maximum function count: 12\n");
  Check(!malformed, "a malformed summary was accepted");

  const pgo::ProfdataSummary truncated =
      pgo::ParseProfdataSummary("error: profile.profdata: Malformed profile data\n");
  Check(!truncated, "an error message was accepted as a summary");

  const pgo::ProfdataSummary missing_maximum =
      pgo::ParseProfdataSummary("Total functions: 12\n");
  Check(!missing_maximum, "a summary with no maximum count was accepted");
}

void TestVersionParsing()
{
  Check(pgo::ParseLlvmMajorVersion("clang version 18.1.8") == 18, "clang version misparsed");
  Check(pgo::ParseLlvmMajorVersion("Homebrew clang version 17.0.6") == 17,
        "vendor-prefixed clang version misparsed");
  Check(pgo::ParseLlvmMajorVersion("  LLVM version 16.0.0") == 16,
        "llvm-profdata version misparsed");
  Check(!pgo::ParseLlvmMajorVersion("clang: command not found").has_value(),
        "a non-version string parsed as a version");
  Check(!pgo::ParseLlvmMajorVersion("").has_value(), "an empty string parsed as a version");
}

void TestWorkspace()
{
  const pgo::Workspace derived = pgo::DeriveWorkspace({}, "/cache/modules", "GM4E01");
  Check(derived.root == fs::path("/cache/modules") / "GM4E01" / "pgo",
        "the derived workspace root is not under the output directory and disc ID");
  Check(derived.raw == derived.root / "raw", "raw directory is not under the workspace root");
  Check(derived.generate_modules == derived.root / "gen",
        "the generation cache is not under the workspace root");
  Check(derived.merged_profile == derived.root / "merged.profdata",
        "merged profile is not under the workspace root");
  Check(derived.manifest == derived.root / "pgo-manifest.txt",
        "manifest is not under the workspace root");
  // The instrumented module cache and the final module must never resolve to
  // the same place, or a failed run can publish an instrumented module.
  Check(derived.generate_modules != fs::path("/cache/modules"),
        "the generation build shares the caller's module cache root");

  const pgo::Workspace explicit_dir =
      pgo::DeriveWorkspace("/build/pgo/GM4E01", "/cache/modules", "GM4E01");
  Check(explicit_dir.root == fs::path("/build/pgo/GM4E01"),
        "--profile-dir did not win over the derived location");
  Check(explicit_dir.raw == fs::path("/build/pgo/GM4E01") / "raw",
        "--profile-dir did not carry through to the raw directory");
}

// This is the check that turns a twenty-minute build failing with "Unable to
// create file" into an immediate message, so it has to be measured against the
// real layout rather than a round number.
void TestModuleObjectPathLength()
{
  // The path this was written for. It failed a real GM4E01 build at 260
  // characters with the old, longer workspace directory name.
  const std::size_t observed = pgo::LongestModuleObjectPathLength(
      "C:/Users/douglaswhittingham/mg-pgo-e2e/c/pgo/generate-modules", "GM4E01");
  Check(observed > pgo::WINDOWS_PATH_LIMIT,
        "the path that really failed is not reported as too long (" +
            std::to_string(observed) + ")");

  // A short workspace has to pass, or the check refuses builds that work.
  const std::size_t shortest = pgo::LongestModuleObjectPathLength("C:/mg/pgo/gen", "GM4E01");
  Check(shortest <= pgo::WINDOWS_PATH_LIMIT,
        "a short workspace is reported as too long (" + std::to_string(shortest) + ")");

  // It has to actually depend on the root, or it is a constant in disguise.
  Check(pgo::LongestModuleObjectPathLength("C:/a", "GM4E01") <
            pgo::LongestModuleObjectPathLength("C:/aaaaaaaaaa", "GM4E01"),
        "the estimate does not grow with the cache root");
  Check(pgo::LongestModuleObjectPathLength("C:/a", "GM4E01") ==
            pgo::LongestModuleObjectPathLength("C:/a", "RMCE01"),
        "the estimate changed for an equally long disc ID");
}

void TestManifest()
{
  auto options = UseOptions("/profiles/merged.profdata", "0123456789abcdef");
  pgo::ManifestFacts facts;
  facts.backend = "llvm";
  facts.clang_version = "clang version 18.1.8";
  facts.llvm_profdata_version = "LLVM version 18.1.8";
  facts.raw_profile_count = 3;
  facts.total_functions = 4213;
  facts.maximum_function_count = 918273;

  const std::string manifest = pgo::FormatPgoManifest(options, facts);
  for (const std::string field :
       {"pgo_mode=use", "pgo_backend=llvm", "pgo_profile_sha256=0123456789abcdef",
        "pgo_raw_profile_count=3", "pgo_max_function_count=918273", "pgo_stale_policy=error",
        "clang_version=clang version 18.1.8", "llvm_profdata_version=LLVM version 18.1.8"})
    Check(manifest.find(field) != std::string::npos, "manifest is missing " + field);
  Check(manifest.find("pgo_profile_path=") != std::string::npos,
        "manifest is missing the profile path");
  Check(!manifest.empty() && manifest.back() == '\n',
        "manifest block does not end in a newline, so it would run into the next field");
}

void TestProfdataCandidates()
{
  pgo::LlvmProfdataSources sources;
  sources.explicit_path = "/opt/explicit/llvm-profdata";
  sources.environment_path = "/opt/env/llvm-profdata";
  sources.clang_path = "/usr/lib/llvm-18/bin/clang";
  sources.print_prog_name = "/usr/lib/llvm-18/bin/llvm-profdata-18";
  sources.xcrun_path = "/Library/Developer/xcrun/llvm-profdata";

  const std::vector<fs::path> candidates = pgo::LlvmProfdataCandidates(sources);
  Check(candidates.size() >= 6, "not every llvm-profdata source produced a candidate");
  Check(candidates.front() == sources.explicit_path, "--llvm-profdata is not tried first");
  Check(candidates[1] == sources.environment_path, "$LLVM_PROFDATA is not tried second");
  Check(candidates[2].parent_path() == sources.clang_path.parent_path(),
        "the binary beside clang is not tried third");
  Check(candidates.back().parent_path().empty(),
        "the bare PATH lookup is not tried last");

  // An unset source contributes nothing rather than an empty path that would
  // be probed as if it were a real candidate.
  pgo::LlvmProfdataSources sparse;
  sparse.clang_path = "clang";
  const std::vector<fs::path> sparse_candidates = pgo::LlvmProfdataCandidates(sparse);
  Check(sparse_candidates.size() == 1,
        "an unset llvm-profdata source produced a candidate anyway");
  for (const fs::path& candidate : sparse_candidates)
    Check(!candidate.empty(), "an empty candidate was produced");
}

int TestRawProfileDiscovery()
{
  // Unique per run: a fixed name races when two runs overlap and inherits
  // whatever a run that died before cleanup left behind.
  const fs::path root =
      fs::temp_directory_path() /
      ("moderngekko-pgo-support-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  // A space and non-ASCII in the same path: both have broken this on Windows
  // before, and the workspace sits under a user-chosen directory.
  const fs::path raw = root / fs::path(std::u8string(u8"raw profiles プレイヤー"));
  std::error_code ec;
  fs::create_directories(raw / "nested", ec);
  if (ec)
  {
    std::cerr << "could not create " << raw << ": " << ec.message() << '\n';
    return 1;
  }

  std::ofstream(raw / "GM4E01-1234.profraw").put('a');
  std::ofstream(raw / "GM4E01-5678.profraw").put('b');
  std::ofstream(raw / "nested" / "GM4E01-9012.profraw").put('c');
  // Neither of these is a raw profile, and merging either would fail.
  std::ofstream(raw / "merged.profdata").put('d');
  std::ofstream(raw / "notes.txt").put('e');

  const std::vector<fs::path> found = pgo::DiscoverRawProfiles(raw);
  Check(found.size() == 3, "raw-profile discovery found " + std::to_string(found.size()) +
                               " profiles rather than 3");
  for (const fs::path& profile : found)
    Check(profile.extension() == ".profraw",
          "discovery returned a non-.profraw file: " + pgo::PathText(profile));
  Check(std::is_sorted(found.begin(), found.end()), "discovery returned an unsorted list");

  // An absent directory is a normal outcome -- the training run wrote nothing --
  // and must report empty rather than throw.
  const std::vector<fs::path> missing = pgo::DiscoverRawProfiles(root / "does-not-exist");
  Check(missing.empty(), "discovery in a missing directory returned entries");

  fs::remove_all(root, ec);
  return 0;
}

void TestPathFormatting()
{
  const fs::path windows_style = fs::path("C:/Users/a b/pgo") / "merged.profdata";
  const std::string clang_text = pgo::ClangPathText(windows_style);
  Check(clang_text.find('\\') == std::string::npos,
        "ClangPathText emitted a backslash: " + clang_text);
  Check(clang_text.find("merged.profdata") != std::string::npos,
        "ClangPathText lost the filename: " + clang_text);

  const fs::path unicode = fs::path(std::u8string(u8"/profiles/プレイヤー/merged.profdata"));
  const std::string text = pgo::PathText(unicode);
  const std::u8string expected = unicode.u8string();
  Check(text == std::string(expected.begin(), expected.end()),
        "PathText did not round-trip a Unicode path");
}

void TestModeNames()
{
  Check(pgo::PgoModeName(pgo::PgoMode::Off) == "off", "PgoMode::Off has the wrong name");
  Check(pgo::PgoModeName(pgo::PgoMode::Generate) == "generate",
        "PgoMode::Generate has the wrong name");
  Check(pgo::PgoModeName(pgo::PgoMode::Use) == "use", "PgoMode::Use has the wrong name");
}

// The profile digest is what makes two profiles distinguishable in the cache
// key, so a digest that does not actually depend on the contents would defeat
// every check above.
void TestProfileDigest()
{
  Check(moderngekko::Sha256(std::string_view("")) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 of the empty input is wrong");
  Check(moderngekko::Sha256(std::string_view("abc")) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 of \"abc\" is wrong");
  Check(moderngekko::Sha256(std::string_view("profile-a")) !=
            moderngekko::Sha256(std::string_view("profile-b")),
        "two different profiles produced the same digest");
}
}  // namespace

int main()
{
  TestModeNames();
  TestCacheIdentity();
  TestSummaryParsing();
  TestVersionParsing();
  TestWorkspace();
  TestModuleObjectPathLength();
  TestManifest();
  TestProfdataCandidates();
  TestPathFormatting();
  TestProfileDigest();
  if (TestRawProfileDiscovery() != 0)
    return 1;
  return g_failures == 0 ? 0 : 1;
}
