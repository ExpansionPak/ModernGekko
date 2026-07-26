#include "moderngekko/mod_loader.hpp"

#if defined(MODERNGEKKO_ENABLE_DYNAMIC_MODULES)
#include "Common/DynamicLibrary.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace moderngekko {
namespace {
constexpr std::uint32_t MAX_ITEMS = 1u << 20;

struct Version {
  std::array<std::uint32_t, 3> parts{};
};

bool ParseVersion(const char *text, Version *version) {
  if (!text || !*text)
    return false;
  const char *current = text;
  for (std::size_t i = 0; i < version->parts.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(*current)))
      return false;
    std::uint64_t value = 0;
    do {
      value = value * 10u + static_cast<unsigned int>(*current - '0');
      if (value > std::numeric_limits<std::uint32_t>::max())
        return false;
      ++current;
    } while (std::isdigit(static_cast<unsigned char>(*current)));
    version->parts[i] = static_cast<std::uint32_t>(value);
    if (i + 1u != version->parts.size()) {
      if (*current != '.')
        return false;
      ++current;
    }
  }
  return *current == '\0' || *current == '-' || *current == '+';
}

bool VersionAtLeast(const char *actual, const char *minimum) {
  if (!minimum || !*minimum)
    return true;
  Version actual_version;
  Version minimum_version;
  if (!ParseVersion(actual, &actual_version) ||
      !ParseVersion(minimum, &minimum_version))
    return false;
  return actual_version.parts >= minimum_version.parts;
}

bool ValidName(const char *text) {
  if (!text || !*text)
    return false;
  for (const unsigned char ch : std::string(text)) {
    if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.')
      return false;
  }
  return true;
}

bool ValidText(const char *text) { return text && *text; }

template <typename T> bool ValidArray(const T *values, std::uint32_t count) {
  return count <= MAX_ITEMS && (count == 0u || values != nullptr);
}

std::string EventKey(const std::string &provider, const std::string &event) {
  std::string key = provider;
  key.push_back('\0');
  key += event;
  return key;
}

std::string LibrarySuffix() {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

bool EndsWith(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsPackagedLibrary(const std::filesystem::path &path) {
  return EndsWith(path.filename().string(), ".mgm" + LibrarySuffix());
}
}

struct ModManager::Impl {
  struct Mod {
    std::string source;
#if defined(MODERNGEKKO_ENABLE_DYNAMIC_MODULES)
    std::unique_ptr<Common::DynamicLibrary> library;
#endif
    const ModernGekkoModDesc *descriptor = nullptr;
    bool loaded = false;
  };

  struct Hooks {
    std::vector<ModernGekkoModFunction> entry;
    std::vector<ModernGekkoModFunction> returning;
  };

  struct PendingReturn {
    std::uint32_t address = 0;
    std::uint32_t stack_pointer = 0;
    std::vector<ModernGekkoModFunction> functions;
  };

  std::vector<std::unique_ptr<Mod>> mods;
  std::vector<LoadedModInfo> loaded;
  std::unordered_map<std::string, Mod *> mods_by_id;
  std::unordered_map<std::string, ModernGekkoModFunction> exports;
  std::unordered_map<std::uint32_t, ModernGekkoModFunction> patches;
  std::unordered_map<std::uint32_t, Hooks> hooks;
  std::unordered_map<std::string, std::vector<ModernGekkoModFunction>>
      callbacks;
  std::vector<ModernGekkoModFunction *> import_slots;
  std::vector<PendingReturn> pending_returns;
  bool runtime_started = false;
  ModernGekkoModHostApi host_api{};

  static std::string ExportKey(const std::string &provider,
                               const std::string &name) {
    return EventKey(provider, name);
  }

  static int TriggerEventThunk(void *user_data, const char *provider_id,
                               const char *event_name, CPUState *state) {
    if (!user_data || !provider_id || !event_name || !state)
      return 0;
    return static_cast<ModManager *>(user_data)->TriggerEvent(provider_id,
                                                              event_name, state)
               ? 1
               : 0;
  }

  static ModernGekkoModFunction FindExportThunk(void *user_data,
                                                const char *provider_id,
                                                const char *export_name) {
    if (!user_data || !provider_id || !export_name)
      return nullptr;
    return static_cast<ModManager *>(user_data)->FindExport(provider_id,
                                                            export_name);
  }
};

ModSource ModSource::DynamicPath(std::filesystem::path path) {
  ModSource source;
  source.kind = Kind::DynamicPath;
  source.path = std::move(path);
  return source;
}

ModSource ModSource::AttachedDescriptor(const ModernGekkoModDesc *descriptor,
                                        std::string label) {
  ModSource source;
  source.kind = Kind::AttachedDescriptor;
  source.descriptor = descriptor;
  source.label = std::move(label);
  return source;
}

ModManager::ModManager() : m_impl(std::make_unique<Impl>()) {
  m_impl->host_api.abi_version = MODERNGEKKO_MOD_HOST_ABI_VERSION;
  m_impl->host_api.user_data = this;
  m_impl->host_api.trigger_event = Impl::TriggerEventThunk;
  m_impl->host_api.find_export = Impl::FindExportThunk;
}

ModManager::~ModManager() { Unload(); }

ModLoadReport ModManager::Load(const std::vector<ModSource> &sources,
                               const std::string &game_id) {
  Unload();
  ModLoadReport report;
  const auto issue = [&](const std::string &source, std::string message) {
    report.issues.push_back({source, std::move(message)});
  };

  for (std::size_t index = 0; index < sources.size(); ++index) {
    const ModSource &source = sources[index];
    auto mod = std::make_unique<Impl::Mod>();
    mod->source = source.kind == ModSource::Kind::DynamicPath
                      ? source.path.string()
                      : source.label;
    if (mod->source.empty())
      mod->source = "attached:" + std::to_string(index);
    if (source.kind == ModSource::Kind::AttachedDescriptor) {
      mod->descriptor = source.descriptor;
    } else {
#if defined(MODERNGEKKO_ENABLE_DYNAMIC_MODULES)
      mod->library = std::make_unique<Common::DynamicLibrary>();
      if (!mod->library->Open(source.path.string().c_str())) {
        issue(mod->source, "could not open the mod library");
        continue;
      }
      ModernGekkoGetModFn get_mod = nullptr;
      if (!mod->library->GetSymbol(MODERNGEKKO_GET_MOD_SYMBOL, &get_mod)) {
        issue(mod->source, "missing moderngekko_get_mod");
        continue;
      }
      mod->descriptor = get_mod();
#else
      issue(mod->source, "dynamic mod loading is unavailable");
      continue;
#endif
    }

    const ModernGekkoModDesc *desc = mod->descriptor;
    if (!desc)
      issue(mod->source, "null mod descriptor");
    else if (desc->abi_version != MODERNGEKKO_MOD_ABI_VERSION)
      issue(mod->source, "unsupported mod ABI");
    else if (desc->cpu_abi_version != MODERNGEKKO_CPU_ABI_VERSION ||
             desc->cpu_state_size != sizeof(CPUState))
      issue(mod->source, "CPU ABI mismatch");
    else if (!std::memchr(desc->game_id, '\0', sizeof(desc->game_id)) ||
             game_id != desc->game_id)
      issue(mod->source, "target game ID mismatch");
    else if (!ValidName(desc->id))
      issue(mod->source, "invalid mod ID");
    else if (!ValidText(desc->display_name))
      issue(mod->source, "missing display name");
    else {
      Version version;
      if (!ParseVersion(desc->version, &version))
        issue(mod->source, "invalid mod version");
      else if (!ValidArray(desc->dependencies, desc->num_dependencies) ||
               !ValidArray(desc->patches, desc->num_patches) ||
               !ValidArray(desc->hooks, desc->num_hooks) ||
               !ValidArray(desc->exports, desc->num_exports) ||
               !ValidArray(desc->imports, desc->num_imports) ||
               !ValidArray(desc->events, desc->num_events) ||
               !ValidArray(desc->callbacks, desc->num_callbacks))
        issue(mod->source, "invalid descriptor arrays");
      else if (m_impl->mods_by_id.contains(desc->id))
        issue(mod->source, "duplicate mod ID");
      else {
        m_impl->mods_by_id.emplace(desc->id, mod.get());
        m_impl->mods.push_back(std::move(mod));
      }
    }
  }

  if (!report.issues.empty()) {
    Unload();
    return report;
  }

  std::unordered_map<Impl::Mod *, std::size_t> mod_indices;
  for (std::size_t i = 0; i < m_impl->mods.size(); ++i)
    mod_indices.emplace(m_impl->mods[i].get(), i);
  std::vector<std::vector<std::size_t>> edges(m_impl->mods.size());
  std::vector<std::size_t> indegree(m_impl->mods.size());

  for (std::size_t i = 0; i < m_impl->mods.size(); ++i) {
    const auto &mod = m_impl->mods[i];
    const auto *desc = mod->descriptor;
    std::unordered_set<std::string> seen;
    for (std::uint32_t d = 0; d < desc->num_dependencies; ++d) {
      const auto &dependency = desc->dependencies[d];
      if (!ValidName(dependency.id) || !seen.emplace(dependency.id).second) {
        issue(mod->source, "invalid or duplicate dependency");
        continue;
      }
      const auto found = m_impl->mods_by_id.find(dependency.id);
      if (found == m_impl->mods_by_id.end()) {
        if (!dependency.optional)
          issue(mod->source,
                "missing dependency " + std::string(dependency.id));
        continue;
      }
      if (!VersionAtLeast(found->second->descriptor->version,
                          dependency.minimum_version)) {
        issue(mod->source, "dependency version is too old for " +
                               std::string(dependency.id));
        continue;
      }
      const std::size_t dependency_index = mod_indices.at(found->second);
      edges[dependency_index].push_back(i);
      ++indegree[i];
    }
  }

  if (!report.issues.empty()) {
    Unload();
    return report;
  }

  std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>>
      ready;
  for (std::size_t i = 0; i < indegree.size(); ++i) {
    if (indegree[i] == 0)
      ready.push(i);
  }
  std::vector<std::size_t> order;
  order.reserve(m_impl->mods.size());
  while (!ready.empty()) {
    const std::size_t index = ready.top();
    ready.pop();
    for (const std::size_t dependent : edges[index]) {
      if (--indegree[dependent] == 0)
        ready.push(dependent);
    }
    order.push_back(index);
  }
  if (order.size() != m_impl->mods.size()) {
    issue("mods", "dependency cycle");
    Unload();
    return report;
  }
  std::vector<std::unique_ptr<Impl::Mod>> ordered;
  ordered.reserve(m_impl->mods.size());
  for (const std::size_t index : order)
    ordered.push_back(std::move(m_impl->mods[index]));
  m_impl->mods = std::move(ordered);
  m_impl->mods_by_id.clear();
  for (const auto &mod : m_impl->mods)
    m_impl->mods_by_id.emplace(mod->descriptor->id, mod.get());

  std::unordered_set<std::string> event_names;
  event_names.emplace(EventKey("*", "runtime_start"));
  for (const auto &mod : m_impl->mods) {
    const auto *desc = mod->descriptor;
    std::unordered_set<std::string> local_exports;
    std::unordered_set<std::string> local_events;
    for (std::uint32_t i = 0; i < desc->num_exports; ++i) {
      const auto &entry = desc->exports[i];
      if (!ValidName(entry.name) || !entry.function ||
          !local_exports.emplace(entry.name).second) {
        issue(mod->source, "invalid or duplicate export");
        continue;
      }
      m_impl->exports.emplace(Impl::ExportKey(desc->id, entry.name),
                              entry.function);
    }
    for (std::uint32_t i = 0; i < desc->num_events; ++i) {
      const auto &event = desc->events[i];
      if (!ValidName(event.name) || !local_events.emplace(event.name).second) {
        issue(mod->source, "invalid or duplicate event");
        continue;
      }
      event_names.emplace(EventKey(desc->id, event.name));
    }
  }

  for (const auto &mod : m_impl->mods) {
    const auto *desc = mod->descriptor;
    for (std::uint32_t i = 0; i < desc->num_imports; ++i) {
      const auto &entry = desc->imports[i];
      if (!ValidName(entry.dependency_id) || !ValidName(entry.name) ||
          !entry.slot) {
        issue(mod->source, "invalid import");
        continue;
      }
      const ModernGekkoModDependency *declared = nullptr;
      for (std::uint32_t d = 0; d < desc->num_dependencies; ++d) {
        if (std::strcmp(desc->dependencies[d].id, entry.dependency_id) == 0) {
          declared = &desc->dependencies[d];
          break;
        }
      }
      if (!declared) {
        issue(mod->source, "import provider is not a declared dependency");
        continue;
      }
      const auto dependency = m_impl->mods_by_id.find(entry.dependency_id);
      const auto function = m_impl->exports.find(
          Impl::ExportKey(entry.dependency_id, entry.name));
      if (dependency == m_impl->mods_by_id.end()) {
        *entry.slot = nullptr;
      } else if (function == m_impl->exports.end()) {
        issue(mod->source, "missing export " +
                               std::string(entry.dependency_id) + ":" +
                               entry.name);
      } else {
        *entry.slot = function->second;
      }
      m_impl->import_slots.push_back(entry.slot);
    }
    for (std::uint32_t i = 0; i < desc->num_patches; ++i) {
      const auto &patch = desc->patches[i];
      if ((patch.address & 3u) != 0u || !patch.function ||
          (patch.flags & ~MODERNGEKKO_MOD_PATCH_FORCE) != 0u) {
        issue(mod->source, "invalid patch");
        continue;
      }
      const auto found = m_impl->patches.find(patch.address);
      if (found != m_impl->patches.end() &&
          (patch.flags & MODERNGEKKO_MOD_PATCH_FORCE) == 0u) {
        issue(mod->source,
              "patch conflict at " + std::to_string(patch.address));
        continue;
      }
      m_impl->patches[patch.address] = patch.function;
    }
    for (std::uint32_t i = 0; i < desc->num_hooks; ++i) {
      const auto &hook = desc->hooks[i];
      if ((hook.address & 3u) != 0u || !hook.function ||
          hook.kind > MODERNGEKKO_MOD_HOOK_RETURN) {
        issue(mod->source, "invalid hook");
        continue;
      }
      auto &hooks = m_impl->hooks[hook.address];
      if (hook.kind == MODERNGEKKO_MOD_HOOK_ENTRY)
        hooks.entry.push_back(hook.function);
      else
        hooks.returning.push_back(hook.function);
    }
    for (std::uint32_t i = 0; i < desc->num_callbacks; ++i) {
      const auto &callback = desc->callbacks[i];
      const std::string provider =
          callback.dependency_id &&
                  std::strcmp(callback.dependency_id, ".") == 0
              ? desc->id
          : callback.dependency_id ? callback.dependency_id
                                   : "";
      if ((!ValidName(provider.c_str()) && provider != "*") ||
          !ValidName(callback.event_name) || !callback.function) {
        issue(mod->source, "invalid callback");
        continue;
      }
      if (provider != desc->id && provider != "*") {
        bool declared = false;
        for (std::uint32_t d = 0; d < desc->num_dependencies; ++d)
          declared |= provider == desc->dependencies[d].id;
        if (!declared) {
          issue(mod->source, "callback provider is not a declared dependency");
          continue;
        }
      }
      const std::string key = EventKey(provider, callback.event_name);
      if (!event_names.contains(key)) {
        issue(mod->source,
              "missing event " + provider + ":" + callback.event_name);
        continue;
      }
      m_impl->callbacks[key].push_back(callback.function);
    }
  }

  if (!report.issues.empty()) {
    Unload();
    return report;
  }

  for (const auto &mod : m_impl->mods) {
    const auto *desc = mod->descriptor;
    m_impl->loaded.push_back(
        {desc->id, desc->version, desc->display_name, mod->source});
    if (desc->on_load)
      desc->on_load(&m_impl->host_api);
    mod->loaded = true;
  }
  report.loaded = m_impl->loaded;
  return report;
}

ModLoadReport ModManager::LoadDirectories(
    const std::vector<std::filesystem::path> &directories,
    const std::string &game_id) {
  ModLoadReport discovery;
  std::vector<ModSource> sources =
      DiscoverModSources(directories, &discovery.issues);
  if (!discovery.issues.empty())
    return discovery;
  return Load(sources, game_id);
}

std::vector<ModSource>
DiscoverModSources(const std::vector<std::filesystem::path> &directories,
                   std::vector<ModLoadIssue> *issues) {
  std::vector<std::filesystem::path> paths;
  const auto issue = [&](const std::filesystem::path &path,
                         std::string message) {
    if (issues)
      issues->push_back({path.string(), std::move(message)});
  };
  for (const auto &directory : directories) {
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec))
      continue;
    if (std::filesystem::is_regular_file(directory, ec)) {
      paths.push_back(directory);
      continue;
    }
    if (!std::filesystem::is_directory(directory, ec)) {
      issue(directory, "mod path is not a directory or library");
      continue;
    }
    if (directory.extension() == ".mgm") {
      const auto library = directory / ("mod" + LibrarySuffix());
      if (std::filesystem::is_regular_file(library, ec))
        paths.push_back(library);
      else
        issue(directory, "package has no platform mod library");
      continue;
    }
    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end; it.increment(ec)) {
      if (it->is_directory(ec) && it->path().extension() == ".mgm") {
        const auto library = it->path() / ("mod" + LibrarySuffix());
        if (std::filesystem::is_regular_file(library, ec))
          paths.push_back(library);
        else
          issue(it->path(), "package has no platform mod library");
      } else if (it->is_regular_file(ec) && IsPackagedLibrary(it->path())) {
        paths.push_back(it->path());
      }
    }
    if (ec)
      issue(directory, "could not enumerate mod directory");
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  std::vector<ModSource> sources;
  sources.reserve(paths.size());
  for (auto &path : paths)
    sources.push_back(ModSource::DynamicPath(std::move(path)));
  return sources;
}

void ModManager::Unload() {
  for (auto it = m_impl->mods.rbegin(); it != m_impl->mods.rend(); ++it) {
    if ((*it) && (*it)->loaded && (*it)->descriptor &&
        (*it)->descriptor->on_unload)
      (*it)->descriptor->on_unload();
  }
  for (ModernGekkoModFunction *slot : m_impl->import_slots) {
    if (slot)
      *slot = nullptr;
  }
  m_impl->pending_returns.clear();
  m_impl->callbacks.clear();
  m_impl->hooks.clear();
  m_impl->patches.clear();
  m_impl->exports.clear();
  m_impl->mods_by_id.clear();
  m_impl->loaded.clear();
  m_impl->import_slots.clear();
  m_impl->mods.clear();
  m_impl->runtime_started = false;
}

bool ModManager::Dispatch(CPUState *state, std::uint32_t address) {
  if (!state)
    return false;
  if (!m_impl->runtime_started) {
    m_impl->runtime_started = true;
    TriggerEvent("*", "runtime_start", state);
  }
  if (!m_impl->pending_returns.empty() &&
      m_impl->pending_returns.back().address == address &&
      m_impl->pending_returns.back().stack_pointer == state->gpr[1]) {
    auto pending = std::move(m_impl->pending_returns.back());
    m_impl->pending_returns.pop_back();
    for (auto it = pending.functions.rbegin(); it != pending.functions.rend();
         ++it) {
      const CPUState saved = *state;
      (*it)(state);
      *state = saved;
    }
  }
  const auto hooks = m_impl->hooks.find(address);
  if (hooks != m_impl->hooks.end()) {
    for (ModernGekkoModFunction function : hooks->second.entry) {
      const CPUState saved = *state;
      function(state);
      *state = saved;
    }
    if (!hooks->second.returning.empty()) {
      if (m_impl->pending_returns.size() >= 4096u)
        m_impl->pending_returns.erase(m_impl->pending_returns.begin());
      m_impl->pending_returns.push_back(
          {state->lr, state->gpr[1], hooks->second.returning});
    }
  }
  const auto patch = m_impl->patches.find(address);
  if (patch == m_impl->patches.end())
    return false;
  patch->second(state);
  return true;
}

bool ModManager::TriggerEvent(const std::string &provider_id,
                              const std::string &event_name, CPUState *state) {
  if (!state)
    return false;
  const auto found = m_impl->callbacks.find(EventKey(provider_id, event_name));
  if (found == m_impl->callbacks.end())
    return false;
  const CPUState saved = *state;
  for (ModernGekkoModFunction function : found->second) {
    function(state);
    *state = saved;
  }
  return true;
}

ModernGekkoModFunction
ModManager::FindExport(const std::string &provider_id,
                       const std::string &export_name) const {
  const auto found =
      m_impl->exports.find(Impl::ExportKey(provider_id, export_name));
  return found == m_impl->exports.end() ? nullptr : found->second;
}

const std::vector<LoadedModInfo> &ModManager::GetLoadedMods() const {
  return m_impl->loaded;
}

bool ModManager::HandlesAddress(std::uint32_t address) const {
  if (m_impl->patches.contains(address) || m_impl->hooks.contains(address))
    return true;
  return std::ranges::any_of(m_impl->pending_returns,
                             [address](const Impl::PendingReturn &pending) {
                               return pending.address == address;
                             });
}

bool ModManager::HandlesRange(std::uint32_t start, std::uint32_t end) const {
  if (start >= end)
    return false;
  const auto inside = [start, end](const auto &entry) {
    return entry.first >= start && entry.first < end;
  };
  if (std::ranges::any_of(m_impl->patches, inside) ||
      std::ranges::any_of(m_impl->hooks, inside))
    return true;
  return std::ranges::any_of(
      m_impl->pending_returns, [start, end](const Impl::PendingReturn &pending) {
        return pending.address >= start && pending.address < end;
      });
}

bool ModManager::Empty() const { return m_impl->mods.empty(); }

bool ModManager::HostCall(CPUState *state, std::uint32_t address,
                          void *user_data) {
  return user_data &&
         static_cast<ModManager *>(user_data)->Dispatch(state, address);
}

bool ModManager::HostCallContains(std::uint32_t address, void *user_data) {
  return user_data &&
         static_cast<ModManager *>(user_data)->HandlesAddress(address);
}

bool ModManager::HostCallRangeContains(std::uint32_t start, std::uint32_t end,
                                       void *user_data) {
  return user_data &&
         static_cast<ModManager *>(user_data)->HandlesRange(start, end);
}
}
