#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace moderngekko
{
enum class GamePlatform
{
  GameCube,
  Wii,
};

struct GameMetadata
{
  std::filesystem::path root;
  std::filesystem::path main_dol;
  std::string game_name;
  std::string disc_id;
  GamePlatform platform = GamePlatform::GameCube;
  std::uint32_t entry_point = 0;
  std::string dol_sha256;
};

struct GameInspectResult
{
  std::optional<GameMetadata> metadata;
  std::string error;

  explicit operator bool() const { return metadata.has_value(); }
};

GameInspectResult InspectGame(const std::filesystem::path& root);
}  // namespace moderngekko

namespace ModernGekko = moderngekko;
