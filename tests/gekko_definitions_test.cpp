#include "Core/PowerPC/Gekko.h"

int main()
{
  const UGeckoInstruction addi{0x38600001u};
  if (addi.OPCD != 14u || addi.RD != 3u || addi.RA != 0u || addi.SIMM_16 != 1)
    return 1;

  const UGeckoInstruction branch{0x48000005u};
  if (branch.OPCD != 18u || branch.LK != 1u || branch.AA != 0u || branch.LI != 1u)
    return 2;

  const UReg_MSR msr{0x00002032u};
  if (!msr.RI || !msr.DR || !msr.IR || !msr.FP || msr.EE)
    return 3;

  UReg_XER xer{};
  xer.CA = 1u;
  xer.OV = 1u;
  if (!xer.CA || !xer.OV)
    return 4;

  return 0;
}
