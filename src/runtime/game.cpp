#include "moderngekko/game.hpp"

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
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t ReadBE32(const std::uint8_t* p)
{
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
         (std::uint32_t{p[2]} << 8) | p[3];
}

std::string Sha256(std::vector<std::uint8_t> data)
{
  const std::uint64_t bit_size = static_cast<std::uint64_t>(data.size()) * 8;
  data.push_back(0x80);
  while ((data.size() % 64) != 56)
    data.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8)
    data.push_back(static_cast<std::uint8_t>(bit_size >> shift));

  std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  for (std::size_t offset = 0; offset < data.size(); offset += 64)
  {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
      w[i] = ReadBE32(data.data() + offset + i * 4);
    for (std::size_t i = 16; i < 64; ++i)
    {
      const auto s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, hh] = h;
    for (std::size_t i = 0; i < 64; ++i)
    {
      const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto ch = (e & f) ^ (~e & g);
      const auto t1 = hh + s1 + ch + K[i] + w[i];
      const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + s0 + maj;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto word : h)
    out << std::setw(8) << word;
  return out.str();
}

std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return std::nullopt;
  const auto size = file.tellg();
  if (size < 0)
    return std::nullopt;
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
    return std::nullopt;
  return bytes;
}
}  // namespace

GameInspectResult InspectGame(const std::filesystem::path& input_root)
{
  std::error_code ec;
  const auto root = std::filesystem::weakly_canonical(input_root, ec);
  if (ec || !std::filesystem::is_directory(root))
    return {{}, "game root is not a readable directory"};
  const auto dol_path = root / "sys" / "main.dol";
  const auto boot_path = root / "sys" / "boot.bin";
  if (!std::filesystem::is_regular_file(dol_path))
    return {{}, "missing sys/main.dol"};
  if (!std::filesystem::is_directory(root / "files"))
    return {{}, "missing files directory"};
  const auto boot = ReadFile(boot_path);
  const auto dol = ReadFile(dol_path);
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
  metadata.game_name = std::move(name);
  metadata.disc_id = std::move(id);
  metadata.platform = wii_magic == 0x5d1c9ea3 ? GamePlatform::Wii : GamePlatform::GameCube;
  metadata.entry_point = entry_point;
  metadata.dol_sha256 = Sha256(std::move(*dol));
  return {std::move(metadata), {}};
}
}  // namespace moderngekko
