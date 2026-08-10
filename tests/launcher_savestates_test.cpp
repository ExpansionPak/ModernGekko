// The listing, ordering and pruning rules are defined and tested in the runtime
// (Core/SavestateLayout.h, SavestateLayoutTest). What is left here is the
// launcher's own concern -- how an entry is labelled -- plus one check that the
// delegation is wired up, so this cannot silently stop calling the shared
// definition and start drifting from the in-game menu.
#include "launcher_savestates.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  if (ec)
  {
    std::cerr << "could not set older write time: " << ec.message() << '\n';
    fs::remove_all(root, ec);
    return 2;
  }
  fs::last_write_time(newer, fs::file_time_type::clock::now(), ec);
  if (ec)
  {
    std::cerr << "could not set newer write time: " << ec.message() << '\n';
    fs::remove_all(root, ec);
    return 3;
  }

  // Delegation: newest first, straight from the shared definition.
  const auto states = moderngekko::frontend::ListLauncherSavestates(root);
  if (states.size() != 2 || states[0].filename() != newer.filename() ||
      states[1].filename() != older.filename())
  {
    std::cerr << "unexpected savestate order:";
    for (const fs::path& state : states)
      std::cerr << ' ' << moderngekko::frontend::PathText(state.filename());
    std::cerr << '\n';
    fs::remove_all(root, ec);
    return 4;
  }

  const std::string latest_label =
      moderngekko::frontend::LauncherSavestateLabel(states[0], true);
  const std::string older_label =
      moderngekko::frontend::LauncherSavestateLabel(states[1], false);
  if (latest_label != "Latest - player-latest.sav" || older_label != "player-old.sav")
  {
    std::cerr << "unexpected labels: " << latest_label << ", " << older_label << '\n';
    fs::remove_all(root, ec);
    return 5;
  }

  const std::u8string unicode_filename = u8"state-プレイヤー.sav";
  const fs::path unicode_name = fs::path(unicode_filename);
  const std::string unicode_label =
      moderngekko::frontend::LauncherSavestateLabel(unicode_name, false);
  const std::string expected_unicode_label(unicode_filename.begin(), unicode_filename.end());
  if (unicode_label != expected_unicode_label)
  {
    std::cerr << "unexpected Unicode label: " << unicode_label << '\n';
    fs::remove_all(root, ec);
    return 6;
  }

  fs::remove_all(root, ec);
  return 0;
}
