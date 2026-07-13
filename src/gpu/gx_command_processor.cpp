// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/gx_command_processor.hpp"

#include "moderngekko/address_space.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace moderngekko
{
namespace
{
std::uint16_t Read16(std::span<const std::uint8_t> data, std::size_t offset)
{
  return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::uint32_t Read32(std::span<const std::uint8_t> data, std::size_t offset)
{
  return (static_cast<std::uint32_t>(data[offset]) << 24) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

std::uint32_t Bits(std::uint32_t value, unsigned offset, unsigned count)
{
  return (value >> offset) & ((1u << count) - 1u);
}

std::uint32_t ComponentByteSize(std::uint32_t format)
{
  return format < 2u ? 1u : format < 4u ? 2u : 4u;
}

std::uint32_t AttributeSize(std::uint32_t descriptor, std::uint32_t direct_size)
{
  switch (descriptor)
  {
  case 0: return 0;
  case 1: return direct_size;
  case 2: return 1;
  case 3: return 2;
  default: return 0;
  }
}

std::uint32_t ColorDirectSize(std::uint32_t format)
{
  constexpr std::array<std::uint32_t, 8> sizes = {2, 3, 4, 2, 3, 4, 0, 0};
  return sizes[format & 7u];
}

std::pair<std::uint32_t, std::uint32_t> TextureFormat(std::uint32_t g0, std::uint32_t g1,
                                                       std::uint32_t g2, unsigned index)
{
  switch (index)
  {
  case 0: return {Bits(g0, 22, 3), Bits(g0, 21, 1)};
  case 1: return {Bits(g1, 1, 3), Bits(g1, 0, 1)};
  case 2: return {Bits(g1, 10, 3), Bits(g1, 9, 1)};
  case 3: return {Bits(g1, 19, 3), Bits(g1, 18, 1)};
  case 4: return {Bits(g1, 28, 3), Bits(g1, 27, 1)};
  case 5: return {Bits(g2, 6, 3), Bits(g2, 5, 1)};
  case 6: return {Bits(g2, 15, 3), Bits(g2, 14, 1)};
  case 7: return {Bits(g2, 24, 3), Bits(g2, 23, 1)};
  default: return {};
  }
}
}

GxCommandProcessor::GxCommandProcessor(GpuBackend* backend, AddressSpace* memory)
    : m_backend(backend), m_memory(memory)
{
}

void GxCommandProcessor::SetAddressSpace(AddressSpace* memory)
{
  m_memory = memory;
}

void GxCommandProcessor::SetBackend(GpuBackend* backend)
{
  m_backend = backend;
}

void GxCommandProcessor::Reset()
{
  m_buffer.clear();
  m_vcd_low = 0;
  m_vcd_high = 0;
  m_vat = {};
  m_array_bases = {};
  m_array_strides = {};
  m_command_count = 0;
  m_unknown_opcode_count = 0;
  m_display_list_depth = 0;
}

void GxCommandProcessor::Write(std::uint64_t value, std::uint8_t size)
{
  if (size != 1u && size != 2u && size != 4u && size != 8u)
    return;
  std::array<std::uint8_t, 8> bytes{};
  for (std::uint8_t i = 0; i < size; ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> ((size - i - 1u) * 8u));
  WriteBytes(std::span{bytes}.first(size));
}

void GxCommandProcessor::WriteBytes(std::span<const std::uint8_t> bytes)
{
  m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
  std::size_t consumed = 0;
  while (consumed < m_buffer.size())
  {
    const std::size_t size = DecodeOne(std::span{m_buffer}.subspan(consumed));
    if (size == 0u)
      break;
    consumed += size;
    ++m_command_count;
  }
  if (consumed != 0u)
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
}

std::size_t GxCommandProcessor::GetBufferedByteCount() const
{
  return m_buffer.size();
}

std::uint64_t GxCommandProcessor::GetCommandCount() const
{
  return m_command_count;
}

std::uint64_t GxCommandProcessor::GetUnknownOpcodeCount() const
{
  return m_unknown_opcode_count;
}

std::uint32_t GxCommandProcessor::GetVertexSize(std::uint8_t vat) const
{
  const auto& attributes = m_vat[vat & 7u];
  const std::uint32_t g0 = attributes[0];
  const std::uint32_t g1 = attributes[1];
  const std::uint32_t g2 = attributes[2];
  std::uint32_t size = std::popcount(m_vcd_low & 0x1FFu);

  const std::uint32_t position = Bits(m_vcd_low, 9, 2);
  const std::uint32_t position_components = Bits(g0, 0, 1) + 2u;
  size += AttributeSize(position, ComponentByteSize(Bits(g0, 1, 3)) * position_components);

  const std::uint32_t normal = Bits(m_vcd_low, 11, 2);
  const bool ntb = Bits(g0, 9, 1) != 0u;
  const bool index3 = Bits(g0, 31, 1) != 0u;
  if (normal == 1u)
    size += ComponentByteSize(Bits(g0, 10, 3)) * (ntb ? 9u : 3u);
  else if (normal == 2u || normal == 3u)
    size += (normal - 1u) * (ntb && index3 ? 3u : 1u);

  for (unsigned color = 0; color < 2; ++color)
  {
    const std::uint32_t descriptor = Bits(m_vcd_low, 13u + color * 2u, 2);
    const std::uint32_t format = Bits(g0, 14u + color * 4u, 3);
    size += AttributeSize(descriptor, ColorDirectSize(format));
  }
  for (unsigned texture = 0; texture < 8; ++texture)
  {
    const std::uint32_t descriptor = Bits(m_vcd_high, texture * 2u, 2);
    const auto [format, elements] = TextureFormat(g0, g1, g2, texture);
    size += AttributeSize(descriptor, ComponentByteSize(format) * (elements + 1u));
  }
  return size;
}

std::size_t GxCommandProcessor::DecodeOne(std::span<const std::uint8_t> data)
{
  if (data.empty())
    return 0;
  const std::uint8_t command = data[0];
  if (command == 0x00u)
  {
    const auto first_non_nop = std::ranges::find_if(data, [](std::uint8_t value) {
      return value != 0u;
    });
    return static_cast<std::size_t>(first_non_nop - data.begin());
  }
  if (command == 0x44u || command == 0x48u)
  {
    if (command == 0x48u && m_backend)
      m_backend->InvalidateVertexCache();
    return 1;
  }
  if (command == 0x08u)
  {
    if (data.size() < 6u)
      return 0;
    LoadCp(data[1], Read32(data, 2));
    return 6;
  }
  if (command == 0x10u)
  {
    if (data.size() < 5u)
      return 0;
    const std::uint32_t header = Read32(data, 1);
    const std::uint8_t count = static_cast<std::uint8_t>(((header >> 16) & 0xFu) + 1u);
    const std::size_t size = 5u + count * 4u;
    if (data.size() < size)
      return 0;
    std::array<std::uint32_t, 16> values{};
    for (std::uint8_t i = 0; i < count; ++i)
      values[i] = Read32(data, 5u + i * 4u);
    if (m_backend)
      m_backend->LoadXfRegisters(static_cast<std::uint16_t>(header),
                                 std::span{values}.first(count));
    return size;
  }
  if (command == 0x20u || command == 0x28u || command == 0x30u || command == 0x38u)
  {
    if (data.size() < 5u)
      return 0;
    const std::uint32_t value = Read32(data, 1);
    if (m_backend)
      m_backend->LoadIndexedXf(static_cast<std::uint8_t>(command / 8u + 8u),
                               static_cast<std::uint16_t>(value >> 16),
                               static_cast<std::uint16_t>(value & 0xFFFu),
                               static_cast<std::uint8_t>(((value >> 12) & 0xFu) + 1u));
    return 5;
  }
  if (command == 0x40u)
  {
    if (data.size() < 9u)
      return 0;
    ExecuteDisplayList(Read32(data, 1) & ~31u, Read32(data, 5) & ~31u);
    return 9;
  }
  if (command == 0x61u)
  {
    if (data.size() < 5u)
      return 0;
    const std::uint32_t value = Read32(data, 1);
    if (m_backend)
      m_backend->LoadBpRegister(static_cast<std::uint8_t>(value >> 24), value & 0xFFFFFFu);
    return 5;
  }
  if (command >= 0x80u && command <= 0xBFu)
  {
    if (data.size() < 3u)
      return 0;
    const std::uint8_t vat = command & 7u;
    const std::uint16_t count = Read16(data, 1);
    const std::uint32_t vertex_size = GetVertexSize(vat);
    if (vertex_size != 0u && count > (std::numeric_limits<std::size_t>::max() - 3u) / vertex_size)
      return 0;
    const std::size_t size = 3u + static_cast<std::size_t>(count) * vertex_size;
    if (data.size() < size)
      return 0;
    if (m_backend)
      m_backend->Draw({static_cast<GxPrimitive>((command & 0x78u) >> 3), vat, vertex_size,
                       count, data.subspan(3, size - 3)});
    return size;
  }

  ++m_unknown_opcode_count;
  return 1;
}

void GxCommandProcessor::ExecuteDisplayList(std::uint32_t address, std::uint32_t size)
{
  if (m_backend)
    m_backend->CallDisplayList(address, size);
  if (m_memory == nullptr || size == 0u || m_display_list_depth >= 16u)
    return;
  const std::uint8_t* data = m_memory->Resolve(address, size);
  if (data == nullptr)
    return;

  ++m_display_list_depth;
  std::size_t consumed = 0;
  const std::span<const std::uint8_t> commands{data, size};
  while (consumed < commands.size())
  {
    const std::size_t command_size = DecodeOne(commands.subspan(consumed));
    if (command_size == 0u)
      break;
    consumed += command_size;
    ++m_command_count;
  }
  --m_display_list_depth;
}

void GxCommandProcessor::LoadCp(std::uint8_t command, std::uint32_t value)
{
  switch (command & 0xF0u)
  {
  case 0x50: m_vcd_low = value; break;
  case 0x60: m_vcd_high = value; break;
  case 0x70: m_vat[command & 7u][0] = value; break;
  case 0x80: m_vat[command & 7u][1] = value; break;
  case 0x90: m_vat[command & 7u][2] = value; break;
  case 0xA0: m_array_bases[command & 0xFu] = value; break;
  case 0xB0: m_array_strides[command & 0xFu] = static_cast<std::uint8_t>(value); break;
  default: break;
  }
  if (m_backend)
    m_backend->LoadCpRegister(command, value);
}
}
