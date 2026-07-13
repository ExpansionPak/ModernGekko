#include "moderngekko/mmio_bus.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace moderngekko
{
namespace
{
bool IsValidWidth(std::uint8_t size)
{
  return size == 1u || size == 2u || size == 4u || size == 8u;
}
}

bool MmioBus::Register(std::uint32_t base, std::uint32_t size, ReadHandler read,
                       WriteHandler write)
{
  if (size == 0u || size - 1u > std::numeric_limits<std::uint32_t>::max() - base)
    return false;
  const std::uint32_t end = base + size;
  if (std::ranges::any_of(m_ranges, [base, end](const Range& range) {
        return base < range.base + range.size && range.base < end;
      }))
  {
    return false;
  }

  m_ranges.push_back({base, size, std::move(read), std::move(write)});
  return true;
}

const MmioBus::Range* MmioBus::Find(std::uint32_t address, std::uint8_t size) const
{
  if (!IsValidWidth(size) || size - 1u > std::numeric_limits<std::uint32_t>::max() - address)
    return nullptr;
  const std::uint32_t end = address + size;
  const auto range = std::ranges::find_if(m_ranges, [address, end](const Range& value) {
    return address >= value.base && end <= value.base + value.size;
  });
  return range == m_ranges.end() ? nullptr : &*range;
}

bool MmioBus::Read(std::uint32_t address, std::uint8_t size, std::uint64_t* value) const
{
  const Range* range = Find(address, size);
  if (range == nullptr || !range->read || value == nullptr)
    return false;
  *value = range->read(address, size);
  return true;
}

bool MmioBus::Write(std::uint32_t address, std::uint64_t value, std::uint8_t size)
{
  const Range* range = Find(address, size);
  if (range == nullptr || !range->write)
    return false;
  range->write(address, value, size);
  return true;
}

void MmioBus::Clear()
{
  m_ranges.clear();
}
}
