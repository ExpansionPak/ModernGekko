#include "Common/Align.h"
#include "Common/BitUtils.h"
#include "Core/PowerPC/ConditionRegister.h"

#include <cstdint>

int main()
{
  PowerPC::ConditionRegister condition_register{};
  condition_register.Set(0x8A531FC0u);

  if (condition_register.Get() != 0x8A531FC0u)
    return 1;

  condition_register.SetBit(0u, 0u);
  condition_register.SetBit(1u, 1u);
  if (condition_register.GetBit(0u) != 0u || condition_register.GetBit(1u) != 1u)
    return 2;

  if (Common::AlignUp<std::uint32_t>(0x101u, 0x20u) != 0x120u)
    return 3;
  if (Common::ExtractBits<std::uint32_t>(0x12345678u, 8u, 15u) != 0x56u)
    return 4;
  if (!Common::IsValidLowMask<std::uint32_t>(0x0000FFFFu))
    return 5;

  return 0;
}
