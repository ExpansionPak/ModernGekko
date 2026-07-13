#pragma once

#include "moderngekko/module.h"

#include <memory>
#include <string>

namespace moderngekko
{
enum class ModuleLoadStatus
{
  Ok = 0,
  LibraryOpenFailed,
  DynamicLoadingUnavailable,
  EntryPointMissing,
  DescriptorRejected,
};

struct ModuleLoadResult
{
  ModuleLoadStatus status = ModuleLoadStatus::LibraryOpenFailed;
  ModernGekkoModuleStatus validation_status = MODERNGEKKO_MODULE_OK;
};

class ModuleLibrary final
{
public:
  ModuleLibrary();
  ~ModuleLibrary();

  ModuleLibrary(const ModuleLibrary&) = delete;
  ModuleLibrary& operator=(const ModuleLibrary&) = delete;
  ModuleLibrary(ModuleLibrary&&) = delete;
  ModuleLibrary& operator=(ModuleLibrary&&) = delete;

  ModuleLoadResult Open(const std::string& path,
                        const ModernGekkoModuleRequirements& requirements);
  ModuleLoadResult Attach(const ModernGekkoModuleDesc* descriptor,
                          const ModernGekkoModuleRequirements& requirements);
  void Close();

  bool IsOpen() const;
  const ModernGekkoModuleDesc* GetDescriptor() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
}
