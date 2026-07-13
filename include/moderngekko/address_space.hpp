#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace moderngekko
{
class AddressSpace final
{
public:
  static constexpr std::uint32_t Mem1Base = 0x00000000u;
  static constexpr std::uint32_t Mem2Base = 0x10000000u;
  static constexpr std::uint32_t RetailMem1Size = 0x01800000u;
  static constexpr std::uint32_t RetailMem2Size = 0x04000000u;

  explicit AddressSpace(bool enable_mem2 = false);
  AddressSpace(std::size_t mem1_size, std::size_t mem2_size);

  std::span<std::uint8_t> GetMem1();
  std::span<const std::uint8_t> GetMem1() const;
  std::span<std::uint8_t> GetMem2();
  std::span<const std::uint8_t> GetMem2() const;

  std::uint8_t* Resolve(std::uint32_t address, std::size_t size);
  const std::uint8_t* Resolve(std::uint32_t address, std::size_t size) const;

  bool Read8(std::uint32_t address, std::uint8_t* value) const;
  bool Read16(std::uint32_t address, std::uint16_t* value) const;
  bool Read32(std::uint32_t address, std::uint32_t* value) const;
  bool Read64(std::uint32_t address, std::uint64_t* value) const;

  bool Write8(std::uint32_t address, std::uint8_t value);
  bool Write16(std::uint32_t address, std::uint16_t value);
  bool Write32(std::uint32_t address, std::uint32_t value);
  bool Write64(std::uint32_t address, std::uint64_t value);

  void Clear();

private:
  std::vector<std::uint8_t> m_mem1;
  std::vector<std::uint8_t> m_mem2;
};
}
