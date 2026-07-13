#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace moderngekko
{
class MmioBus final
{
public:
  using ReadHandler = std::function<std::uint64_t(std::uint32_t address, std::uint8_t size)>;
  using WriteHandler =
      std::function<void(std::uint32_t address, std::uint64_t value, std::uint8_t size)>;

  bool Register(std::uint32_t base, std::uint32_t size, ReadHandler read, WriteHandler write);
  bool Read(std::uint32_t address, std::uint8_t size, std::uint64_t* value) const;
  bool Write(std::uint32_t address, std::uint64_t value, std::uint8_t size);
  void Clear();

private:
  struct Range
  {
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    ReadHandler read;
    WriteHandler write;
  };

  const Range* Find(std::uint32_t address, std::uint8_t size) const;
  std::vector<Range> m_ranges;
};
}
