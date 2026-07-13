#include "frontend_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
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

std::optional<fs::path> FindDolphinControllerConfig()
{
  std::vector<fs::path> candidates;
  if (const char* explicit_dir = std::getenv("DOLPHIN_USER_DIR"))
  {
    candidates.emplace_back(fs::path(explicit_dir) / "WiimoteNew.ini");
    candidates.emplace_back(fs::path(explicit_dir) / "Config" / "WiimoteNew.ini");
  }
  if (const char* home = std::getenv("HOME"))
  {
    const fs::path root(home);
    candidates.emplace_back(root / ".var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu/WiimoteNew.ini");
    candidates.emplace_back(root / ".config/dolphin-emu/WiimoteNew.ini");
    candidates.emplace_back(root / ".local/share/dolphin-emu/Config/WiimoteNew.ini");
  }
  for (const fs::path& candidate : candidates)
  {
    if (fs::is_regular_file(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::string NormalizeController(std::istream& input)
{
  std::vector<std::string> wiimote;
  bool in_wiimote_one = false;
  std::string line;
  while (std::getline(input, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.starts_with('[') && trimmed.ends_with(']'))
    {
      in_wiimote_one = trimmed == "[Wiimote1]";
      continue;
    }
    if (!in_wiimote_one || trimmed.empty())
      continue;

    const std::size_t separator = trimmed.find('=');
    const std::string key = Trim(trimmed.substr(0, separator));
    if (key.starts_with("Nunchuk/") || key == "Extension" ||
        key == "Options/Sideways Wiimote")
    {
      continue;
    }
    wiimote.push_back(line);
  }

  std::ostringstream output;
  output << "[Wiimote1]\n";
  for (const std::string& setting : wiimote)
    output << setting << '\n';
  output << "Extension = None\n"
            "Options/Sideways Wiimote = True\n"
            "[Wiimote2]\n"
            "[Wiimote3]\n"
            "[Wiimote4]\n"
            "[BalanceBoard]\n";
  return output.str();
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
    if (!SaveConfig(user_directory, "1920x1080", &error))
      return {.error = std::move(error)};
  }

  std::ifstream file(path);
  if (!file)
    return {.error = "can't open " + path.string()};

  std::string resolution;
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
    if (Lower(Trim(trimmed.substr(0, separator))) == "resolution")
      resolution = Lower(Trim(trimmed.substr(separator + 1)));
  }
  if (resolution.empty())
    return {.error = "config.ini is missing resolution=<width>x<height>"};

  for (const ResolutionOption& option : SupportedResolutions())
  {
    if (resolution == option.text)
      return {.dolphin_scale = option.dolphin_scale, .resolution = std::move(resolution)};
  }

  // Dolphin also accepts exact raw EFB multiples even when they do not have a common display label.
  for (int scale = 1; scale <= 12; ++scale)
  {
    const std::string raw = std::to_string(640 * scale) + "x" + std::to_string(528 * scale);
    if (resolution == raw)
      return {.dolphin_scale = scale, .resolution = std::move(resolution)};
  }

  return {.error = "unsupported Dolphin internal resolution '" + resolution +
                   "'; use a listed display resolution or an exact 640x528 multiple up to 12x"};
}

bool SaveConfig(const fs::path& user_directory, std::string_view resolution, std::string* error)
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
          "resolution=" << resolution << '\n';
  return true;
}

bool ImportDolphinController(const fs::path& user_directory, std::string* message)
{
  const fs::path destination = user_directory / "Config" / "WiimoteNew.ini";
  std::optional<fs::path> source = FindDolphinControllerConfig();
  if (!source && fs::is_regular_file(destination))
    source = destination;
  if (!source)
  {
    if (message)
      *message = "no Dolphin WiimoteNew.ini was found";
    return false;
  }

  std::ifstream input(*source);
  if (!input)
  {
    if (message)
      *message = "can't open Dolphin controller profile " + source->string();
    return false;
  }
  const std::string normalized = NormalizeController(input);
  if (normalized.find("Device =") == std::string::npos)
  {
    if (message)
      *message = "Dolphin controller profile has no Wiimote1 device";
    return false;
  }

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
  output << normalized;
  if (message)
    *message = "imported " + source->string() + " as Sideways Wii Remote (no extension)";
  return true;
}
}  // namespace moderngekko::frontend
