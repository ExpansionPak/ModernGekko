#include "pgo_support.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <system_error>

namespace moderngekko::pgo
{
namespace
{
#if defined(_WIN32)
constexpr std::string_view LLVM_PROFDATA_NAME = "llvm-profdata.exe";
#else
constexpr std::string_view LLVM_PROFDATA_NAME = "llvm-profdata";
#endif

std::string Trim(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return std::string(value);
}

// `llvm-profdata show` prints "Total functions: 42". Nothing else on the line,
// so the value is whatever follows the colon once the label matches exactly.
bool ReadLabelledCount(std::string_view line, std::string_view label, std::uint64_t* value,
                       bool* malformed)
{
  const std::string trimmed = Trim(line);
  if (!trimmed.starts_with(label))
    return false;
  const std::string tail = Trim(std::string_view(trimmed).substr(label.size()));
  if (tail.empty())
  {
    *malformed = true;
    return true;
  }
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(tail.data(), tail.data() + tail.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != tail.data() + tail.size())
  {
    *malformed = true;
    return true;
  }
  *value = parsed;
  return true;
}
}  // namespace

std::string_view PgoModeName(PgoMode mode)
{
  switch (mode)
  {
  case PgoMode::Generate:
    return "generate";
  case PgoMode::Use:
    return "use";
  case PgoMode::Off:
    break;
  }
  return "off";
}

Workspace DeriveWorkspace(const fs::path& profile_dir, const fs::path& output,
                          std::string_view disc_id)
{
  Workspace workspace;
  workspace.root = profile_dir.empty() ? output / std::string(disc_id) / "pgo" : profile_dir;
  workspace.raw = workspace.root / "raw";
  workspace.generate_modules = workspace.root / "generate-modules";
  workspace.merged_profile = workspace.root / "merged.profdata";
  workspace.manifest = workspace.root / "pgo-manifest.txt";
  return workspace;
}

std::string PathText(const fs::path& path)
{
  const std::u8string encoded = path.u8string();
  return {encoded.begin(), encoded.end()};
}

std::string ClangPathText(const fs::path& path)
{
  const std::u8string encoded = path.generic_u8string();
  return {encoded.begin(), encoded.end()};
}

ProfdataSummary ParseProfdataSummary(std::string_view text)
{
  ProfdataSummary summary;
  bool saw_total = false;
  bool saw_maximum = false;
  bool malformed = false;

  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line))
  {
    if (ReadLabelledCount(line, "Total functions:", &summary.total_functions, &malformed))
      saw_total = true;
    else if (ReadLabelledCount(line, "Maximum function count:", &summary.maximum_function_count,
                               &malformed))
      saw_maximum = true;
  }

  if (malformed)
  {
    summary.error = "llvm-profdata show produced a summary that could not be parsed";
    return summary;
  }
  if (!saw_total || !saw_maximum)
  {
    summary.error = "llvm-profdata show reported no profile summary";
    return summary;
  }
  if (summary.total_functions == 0)
  {
    summary.error = "the merged profile contains no function records";
    return summary;
  }
  // A profile whose hottest function never executed is what an early flush
  // looks like: the records exist because instrumentation created them, and
  // every count is zero. Building against it produces an untrained module that
  // reports as trained.
  if (summary.maximum_function_count == 0)
  {
    summary.error = "the merged profile has a maximum function count of zero, so nothing "
                    "was executed during training";
    return summary;
  }
  return summary;
}

std::optional<int> ParseLlvmMajorVersion(std::string_view text)
{
  // "clang version 18.1.8", "Homebrew clang version 17.0.6", "LLVM (...)
  // version 16.0.0". In every spelling the major number is the first digit run
  // that follows the word "version".
  constexpr std::string_view marker = "version";
  std::size_t at = text.find(marker);
  while (at != std::string_view::npos)
  {
    std::size_t cursor = at + marker.size();
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])))
      ++cursor;
    const std::size_t start = cursor;
    while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])))
      ++cursor;
    if (cursor > start)
    {
      int value = 0;
      const auto result = std::from_chars(text.data() + start, text.data() + cursor, value);
      if (result.ec == std::errc{})
        return value;
    }
    at = text.find(marker, at + marker.size());
  }
  return std::nullopt;
}

std::vector<fs::path> DiscoverRawProfiles(const fs::path& raw_directory)
{
  std::vector<fs::path> profiles;
  std::error_code ec;
  fs::recursive_directory_iterator it(raw_directory, fs::directory_options::skip_permission_denied,
                                      ec);
  if (ec)
    return profiles;
  for (const fs::directory_entry& entry : it)
  {
    if (entry.path().extension() != ".profraw")
      continue;
    std::error_code file_ec;
    if (fs::is_regular_file(entry.path(), file_ec) && !file_ec)
      profiles.push_back(entry.path());
  }
  std::sort(profiles.begin(), profiles.end());
  return profiles;
}

std::vector<fs::path> LlvmProfdataCandidates(const LlvmProfdataSources& sources)
{
  std::vector<fs::path> candidates;
  const auto add = [&candidates](const fs::path& candidate) {
    if (candidate.empty())
      return;
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
      candidates.push_back(candidate);
  };

  add(sources.explicit_path);
  add(sources.environment_path);
  if (sources.clang_path.has_parent_path())
    add(sources.clang_path.parent_path() / LLVM_PROFDATA_NAME);
  add(sources.print_prog_name);
  add(sources.xcrun_path);
  // Last: whatever PATH resolves the bare name to.
  add(fs::path(std::string(LLVM_PROFDATA_NAME)));
  return candidates;
}

std::string PgoCacheIdentity(const PgoBuildOptions& options)
{
  std::string identity = "pgo=" + std::string(PgoModeName(options.mode));
  if (options.mode != PgoMode::Use)
    return identity;
  identity += "|pgo_profile_sha256=" + options.merged_profile_sha256;
  identity += options.reject_stale_profile ? "|pgo_stale=error" : "|pgo_stale=warn";
  return identity;
}

std::string FormatPgoManifest(const PgoBuildOptions& options, const ManifestFacts& facts)
{
  std::ostringstream out;
  out << "pgo_mode=" << PgoModeName(options.mode) << '\n'
      << "pgo_backend=" << facts.backend << '\n'
      << "pgo_profile_sha256=" << options.merged_profile_sha256 << '\n'
      << "pgo_profile_path=" << PathText(options.merged_profile) << '\n'
      << "pgo_raw_profile_count=" << facts.raw_profile_count << '\n'
      << "pgo_total_function_count=" << facts.total_functions << '\n'
      << "pgo_max_function_count=" << facts.maximum_function_count << '\n'
      << "pgo_stale_policy=" << (options.reject_stale_profile ? "error" : "warn") << '\n'
      << "clang_version=" << facts.clang_version << '\n'
      << "llvm_profdata_version=" << facts.llvm_profdata_version << '\n';
  return out.str();
}
}  // namespace moderngekko::pgo
