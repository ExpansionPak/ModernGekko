// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Boot/BootMemory.h"

#include "moderngekko/address_space.hpp"
#include "moderngekko/cpu_state.h"

namespace DolphinBoot
{
namespace
{
void SetupGameCubeMemory(moderngekko::AddressSpace& memory)
{
  memory.Write32(0x80000020u, 0x0D15EA5Eu);
  memory.Write32(0x80000028u, moderngekko::AddressSpace::RetailMem1Size);
  memory.Write32(0x8000002Cu, 0x10000006u);
  memory.Write32(0x800000CCu, 0u);
  memory.Write32(0x800000D0u, 0x01000000u);
  memory.Write32(0x800000F8u, 0x09A7EC80u);
  memory.Write32(0x800000FCu, 0x1CF7C580u);
  memory.Write32(0x80000300u, 0x4C000064u);
  memory.Write32(0x80000800u, 0x4C000064u);
  memory.Write32(0x80000C00u, 0x4C000064u);
}

void SetupWiiMemory(moderngekko::AddressSpace& memory)
{
  memory.Write32(0x00000020u, 0x0D15EA5Eu);
  memory.Write32(0x00000024u, 0x00000001u);
  memory.Write32(0x00000028u, moderngekko::AddressSpace::RetailMem1Size);
  memory.Write32(0x0000002Cu, 0x00000023u);
  memory.Write32(0x00000030u, 0u);
  memory.Write32(0x00000034u, 0x817FEC60u);
  memory.Write32(0x000000F0u, moderngekko::AddressSpace::RetailMem1Size);
  memory.Write32(0x000000F4u, 0x8179B500u);
  memory.Write32(0x000000F8u, 0x0E7BE2C0u);
  memory.Write32(0x000000FCu, 0x2B73A840u);
  memory.Write32(0x000030D8u, 0xFFFFFFFFu);
  memory.Write16(0x000030E6u, 0x8201u);
  memory.Write16(0x0000315Eu, 0x0113u);
  memory.Write8(0x0000315Cu, 0x80u);
  memory.Write32(0x00003184u, 0x80000000u);

  for (std::uint32_t address = 0x3000u; address <= 0x3038u; address += 4u)
    memory.Write32(address, 0u);
}
}

void SetupMemory(moderngekko::AddressSpace& memory, CPUState& cpu, bool is_wii)
{
  cpu.msr = 0x00002032u;
  cpu.hid2 = 0xE0000000u;

  if (is_wii)
  {
    cpu.gpr[1] = 0x8004D4BCu;
    SetupWiiMemory(memory);
  }
  else
  {
    SetupGameCubeMemory(memory);
  }
}
}
