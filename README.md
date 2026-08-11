# ModernGekko

A runtime for GameCube/Wii recomps.

## Code mods

The runner loads code mods from `Mods` beside the executable and from `<user-dir>/Mods` by default. It accepts package directories named `<id>.mgm` containing `mod.so`, `mod.dll`, or `mod.dylib`, and development libraries named `<id>.mgm.so`, `<id>.mgm.dll`, or `<id>.mgm.dylib`. Use `--mods <directory>` for another location or `--no-mods` to disable loading.

The mod ABI supports dependency ordering, minimum versions, optional dependencies, imports and exports, entry and return hooks, normal and forced function patches, events and callbacks, load callbacks, and exact disc/CPU ABI validation. Netplay fingerprints include every loaded package binary and its filename.

DolRecomp's optional MAP input emits named address constants for code mods. Literal addresses remain supported when a game has no MAP file. See `mod-template` for a minimal package.

## Profile-guided optimisation

`moderngekko-port pgo-run` builds an instrumented module, runs it so you can play
a representative workload, merges the profile that produces, and builds the final
module against it -- in one command.

```bash
moderngekko-port pgo-run extracted/GM4E01 --backend llvm
```

### What is being optimised

The profile describes the **generated per-game module**, not `moderngekko-port`.
The port is a build driver that runs for a few minutes; the module is the
recompiled game, and it is where every frame is spent. Profiling the tool that
produced the module would optimise the wrong program.

That is also why training is a real play session rather than a benchmark: the
profile records which of the game's basic blocks are hot, so it is only as good
as the gameplay you exercise while it runs.

### Backends

Both module backends are supported, and each needs its instrumentation applied in
a different place.

- `--backend c` -- DolRecomp emits C, so Clang's own `-fprofile-generate` and
  `-fprofile-use=` reach the chunks through the module's compile flags.
- `--backend llvm` -- DolRecomp emits and code-generates IR in-process, so no
  compile flag ever reaches those chunks. Instrumentation is applied by
  DolRecomp's own IR-level PGO passes (`DOLRECOMP_LLVM_PGO=gen` / `=use`), which
  `pgo-run` drives for you. Stale profiles are refused
  (`DOLRECOMP_LLVM_PGO_STALE=error`) rather than silently degrading to a
  half-trained module.

In both cases `-fprofile-generate` also reaches the module's **link** line, which
is where LLVM's profiling runtime comes from. Without it the instrumented module
writes no profile at all.

### Requirements

Instrumentation PGO needs **Clang** and an **`llvm-profdata` of the same major
LLVM version**. `--toolchain auto` resolves to Clang for this command; GCC and
MSVC fail immediately with an explanation rather than quietly producing an
unprofiled module.

`llvm-profdata` is looked for in this order: `--llvm-profdata`, `$LLVM_PROFDATA`,
beside the resolved `clang`, `clang --print-prog-name=llvm-profdata`, `xcrun
--find` on macOS, then `PATH`. A candidate is only accepted if it prints an LLVM
version banner, and a known major-version mismatch with Clang is a hard error.

**With `--backend llvm` there is a third LLVM to match.** The merged profile is
written by `llvm-profdata` and read back by the LLVM that DolRecomp itself is
linked against, and those are separate installations. The pinned recompiler
builds against **LLVM 19 or 20**, so a distribution whose default `clang` is
newer will merge a profile the recompiler cannot parse, and the build fails with
`unsupported instrumentation profile format version` repeated once per chunk.

`pgo-run` cannot check this for you — DolRecomp does not report its own LLVM
version — so put the matching release first on `PATH`:

```bash
export PATH=/usr/lib/llvm-20/bin:$PATH
```

Overriding only `--llvm-profdata` is not enough: the module's C sources are
compiled by whichever `clang` is on `PATH`, and that clang supplies the
profiling runtime that writes the raw profile. Check what the recompiler links
against with `ldd $(which dolrecomp) | grep -i llvm`. The `--backend c` path has
no recompiler-side profile reader and is unaffected.

### Examples

Interactive, LLVM backend:

```bash
moderngekko-port pgo-run extracted/GM4E01 --backend llvm
```

Training from a savestate, with runner options forwarded after `--` exactly as
`run` forwards them:

```bash
moderngekko-port pgo-run extracted/GM4E01 \
    --backend llvm \
    --profile-dir build/pgo/GM4E01 \
    -- \
    --load-state states/race.sav \
    --graphics Vulkan \
    --no-mods
```

C backend:

```bash
moderngekko-port pgo-run extracted/GM4E01 --backend c --toolchain clang
```

**Close the game through its own quit path.** The profiling runtime writes the
`.profraw` when the process shuts down normally; a process that is killed skips
that flush, and a run that exits nonzero aborts the workflow rather than merging
whatever happens to be on disk.

### Where things are kept

Everything a run produces lives under `--profile-dir`, or under
`<output>/<disc-id>/pgo` when that is not given:

```
<profile-dir>/
    raw/                 .profraw from the training run
    gen/                 the instrumented module's own private cache
    merged.profdata      the merged, validated profile
    pgo-manifest.txt     how the final module was produced
```

On Windows, note that the generation build nests a second module cache under
this directory, and a module build path is already close to the 260-character
limit. `pgo-run` measures the longest path it would need before building
anything and stops immediately with a message naming `--profile-dir` if it would
not fit, rather than failing twenty minutes later inside ninja. If you hit it,
pass something short such as `--profile-dir C:\mg-pgo`.

The instrumented module is built into a **private** cache root, so it can never
be written to the real `active-module.txt`. The PGO module becomes active only
after every stage has succeeded: instrumented build, clean training exit, raw
profile discovery, merge, validation, PGO build, and final module validation. If
any stage fails the command exits nonzero, names the stage, leaves the previously
active module exactly as it was, and keeps the work files for diagnosis.

`raw/` is emptied at the start of every run, so a profile left by an earlier build
cannot be merged into this one.

### Cache identity

The module cache key includes the PGO mode (`off`, `generate`, `use`) and, for
use builds, the **SHA-256 of the merged profile's contents**. That means a plain
module can never answer a PGO build, an instrumented module can never answer
either, and two different profiles written to the same scratch filename produce
different modules. The same profile copied to a different path reuses the
existing module, because the path is not part of the identity -- only what is in
the file.

The key also now folds in the code-generation-affecting DolRecomp environment
variables (`DOLRECOMP_LLVM_CPU`, `DOLRECOMP_LLVM_FEATURES`,
`DOLRECOMP_LLVM_TARGET`, `DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS`,
`DOLRECOMP_C_CHUNK_INSTRUCTIONS`, `DOLRECOMP_DISPATCH_LOOKUP`,
`DOLRECOMP_UNSAFE_DIRECT_CALLS`) when any of them is set, so builds made with
different code-generation settings stop colliding.

### Keeping the work files

A successful run removes `raw/` and `generate-modules/` once the final module is
safely published, and always keeps `merged.profdata` and `pgo-manifest.txt`. Pass
`--keep-work` to keep the raw profiles and the instrumented module too.

### Limitations of this version

- Training is **interactive**. There is no scripted or timed workload, and no
  title-specific training plan.
- A representative workload is the developer's responsibility. The profile is
  only as good as what was played.
- Clang and a matching `llvm-profdata` are required; there is no GCC or MSVC
  path.
- No universal speedup is claimed. The gain depends on the title, the backend,
  the host architecture, and the training workload.

## Credits

SpecialK / aharonahdoot - RecompCore (referenced heavily)

The Dolphin Team - Foundation of this repo

Literally God / MrPoloGit - Making the Recomp template and adding MacOS support

Please contact me if your name is missing and you contributed something!

## Hall of Fame
binsento - Super Mario Sunshine & Super Smash Bros. Brawl recomp

MOOMAN - 007 AUF

me (Hyperway) Luigi's Mansion & Kirby Wii

Literally God / MrPoloGit - Super Smash Bros. Melee

Contact me to be added to the Hall of Fame
