/* Guards the seam between moderngekko/cpu_state.h and GXRuntime's core/cpu.h.
 *
 * Both headers define `struct CPUState` behind the shared guard DOLRECOMP_CPU_H,
 * so a translation unit sees whichever one it included first and the other is
 * silently skipped. That makes drift between them invisible at build time: it
 * surfaces only when a recompiled module is rejected at load, or -- worse, if the
 * ABI version happens to agree -- as fields read at the wrong offsets.
 *
 * This translation unit deliberately includes ONLY the GXRuntime header, then
 * checks it against the version macro that moderngekko/cpu_state.h publishes
 * separately. The two cannot both be included here; that is the point.
 */
#include "moderngekko/cpu_abi_version.h"

#include <core/cpu.h>

#include <stddef.h>

_Static_assert(GXRUNTIME_CPU_ABI_VERSION == MODERNGEKKO_CPU_ABI_VERSION,
               "GXRuntime and ModernGekko disagree on the CPU ABI version; the "
               "vendor/dolphin pin and include/moderngekko/cpu_state.h must move "
               "together");

/* The tail layout that ABI 4 fixed. `cycle_budget` mirrors the field DolRecomp
 * added to its generated-code CPUState in f0a86be; if it is missing or moves,
 * every field after it shifts and generated code writes to the wrong offsets. */
_Static_assert(offsetof(CPUState, cycle_budget) > offsetof(CPUState, downcount),
               "cycle_budget must follow downcount");
_Static_assert(offsetof(CPUState, exram) > offsetof(CPUState, cycle_budget),
               "cycle_budget must sit between downcount and exram");
_Static_assert(sizeof(((CPUState*)0)->cycle_budget) == 8u,
               "cycle_budget is s64 in DolRecomp's generated-code prefix");
_Static_assert(sizeof(((CPUState*)0)->downcount) == 8u,
               "downcount is s64 in DolRecomp's generated-code prefix");

/* The prefix generated code indexes directly. */
_Static_assert(offsetof(CPUState, gpr) == 0,
               "generated code expects CPUState.gpr at offset 0");
_Static_assert(offsetof(CPUState, fpr) > offsetof(CPUState, gpr),
               "CPUState register prefix order changed");
_Static_assert(offsetof(CPUState, external_pointer) > offsetof(CPUState, ram_size),
               "external_pointer must remain a tail extension");
_Static_assert(offsetof(CPUState, spr_read) > offsetof(CPUState, exram_size),
               "SPR callbacks must follow the exram pair");
_Static_assert(offsetof(CPUState, cache_control) > offsetof(CPUState, spr_write),
               "cache_control must remain the final field");

int main(void)
{
    /* Everything here is checked at compile time. */
    return 0;
}
