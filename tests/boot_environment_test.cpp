#include "moderngekko/legacy_runtime.hpp"

#include <cstdint>

namespace
{
bool Read32(const moderngekko::LegacyRuntime& runtime, std::uint32_t address, std::uint32_t expected)
{
  std::uint32_t value = 0u;
  return runtime.GetAddressSpace().Read32(address, &value) && value == expected;
}
}

int main()
{
  moderngekko::LegacyRuntime gamecube(false);
  if (!Read32(gamecube, 0x80000020u, 0x0D15EA5Eu) ||
      !Read32(gamecube, 0x80000028u, moderngekko::AddressSpace::RetailMem1Size) ||
      !Read32(gamecube, 0x8000002Cu, 0x10000006u) ||
      !Read32(gamecube, 0x800000F8u, 0x09A7EC80u) ||
      !Read32(gamecube, 0x800000FCu, 0x1CF7C580u) ||
      !Read32(gamecube, 0x80000300u, 0x4C000064u))
  {
    return 1;
  }
  if (gamecube.GetCpuState().msr != 0x00002032u ||
      gamecube.GetCpuState().hid2 != 0xE0000000u)
  {
    return 2;
  }

  moderngekko::LegacyRuntime wii(true);
  if (!Read32(wii, 0x00000020u, 0x0D15EA5Eu) ||
      !Read32(wii, 0x00000028u, moderngekko::AddressSpace::RetailMem1Size) ||
      !Read32(wii, 0x0000002Cu, 0x00000023u) ||
      !Read32(wii, 0x000000F8u, 0x0E7BE2C0u) ||
      !Read32(wii, 0x000000FCu, 0x2B73A840u))
  {
    return 3;
  }
  if (wii.GetCpuState().gpr[1] != 0x8004D4BCu || wii.GetCpuState().msr != 0x00002032u ||
      wii.GetCpuState().hid2 != 0xE0000000u ||
      wii.GetCpuState().exram_size != moderngekko::AddressSpace::RetailMem2Size)
  {
    return 4;
  }

  return 0;
}
