#include "automation_protocol.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace moderngekko::automation
{
namespace
{
using ciface::Touch::ControlID;

struct PadField
{
  std::string_view name;
  ControlID control;
  double min_value;
  double max_value;
};

constexpr std::array kPadFields = {
    PadField{"a", ControlID::GCPAD_A_BUTTON, 0.0, 1.0},
    PadField{"b", ControlID::GCPAD_B_BUTTON, 0.0, 1.0},
    PadField{"x", ControlID::GCPAD_X_BUTTON, 0.0, 1.0},
    PadField{"y", ControlID::GCPAD_Y_BUTTON, 0.0, 1.0},
    PadField{"z", ControlID::GCPAD_Z_BUTTON, 0.0, 1.0},
    PadField{"start", ControlID::GCPAD_START_BUTTON, 0.0, 1.0},
    PadField{"dpad_up", ControlID::GCPAD_DPAD_UP, 0.0, 1.0},
    PadField{"dpad_down", ControlID::GCPAD_DPAD_DOWN, 0.0, 1.0},
    PadField{"dpad_left", ControlID::GCPAD_DPAD_LEFT, 0.0, 1.0},
    PadField{"dpad_right", ControlID::GCPAD_DPAD_RIGHT, 0.0, 1.0},
    PadField{"l", ControlID::GCPAD_L_DIGITAL, 0.0, 1.0},
    PadField{"r", ControlID::GCPAD_R_DIGITAL, 0.0, 1.0},
    PadField{"l_analog", ControlID::GCPAD_L_ANALOG, 0.0, 1.0},
    PadField{"r_analog", ControlID::GCPAD_R_ANALOG, 0.0, 1.0},
    PadField{"main_x", ControlID::GCPAD_MAIN_STICK_X, -1.0, 1.0},
    PadField{"main_y", ControlID::GCPAD_MAIN_STICK_Y, -1.0, 1.0},
    PadField{"c_x", ControlID::GCPAD_C_STICK_X, -1.0, 1.0},
    PadField{"c_y", ControlID::GCPAD_C_STICK_Y, -1.0, 1.0},

    // Wii controls. Prefixed so they cannot be confused with the GameCube
    // names above: a Wii title needs wii_a for its A button, and "a" alone
    // drives a GameCube pad that such a game never reads.
    PadField{"wii_a", ControlID::WIIMOTE_A_BUTTON, 0.0, 1.0},
    PadField{"wii_b", ControlID::WIIMOTE_B_BUTTON, 0.0, 1.0},
    PadField{"wii_1", ControlID::WIIMOTE_ONE_BUTTON, 0.0, 1.0},
    PadField{"wii_2", ControlID::WIIMOTE_TWO_BUTTON, 0.0, 1.0},
    PadField{"wii_plus", ControlID::WIIMOTE_PLUS_BUTTON, 0.0, 1.0},
    PadField{"wii_minus", ControlID::WIIMOTE_MINUS_BUTTON, 0.0, 1.0},
    PadField{"wii_home", ControlID::WIIMOTE_HOME_BUTTON, 0.0, 1.0},
    PadField{"wii_dpad_up", ControlID::WIIMOTE_DPAD_UP, 0.0, 1.0},
    PadField{"wii_dpad_down", ControlID::WIIMOTE_DPAD_DOWN, 0.0, 1.0},
    PadField{"wii_dpad_left", ControlID::WIIMOTE_DPAD_LEFT, 0.0, 1.0},
    PadField{"wii_dpad_right", ControlID::WIIMOTE_DPAD_RIGHT, 0.0, 1.0},
    PadField{"wii_ir_x", ControlID::WIIMOTE_IR_X, -1.0, 1.0},
    PadField{"wii_ir_y", ControlID::WIIMOTE_IR_Y, -1.0, 1.0},
    PadField{"nunchuk_c", ControlID::NUNCHUK_C_BUTTON, 0.0, 1.0},
    PadField{"nunchuk_z", ControlID::NUNCHUK_Z_BUTTON, 0.0, 1.0},
    PadField{"nunchuk_x", ControlID::NUNCHUK_STICK_X, -1.0, 1.0},
    PadField{"nunchuk_y", ControlID::NUNCHUK_STICK_Y, -1.0, 1.0},
};

std::string Trim(std::string_view value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::optional<double> ParseDouble(std::string_view value)
{
  const std::string text = Trim(value);
  if (text.empty())
    return std::nullopt;
  char* end = nullptr;
  const double parsed = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size() || !std::isfinite(parsed))
    return std::nullopt;
  return parsed;
}

std::optional<int> ParsePort(std::string_view value)
{
  const std::string text = Trim(value);
  int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed < 0 || parsed > 3)
  {
    return std::nullopt;
  }
  return parsed;
}

std::optional<std::uint32_t> ParseUnsigned(std::string_view value)
{
  std::string text = Trim(value);
  if (text.empty())
    return std::nullopt;

  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X"))
  {
    base = 16;
    text.erase(0, 2);
  }
  if (text.empty())
    return std::nullopt;

  std::uint32_t parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed, base);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    return std::nullopt;
  return parsed;
}

std::optional<std::vector<std::uint8_t>> ParseHexBytes(std::string_view value)
{
  std::string text;
  text.reserve(value.size());
  for (const char character : value)
  {
    if (character != ' ' && character != '\t' && character != '_' && character != '-')
      text.push_back(character);
  }
  if (text.starts_with("0x") || text.starts_with("0X"))
    text.erase(0, 2);
  if (text.empty() || text.size() % 2 != 0)
    return std::nullopt;

  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t offset = 0; offset < text.size(); offset += 2)
  {
    unsigned int byte = 0;
    const auto result =
        std::from_chars(text.data() + offset, text.data() + offset + 2, byte, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + offset + 2)
      return std::nullopt;
    bytes.push_back(static_cast<std::uint8_t>(byte));
  }
  return bytes;
}

std::optional<CommandType> ParseCommandType(std::string_view value)
{
  const std::string text = Trim(value);
  if (text == "pad")
    return CommandType::Pad;
  if (text == "pad_frames")
    return CommandType::PadFrames;
  if (text == "clear_pad")
    return CommandType::ClearPad;
  if (text == "pause")
    return CommandType::Pause;
  if (text == "resume")
    return CommandType::Resume;
  if (text == "save_state")
    return CommandType::SaveState;
  if (text == "load_state")
    return CommandType::LoadState;
  if (text == "screenshot")
    return CommandType::Screenshot;
  if (text == "read_memory")
    return CommandType::ReadMemory;
  if (text == "write_memory")
    return CommandType::WriteMemory;
  if (text == "stop")
    return CommandType::Stop;
  return std::nullopt;
}

const PadField* FindPadField(std::string_view name)
{
  const auto it = std::ranges::find(kPadFields, name, &PadField::name);
  return it == kPadFields.end() ? nullptr : &*it;
}

std::map<std::string, std::string> ParseKeyValueLines(const std::filesystem::path& path,
                                                      std::string* error)
{
  std::ifstream input(path);
  if (!input) {
    if (error)
      *error = "could not open command file";
    return {};
  }

  std::map<std::string, std::string> values;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line))
  {
    ++line_number;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.starts_with('#'))
      continue;
    const auto equals = trimmed.find('=');
    if (equals == std::string::npos)
    {
      if (error)
        *error = "line " + std::to_string(line_number) + " must be key=value";
      return {};
    }
    const std::string key = Trim(trimmed.substr(0, equals));
    const std::string value = Trim(trimmed.substr(equals + 1));
    if (key.empty())
    {
      if (error)
        *error = "line " + std::to_string(line_number) + " has an empty key";
      return {};
    }
    values[key] = value;
  }
  return values;
}

bool ParsePadCommand(const std::map<std::string, std::string>& values, Command* command,
                     bool require_frames, std::string* error)
{
  const auto port_it = values.find("port");
  if (port_it == values.end())
  {
    if (error)
      *error = "pad commands require port=0..3";
    return false;
  }
  const auto port = ParsePort(port_it->second);
  if (!port)
  {
    if (error)
      *error = "port must be between 0 and 3";
    return false;
  }

  command->pad = {};
  command->pad.port = *port;
  if (require_frames)
  {
    const auto frames_it = values.find("frames");
    const auto frames =
        frames_it == values.end() ? std::nullopt : ParseUnsigned(frames_it->second);
    if (!frames || *frames == 0 || *frames > 36000)
    {
      if (error)
        *error = "pad_frames commands require frames=1..36000";
      return false;
    }
    command->frames = *frames;
  }

  for (const auto& [key, value] : values)
  {
    if (key == "command" || key == "port" || (require_frames && key == "frames"))
      continue;
    const PadField* field = FindPadField(key);
    if (!field)
    {
      if (error)
        *error = "unknown pad field: " + key;
      return false;
    }
    const auto parsed = ParseDouble(value);
    if (!parsed || *parsed < field->min_value || *parsed > field->max_value)
    {
      if (error)
      {
        std::ostringstream message;
        message << key << " must be between " << field->min_value << " and "
                << field->max_value;
        *error = message.str();
      }
      return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(field->control) -
        static_cast<std::size_t>(ControlID::FIRST_GC_CONTROL);
    command->pad.controls[index] = *parsed;
  }
  return true;
}

bool ParseReadMemoryCommand(const std::map<std::string, std::string>& values, Command* command,
                            std::string* error)
{
  constexpr std::uint32_t max_read_size = 16 * 1024 * 1024;
  for (const auto& [key, value] : values)
  {
    if (key != "command" && key != "address" && key != "size" && key != "path")
    {
      if (error)
        *error = "unknown read_memory field: " + key;
      return false;
    }
  }

  const auto address_it = values.find("address");
  const auto size_it = values.find("size");
  const auto path_it = values.find("path");
  if (address_it == values.end() || size_it == values.end() || path_it == values.end() ||
      Trim(path_it->second).empty())
  {
    if (error)
      *error = "read_memory requires address=<guest address>, size=<bytes>, and path=<file>";
    return false;
  }

  const auto address = ParseUnsigned(address_it->second);
  const auto size = ParseUnsigned(size_it->second);
  if (!address)
  {
    if (error)
      *error = "address must be an unsigned decimal or 0x-prefixed hexadecimal value";
    return false;
  }
  if (!size || *size == 0 || *size > max_read_size)
  {
    if (error)
      *error = "size must be between 1 and 16777216 bytes";
    return false;
  }
  if (*address > UINT32_MAX - (*size - 1))
  {
    if (error)
      *error = "address and size exceed the guest address space";
    return false;
  }

  command->address = *address;
  command->size = *size;
  command->path = Trim(path_it->second);
  return true;
}

bool ParseWriteMemoryCommand(const std::map<std::string, std::string>& values, Command* command,
                             std::string* error)
{
  constexpr std::size_t max_write_size = 4096;
  for (const auto& [key, value] : values)
  {
    if (key != "command" && key != "address" && key != "data")
    {
      if (error)
        *error = "unknown write_memory field: " + key;
      return false;
    }
  }

  const auto address_it = values.find("address");
  const auto data_it = values.find("data");
  if (address_it == values.end() || data_it == values.end())
  {
    if (error)
      *error = "write_memory requires address=<guest address> and data=<hex bytes>";
    return false;
  }

  const auto address = ParseUnsigned(address_it->second);
  const auto data = ParseHexBytes(data_it->second);
  if (!address)
  {
    if (error)
      *error = "address must be an unsigned decimal or 0x-prefixed hexadecimal value";
    return false;
  }
  if (!data || data->empty() || data->size() > max_write_size)
  {
    if (error)
      *error = "data must contain between 1 and 4096 hexadecimal bytes";
    return false;
  }
  if (*address > UINT32_MAX - static_cast<std::uint32_t>(data->size() - 1))
  {
    if (error)
      *error = "address and data exceed the guest address space";
    return false;
  }

  command->address = *address;
  command->data = *data;
  command->size = static_cast<std::uint32_t>(data->size());
  return true;
}
}  // namespace

std::filesystem::path ResolveControlPath(const std::filesystem::path& automation_directory,
                                         const std::filesystem::path& value)
{
  return value.is_absolute() ? value : automation_directory / value;
}

std::vector<std::filesystem::path>
ListCommandFiles(const std::filesystem::path& commands_directory)
{
  std::vector<std::filesystem::path> result;
  std::error_code ec;
  if (!std::filesystem::is_directory(commands_directory, ec))
    return result;

  for (const auto& entry : std::filesystem::directory_iterator(commands_directory, ec))
  {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    result.push_back(entry.path());
  }

  std::ranges::sort(result);
  return result;
}

bool ParseCommandFile(const std::filesystem::path& path, Command* command, std::string* error)
{
  const auto values = ParseKeyValueLines(path, error);
  if (values.empty())
  {
    if (error && error->empty())
      *error = "command file is empty";
    return false;
  }

  const auto command_it = values.find("command");
  if (command_it == values.end())
  {
    if (error)
      *error = "command=<name> is required";
    return false;
  }

  const auto type = ParseCommandType(command_it->second);
  if (!type)
  {
    if (error)
      *error = "unknown command: " + command_it->second;
    return false;
  }

  Command parsed{};
  parsed.type = *type;
  parsed.source_name = path.filename().string();

  switch (*type)
  {
  case CommandType::Pad:
    if (!ParsePadCommand(values, &parsed, false, error))
      return false;
    break;
  case CommandType::PadFrames:
    if (!ParsePadCommand(values, &parsed, true, error))
      return false;
    break;
  case CommandType::ClearPad:
  {
    const auto port_it = values.find("port");
    if (port_it == values.end())
    {
      if (error)
        *error = "clear_pad commands require port=0..3";
      return false;
    }
    const auto port = ParsePort(port_it->second);
    if (!port)
    {
      if (error)
        *error = "port must be between 0 and 3";
      return false;
    }
    parsed.pad.port = *port;
    break;
  }
  case CommandType::SaveState:
  case CommandType::LoadState:
  case CommandType::Screenshot:
  {
    const auto path_it = values.find("path");
    if (path_it == values.end() || Trim(path_it->second).empty())
    {
      if (error)
        *error = "path=<file> is required";
      return false;
    }
    parsed.path = Trim(path_it->second);
    break;
  }
  case CommandType::ReadMemory:
    if (!ParseReadMemoryCommand(values, &parsed, error))
      return false;
    break;
  case CommandType::WriteMemory:
    if (!ParseWriteMemoryCommand(values, &parsed, error))
      return false;
    break;
  case CommandType::Pause:
  case CommandType::Resume:
  case CommandType::Stop:
    break;
  }

  *command = std::move(parsed);
  return true;
}

std::string FormatStatus(const Status& status)
{
  std::ostringstream output;
  output << "state=" << status.state << '\n';
  output << "booted=" << (status.booted ? "1" : "0") << '\n';
  output << "fps=" << status.fps << '\n';
  output << "vps=" << status.vps << '\n';
  output << "speed=" << status.speed << '\n';
  output << "frame_count=" << status.frame_count << '\n';
  output << "present_count=" << status.present_count << '\n';
  output << "title=" << status.title << '\n';
  output << "game_id=" << status.game_id << '\n';
  output << "game_name=" << status.game_name << '\n';
  output << "navigation_mode=" << status.navigation_mode << '\n';
  output << "navigation_valid=" << (status.navigation_valid ? "1" : "0")
         << '\n';
  output << "navigation_room_id=" << status.navigation_room_id << '\n';
  output << "navigation_player_x=" << status.navigation_player_x << '\n';
  output << "navigation_player_z=" << status.navigation_player_z << '\n';
  output << "navigation_facing=" << status.navigation_facing << '\n';
  output << "navigation_collision_valid="
         << (status.navigation_collision_valid ? "1" : "0") << '\n';
  output << "navigation_collision_base=" << status.navigation_collision_base << '\n';
  output << "navigation_collision_segments="
         << status.navigation_collision_segments << '\n';
  output << "navigation_npc_count=" << status.navigation_npc_count << '\n';
  output << "navigation_archive=" << status.navigation_archive << '\n';
  output << "navigation_location=" << status.navigation_location << '\n';
  output << "processed_commands=" << status.processed_commands << '\n';
  output << "last_command=" << status.last_command << '\n';
  output << "last_error=" << status.last_error << '\n';
  return output.str();
}
}  // namespace moderngekko::automation
