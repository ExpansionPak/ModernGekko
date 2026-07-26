# Source provenance

ModernGekko is distributed under GPL-3.0-or-later. The complete license text
is in `LICENSE`.

## RecompCore and Dolphin

The Dolphin-derived runtime is pinned as the `vendor/dolphin` submodule:

- Repository: `https://github.com/ExpansionPak/RecompCore-ModernGekko.git`
- Original upstream: `https://github.com/aharonahdoot/RecompCore.git`
- Original upstream revision: `53e04dc7940d0f93ff4f56b3f597a2cf7e922374`
- RecompCore revision: `8b47e90bf62a599995425cdcb9bc172c9d39fd9c`
- Branch used for integration: `main`
- Dolphin base immediately before StaticRecomp was introduced:
  `1ccbcaa04a95a5807d92429bf35598da345a3f16`
- First StaticRecomp commit:
  `cf339770523e529fefd051166ab90da6f2d7da19`

Dolphin's original code is primarily GPL-2.0-or-later and the combined source
tree is GPLv3-compatible. Its aggregate notice is in
`vendor/dolphin/COPYING`; per-file SPDX identifiers and the license texts in
`vendor/dolphin/LICENSES/` remain authoritative. DolRecomp is distributed
under GPLv3 and is pinned as RecompCore's `DolRecomp` submodule rather than
duplicated in the RecompCore source tree.

RecompCore's own nested third-party dependencies are pinned by
`vendor/dolphin/.gitmodules`. Their original notices and license files are
retained in their source trees.

## ModernGekko patch inventory

ModernGekko-owned integration is intentionally kept in the top-level runtime,
frontend, tooling, tests, and build files. The RecompCore fork is rebased on
`aharonahdoot/RecompCore` main and retains the ExpansionPak continuation changes
needed for Wii support. Its ModernGekko-specific tail is:

- `1c4110e0f3` (`support moderngekko runtime`): explicit module-source wiring,
  StaticRecomp ABI/loader integration, Wayland NoGUI support, Vulkan surface
  support, and generated-module build fixes.
- `cdde735ce6` (`fix standalone frontend build`): standalone SDL target export,
  static frontend dependency handling, and native Wayland configuration.
- `dd2d375748` (`speed up generated dispatch`): indexed generated dispatch,
  physical PC alias handling, and module-table parsing for compact dispatch runs.
- `4391a572ca` (`improve linux windowing`): native Wayland input and window-state
  handling plus dual X11/Wayland SDL frontend support.
- `f9d079c9ab` (`use DolRecomp submodule`): replaces the embedded DolRecomp
  snapshot with a pinned `ExpansionPak/DolRecomp` submodule.
- `a807cac999` (`forgot to commit these`): Windows portability, audio, input,
  and netplay runtime updates.
- `8b47e90bf6` (`support mod hooks and yielding fallback jit`): code-mod host
  calls and explicit fallback-JIT return to covered native code.

No Nintendo disc image, extracted game data, keys, or copyrighted game assets
are part of either source repository. Users and testers provide their own game
dump.
