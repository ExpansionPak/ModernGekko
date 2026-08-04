#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
namespace fs = std::filesystem;

// Automatic savestates are distinguished from ones the player took by a
// filename prefix rather than by a separate directory, so that both kinds list
// together in the launcher in one pass. A game that wants to name its automatic
// states after whatever it triggers them on -- room transitions, checkpoints,
// chapter breaks -- keeps this prefix and appends its own suffix.
inline constexpr std::string_view DEFAULT_RECOVERY_PREFIX = "recovery-";

inline std::vector<fs::path> ListLauncherSavestates(const fs::path& directory)
{
  std::vector<fs::path> paths;
  std::error_code ec;
  if (!fs::is_directory(directory, ec))
    return paths;

  for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
  {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".sav")
      paths.push_back(entry.path());
  }

  // Newest first, because the state a player wants next is nearly always the
  // one they just took. Filename is the tiebreak so the order is stable when
  // two states share a timestamp, and so a filesystem with coarse timestamp
  // granularity does not produce a different list on each launch.
  std::ranges::sort(paths, [](const fs::path& left, const fs::path& right) {
    std::error_code left_ec;
    std::error_code right_ec;
    const auto left_time = fs::last_write_time(left, left_ec);
    const auto right_time = fs::last_write_time(right, right_ec);
    if (!left_ec && !right_ec && left_time != right_time)
      return left_time > right_time;
    return left.filename().string() < right.filename().string();
  });
  return paths;
}

inline std::string LauncherSavestateLabel(const fs::path& path, bool latest)
{
  return latest ? "Latest - " + path.filename().string() : path.filename().string();
}

inline std::vector<fs::path>
ListRecoverySavestates(const fs::path& directory,
                       std::string_view prefix = DEFAULT_RECOVERY_PREFIX)
{
  std::vector<fs::path> paths;
  for (const fs::path& path : ListLauncherSavestates(directory))
  {
    if (path.filename().string().starts_with(prefix))
      paths.push_back(path);
  }
  return paths;
}

inline std::optional<fs::path>
LatestRecoverySavestate(const fs::path& directory,
                        std::string_view prefix = DEFAULT_RECOVERY_PREFIX)
{
  const std::vector<fs::path> paths = ListRecoverySavestates(directory, prefix);
  return paths.empty() ? std::nullopt : std::optional<fs::path>(paths.front());
}

// Deletes all but the newest `keep` automatic states, and returns how many went.
// Only files carrying the prefix are considered, so a player's own savestates
// are never pruned no matter how many accumulate.
inline std::size_t
PruneRecoverySavestates(const fs::path& directory, std::size_t keep,
                        std::string_view prefix = DEFAULT_RECOVERY_PREFIX)
{
  const std::vector<fs::path> recovery_states = ListRecoverySavestates(directory, prefix);
  std::error_code ec;
  std::size_t removed = 0;
  for (std::size_t index = keep; index < recovery_states.size(); ++index)
  {
    ec.clear();
    if (fs::remove(recovery_states[index], ec))
      ++removed;
  }
  return removed;
}
}  // namespace moderngekko::frontend
