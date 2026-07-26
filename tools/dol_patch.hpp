#pragma once

#include <filesystem>
#include <string>

namespace moderngekko::frontend {
bool ApplyDolPatchManifest(const std::filesystem::path &dol_path,
                           const std::filesystem::path &manifest_path,
                           bool *changed, std::string *error);
}
