#include "moderngekko/address_space.hpp"

#include "Common/Swap.h"

#include <algorithm>

namespace moderngekko
{
namespace
{
std::uint32_t ToPhysicalAddress(std::uint32_t address)
{
  if ((address & 0x80000000u) != 0u)
    return address & 0x3FFFFFFFu;
  return address;
}

template <typename Container>
auto ResolveRegion(Container& memory, std::uint32_t offset, std::size_t size)
    -> decltype(memory.data())
{
  if (offset > memory.size() || size > memory.size() - offset)
    return nullptr;
  return memory.data() + offset;
}
}

AddressSpace::AddressSpace(bool enable_mem2)
    : AddressSpace(RetailMem1Size, enable_mem2 ? RetailMem2Size : 0u)
{
}

AddressSpace::AddressSpace(std::size_t mem1_size, std::size_t mem2_size)
    : m_mem1(mem1_size), m_mem2(mem2_size)
{
}

std::span<std::uint8_t> AddressSpace::GetMem1()
{
  return m_mem1;
}

std::span<const std::uint8_t> AddressSpace::GetMem1() const
{
  return m_mem1;
}

std::span<std::uint8_t> AddressSpace::GetMem2()
{
  return m_mem2;
}

std::span<const std::uint8_t> AddressSpace::GetMem2() const
{
  return m_mem2;
}

std::uint8_t* AddressSpace::Resolve(std::uint32_t address, std::size_t size)
{
  const std::uint32_t physical = ToPhysicalAddress(address);
  if (physical < m_mem1.size())
    return ResolveRegion(m_mem1, physical, size);
  if (physical >= Mem2Base)
    return ResolveRegion(m_mem2, physical - Mem2Base, size);
  return nullptr;
}

const std::uint8_t* AddressSpace::Resolve(std::uint32_t address, std::size_t size) const
{
  const std::uint32_t physical = ToPhysicalAddress(address);
  if (physical < m_mem1.size())
    return ResolveRegion(m_mem1, physical, size);
  if (physical >= Mem2Base)
    return ResolveRegion(m_mem2, physical - Mem2Base, size);
  return nullptr;
}

bool AddressSpace::Read8(std::uint32_t address, std::uint8_t* value) const
{
  const std::uint8_t* data = Resolve(address, sizeof(*value));
  if (data == nullptr || value == nullptr)
    return false;
  *value = *data;
  return true;
}

bool AddressSpace::Read16(std::uint32_t address, std::uint16_t* value) const
{
  const std::uint8_t* data = Resolve(address, sizeof(*value));
  if (data == nullptr || value == nullptr)
    return false;
  *value = Common::swap16(data);
  return true;
}

bool AddressSpace::Read32(std::uint32_t address, std::uint32_t* value) const
{
  const std::uint8_t* data = Resolve(address, sizeof(*value));
  if (data == nullptr || value == nullptr)
    return false;
  *value = Common::swap32(data);
  return true;
}

bool AddressSpace::Read64(std::uint32_t address, std::uint64_t* value) const
{
  const std::uint8_t* data = Resolve(address, sizeof(*value));
  if (data == nullptr || value == nullptr)
    return false;
  *value = Common::swap64(data);
  return true;
}

bool AddressSpace::Write8(std::uint32_t address, std::uint8_t value)
{
  std::uint8_t* data = Resolve(address, sizeof(value));
  if (data == nullptr)
    return false;
  *data = value;
  return true;
}

bool AddressSpace::Write16(std::uint32_t address, std::uint16_t value)
{
  std::uint8_t* data = Resolve(address, sizeof(value));
  if (data == nullptr)
    return false;
  Common::WriteSwap16(data, value);
  return true;
}

bool AddressSpace::Write32(std::uint32_t address, std::uint32_t value)
{
  std::uint8_t* data = Resolve(address, sizeof(value));
  if (data == nullptr)
    return false;
  Common::WriteSwap32(data, value);
  return true;
}

bool AddressSpace::Write64(std::uint32_t address, std::uint64_t value)
{
  std::uint8_t* data = Resolve(address, sizeof(value));
  if (data == nullptr)
    return false;
  Common::WriteSwap64(data, value);
  return true;
}

void AddressSpace::Clear()
{
  std::ranges::fill(m_mem1, 0u);
  std::ranges::fill(m_mem2, 0u);
}
}
