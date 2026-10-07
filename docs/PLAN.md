# Plan: Dungeon Siege II on Apple Silicon

Same approach as the Dungeon Siege 1 port: the user's own GOG copy is recompiled from x86 into C at install time,
compiled for arm64 and linked with a macOS implementation of the Windows APIs it uses. Nothing from the game is
committed.

## Status (2026-10-05)

| Step | State |
|---|---|
| Survey of the GOG 2.3 release | done: see `LLM-Decomp-Guides/dungeon-siege-2` |
| Analysis (`recomp/tools/analyze.py`) | 85,833 functions, 97.1% of `.text` (gap scanning repeated until nothing new; 479 jump tables resolved) |
| Lifting (`recomp/tools/lift.py`) | all functions lifted, 0 internal errors; ~1,000 unsupported instructions, almost all SSE2 C-runtime paths a CPU without SSE2 skips, or data decoded as code; `lock cmpxchg` added |
| Multi-image build (`recomp/build.sh`) | the game plus Miles (`mss32.dll`, `msssoft.m3d`, `mssmp3.asi`, `mssdsp.flt`) and Bink: 62,226 functions, compiles and links in about 4 minutes |
| Windows imports | 517 imported; DS1's runtime covered 345, `ds2_imports.c` adds the rest except Direct3D 9/D3DX (stubs) and Winsock |
| Start-up | **playable**: main menu, new hero, difficulty, intro movie, opening cutscene, then gameplay (NPC dialogue, HUD, minimap) at a steady 60 fps (about 2 ms GPU per frame). Path to the menu: C runtime start-up, settings, archives (filled on first touch through the game's exception filter), Bink intro movie, Winsock, menu art |
| Direct3D 9 (`d3d9.c`, `d3d9_fmt.c`) | device, swap chain, surfaces, 2D/cube/volume textures (CPU copy in the game's format, BGRA8 on the GPU; DXT decoded), vertex/index buffers, declarations (to FVF), state blocks, queries, fixed-function draws, cursor; shaders stored but not run |
| D3DX (`d3dx9.c`) | `D3DXCreate*Texture`, textures from DDS/TGA/BMP in memory (missing mips box-filtered, DXT re-encoded), `D3DXLoadSurfaceFromMemory`; effects/HLSL still stubs |
| Winsock (`winsock.c`) | WSOCK32 1.1 by ordinal on BSD sockets; test mode binds to loopback only |

## Next steps

| # | Work | Notes |
|---|---|---|
| 1 | ~~Direct3D 9 device~~ | done to the main menu; extend as the game calls more |
| 2 | ~~D3DX texture loading~~ | done (DDS/TGA/BMP); PNG/JPEG not yet (not seen so far) |
| 3 | **Shaders and effects** (optional now: with the Radeon 8500-class identity the game falls back to fixed function when `D3DXCreateEffect` fails, and renders the world, characters and UI) | 12 effect files (SM 1.1/2.0 HLSL). Start with effects reported unsupported (the game should fall back), then translate. Options: vkd3d-shader (LGPL) at install time, or a small HLSL-subset → MSL translator. |
| 4 | **C++ exceptions** | ~1,400 functions with try blocks: catch continuations in the lifter/runtime (extends DS1's `__except` landing pads). Needed as soon as the game throws in normal play. |
| 5 | Analysis coverage: 97.1%; 73 jump tables still bounded heuristically | as in DS1. |
| 6 | Text rendering (GDI glyphs), Winsock, audio check, movies | |
| 7 | Launcher, installer, updater, app bundle (from DS1) | |
| 8 | Multiplayer | **Disabled by the game itself in this build**: `UIFrontend::TransitionToMain` (0x44c2d3) always calls `UIButton::DisableButton` on `button_multiplayer` (and `main_menu.gas` calls a misspelled `SelectMultiPlayer$`). Its network layer is DirectPlay 8 (DS1's `dpnet.c`/`dp8proto.c` would carry over) over Winsock. Re-enabling it changes the game's behaviour: the user's decision. |
| 9 | Performance, hardening | 60 fps at the display's resolution with 2-3.5 ms GPU; a 6-minute random-input soak is clean. |

## Running a development build

```sh
recomp/build.sh ~/Games/DungeonSiege2/game/DungeonSiege2.exe      # game folder unpacked from the GOG installer (innoextract)
recomp/tools/build_app.sh recomp/work/full
DS_TEST=1 W32_AUDIO_MUTE=1 recomp/work/full/app/Diablo2Native --exe recomp/work/DungeonSiege2.exe \
    --game ~/Games/DungeonSiege2/game --data <scratch data folder>
```
