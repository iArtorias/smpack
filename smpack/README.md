# smpack CLI

> The command line part of smpack, the AIO modding tool for God of War Ragnarok. It unpacks
> every shipped container, converts textures, exports and imports models, and builds the files
> that get texture, model, animation, audio and shader mods into the game.

> [!NOTE]
> Most people want the desktop app described in the [main README](../README.md). This tool is
> what the app runs under the hood, and it is the place for scripting and batch work.

---

## Overview

The formats were reverse engineered and checked against the shipped files.

| Format | Read | Write | Notes |
|---|---|---|---|
| `.wad`, `.wypdb`, `exec/dc/*.dcb` (WTOC) | Yes | Full rebuild | Chunks can change size, extract and pack is byte identical |
| `.texpack`, `.texpack.toc` | to DDS | Patch packs | Native AGC detiler, bit exact with Sony's dynamic library |
| WAD textures (low mips, small textures) | To DDS | Yes | Linear AGC layout |
| Models (`MESH_`, `MG_`, static meshes) | To glTF, FBX | From glTF | Skeleton, skin weights, variants |
| `.lodpack`, `.lodpack.toc` (geometry) | Yes | Patch packs | Block replacement by hash |
| `.as` (streamed animation sets) | Yes | Yes | WAD stubs are updated |
| `.shaderpack` | To DXBC | Yes | No-op rebuild is byte identical |
| `.audiopack` (Wwise `.wem`) | Yes | Yes | Replace or add streams, round trip is byte identical |
| `exec/boot-options.json` | Yes | Yes | Registers patch packs |
| LZ4 frames (`filehashes.csv` and others) | Yes | Yes | `smpack lz4 d/c` |

The format is detected from the file content, not the extension. LZ4 framed files are unwrapped
automatically, and multi GB `.texpack`, `.lodpack` payloads are memory mapped instead of read
into RAM. `--json` prints machine readable output, which is what the app uses.

---

## Usage

The steps below replace one texture. Models, audio and the other formats follow the same
pattern: export, edit, build a patch, install. See [Workflows](#workflows).

### 1. Build the texture index

Texture names only exist in WADs, so build an index once. It reads every `.wad` and takes a
minute or two.

```bat
smpack index "<game>\exec\wad\pc_le" -o texindex.json
smpack find texindex.json wolf00_head_d
```

### 2. Export the texture

```bat
smpack tex export "<game>\exec\wad\pc_le\<pack>.texpack" --index texindex.json --filter wolf00_head_d -o work
```

Every exported `.dds` gets a `.json` sidecar. Keep it next to the DDS, the import uses it to
find the IDs, descriptor and target file.

### 3. Edit it

Keep the same BC format and save the full mip chain, for example
`texconv -f BC1_UNORM_SRGB -m 0 -y edited.png`. The resolution may change if the aspect ratio
stays the same.

### 4. Build and install the patch

```bat
smpack tex import work --patch mymod --wad "<game>\exec\wad\pc_le\add_atreusplayable00.wad" -o out --install "<game>"
```

This writes a patch texpack, copies it into the game, backs up anything it overwrites as
`*.smpack-orig` and registers the patch in `exec\boot-options.json`.

> [!TIP]
> `--wad` is optional. It also rewrites the 64x64 copy the WAD keeps of each streamed texture,
> so distant objects match the edit. Small textures that live only in a WAD (`streamed: false`
> in the sidecar) are exported with `smpack tex export x.wad`, and `tex import` writes a new WAD
> for them.

---

## Command reference

```
smpack info    <file>
smpack list    <file>                [--filter TEXT] [--index texindex.json] [--json]
smpack extract <file> [-o DIR]       [--filter TEXT] [--raw] [--no-dds] [--blocks]

    .wad, .wypdb  - Chunks and 'manifest.json'.
    .texpack      - '<ID or name>.dds' and .json per texture (needs the payload).
    .lodpack      - Groups and blocks with -'-blocks' and 'manifest.json'.
    .as           - One .anmset per animation set and 'manifest.json'.
    .shaderpack   - 'shaders/*.dxbc' and sections, and 'manifest.json'.
    .audiopack    - '<file id>.wem' (Wwise streams)
    Other LZ4     - Decompressed file.

smpack tex list    <file.texpack|.toc|.wad>   [--index texindex.json] [--json]
smpack tex export  <file.texpack|.wad> [-o DIR] [--filter TEXT] [--name EXACT] [--id HEX] [--index F] [--raw]
smpack tex import  <dds files/dirs...> [--patch NAME] [-o DIR] [--wad FILE.wad ...] [--game DIR]
                    [--install GAME_DIR] [--no-compress]

    DDS files must keep the .json sidecar written by export. Texpack textures
    become a patch texpack '<NAME>.texpack/.toc'. WAD textures rewrite the WAD.
    '--wad' also refreshes the WAD resident low mips of streamed textures.

smpack tex verify  <file.texpack> [--limit N] [--sce-dll GAME_DIR (Windows)]
smpack wad pack    <extracted dir> [-o out.wad] [--no-compress] [--level N]
smpack wad replace <in.wad> <#index|name[type]> <file> [-o DIR] [--debug-file F] [--temp-file F]
smpack mesh list   <file.wad> [--filter TEXT] [--json]
smpack mesh export <file.wad> [-o DIR] [--name EXACT | --id N | --filter TEXT] [--as glb|gltf|fbx]
                    [--lod N|all] [--variant NAME|N|all] [--all-parts] [--no-textures] [--world] [--json]

    Metres, Y up, bind pose with skeleton and skin weights. Streamed LODs are read from the
    .lodpack files next to the WAD. A missing one falls back to the next LOD.
    Parts on hidden joints (wounds, damage states) are left out unless '--all-parts' is given.
    Models with visual variants export the default variant unless '--variant' picks one.
    Materials reference 'textures/<name>.png' (base color with the cutout in alpha, normal map).
    Level props are moved to the origin unless '--world' is given.

smpack mesh import <file.wad> <model.glb|gltf> [--name EXACT | --id N] [--lods all|same] [--game DIR]
                    [--patch NAME] [--append] [-o DIR] [--install GAME_DIR] [--json]

    Replaces the geometry of a model with an edited export. Nodes and materials are matched by
    the names the export wrote ('<model>_part<N>', 'MAT_<hash>'). '--lods all' (default) also
    writes the new mesh into the coarser LODs. Streamed geometry goes into a patch lodpack.

smpack lod patch   --pack <orig.lodpack> <blocks/HASH.bin ...> [--patch NAME] [-o DIR] [--install GAME_DIR]
smpack anim replace <x.as> <x.wad> --set <name|#i> --blob <f.anmset> [...] [-o DIR] [--no-identity]
smpack shader replace <x.shaderpack> <#i|name|hash> <file.dxbc> [-o FILE]
smpack audio replace <x.audiopack.toc> <ID.wem ...> [-o DIR] [--add] [--pristine] [--install GAME_DIR]

    '--pristine' reads '*.smpack-orig' backups so rebuilding never stacks edits.

smpack index <exec/wad/pc_le> [-o texindex.json]
smpack find  <texindex.json> <name|hash>
smpack patch add|remove|list <GAME_DIR> [--texpack NAME] [--lodpack NAME] [--json]
smpack lz4 d <in> <out>        smpack lz4 c <in> <out> [--level N]
smpack --version

    Prints the current program version.

smpack --author

    Prints the program's author.
```

---

## Workflows

### WAD chunks

Materials, gameplay data, Lua, animations and meshes all live in WAD chunks.

```bat
smpack extract boatglobal.wad -o bg
:: Edit or replace files in 'bg\chunks\' (sizes may change)
smpack wad pack bg -o out\boatglobal.wad
```

Or swap a single chunk: `smpack wad replace boatglobal.wad ANM_paddle00@anim new.bin -o out`.

The manifest is plain JSON. Entry metadata (flags, versions) can be edited and entries can be
reordered, added or removed. On rebuild, arena offsets, alignment padding and per type totals
are recomputed the way the loader expects. The game also reads uncompressed WADs
(`--no-compress`), which is handy while iterating.

### Models

```bat
smpack mesh list   r_bandit00.wad :: ID, kind, parts, LODs, variants
smpack mesh export r_bandit00.wad --name bandit00_0 -o models :: Default variant
smpack mesh export r_bandit00.wad --name bandit00_0 --variant all -o models
smpack mesh export muspelheim_zoo.wad --filter rock --as fbx --lod all -o models
```

Each `MESH_<name>` model is written as glTF 2.0 (`.glb`, or `.gltf` with a `.bin`) or binary FBX
7.4 with positions, normals, every UV set, vertex colors and one material per game material
(`MAT_<hash>`). Streamed LODs come from the `.lodpack` files next to the WAD, and when a pack is
missing the next coarser LOD is used. Units are metres, Y up. Level props are moved to the
origin unless `--world` is given.

- **Skeleton:** jointed models get their skeleton and skin weights (four strongest influences, 
  decoded from the 4, 7 and 10 influence layouts). Rigid parts are attached to their joint.
- **Hidden parts:** parts on hidden joints (wounds, bruises, arrows, damage states) are skipped
  unless `--all-parts` is given.
- **Variants:** models with variant configs export the default variant, `--variant NAME|N|all`
  picks another one.
- **Textures:** materials reference `textures/<name>.png` for the base color and the normal
  map. A separate cutout map goes into the alpha channel and the material uses alpha mask. The
  app writes these PNGs, with the CLI you convert the textures yourself.

To put an edited model back:

```bat
smpack mesh import r_baldur00.wad baldur00_0_edit.glb --patch mymod -o out --install <game>
```

Nodes are matched by the export's extras or by name (`<model>_part<N>[_lod<K>]`), materials by
name (`MAT_<hash>`, a `.001` suffix is ignored). Each primitive is re-encoded in its original
vertex layout and appended to the WAD chunk or lodpack block, then the primitive parameters,
bounds, meshlets and tessellation patches are rebuilt. `--lods all` (default) also writes the
new mesh into the coarser LODs. Streamed geometry goes into a patch lodpack, and `--append`
adds to an existing one so several models can share a patch.

### Animations

`.as` files hold the streamed animation sets of the WAD with the same name.

```bat
smpack extract add_atreusplayable00.as -o as :: One .anmset per set
smpack anim replace add_atreusplayable00.as add_atreusplayable00.wad --set envGapJump --blob as\0002_envRaceExit.anmset -o out
```

The replacement keeps the target's name, hash and ID unless `--no-identity` is given, so
everything that references the set still resolves, and every WAD stub that points at a moved
or resized set is repointed. Animations that are not streamed live in WAD `ANM_*` chunks and
are changed with the WAD workflow.

### Geometry blocks

```bat
smpack extract 170_midgard10_postgame.lodpack -o lod --blocks :: blocks\<hash>.bin
:: Replace some 'blocks\<hash>.bin'
smpack lod patch --pack 170_midgard10_postgame.lodpack lod\blocks\0031812a013c6a57dc.bin --patch mygeo --install <game>
```

Patch lodpacks are registered after the base packs. The engine's block map lets the last
registration win, so the patch overrides by block hash.

### Audio

```bat
smpack extract 150_niflheim1.audiopack.toc -o au :: <wwise id>.wem
smpack audio replace 150_niflheim1.audiopack.toc my\323303041.wem -o out --install <game>
```

Use Wwise encoded `.wem` files. `--pristine` reads the `*.smpack-orig` backups, so rebuilding
after an install never stacks edits. The app adds playback and conversion on top (vgmstream
for decoding, Media Foundation for MP3, WwiseConsole for encoding).

### Shaders

`smpack extract x.shaderpack` dumps the DXBC and DXIL containers and `shader replace` rebuilds the
pack.

> [!WARNING]
> D3D12 only loads signed DXIL, so sign edited bytecode before putting it back.

---

## Limitations

- Model import reads glTF only and keeps the skeleton and the set of parts and materials.
  Meshlet and tessellated geometry has had less testing in game than plain meshes, and placement
  bounds of static level props are not updated.
- Animations are not exported. Animation sets can only be swapped as binary blobs.
- Texture formats must match the original. A BC7 DDS over a BC1 texture is refused.
- Native detiling covers linear, Standard256B, Standard4KB and Standard64KB 2D, array and cube
  surfaces, which is everything assets use. 
  3D textures need the Sony dynamic library path (`--sce-dll`, Windows only).
- A streamed texture can not use more than about 100 MB per mip block (the engine's stream
  buffer), so textures up to 8K are fine.

---

## Compilation and dependencies

Any C++23 compiler works: MSVC 2022 17.8+, GCC 13+ or Clang 17+.

```bat
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

The only dependency is [`LZ4`](https://github.com/lz4/lz4) (BSD 2-Clause), included in
`third_party/lz4`.