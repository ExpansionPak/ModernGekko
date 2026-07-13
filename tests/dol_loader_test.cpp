#include "moderngekko/legacy_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
void WriteBigEndian32(std::vector<std::uint8_t>& image, std::size_t offset, std::uint32_t value)
{
  image[offset] = static_cast<std::uint8_t>(value >> 24);
  image[offset + 1] = static_cast<std::uint8_t>(value >> 16);
  image[offset + 2] = static_cast<std::uint8_t>(value >> 8);
  image[offset + 3] = static_cast<std::uint8_t>(value);
}
}

int main()
{
  std::vector<std::uint8_t> image(0x108u, 0u);
  WriteBigEndian32(image, 0x00u, 0x100u);
  WriteBigEndian32(image, 0x1Cu, 0x104u);
  WriteBigEndian32(image, 0x48u, 0x80001000u);
  WriteBigEndian32(image, 0x64u, 0x80002000u);
  WriteBigEndian32(image, 0x90u, 4u);
  WriteBigEndian32(image, 0xACu, 4u);
  WriteBigEndian32(image, 0xD8u, 0x80003000u);
  WriteBigEndian32(image, 0xDCu, 8u);
  WriteBigEndian32(image, 0xE0u, 0x80001000u);

  image[0x100u] = 0x7Cu;
  image[0x101u] = 0x13u;
  image[0x102u] = 0xFBu;
  image[0x103u] = 0xA6u;
  image[0x104u] = 0x12u;
  image[0x105u] = 0x34u;
  image[0x106u] = 0x56u;
  image[0x107u] = 0x78u;

  moderngekko::LegacyRuntime runtime(true);
  runtime.GetAddressSpace().GetMem1()[0x3000u] = 0xFFu;
  const moderngekko::LegacyDolLoadResult result = runtime.LoadDol(image);
  if (result.status != moderngekko::LegacyDolLoadStatus::Ok || result.entry_point != 0x80001000u ||
      !result.is_wii)
  {
    return 1;
  }
  if (runtime.GetCpuState().pc != 0x80001000u)
    return 2;

  std::uint32_t value = 0u;
  if (!runtime.GetAddressSpace().Read32(0x80002000u, &value) || value != 0x12345678u)
    return 3;
  if (!runtime.GetAddressSpace().Read32(0x80003000u, &value) || value != 0u)
    return 4;

  if (runtime.LoadDol(std::span(image).first(0x80u)).status !=
      moderngekko::LegacyDolLoadStatus::InvalidImage)
  {
    return 5;
  }

  WriteBigEndian32(image, 0x48u, 0x70000000u);
  if (runtime.LoadDol(image).status != moderngekko::LegacyDolLoadStatus::AddressOutOfRange)
    return 6;

  return 0;
}
