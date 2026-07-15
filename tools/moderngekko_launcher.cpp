#include "frontend_config.hpp"
#include "moderngekko/game.hpp"

#include "DiscIO/DiscExtractor.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
#ifndef MODERNGEKKO_FRONTEND_NAME
#define MODERNGEKKO_FRONTEND_NAME "ModernGekko"
#endif

#ifndef MODERNGEKKO_RUNNER_FILENAME
#define MODERNGEKKO_RUNNER_FILENAME "moderngekko-run"
#endif

#ifndef MODERNGEKKO_USER_DIRECTORY_NAME
#define MODERNGEKKO_USER_DIRECTORY_NAME "moderngekko"
#endif

struct ExtractionState
{
  std::atomic<bool> running{false};
  std::atomic<unsigned> completed{0};
  std::atomic<unsigned> total{1};
  std::mutex mutex;
  std::string status;
  std::string error;
  std::optional<fs::path> finished_game;
};

struct DialogState
{
  std::mutex mutex;
  std::optional<fs::path> selected;
  std::string error;
};

fs::path DefaultUserDirectory()
{
  if (const char* xdg = std::getenv("XDG_DATA_HOME"))
    return fs::path(xdg) / MODERNGEKKO_USER_DIRECTORY_NAME;
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / ".local/share" / MODERNGEKKO_USER_DIRECTORY_NAME;
  return std::string(MODERNGEKKO_USER_DIRECTORY_NAME) + "-user";
}

fs::path DocumentsDirectory()
{
  if (const char* home = std::getenv("HOME"))
    return fs::path(home) / "Documents";
  return fs::current_path();
}

fs::path ReadDefaultGame(const fs::path& user_directory)
{
  std::ifstream file(user_directory / "default-game.txt");
  std::string value;
  std::getline(file, value);
  if (!value.empty() && value.back() == '\r')
    value.pop_back();
  return value;
}

bool WriteDefaultGame(const fs::path& user_directory, const fs::path& game, std::string* error)
{
  std::error_code ec;
  fs::create_directories(user_directory, ec);
  std::ofstream file(user_directory / "default-game.txt", std::ios::trunc);
  if (!file)
  {
    if (error)
      *error = "can't save default-game.txt";
    return false;
  }
  file << game.string() << '\n';
  return true;
}

std::vector<fs::path> FindDiscImages()
{
  std::vector<fs::path> images;
  std::error_code ec;
  const fs::path documents = DocumentsDirectory();
  if (!fs::is_directory(documents, ec))
    return images;
  fs::recursive_directory_iterator iterator(
      documents, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (iterator != end)
  {
    if (iterator.depth() > 4)
      iterator.disable_recursion_pending();
    if (iterator->is_regular_file(ec))
    {
      std::string extension = iterator->path().extension().string();
      std::ranges::transform(extension, extension.begin(),
                             [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (extension == ".wbfs" || extension == ".iso")
        images.push_back(iterator->path());
    }
    iterator.increment(ec);
    if (ec)
      ec.clear();
  }
  std::ranges::sort(images);
  return images;
}

bool ExtractDisc(const fs::path& image, const fs::path& user_directory, ExtractionState* state)
{
  auto fail = [&](std::string message) {
    std::lock_guard lock(state->mutex);
    state->error = std::move(message);
    state->running = false;
    return false;
  };

  {
    std::lock_guard lock(state->mutex);
    state->status = "Opening " + image.filename().string();
  }
  std::unique_ptr<DiscIO::Volume> volume = DiscIO::CreateVolume(image.string());
  if (!volume)
    return fail("Dolphin rejected the selected WBFS/ISO");

  const DiscIO::Partition partition = volume->GetGamePartition();
  const DiscIO::FileSystem* filesystem = volume->GetFileSystem(partition);
  if (!filesystem || !filesystem->IsValid())
    return fail("Dolphin could not read the game partition filesystem");

  std::string disc_id = volume->GetGameID(partition);
  if (disc_id.size() != 6)
    return fail("the selected image has an invalid disc ID");
#ifdef MODERNGEKKO_REQUIRED_DISC_ID
  if (disc_id != MODERNGEKKO_REQUIRED_DISC_ID)
    return fail("this frontend requires disc ID " MODERNGEKKO_REQUIRED_DISC_ID "; selected " +
                disc_id);
#endif
  const fs::path games_directory = user_directory / "games";
  const fs::path output = games_directory / disc_id;
  if (moderngekko::InspectGame(output))
  {
    std::string error;
    if (!WriteDefaultGame(user_directory, output, &error))
      return fail(error);
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = "Using existing extraction";
    state->running = false;
    return true;
  }

  const fs::path staging = games_directory / (disc_id + ".extracting");
  std::error_code ec;
  fs::remove_all(staging, ec);
  fs::create_directories(staging / "files", ec);
  if (ec)
    return fail("can't create extraction directory: " + ec.message());

  {
    std::lock_guard lock(state->mutex);
    state->status = "Extracting system data";
  }
  if (!DiscIO::ExportSystemData(*volume, partition, staging.string()))
  {
    fs::remove_all(staging, ec);
    return fail("Dolphin failed while extracting the disc system data");
  }

  state->total = std::max(1u, filesystem->GetRoot().GetTotalChildren());
  {
    std::lock_guard lock(state->mutex);
    state->status = "Extracting game files";
  }
  DiscIO::ExportDirectory(
      *volume, partition, filesystem->GetRoot(), true, "", (staging / "files").string(),
      [state](const std::string& path) {
        ++state->completed;
        std::lock_guard lock(state->mutex);
        state->status = "Extracting " + path;
        return false;
      });

  const auto inspected = moderngekko::InspectGame(staging);
  if (!inspected)
  {
    fs::remove_all(staging, ec);
    return fail("extracted game validation failed: " + inspected.error);
  }

  fs::remove_all(output, ec);
  ec.clear();
  fs::rename(staging, output, ec);
  if (ec)
    return fail("can't publish extracted game: " + ec.message());
  std::string error;
  if (!WriteDefaultGame(user_directory, output, &error))
    return fail(error);

  {
    std::lock_guard lock(state->mutex);
    state->finished_game = output;
    state->status = "Extraction complete";
  }
  state->running = false;
  return true;
}

void SDLCALL FileDialogCallback(void* userdata, const char* const* filelist, int)
{
  auto* state = static_cast<DialogState*>(userdata);
  std::lock_guard lock(state->mutex);
  if (!filelist)
    state->error = SDL_GetError();
  else if (filelist[0])
    state->selected = filelist[0];
}

std::string Quote(const fs::path& path)
{
  std::string result = "'";
  for (char c : path.string())
    result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
}

fs::path SiblingRunner(const char* argv0)
{
  std::error_code ec;
  const fs::path self = fs::weakly_canonical(argv0, ec);
  const fs::path sibling = self.parent_path() / MODERNGEKKO_RUNNER_FILENAME;
  return fs::is_regular_file(sibling) ? sibling : fs::path(MODERNGEKKO_RUNNER_FILENAME);
}
}  // namespace

int main(int argc, char** argv)
{
  bool use_wayland = false;
  std::optional<fs::path> extract_only;
  for (int i = 1; i < argc; ++i)
  {
    if (std::string_view(argv[i]) == "-X11" || std::string_view(argv[i]) == "--x11")
      use_wayland = false;
    else if (std::string_view(argv[i]) == "--wayland")
      use_wayland = true;
    else if (std::string_view(argv[i]) == "--extract" && i + 1 < argc)
      extract_only = argv[++i];
  }

  const fs::path user_directory = DefaultUserDirectory();
  if (extract_only)
  {
    ExtractionState extraction;
    extraction.running = true;
    const bool success = ExtractDisc(*extract_only, user_directory, &extraction);
    std::lock_guard lock(extraction.mutex);
    if (!success)
      std::cerr << "extraction failed: " << extraction.error << '\n';
    else
      std::cout << extraction.status << ": " << *extraction.finished_game << '\n';
    return success ? 0 : 1;
  }

  auto config = moderngekko::frontend::LoadConfig(user_directory, true);
  if (!config)
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Invalid " MODERNGEKKO_FRONTEND_NAME " config.ini",
                             config.error.c_str(), nullptr);
    return 2;
  }
  std::string controller_status;
  moderngekko::frontend::ImportDolphinController(user_directory, &controller_status);

  // X11 is the conservative default while the native Wayland path remains opt-in.
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, use_wayland ? "wayland" : "x11");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    return 1;

  const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  SDL_Window* window = SDL_CreateWindow(
      MODERNGEKKO_FRONTEND_NAME, static_cast<int>(820 * scale), static_cast<int>(560 * scale),
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
  {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, "vulkan");
  if (!renderer)
    renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer)
  {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetStyle().FontScaleDpi = scale;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  std::vector<fs::path> images = FindDiscImages();
  std::optional<fs::path> selected_image = images.empty() ? std::nullopt : std::optional(images[0]);
  fs::path current_game = ReadDefaultGame(user_directory);
  auto current_metadata = moderngekko::InspectGame(current_game);
  const auto& resolutions = moderngekko::frontend::SupportedResolutions();
  int resolution_index = 0;
  for (std::size_t i = 0; i < resolutions.size(); ++i)
  {
    if (config.resolution == resolutions[i].text)
      resolution_index = static_cast<int>(i);
  }

  DialogState dialog;
  ExtractionState extraction;
  std::jthread extraction_thread;
  bool done = false;
  bool launch = false;
  while (!done)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        done = true;
    }

    {
      std::lock_guard lock(dialog.mutex);
      if (dialog.selected)
      {
        selected_image = std::move(dialog.selected);
        dialog.selected.reset();
      }
    }
    {
      std::lock_guard lock(extraction.mutex);
      if (extraction.finished_game)
      {
        current_game = *extraction.finished_game;
        launch = true;
        done = true;
      }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(MODERNGEKKO_FRONTEND_NAME " Launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(MODERNGEKKO_FRONTEND_NAME);
    ImGui::Separator();

    if (current_metadata)
    {
      ImGui::Text("Ready: %s [%s]", current_metadata.metadata->game_name.c_str(),
                  current_metadata.metadata->disc_id.c_str());
      if (ImGui::Button("Play", ImVec2(180 * scale, 42 * scale)))
      {
        launch = true;
        done = true;
      }
    }
    else
    {
      ImGui::TextUnformatted("No extracted game is configured yet.");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Internal resolution (Dolphin EFB upscale)");
    if (ImGui::BeginCombo("##resolution", resolutions[resolution_index].text))
    {
      for (std::size_t i = 0; i < resolutions.size(); ++i)
      {
        const bool selected = resolution_index == static_cast<int>(i);
        if (ImGui::Selectable(resolutions[i].text, selected))
        {
          std::string error;
          if (moderngekko::frontend::SaveConfig(user_directory, resolutions[i].text, &error))
            resolution_index = static_cast<int>(i);
          else
          {
            std::lock_guard lock(dialog.mutex);
            dialog.error = std::move(error);
          }
        }
      }
      ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Wii disc image");
    if (selected_image)
      ImGui::TextWrapped("%s", selected_image->string().c_str());
    else
      ImGui::TextDisabled("No WBFS or ISO selected");

    if (!extraction.running)
    {
      if (ImGui::Button("Browse for WBFS / ISO"))
      {
        static constexpr SDL_DialogFileFilter filters[] = {
            {"Wii disc images", "wbfs;iso"}, {"WBFS", "wbfs"}, {"ISO", "iso"}};
        const std::string documents = DocumentsDirectory().string();
        SDL_ShowOpenFileDialog(FileDialogCallback, &dialog, window, filters,
                               static_cast<int>(std::size(filters)), documents.c_str(), false);
      }
      if (selected_image)
      {
        ImGui::SameLine();
        if (ImGui::Button("Extract and Play"))
        {
          extraction.completed = 0;
          extraction.total = 1;
          extraction.running = true;
          {
            std::lock_guard lock(extraction.mutex);
            extraction.error.clear();
            extraction.finished_game.reset();
          }
          const fs::path image = *selected_image;
          extraction_thread = std::jthread(
              [image, user_directory, &extraction] { ExtractDisc(image, user_directory, &extraction); });
        }
      }
    }
    else
    {
      const float progress = std::min(1.0f, static_cast<float>(extraction.completed.load()) /
                                               extraction.total.load());
      ImGui::ProgressBar(progress, ImVec2(-1, 0));
    }

    {
      std::lock_guard lock(extraction.mutex);
      if (!extraction.status.empty())
        ImGui::TextWrapped("%s", extraction.status.c_str());
      if (!extraction.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", extraction.error.c_str());
    }
    {
      std::lock_guard lock(dialog.mutex);
      if (!dialog.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", dialog.error.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Controller: %s", controller_status.c_str());
#ifdef MODERNGEKKO_FORCE_SIDEWAYS_WIIMOTE
    ImGui::TextDisabled("Forced profile: Sideways Wii Remote, Extension: None");
#endif
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  if (extraction_thread.joinable())
    extraction_thread.join();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  if (!launch)
    return 0;
  std::string error;
  if (!WriteDefaultGame(user_directory, current_game, &error))
    return 1;
  std::string command = Quote(SiblingRunner(argv[0])) + " --game " + Quote(current_game) +
                        " --user-dir " + Quote(user_directory);
  if (use_wayland)
    command += " --wayland";
  return std::system(command.c_str()) == 0 ? 0 : 1;
}
