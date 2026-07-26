# ModernGekko mod template

Configure a package with a disc ID and a function address from DolRecomp's generated symbol header:

```sh
cmake -S . -B build -G Ninja \
  -DMODERNGEKKO_SOURCE_DIR=/path/to/ModernGekko \
  -DMOD_GAME_ID=SUKE01 \
  -DMOD_ID=my_mod \
  -DMOD_PATCH_ADDRESS=0x80000000
cmake --build build
```

To patch by MAP name, also pass `-DMOD_SYMBOL_HEADER=/path/to/generated_symbols.h -DMOD_PATCH_ADDRESS=DOLRECOMP_SYMBOL_FunctionName`.

Copy `build/my_mod.mgm` into the runner's `Mods` directory. A single-file development build named `my_mod.mgm.so`, `my_mod.mgm.dll`, or `my_mod.mgm.dylib` can also be placed there.

The public ABI provides `RECOMP_PATCH`, `RECOMP_FORCE_PATCH`, `RECOMP_HOOK`, `RECOMP_HOOK_RETURN`, `RECOMP_EXPORT`, `RECOMP_IMPORT`, `RECOMP_DECLARE_EVENT`, and `RECOMP_CALLBACK`. Dependencies and semantic versions live in `ModernGekkoModDesc`. Mods are ordered by dependencies and then package filename.
