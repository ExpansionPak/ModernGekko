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

inline std::string PathText(const fs::path& path)
{
  const std::u8string encoded = path.u8string();
  return {encoded.begin(), encoded.end()};
}

// The newest entry is worth calling out so the label explains why it is first.
inline std::string LauncherSavestateLabel(const fs::path& path, bool latest)
{
  const std::string filename = PathText(path.filename());
  return latest ? "Latest - " + filename : filename;
}
}  // namespace moderngekko::frontend
