#include "netplay_compatibility.hpp"

#include "Common/Crypto/SHA1.h"
#include "Common/Version.h"
#include "moderngekko/cpu_state.h"
#include "moderngekko/mod_loader.hpp"
#include "moderngekko/module_loader.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace moderngekko::frontend {
namespace {
void HashU32(Common::SHA1::Context &context, std::uint32_t value) {
  const std::array<u8, 4> bytes = {
      static_cast<u8>(value), static_cast<u8>(value >> 8),
      static_cast<u8>(value >> 16), static_cast<u8>(value >> 24)};
  context.Update(bytes);
}

void HashU64(Common::SHA1::Context &context, std::uint64_t value) {
  const std::array<u8, 8> bytes = {
      static_cast<u8>(value),       static_cast<u8>(value >> 8),
      static_cast<u8>(value >> 16), static_cast<u8>(value >> 24),
      static_cast<u8>(value >> 32), static_cast<u8>(value >> 40),
      static_cast<u8>(value >> 48), static_cast<u8>(value >> 56)};
  context.Update(bytes);
}

void HashRanges(Common::SHA1::Context &context, const ModernGekkoRange *ranges,
                std::uint32_t count) {
  HashU32(context, count);
  for (std::uint32_t i = 0; i < count; ++i) {
    HashU32(context, ranges[i].start);
    HashU32(context, ranges[i].end);
  }
}

void HashString(Common::SHA1::Context &context, const std::string &value) {
  HashU64(context, value.size());
  context.Update(
      std::span(reinterpret_cast<const u8 *>(value.data()), value.size()));
}

std::string ModsFingerprint(const RuntimeConfig &config) {
  std::vector<ModLoadIssue> issues;
  const std::vector<ModSource> sources =
      DiscoverModSources(config.mod_directories, &issues);
  if (!issues.empty())
    return "rejected";
  auto context = Common::SHA1::CreateContext();
  HashU32(*context, MODERNGEKKO_MOD_ABI_VERSION);
  HashU64(*context, sources.size());
  std::array<u8, 65536> buffer{};
  for (const ModSource &source : sources) {
    if (source.kind != ModSource::Kind::DynamicPath)
      return "attached";
    HashString(*context, source.path.filename().string());
    std::ifstream file(source.path, std::ios::binary);
    if (!file)
      return "rejected";
    while (file) {
      file.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
      const std::streamsize count = file.gcount();
      if (count > 0)
        context->Update(
            std::span(buffer.data(), static_cast<std::size_t>(count)));
    }
  }
  return Common::SHA1::DigestToString(context->Finish());
}

std::string DescriptorFingerprint(const ModernGekkoModuleDesc &descriptor) {
  auto context = Common::SHA1::CreateContext();
  HashU32(*context, descriptor.abi_version);
  HashU32(*context, descriptor.cpu_abi_version);
  HashU32(*context, descriptor.cpu_state_size);
  context->Update(std::span(reinterpret_cast<const u8 *>(descriptor.game_id),
                            sizeof(descriptor.game_id)));
  HashU32(*context, descriptor.entry_point);
  HashU32(*context, descriptor.on_state_loaded ? 1 : 0);
  HashRanges(*context, descriptor.code_ranges, descriptor.num_code_ranges);
  HashRanges(*context, descriptor.smc_ranges, descriptor.num_smc_ranges);
  HashRanges(*context, descriptor.chunk_ranges, descriptor.num_chunk_ranges);
  for (std::uint32_t i = 0; i < descriptor.num_chunk_ranges; ++i)
    HashU64(*context, descriptor.chunk_hashes[i]);
  HashU32(*context, descriptor.num_rel_modules);
  for (std::uint32_t i = 0; i < descriptor.num_rel_modules; ++i) {
    const ModernGekkoRelModule &module = descriptor.rel_modules[i];
    HashU32(*context, module.module_id);
    HashU32(*context, module.version);
    HashU32(*context, module.section_count);
    HashU32(*context, module.section_info_offset);
    HashU32(*context, module.file_size);
    HashU32(*context, module.num_sections);
    for (std::uint32_t j = 0; j < module.num_sections; ++j) {
      const ModernGekkoRelSection &section = module.sections[j];
      HashU32(*context, section.module_id);
      HashU32(*context, section.section_index);
      HashU32(*context, section.linked_start);
      HashU32(*context, section.size);
    }
  }
  return Common::SHA1::DigestToString(context->Finish());
}

#if defined(_WIN32)
std::vector<std::unique_ptr<ModuleLibrary>> &PinnedModuleLibraries() {
  static auto *libraries = new std::vector<std::unique_ptr<ModuleLibrary>>();
  return *libraries;
}
#endif
} // namespace

std::string CompatibilityFingerprint(const RuntimeConfig &config,
                                     const GameMetadata &game) {
  std::string module = "interpreter";
  if (config.module.kind != ModuleSource::Kind::None) {
    const ModernGekkoModuleRequirements requirements = {
        MODERNGEKKO_CPU_ABI_VERSION,
        static_cast<std::uint32_t>(sizeof(CPUState)), game.disc_id.c_str()};
    auto library = std::make_unique<ModuleLibrary>();
    ModuleLoadResult loaded;
    if (config.module.kind == ModuleSource::Kind::DynamicPath) {
      loaded = library->Open(config.module.path.string(), requirements);
    } else {
      loaded = library->Attach(config.module.descriptor, requirements);
    }
    if (loaded.status == ModuleLoadStatus::Ok) {
      module = DescriptorFingerprint(*library->GetDescriptor());
#if defined(_WIN32)
      if (config.module.kind == ModuleSource::Kind::DynamicPath)
        PinnedModuleLibraries().push_back(std::move(library));
#endif
    } else {
      module = "rejected";
    }
  }
  return "moderngekko-netplay-8|" + Common::GetScmRevGitStr() + "|" +
         game.disc_id + "|" + game.dol_sha256 + "|" + game.rel_sha256 + "|" +
         game.assets_sha256 + "|" +
         std::to_string(MODERNGEKKO_MODULE_ABI_VERSION) + "|" +
         std::to_string(MODERNGEKKO_CPU_ABI_VERSION) + "|" +
         std::to_string(sizeof(CPUState)) + "|" + module + "|" +
         ModsFingerprint(config);
}
} // namespace moderngekko::frontend
