# ModernGekko

ModernGekko is a playable RecompCore/Dolphin runtime for Wii and GameCube
ports. It runs covered PowerPC code from a DolRecomp native module and safely
falls back to Dolphin for unsupported or modified code.

## Play on Linux

Clone the runtime and its pinned dependencies:

```sh
git clone --recurse-submodules https://github.com/ExpansionPak/ModernGekko.git
cd ModernGekko
```

If the repository was cloned without submodules, initialize them with:

```sh
git submodule update --init --recursive --depth 1
```

Build the launcher, then double-click `build/ModernGekko`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target moderngekko-launcher
```

The launcher finds WBFS and ISO files in your Documents folder, or lets you
browse for one. Select a disc, click **Extract and Play**, and ModernGekko
keeps the extracted game in your user data directory instead of changing the
original image. A previously extracted game appears as a one-click **Play**
button the next time the launcher opens.

Wayland is the default. Pass `-X11` to `ModernGekko` when an X11 game window
is required:

```sh
build/ModernGekko -X11
```

ModernGekko imports the current Dolphin controller profile by default. For
Kirby it forces a sideways Wii Remote with no Nunchuk so the profile matches
the controls expected by the game.

Rendering resolution is configured in
`~/.local/share/moderngekko/config.ini`. The launcher writes supported Dolphin
EFB upscale values such as `1920x1080`; unsupported values are rejected.

## Command-line tools

An extracted root contains `sys/main.dol` and `files/`. Inspect, build, or run
one without writing generated files into the game dump:

```sh
build/moderngekko-port inspect /path/to/game
build/moderngekko-port build /path/to/game
build/moderngekko-port run /path/to/game
```

Run an existing native module directly:

```sh
build/moderngekko-run --game /path/to/game \
  --module /path/to/gGAMEID_recomp.so
```

A missing or rejected module is fatal unless `--allow-interpreter` explicitly
permits Dolphin-only execution. The old handwritten machine remains available
only as `moderngekko::LegacyRuntime` for conformance tests.

See `PROVENANCE.md` for pinned revisions, licenses, and the runtime patch
inventory.
