# Source provenance

ModernGekko is distributed under GPL-3.0-or-later. The complete license text
is in `LICENSE`.

## RecompCore and Dolphin

The Dolphin-derived runtime is pinned as the `vendor/dolphin` submodule:

- Repository: `https://github.com/ExpansionPak/RecompCore.git`
- RecompCore revision: `1873066167f3d03b39771b547f280d2b970427b6`
- Branch used for local integration: `moderngekko-runtime`
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
frontend, tooling, tests, and build files. The RecompCore integration branch
contains five local commits beyond `ExpansionPak/RecompCore` main:

- `0fafb0f796` (`support moderngekko runtime`): explicit module-source wiring,
  StaticRecomp ABI/loader integration, Wayland NoGUI support, Vulkan surface
  support, and generated-module build fixes.
- `7ddd35f373` (`fix standalone frontend build`): standalone SDL target export,
  static frontend dependency handling, and native Wayland configuration.
- `6ed835397d` (`speed up generated dispatch`): indexed generated dispatch,
  physical PC alias handling, and module-table parsing for compact dispatch runs.
- `0448c53fdf` (`improve linux windowing`): native Wayland input and window-state
  handling plus dual X11/Wayland SDL frontend support.
- `1873066167` (`use DolRecomp submodule`): replaces the embedded DolRecomp
  snapshot with a pinned `ExpansionPak/DolRecomp` submodule.

No Nintendo disc image, extracted game data, keys, or copyrighted game assets
are part of either source repository. Users and testers provide their own game
dump.

Note: written by Codex. Rewrite this soon
