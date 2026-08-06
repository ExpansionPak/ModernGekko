#pragma once

// Presentation only. Where savestates live, what they are called, and what order
// they list in is defined once in the runtime and included here rather than
// restated -- see Core/SavestateLayout.h. Two copies of that rule would let the
// launcher and the in-game menu show the same directory in a different order,
// which is a confusing bug and an easy one to introduce by editing one side.

#include "Core/SavestateLayout.h"

#include <filesystem>
#include <string>
#include <vector>

namespace moderngekko::frontend
{
namespace fs = std::filesystem;

using State::Layout::LatestAutomatic;
using State::Layout::ListAutomatic;
using State::Layout::PruneAutomatic;

inline std::vector<fs::path> ListLauncherSavestates(const fs::path& directory)
{
  return State::Layout::List(directory);
}

// The newest entry is worth calling out: it is preselected because it is the one
// a player almost always wants next, so the label says why it is at the top.
inline std::string LauncherSavestateLabel(const fs::path& path, bool latest)
{
  return latest ? "Latest - " + path.filename().string() : path.filename().string();
}
}  // namespace moderngekko::frontend
