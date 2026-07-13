// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Boot/DolReader.h"

#include "Common/Swap.h"
#include "moderngekko/address_space.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

DolReader::DolReader(std::span<const u8> buffer)
{
  m_is_valid = Initialize(buffer);
}

bool DolReader::Initialize(std::span<const u8> buffer)
{
  if (buffer.size() < sizeof(DolHeader) || buffer.size() > std::numeric_limits<u32>::max())
    return false;

  std::memcpy(&m_dolheader, buffer.data(), sizeof(DolHeader));
  u32* header_words = reinterpret_cast<u32*>(&m_dolheader);
  for (std::size_t i = 0; i < sizeof(DolHeader) / sizeof(u32); ++i)
    header_words[i] = Common::swap32(header_words[i]);

  const u32 hid4_pattern = Common::swap32(0x7C13FBA6u);
  const u32 hid4_mask = Common::swap32(0xFC1FFFFFu);

  m_text_sections.reserve(NumTextSections);
  for (std::size_t i = 0; i < NumTextSections; ++i)
  {
    if (m_dolheader.text_size[i] == 0u)
    {
      m_text_sections.emplace_back();
      continue;
    }

    const u32 offset = m_dolheader.text_offset[i];
    const u32 size = m_dolheader.text_size[i];
    if (offset > buffer.size() || size > buffer.size() - offset)
      return false;

    const u8* start = buffer.data() + offset;
    m_text_sections.emplace_back(start, start + size);

    for (std::size_t word_index = 0; !m_is_wii && word_index < size / sizeof(u32);
         ++word_index)
    {
      u32 word;
      std::memcpy(&word, start + word_index * sizeof(u32), sizeof(word));
      if ((word & hid4_mask) == hid4_pattern)
        m_is_wii = true;
    }
  }

  m_data_sections.reserve(NumDataSections);
  for (std::size_t i = 0; i < NumDataSections; ++i)
  {
    if (m_dolheader.data_size[i] == 0u)
    {
      m_data_sections.emplace_back();
      continue;
    }

    const u32 offset = m_dolheader.data_offset[i];
    const u32 size = m_dolheader.data_size[i];
    if (offset > buffer.size())
      return false;

    std::vector<u8> data(size);
    std::memcpy(data.data(), buffer.data() + offset,
                std::min<std::size_t>(size, buffer.size() - offset));
    m_data_sections.emplace_back(std::move(data));
  }

  return true;
}

bool DolReader::LoadIntoMemory(moderngekko::AddressSpace& memory) const
{
  if (!m_is_valid)
    return false;

  for (std::size_t i = 0; i < m_text_sections.size(); ++i)
  {
    if (m_text_sections[i].empty())
      continue;
    u8* destination = memory.Resolve(m_dolheader.text_address[i], m_text_sections[i].size());
    if (destination == nullptr)
      return false;
    std::memcpy(destination, m_text_sections[i].data(), m_text_sections[i].size());
  }

  for (std::size_t i = 0; i < m_data_sections.size(); ++i)
  {
    if (m_data_sections[i].empty())
      continue;
    u8* destination = memory.Resolve(m_dolheader.data_address[i], m_data_sections[i].size());
    if (destination == nullptr)
      return false;
    std::memcpy(destination, m_data_sections[i].data(), m_data_sections[i].size());
  }

  if (m_dolheader.bss_size != 0u)
  {
    u8* bss = memory.Resolve(m_dolheader.bss_address, m_dolheader.bss_size);
    if (bss == nullptr)
      return false;
    std::memset(bss, 0, m_dolheader.bss_size);
  }

  return true;
}
