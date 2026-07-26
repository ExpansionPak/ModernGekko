#pragma once

#include "moderngekko/mod_abi.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace moderngekko {
struct ModSource {
  enum class Kind {
    DynamicPath,
    AttachedDescriptor,
  };

  static ModSource DynamicPath(std::filesystem::path path);
  static ModSource AttachedDescriptor(const ModernGekkoModDesc *descriptor,
                                      std::string label = {});

  Kind kind = Kind::DynamicPath;
  std::filesystem::path path;
  const ModernGekkoModDesc *descriptor = nullptr;
  std::string label;
};

struct ModLoadIssue {
  std::string source;
  std::string message;
};

struct LoadedModInfo {
  std::string id;
  std::string version;
  std::string display_name;
  std::string source;
};

struct ModLoadReport {
  std::vector<LoadedModInfo> loaded;
  std::vector<ModLoadIssue> issues;

  explicit operator bool() const { return issues.empty(); }
};

std::vector<ModSource>
DiscoverModSources(const std::vector<std::filesystem::path> &directories,
                   std::vector<ModLoadIssue> *issues = nullptr);

class ModManager final {
public:
  ModManager();
  ~ModManager();
  ModManager(const ModManager &) = delete;
  ModManager &operator=(const ModManager &) = delete;
  ModManager(ModManager &&) = delete;
  ModManager &operator=(ModManager &&) = delete;

  ModLoadReport Load(const std::vector<ModSource> &sources,
                     const std::string &game_id);
  ModLoadReport
  LoadDirectories(const std::vector<std::filesystem::path> &directories,
                  const std::string &game_id);
  void Unload();
  bool Dispatch(CPUState *state, std::uint32_t address);
  bool TriggerEvent(const std::string &provider_id,
                    const std::string &event_name, CPUState *state);
  ModernGekkoModFunction FindExport(const std::string &provider_id,
                                    const std::string &export_name) const;
  const std::vector<LoadedModInfo> &GetLoadedMods() const;
  bool HandlesAddress(std::uint32_t address) const;
  bool HandlesRange(std::uint32_t start, std::uint32_t end) const;
  bool Empty() const;

  static bool HostCall(CPUState *state, std::uint32_t address, void *user_data);
  static bool HostCallContains(std::uint32_t address, void *user_data);
  static bool HostCallRangeContains(std::uint32_t start, std::uint32_t end,
                                    void *user_data);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
}

namespace ModernGekko = moderngekko;
