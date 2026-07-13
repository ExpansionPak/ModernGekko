#include "moderngekko/address_space.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

int main()
{
  moderngekko::AddressSpace memory(true);

  if (!memory.Write32(0x80000100u, 0x12345678u))
    return 1;

  std::uint32_t value32 = 0u;
  if (!memory.Read32(0x00000100u, &value32) || value32 != 0x12345678u)
    return 2;
  if (!memory.Read32(0xC0000100u, &value32) || value32 != 0x12345678u)
    return 3;

  const std::array<std::uint8_t, 4> expected = {0x12u, 0x34u, 0x56u, 0x78u};
  const std::uint8_t* bytes = memory.Resolve(0x80000100u, expected.size());
  if (bytes == nullptr || !std::ranges::equal(expected, std::span(bytes, expected.size())))
    return 4;

  if (!memory.Write64(0x90000020u, 0x0123456789ABCDEFull))
    return 5;
  std::uint64_t value64 = 0u;
  if (!memory.Read64(0xD0000020u, &value64) || value64 != 0x0123456789ABCDEFull)
    return 6;

  if (memory.Resolve(0x817FFFFFu, 2u) != nullptr)
    return 7;
  if (memory.Resolve(0xCC000000u, 4u) != nullptr)
    return 8;

  memory.Clear();
  if (!memory.Read32(0x80000100u, &value32) || value32 != 0u)
    return 9;

  return 0;
}
