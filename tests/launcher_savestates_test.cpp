#include "launcher_savestates.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
void Touch(const fs::path& path, char contents, std::chrono::hours age)
{
  std::ofstream(path).put(contents);
  std::error_code ec;
  fs::last_write_time(path, fs::file_time_type::clock::now() - age, ec);
}
}  // namespace

int main()
{
  // Timestamped, as the other tests here are: a fixed name collides when two
  // runs overlap, and inherits whatever a previously crashed run left behind.
  const fs::path root =
      fs::temp_directory_path() /
      ("moderngekko-launcher-savestates-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  if (ec)
    return 1;

  const fs::path old_state = root / "player-old.sav";
  const fs::path latest_state = root / "player-latest.sav";
  const fs::path ignored_file = root / "notes.txt";
  Touch(old_state, 'a', std::chrono::hours(1));
  Touch(latest_state, 'b', std::chrono::hours(0));
  std::ofstream(ignored_file).put('c');

  // Newest first, and anything that is not a .sav stays out of the list.
  const auto states = moderngekko::frontend::ListLauncherSavestates(root);
  if (states.size() != 2 || states[0].filename() != latest_state.filename() ||
      states[1].filename() != old_state.filename())
  {
    fs::remove_all(root, ec);
    return 2;
  }
  if (moderngekko::frontend::LauncherSavestateLabel(states[0], true) !=
          "Latest - player-latest.sav" ||
      moderngekko::frontend::LauncherSavestateLabel(states[1], false) != "player-old.sav")
  {
    fs::remove_all(root, ec);
    return 3;
  }

  const fs::path recovery_old = root / "recovery-001-old.sav";
  const fs::path recovery_latest = root / "recovery-002-latest.sav";
  Touch(recovery_old, 'd', std::chrono::hours(2));
  Touch(recovery_latest, 'e', std::chrono::hours(0));

  const auto newest_recovery = moderngekko::frontend::LatestRecoverySavestate(root);
  if (!newest_recovery || newest_recovery->filename() != recovery_latest.filename())
  {
    fs::remove_all(root, ec);
    return 4;
  }

  // Pruning takes automatic states only. A player's own savestates must survive
  // regardless of how many automatic ones accumulate.
  if (moderngekko::frontend::PruneRecoverySavestates(root, 1) != 1 ||
      fs::exists(recovery_old) || !fs::exists(recovery_latest) ||
      !fs::exists(old_state) || !fs::exists(latest_state))
  {
    fs::remove_all(root, ec);
    return 5;
  }

  // A game may name its automatic states after whatever triggers them. Under a
  // different prefix the default-prefixed files are somebody else's business
  // and must not be listed or pruned.
  const fs::path chapter_old = root / "chapter-001.sav";
  const fs::path chapter_new = root / "chapter-002.sav";
  Touch(chapter_old, 'f', std::chrono::hours(3));
  Touch(chapter_new, 'g', std::chrono::hours(0));
  const auto chapters = moderngekko::frontend::ListRecoverySavestates(root, "chapter-");
  if (chapters.size() != 2 || chapters[0].filename() != chapter_new.filename())
  {
    fs::remove_all(root, ec);
    return 6;
  }
  if (moderngekko::frontend::PruneRecoverySavestates(root, 1, "chapter-") != 1 ||
      fs::exists(chapter_old) || !fs::exists(chapter_new) ||
      !fs::exists(recovery_latest) || !fs::exists(latest_state))
  {
    fs::remove_all(root, ec);
    return 7;
  }

  // Keeping more than exist is not an error and removes nothing.
  if (moderngekko::frontend::PruneRecoverySavestates(root, 10) != 0)
  {
    fs::remove_all(root, ec);
    return 8;
  }

  // A directory that is not there yields an empty list rather than throwing.
  if (!moderngekko::frontend::ListLauncherSavestates(root / "absent").empty())
  {
    fs::remove_all(root, ec);
    return 9;
  }

  fs::remove_all(root, ec);
  return 0;
}
