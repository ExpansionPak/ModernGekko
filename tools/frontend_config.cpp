#include "frontend_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace moderngekko::frontend
{
namespace
{
std::string Trim(std::string value)
{
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string Lower(std::string value)
{
  std::ranges::transform(value, value.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

const std::vector<ResolutionOption>& SupportedResolutions()
{
  // These are the output-resolution labels used by Dolphin's integer EFB scales.
  static const std::vector<ResolutionOption> resolutions = {
      {"640x528", 1},   {"1280x720", 2},  {"1920x1080", 3}, {"2560x1440", 4},
      {"3840x2160", 6}, {"5120x2880", 8}, {"7680x4320", 12},
  };
  return resolutions;
}

ConfigResult LoadConfig(const fs::path& user_directory, bool create_if_missing)
{
  const fs::path path = user_directory / "config.ini";
  if (!fs::exists(path) && create_if_missing)
  {
    std::string error;
    if (!SaveConfig(user_directory, "1920x1080", true, {}, &error))
      return {.error = std::move(error)};
  }

  std::ifstream file(path);
  if (!file)
    return {.error = "can't open " + path.string()};

  std::string resolution;
  std::string controller;
  bool show_fps_in_title = true;
  std::string line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[')
      continue;
    const std::size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
      return {.error = "invalid config.ini line: " + trimmed};
    const std::string key = Lower(Trim(trimmed.substr(0, separator)));
    const std::string raw_value = Trim(trimmed.substr(separator + 1));
    const std::string value = Lower(raw_value);
    if (key == "resolution")
      resolution = value;
    else if (key == "controller")
      controller = raw_value;
    else if (key == "show_fps_in_title")
    {
      if (value == "true" || value == "1" || value == "yes" || value == "on")
        show_fps_in_title = true;
      else if (value == "false" || value == "0" || value == "no" || value == "off")
        show_fps_in_title = false;
      else
        return {.error = "show_fps_in_title must be true or false"};
    }
  }
  if (resolution.empty())
    return {.error = "config.ini is missing resolution=<width>x<height>"};

  for (const ResolutionOption& option : SupportedResolutions())
  {
    if (resolution == option.text)
      return {.dolphin_scale = option.dolphin_scale,
              .resolution = std::move(resolution),
              .controller = std::move(controller),
              .show_fps_in_title = show_fps_in_title};
  }

  // Dolphin also accepts exact raw EFB multiples even when they do not have a common display label.
  for (int scale = 1; scale <= 12; ++scale)
  {
    const std::string raw = std::to_string(640 * scale) + "x" + std::to_string(528 * scale);
    if (resolution == raw)
      return {.dolphin_scale = scale,
              .resolution = std::move(resolution),
              .controller = std::move(controller),
              .show_fps_in_title = show_fps_in_title};
  }

  return {.error = "unsupported Dolphin internal resolution '" + resolution +
                   "'; use a listed display resolution or an exact 640x528 multiple up to 12x"};
}

bool SaveConfig(const fs::path& user_directory, std::string_view resolution,
                bool show_fps_in_title, std::string_view controller, std::string* error)
{
  std::error_code ec;
  fs::create_directories(user_directory, ec);
  if (ec)
  {
    if (error)
      *error = "can't create user directory: " + ec.message();
    return false;
  }
  std::ofstream file(user_directory / "config.ini", std::ios::trunc);
  if (!file)
  {
    if (error)
      *error = "can't write " + (user_directory / "config.ini").string();
    return false;
  }
  file << "# ModernGekko frontend settings\n"
          "# This is Dolphin's internal render target, not the window size.\n"
          "[Video]\n"
          "resolution=" << resolution << '\n'
       << "show_fps_in_title=" << (show_fps_in_title ? "true" : "false") << '\n'
       << "[Input]\n"
       << "controller=" << controller << '\n';
  return true;
}

std::string ReadConfiguredController(const fs::path& user_directory)
{
  std::ifstream input(user_directory / "Config" / "WiimoteNew.ini");
  std::string line;
  bool wiimote_one = false;
  while (std::getline(input, line))
  {
    const std::string trimmed = Trim(line);
    if (trimmed.starts_with('[') && trimmed.ends_with(']'))
    {
      wiimote_one = trimmed == "[Wiimote1]";
      continue;
    }
    if (!wiimote_one)
      continue;
    const std::size_t separator = trimmed.find('=');
    if (separator != std::string::npos && Trim(trimmed.substr(0, separator)) == "Device")
      return Trim(trimmed.substr(separator + 1));
  }
  return {};
}

bool GenerateControllerConfig(const fs::path& user_directory, std::string_view controller,
                              std::string* message)
{
  if (controller.empty() || controller.find_first_of("\r\n") != std::string_view::npos)
  {
    if (message)
      *message = "select a connected SDL gamepad";
    return false;
  }

  const fs::path destination = user_directory / "Config" / "WiimoteNew.ini";
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec)
  {
    if (message)
      *message = "can't create controller config directory: " + ec.message();
    return false;
  }
  std::ofstream output(destination, std::ios::trunc);
  if (!output)
  {
    if (message)
      *message = "can't write " + destination.string();
    return false;
  }
  output << "[Wiimote1]\n"
         << "Device = " << controller << '\n'
         << "Buttons/A = `Shoulder L`\n"
            "Buttons/B = `Shoulder R`\n"
            "Buttons/1 = `Button W`\n"
            "Buttons/2 = `Button S`\n"
            "Buttons/- = Back\n"
            "Buttons/+ = Start\n"
            "Buttons/Home = Guide\n"
            "D-Pad/Up = `Pad N`\n"
            "D-Pad/Down = `Pad S`\n"
            "D-Pad/Left = `Pad W`\n"
            "D-Pad/Right = `Pad E`\n"
            "IR/Up = `Cursor Y-`\n"
            "IR/Down = `Cursor Y+`\n"
            "IR/Left = `Cursor X-`\n"
            "IR/Right = `Cursor X+`\n"
            "Shake/X = `Trigger L`\n"
            "Shake/Y = `Trigger R`\n"
            "Shake/Z = `Trigger L`\n"
            "IRPassthrough/Object 1 X = `IR Object 1 X`\n"
            "IRPassthrough/Object 1 Y = `IR Object 1 Y`\n"
            "IRPassthrough/Object 1 Size = `IR Object 1 Size`\n"
            "IRPassthrough/Object 2 X = `IR Object 2 X`\n"
            "IRPassthrough/Object 2 Y = `IR Object 2 Y`\n"
            "IRPassthrough/Object 2 Size = `IR Object 2 Size`\n"
            "IRPassthrough/Object 3 X = `IR Object 3 X`\n"
            "IRPassthrough/Object 3 Y = `IR Object 3 Y`\n"
            "IRPassthrough/Object 3 Size = `IR Object 3 Size`\n"
            "IRPassthrough/Object 4 X = `IR Object 4 X`\n"
            "IRPassthrough/Object 4 Y = `IR Object 4 Y`\n"
            "IRPassthrough/Object 4 Size = `IR Object 4 Size`\n"
            "IMUAccelerometer/Up = `Accel Up`\n"
            "IMUAccelerometer/Down = `Accel Down`\n"
            "IMUAccelerometer/Left = `Accel Left`\n"
            "IMUAccelerometer/Right = `Accel Right`\n"
            "IMUAccelerometer/Forward = `Accel Forward`\n"
            "IMUAccelerometer/Backward = `Accel Backward`\n"
            "IMUGyroscope/Pitch Up = `Gyro Pitch Up`\n"
            "IMUGyroscope/Pitch Down = `Gyro Pitch Down`\n"
            "IMUGyroscope/Roll Left = `Gyro Roll Left`\n"
            "IMUGyroscope/Roll Right = `Gyro Roll Right`\n"
            "IMUGyroscope/Yaw Left = `Gyro Yaw Left`\n"
            "IMUGyroscope/Yaw Right = `Gyro Yaw Right`\n"
            "Rumble/Motor = Motor\n"
            "Extension = None\n"
            "Options/Sideways Wiimote = True\n"
            "[Wiimote2]\n"
            "[Wiimote3]\n"
            "[Wiimote4]\n"
            "[BalanceBoard]\n";
  if (!output)
  {
    if (message)
      *message = "can't write " + destination.string();
    return false;
  }
  if (message)
    *message = "Sideways Wii Remote mapped to " + std::string(controller);
  return true;
}
}  // namespace moderngekko::frontend
