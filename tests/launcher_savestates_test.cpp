// The listing, ordering and pruning rules are defined and tested in the runtime
// (Core/SavestateLayout.h, SavestateLayoutTest). What is left here is the
// launcher's own concern -- how an entry is labelled -- plus one check that the
// delegation is wired up, so this cannot silently stop calling the shared
// definition and start drifting from the in-game menu.
#include "launcher_savestates.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main()
{
  // Unique per run: a fixed name races when two runs overlap and inherits
  // whatever a run that died before cleanup left behind.
  const fs::path root =
      fs::temp_directory_path() /
      ("moderngekko-launcher-savestates-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec)
    return 1;

  const fs::path older = root / "player-old.sav";
  const fs::path newer = root / "player-latest.sav";
  std::ofstream(older).put('a');
  std::ofstream(newer).put('b');
  fs::last_write_time(older, fs::file_time_type::clock::now() - std::chrono::hours(1), ec);
  fs::last_write_time(newer, fs::file_time_type::clock::now(), ec);

  // Delegation: newest first, straight from the shared definition.
  const auto states = moderngekko::frontend::ListLauncherSavestates(root);
  if (states.size() != 2 || states[0].filename() != newer.filename() ||
      states[1].filename() != older.filename())
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

  fs::remove_all(root, ec);
  return 0;
}
