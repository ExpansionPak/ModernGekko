#include "frontend_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
  namespace fs = std::filesystem;
  const fs::path directory =
      fs::temp_directory_path() /
      ("moderngekko-frontend-config-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  std::string error;
  const std::string controller = "SDL/0/Test Controller";
  if (!moderngekko::frontend::SaveConfig(directory, "1920x1080", false, controller, &error))
    return 1;

  const auto loaded = moderngekko::frontend::LoadConfig(directory, false);
  if (!loaded || loaded.dolphin_scale != 3 || loaded.show_fps_in_title ||
      loaded.controller != controller)
  {
    return 2;
  }

  if (!moderngekko::frontend::GenerateControllerConfig(directory, controller, &error))
    return 3;
  if (moderngekko::frontend::ReadConfiguredController(directory) != controller)
    return 4;

  std::ifstream input(directory / "Config" / "WiimoteNew.ini");
  const std::string generated{std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()};
  if (!generated.contains("Buttons/A = `Shoulder L`\n") ||
      !generated.contains("Buttons/1 = `Button W`\n") ||
      !generated.contains("Buttons/2 = `Button S`\n") ||
      !generated.contains("Shake/X = `Trigger L`\n") ||
      !generated.contains("Extension = None\n") ||
      !generated.contains("Options/Sideways Wiimote = True\n") ||
      generated.contains("Nunchuk/"))
  {
    return 5;
  }

  fs::remove_all(directory);
  return 0;
}
