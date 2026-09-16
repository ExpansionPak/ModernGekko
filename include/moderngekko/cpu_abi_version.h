#ifndef MODERNGEKKO_CPU_ABI_VERSION_H
#define MODERNGEKKO_CPU_ABI_VERSION_H

/* The CPU ABI version, kept in a header of its own.
 *
 * moderngekko/cpu_state.h and vendor/dolphin/GXRuntime/include/core/cpu.h both
 * define `struct CPUState`, and both share the include guard DOLRECOMP_CPU_H so
 * that a translation unit reaching for either one gets exactly one definition.
 * That makes the two interchangeable, but it also means the second header to be
 * included contributes nothing at all -- including its version macro.
 *
 * Keeping the version here, behind its own guard, means MODERNGEKKO_CPU_ABI_VERSION
 * is defined regardless of which CPUState header won, and gives the conformance
 * test something to compare GXRUNTIME_CPU_ABI_VERSION against without having to
 * include two mutually exclusive headers in one translation unit.
 *
 * Version history:
 *   3 - external_pointer and downcount tail extensions.
 *   4 - cycle_budget, mirroring DolRecomp f0a86be, directly after downcount.
 */
#define MODERNGEKKO_CPU_ABI_VERSION 4u

#endif
