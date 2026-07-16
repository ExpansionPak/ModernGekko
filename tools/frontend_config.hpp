#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
struct ResolutionOption
{
  const char* text;
  int dolphin_scale;
};

struct ConfigResult
{
  int dolphin_scale = 0;
  std::string resolution;
  std::string controller;
  bool show_fps_in_title = true;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

const std::vector<ResolutionOption>& SupportedResolutions();
ConfigResult LoadConfig(const std::filesystem::path& user_directory, bool create_if_missing);
bool SaveConfig(const std::filesystem::path& user_directory, std::string_view resolution,
                bool show_fps_in_title, std::string_view controller, std::string* error);
std::string ReadConfiguredController(const std::filesystem::path& user_directory);
bool GenerateControllerConfig(const std::filesystem::path& user_directory,
                              std::string_view controller, std::string* message);
}  // namespace moderngekko::frontend
