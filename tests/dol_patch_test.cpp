#include "dol_patch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {
void WriteBE32(std::uint8_t *data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value >> 24);
  data[1] = static_cast<std::uint8_t>(value >> 16);
  data[2] = static_cast<std::uint8_t>(value >> 8);
  data[3] = static_cast<std::uint8_t>(value);
}

std::uint32_t ReadBE32(const std::uint8_t *data) {
  return (std::uint32_t{data[0]} << 24) | (std::uint32_t{data[1]} << 16) |
         (std::uint32_t{data[2]} << 8) | data[3];
}

bool Write(const fs::path &path, const auto &bytes) {
  std::ofstream output(path, std::ios::binary);
  return output && output.write(reinterpret_cast<const char *>(bytes.data()),
                                bytes.size());
}
} // namespace

int main() {
  const fs::path root =
      fs::temp_directory_path() /
      ("moderngekko-dol-patch-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  const fs::path dol = root / "main.dol";
  const fs::path manifest = root / "patches.csv";
  std::array<std::uint8_t, 0x140> bytes{};
  WriteBE32(bytes.data() + 0x00, 0x100);
  WriteBE32(bytes.data() + 0x48, 0x80004000);
  WriteBE32(bytes.data() + 0x90, 0x40);
  WriteBE32(bytes.data() + 0x100, 0x4092000C);
  WriteBE32(bytes.data() + 0x104, 0x7C00F850);
  if (!Write(dol, bytes))
    return 1;
  {
    std::ofstream output(manifest);
    output << "address,expected,replacement\n"
              "80004000,4092000C,4800000C\n"
              "80004004,7C00F850,7C000050\n";
  }

  bool changed = false;
  std::string error;
  if (!moderngekko::frontend::ApplyDolPatchManifest(dol, manifest, &changed,
                                                    &error) ||
      !changed) {
    std::cerr << error << '\n';
    return 1;
  }
  std::ifstream input(dol, std::ios::binary);
  input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (ReadBE32(bytes.data() + 0x100) != 0x4800000C ||
      ReadBE32(bytes.data() + 0x104) != 0x7C000050)
    return 1;
  if (!moderngekko::frontend::ApplyDolPatchManifest(dol, manifest, &changed,
                                                    &error) ||
      changed)
    return 1;

  WriteBE32(bytes.data() + 0x100, 0xDEADBEEF);
  if (!Write(dol, bytes))
    return 1;
  if (moderngekko::frontend::ApplyDolPatchManifest(dol, manifest, &changed,
                                                   &error) ||
      error.find("0x80004000") == std::string::npos)
    return 1;

  fs::remove_all(root);
  return 0;
}
