// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct CPUState;

namespace moderngekko
{
class AddressSpace;
}

namespace DolphinBoot
{
void SetupMemory(moderngekko::AddressSpace& memory, CPUState& cpu, bool is_wii);
}
