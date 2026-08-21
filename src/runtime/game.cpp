#include "moderngekko/game.hpp"

#include "moderngekko/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace moderngekko
{
namespace
{
// SHA-256 and the whole-file read live in moderngekko/sha256.hpp so the PGO
// workflow can hash a merged profile without linking the runtime.
using detail::ReadBE32;
}  // namespace

std::optional<std::string> HashFileSha256(const std::filesystem::path& path)
{
  auto bytes = ReadFileBytes(path);
  if (!bytes)
    return std::nullopt;
  return Sha256(std::move(*bytes));
}

std::optional<std::string> HashDirectorySha256(const std::filesystem::path& root)
{
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec)
    return std::nullopt;
  std::vector<std::filesystem::path> files;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  while (!ec && iterator != end)
  {
    if (iterator->is_regular_file(ec) && !ec)
      files.push_back(iterator->path());
    iterator.increment(ec);
  }
  if (ec)
    return std::nullopt;
  std::ranges::sort(files, [&](const auto& left, const auto& right) {
    return std::filesystem::relative(left, root).generic_string() <
           std::filesystem::relative(right, root).generic_string();
  });
  std::vector<std::uint8_t> manifest;
  for (const auto& path : files)
  {
    const std::string relative = std::filesystem::relative(path, root, ec).generic_string();
    if (ec)
      return std::nullopt;
    const auto size = std::filesystem::file_size(path, ec);
    const auto hash = HashFileSha256(path);
    if (ec || !hash)
      return std::nullopt;
    const auto append_u64 = [&](std::uint64_t value) {
      for (int shift = 56; shift >= 0; shift -= 8)
        manifest.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    append_u64(relative.size());
    manifest.insert(manifest.end(), relative.begin(), relative.end());
    append_u64(size);
    manifest.insert(manifest.end(), hash->begin(), hash->end());
  }
  return Sha256(std::move(manifest));
}

GameInspectResult InspectGame(const std::filesystem::path& input_root)
{
  std::error_code ec;
  const auto root = std::filesystem::weakly_canonical(input_root, ec);
  if (ec || !std::filesystem::is_directory(root))
    return {{}, "game root is not a readable directory"};
  const auto dol_path = root / "sys" / "main.dol";
  const auto boot_path = root / "sys" / "boot.bin";
  const auto rel_path = root / "files" / "_Main.rel";
  if (!std::filesystem::is_regular_file(dol_path))
    return {{}, "missing sys/main.dol"};
  if (!std::filesystem::is_directory(root / "files"))
    return {{}, "missing files directory"};
  const auto boot = ReadFileBytes(boot_path);
  const auto dol = ReadFileBytes(dol_path);
  if (!boot || boot->size() < 0x60)
    return {{}, "missing or malformed sys/boot.bin"};
  if (!dol || dol->size() < 0x100)
    return {{}, "malformed sys/main.dol"};

  const std::uint32_t entry_point = ReadBE32(dol->data() + 0xe0);
  bool entry_is_executable = false;
  for (std::size_t section = 0; section < 18; ++section)
  {
    const std::uint32_t offset = ReadBE32(dol->data() + section * 4);
    const std::uint32_t address = ReadBE32(dol->data() + 0x48 + section * 4);
    const std::uint32_t size = ReadBE32(dol->data() + 0x90 + section * 4);
    if (size == 0)
      continue;
    if (offset == 0 || address == 0 || static_cast<std::uint64_t>(offset) + size > dol->size() ||
        static_cast<std::uint64_t>(address) + size > 0x100000000ULL)
      return {{}, "malformed DOL section table"};
    if (section < 7 && entry_point >= address &&
        static_cast<std::uint64_t>(entry_point) < static_cast<std::uint64_t>(address) + size)
      entry_is_executable = true;
  }
  if (!entry_is_executable)
    return {{}, "DOL entry point is outside its text sections"};

  std::string id(reinterpret_cast<const char*>(boot->data()), 6);
  if (id.size() != 6 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0;
      }))
    return {{}, "invalid six-character disc ID in boot.bin"};

  std::string name(reinterpret_cast<const char*>(boot->data() + 0x20), 0x40);
  if (const auto terminator = name.find('\0'); terminator != std::string::npos)
    name.resize(terminator);
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
    name.pop_back();
  if (name.empty())
    name = id;
  const auto wii_magic = ReadBE32(boot->data() + 0x18);
  const auto gc_magic = ReadBE32(boot->data() + 0x1c);
  if (wii_magic != 0x5d1c9ea3 && gc_magic != 0xc2339f3d)
    return {{}, "boot.bin has neither Wii nor GameCube disc magic"};

  GameMetadata metadata;
  metadata.root = root;
  metadata.main_dol = dol_path;
  if (std::filesystem::is_regular_file(rel_path))
    metadata.main_rel = rel_path;
  metadata.game_name = std::move(name);
  metadata.disc_id = std::move(id);
  metadata.platform = wii_magic == 0x5d1c9ea3 ? GamePlatform::Wii : GamePlatform::GameCube;
  metadata.entry_point = entry_point;
  metadata.dol_sha256 = Sha256(std::move(*dol));
  if (!metadata.main_rel.empty())
  {
    const auto rel_hash = HashFileSha256(metadata.main_rel);
    if (!rel_hash)
      return {{}, "can't hash files/_Main.rel"};
    metadata.rel_sha256 = *rel_hash;
  }
  const auto assets_hash = HashDirectorySha256(root / "files");
  if (!assets_hash)
    return {{}, "can't hash the files directory"};
  metadata.assets_sha256 = *assets_hash;
  return {std::move(metadata), {}};
}
}  // namespace moderngekko
