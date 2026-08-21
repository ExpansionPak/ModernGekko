#include "moderngekko/mod_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
int entry_hooks = 0;
int return_hooks = 0;
int event_callbacks = 0;
int runtime_callbacks = 0;
int exports_called = 0;
int loads = 0;
int unloads = 0;
ModernGekkoModFunction imported_function = nullptr;

void BaseExport(CPUState *) { ++exports_called; }

void BasePatch(CPUState *state) { moderngekko_mod_return_u32(state, 77u); }

void EntryHook(CPUState *state) {
  ++entry_hooks;
  state->gpr[3] = 0xDEADBEEFu;
}

void ReturnHook(CPUState *state) {
  ++return_hooks;
  state->gpr[3] = 0xCAFEBABEu;
}

void EventCallback(CPUState *state) {
  ++event_callbacks;
  state->gpr[3] = 0x11111111u;
}

void RuntimeCallback(CPUState *state) {
  ++runtime_callbacks;
  state->gpr[3] = 0x22222222u;
}

void OnLoad(const ModernGekkoModHostApi *) { ++loads; }

void OnUnload() { ++unloads; }

constexpr ModernGekkoModPatch base_patches[] = {
    RECOMP_PATCH(0x80002000u, BasePatch),
};

constexpr ModernGekkoModHook base_hooks[] = {
    RECOMP_HOOK(0x80003000u, EntryHook),
    RECOMP_HOOK_RETURN(0x80003000u, ReturnHook),
};

constexpr ModernGekkoModExportEntry base_exports[] = {
    RECOMP_EXPORT("call", BaseExport),
};

constexpr ModernGekkoModEvent base_events[] = {
    RECOMP_DECLARE_EVENT("tick"),
};

const ModernGekkoModDesc base_descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    "base_mod",
    "1.2.0",
    "Base mod",
    nullptr,
    0u,
    base_patches,
    1u,
    base_hooks,
    2u,
    base_exports,
    1u,
    nullptr,
    0u,
    base_events,
    1u,
    nullptr,
    0u,
    OnLoad,
    OnUnload,
};

const ModernGekkoModDesc old_cpu_descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    3u,
    sizeof(CPUState) - sizeof(std::int64_t),
    "TEST01",
    "old_cpu_mod",
    "1.0.0",
    "Old CPU ABI mod",
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    nullptr,
};

constexpr ModernGekkoModDependency dependent_dependencies[] = {
    {"base_mod", "1.1.0", 0u},
};

ModernGekkoModImportEntry dependent_imports[] = {
    RECOMP_IMPORT("base_mod", "call", &imported_function),
};

constexpr ModernGekkoModCallback dependent_callbacks[] = {
    RECOMP_CALLBACK("base_mod", "tick", EventCallback),
    RECOMP_CALLBACK("*", "runtime_start", RuntimeCallback),
};

const ModernGekkoModDesc dependent_descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    "dependent_mod",
    "2.0.0",
    "Dependent mod",
    dependent_dependencies,
    1u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    dependent_imports,
    1u,
    nullptr,
    0u,
    dependent_callbacks,
    2u,
    nullptr,
    nullptr,
};

constexpr ModernGekkoModDependency missing_dependencies[] = {
    {"not_installed", "1.0.0", 0u},
};

const ModernGekkoModDesc missing_descriptor = {
    MODERNGEKKO_MOD_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    "missing_dep_mod",
    "1.0.0",
    "Missing dependency mod",
    missing_dependencies,
    1u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    0u,
    nullptr,
    nullptr,
};
}

int main() {
  moderngekko::ModManager manager;
  const auto initial_generation = manager.InterceptionGeneration();
  const std::vector<moderngekko::ModSource> sources = {
      moderngekko::ModSource::AttachedDescriptor(&dependent_descriptor,
                                                 "dependent"),
      moderngekko::ModSource::AttachedDescriptor(&base_descriptor, "base"),
  };
  const auto loaded = manager.Load(sources, "TEST01");
  if (!loaded || loaded.loaded.size() != 2u)
    return 1;
  if (!manager.HasGuestInterception() ||
      manager.InterceptionGeneration() == initial_generation || loads != 1)
    return 21;
  if (loaded.loaded[0].id != "base_mod" ||
      loaded.loaded[1].id != "dependent_mod")
    return 2;
  if (imported_function != BaseExport)
    return 3;
  if (!manager.HandlesAddress(0x80002000u) ||
      !manager.HandlesAddress(0x80003000u) ||
      manager.HandlesAddress(0x80004000u))
    return 17;
  if (!manager.HandlesRange(0x80001000u, 0x80002800u) ||
      !manager.HandlesRange(0x80003000u, 0x80003004u) ||
      manager.HandlesRange(0x80004000u, 0x80005000u) ||
      manager.HandlesRange(0x80002000u, 0x80002000u))
    return 20;
  imported_function(nullptr);
  if (exports_called != 1)
    return 4;

  CPUState state{};
  state.lr = 0x80004000u;
  state.gpr[3] = 5u;
  if (!manager.Dispatch(&state, 0x80002000u))
    return 5;
  if (state.gpr[3] != 77u || state.pc != state.lr || runtime_callbacks != 1)
    return 6;

  state.gpr[3] = 9u;
  if (manager.Dispatch(&state, 0x80003000u))
    return 7;
  if (entry_hooks != 1 || state.gpr[3] != 9u)
    return 8;
  if (!manager.HandlesAddress(state.lr))
    return 18;
  const auto pending_generation = manager.InterceptionGeneration();
  if (manager.Dispatch(&state, state.lr))
    return 9;
  if (return_hooks != 1 || state.gpr[3] != 9u)
    return 10;
  if (manager.HandlesAddress(state.lr))
    return 19;
  if (manager.InterceptionGeneration() == pending_generation)
    return 22;

  if (!manager.TriggerEvent("base_mod", "tick", &state))
    return 11;
  if (event_callbacks != 1 || state.gpr[3] != 9u)
    return 12;

  const auto loaded_generation = manager.InterceptionGeneration();
  manager.Unload();
  if (imported_function != nullptr || manager.HasGuestInterception() ||
      manager.InterceptionGeneration() == loaded_generation || unloads != 1)
    return 13;
  const auto old_cpu =
      manager.Load({moderngekko::ModSource::AttachedDescriptor(
                       &old_cpu_descriptor, "old_cpu")},
                   "TEST01");
  if (old_cpu || old_cpu.issues.empty())
    return 23;
  const auto rejected =
      manager.Load({moderngekko::ModSource::AttachedDescriptor(
                       &missing_descriptor, "missing")},
                   "TEST01");
  if (rejected || rejected.issues.empty() || !manager.Empty())
    return 14;

  const auto dynamic =
      manager.LoadDirectories({MODERNGEKKO_TEST_MOD_DIR}, "TEST01");
  if (!dynamic || dynamic.loaded.size() != 1u ||
      dynamic.loaded[0].id != "dynamic_fixture")
    return 15;
  state.lr = 0x80005000u;
  if (!manager.Dispatch(&state, 0x80001000u) || state.gpr[3] != 999u ||
      state.pc != state.lr)
    return 16;

  return 0;
}
