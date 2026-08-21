#pragma once

#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace moderngekko::automation
{
enum class CommandType
{
  Pad,
  PadFrames,
  ClearPad,
  Pause,
  Resume,
  SaveState,
  LoadState,
  Screenshot,
  ReadMemory,
  WriteMemory,
  Stop,
};

struct PadState
{
  int port = 0;
  // Sized through the Wii range, not just the GameCube one. ApplyAutomationPad
  // indexes this array straight off FIRST_GC_CONTROL, so a GC-sized array made
  // every Wiimote and Nunchuk control unreachable -- automation could not press
  // A on a Wii title's health-warning screen, let alone move the player.
  std::array<double, ciface::Touch::LAST_WII_CONTROL - ciface::Touch::FIRST_GC_CONTROL + 1>
      controls{};
};

struct Command
{
  CommandType type = CommandType::Pause;
  std::string source_name;
  PadState pad;
  std::filesystem::path path;
  std::uint32_t address = 0;
  std::uint32_t size = 0;
  std::uint32_t frames = 0;
  std::vector<std::uint8_t> data;
};

struct Status
{
  std::string state = "stopped";
  bool booted = false;
  double fps = 0.0;
  double vps = 0.0;
  double speed = 0.0;
  std::uint64_t frame_count = 0;
  std::uint64_t present_count = 0;
  std::string title;
  std::string game_id;
  std::string game_name;
  int navigation_mode = 0;
  bool navigation_valid = false;
  unsigned int navigation_room_id = 0;
  float navigation_player_x = 0.0f;
  float navigation_player_z = 0.0f;
  float navigation_facing = 0.0f;
  bool navigation_collision_valid = false;
  std::uint32_t navigation_collision_base = 0;
  std::uint32_t navigation_collision_segments = 0;
  std::uint32_t navigation_npc_count = 0;
  std::string navigation_archive;
  std::string navigation_location;
  std::uint64_t processed_commands = 0;
  std::string last_command;
  std::string last_error;
};

std::filesystem::path ResolveControlPath(const std::filesystem::path& automation_directory,
                                         const std::filesystem::path& value);
std::vector<std::filesystem::path>
ListCommandFiles(const std::filesystem::path& commands_directory);
bool ParseCommandFile(const std::filesystem::path& path, Command* command, std::string* error);
std::string FormatStatus(const Status& status);
}  // namespace moderngekko::automation
