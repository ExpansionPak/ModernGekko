#include "dol_patch.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace moderngekko::frontend {
namespace {
struct Patch {
  std::uint32_t address;
  std::uint32_t expected;
  std::uint32_t replacement;
};

std::uint32_t ReadBE32(const std::uint8_t *data) {
  return (std::uint32_t{data[0]} << 24) | (std::uint32_t{data[1]} << 16) |
         (std::uint32_t{data[2]} << 8) | data[3];
}

void WriteBE32(std::uint8_t *data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value >> 24);
  data[1] = static_cast<std::uint8_t>(value >> 16);
  data[2] = static_cast<std::uint8_t>(value >> 8);
  data[3] = static_cast<std::uint8_t>(value);
}

bool ParseWord(std::string_view text, std::uint32_t *value) {
  if (text.starts_with("0x") || text.starts_with("0X"))
    text.remove_prefix(2);
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), *value, 16);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool LoadManifest(const fs::path &path, std::vector<Patch> *patches,
                  std::string *error) {
  std::ifstream input(path);
  if (!input) {
    *error = "can't open DOL patch manifest " + path.string();
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    *error = "invalid DOL patch manifest header";
    return false;
  }
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line != "address,expected,replacement") {
    *error = "invalid DOL patch manifest header";
    return false;
  }
  std::uint32_t previous = 0;
  bool first = true;
  unsigned line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::size_t first_comma = line.find(',');
    const std::size_t second_comma =
        line.find(',', first_comma == std::string::npos ? 0 : first_comma + 1);
    if (first_comma == std::string::npos || second_comma == std::string::npos ||
        line.find(',', second_comma + 1) != std::string::npos) {
      *error = "invalid DOL patch manifest line " + std::to_string(line_number);
      return false;
    }
    Patch patch{};
    if (!ParseWord(std::string_view(line).substr(0, first_comma),
                   &patch.address) ||
        !ParseWord(std::string_view(line).substr(
                       first_comma + 1, second_comma - first_comma - 1),
                   &patch.expected) ||
        !ParseWord(std::string_view(line).substr(second_comma + 1),
                   &patch.replacement) ||
        (patch.address & 3) != 0 || patch.expected == patch.replacement ||
        (!first && patch.address <= previous)) {
      *error = "invalid DOL patch manifest line " + std::to_string(line_number);
      return false;
    }
    patches->push_back(patch);
    previous = patch.address;
    first = false;
  }
  if (patches->empty()) {
    *error = "DOL patch manifest contains no patches";
    return false;
  }
  return true;
}

bool ReadFile(const fs::path &path, std::vector<std::uint8_t> *bytes,
              std::string *error) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    *error = "can't open " + path.string();
    return false;
  }
  const std::streamoff size = input.tellg();
  if (size < 0) {
    *error = "can't read " + path.string();
    return false;
  }
  bytes->resize(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(bytes->data()), size)) {
    *error = "can't read " + path.string();
    return false;
  }
  return true;
}

bool ReplaceFile(const fs::path &path, const std::vector<std::uint8_t> &bytes,
                 std::string *error) {
  fs::path temporary = path;
  temporary += ".patching";
  std::error_code ec;
  fs::remove(temporary, ec);
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char *>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) {
      fs::remove(temporary, ec);
      *error = "can't write " + temporary.string();
      return false;
    }
  }
  fs::permissions(temporary, fs::status(path, ec).permissions(), ec);
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(temporary, ec);
    *error = "can't publish patched " + path.string();
    return false;
  }
#else
  fs::rename(temporary, path, ec);
  if (ec) {
    fs::remove(temporary, ec);
    *error = "can't publish patched " + path.string();
    return false;
  }
#endif
  return true;
}
} // namespace

bool ApplyDolPatchManifest(const fs::path &dol_path,
                           const fs::path &manifest_path, bool *changed,
                           std::string *error) {
  *changed = false;
  std::vector<Patch> patches;
  if (!LoadManifest(manifest_path, &patches, error))
    return false;
  std::vector<std::uint8_t> bytes;
  if (!ReadFile(dol_path, &bytes, error))
    return false;
  if (bytes.size() < 0x100) {
    *error = "DOL header is truncated";
    return false;
  }

  struct Section {
    std::uint32_t address;
    std::uint32_t offset;
    std::uint32_t size;
  };
  std::vector<Section> text_sections;
  for (unsigned index = 0; index < 7; ++index) {
    const Section section{ReadBE32(bytes.data() + 0x48 + index * 4),
                          ReadBE32(bytes.data() + index * 4),
                          ReadBE32(bytes.data() + 0x90 + index * 4)};
    if (section.size == 0)
      continue;
    if (section.offset < 0x100 ||
        static_cast<std::uint64_t>(section.offset) + section.size >
            bytes.size()) {
      *error = "DOL text section is outside the file";
      return false;
    }
    text_sections.push_back(section);
  }

  for (const Patch &patch : patches) {
    const Section *mapped = nullptr;
    for (const Section &section : text_sections) {
      if (patch.address >= section.address &&
          static_cast<std::uint64_t>(patch.address) + 4 <=
              static_cast<std::uint64_t>(section.address) + section.size) {
        if (mapped) {
          *error = "DOL patch address is ambiguously mapped";
          return false;
        }
        mapped = &section;
      }
    }
    if (!mapped) {
      std::array<char, 64> message{};
      std::snprintf(message.data(), message.size(),
                    "DOL patch address 0x%08x is outside executable code",
                    patch.address);
      *error = message.data();
      return false;
    }
    const std::size_t offset = mapped->offset + patch.address - mapped->address;
    const std::uint32_t actual = ReadBE32(bytes.data() + offset);
    if (actual == patch.replacement)
      continue;
    if (actual != patch.expected) {
      std::array<char, 96> message{};
      std::snprintf(message.data(), message.size(),
                    "DOL patch mismatch at 0x%08x: expected %08x, found %08x",
                    patch.address, patch.expected, actual);
      *error = message.data();
      return false;
    }
    WriteBE32(bytes.data() + offset, patch.replacement);
    *changed = true;
  }
  return !*changed || ReplaceFile(dol_path, bytes, error);
}
} // namespace moderngekko::frontend
