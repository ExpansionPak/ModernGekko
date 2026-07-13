// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <vector>

#include "Common/CommonTypes.h"

namespace moderngekko
{
class AddressSpace;
}

class DolReader final
{
public:
  explicit DolReader(std::span<const u8> buffer);

  bool IsValid() const { return m_is_valid; }
  bool IsWii() const { return m_is_wii; }
  u32 GetEntryPoint() const { return m_dolheader.entry_point; }
  bool LoadIntoMemory(moderngekko::AddressSpace& memory) const;

private:
  static constexpr std::size_t NumTextSections = 7;
  static constexpr std::size_t NumDataSections = 11;

  struct DolHeader
  {
    u32 text_offset[NumTextSections];
    u32 data_offset[NumDataSections];
    u32 text_address[NumTextSections];
    u32 data_address[NumDataSections];
    u32 text_size[NumTextSections];
    u32 data_size[NumDataSections];
    u32 bss_address;
    u32 bss_size;
    u32 entry_point;
  };

  bool Initialize(std::span<const u8> buffer);

  DolHeader m_dolheader{};
  std::vector<std::vector<u8>> m_data_sections;
  std::vector<std::vector<u8>> m_text_sections;
  bool m_is_valid = false;
  bool m_is_wii = false;
};
