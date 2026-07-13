#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace moderngekko
{
struct ControllerState
{
  bool connected = false;
  std::uint16_t buttons = 0;
  std::uint8_t stick_x = 128;
  std::uint8_t stick_y = 128;
  std::uint8_t substick_x = 128;
  std::uint8_t substick_y = 128;
  std::uint8_t trigger_left = 0;
  std::uint8_t trigger_right = 0;
  std::uint8_t analog_a = 0;
  std::uint8_t analog_b = 0;
};

class ControllerPort final
{
public:
  using Provider = std::function<ControllerState()>;

  void SetProvider(Provider provider);
  void SetMode(std::uint8_t mode);
  bool Read(std::uint32_t* high, std::uint32_t* low) const;

private:
  Provider m_provider;
  std::uint8_t m_mode = 3;
};
}
