# Plan: Diablo II on Apple Silicon

Same approach as the Dungeon Siege ports: the user's own copy (1.14d `Game.exe`) is recompiled from x86 into C at
install time, compiled for arm64 and linked with a macOS implementation of the Windows APIs it uses. Nothing from the
game is committed.

## Status (2026-10-06)

| Step | State |
|---|---|
| Survey of 1.14d | `Game.exe` (3.6 MB; Blizzard merged the old DLLs into it) imports DirectDraw, DirectSound and Winsock by ordinal, Bink, Smacker and the Intel JPEG Library (game-folder DLLs), crypt32/wintrust, imm32, gdi32, user32, advapi32 |
| Analysis | 18,414 functions, 95.6% of `.text`, 782 jump tables resolved |
| Lifting | 0 internal errors; unsupported: SSE2 C-runtime paths (the reported CPU has no SSE2 or MMX), data decoded as code. Added: `repz ret`, `cmpxchg8b`, 16-bit `mul`/`imul`/`div`/`idiv` |
| Multi-image build | the game plus `binkw32.dll`, `SmackW32.dll` (rebased to 0x11000000) and `ijl11.dll`: 20,315 functions, about 2 minutes |
| Windows imports | 639 imported; 615 implemented, the rest unused so far |
| Renderers | DirectDraw (palettized, versions 1-7) and the windowed GDI mode, chosen by a start-up hook; Direct3D not provided |
| Video | Bink cinematics through DirectDraw (16-bit) with sound |
| Sound | DirectSound on Core Audio: streaming music, effects, speech (verified against the archives) |
| Single player | character creation, Act 1, dialogue, saving and loading |
| Multiplayer | TCP/IP host and join (two instances on one Mac verified) |
| Installer, launch window, updater | done (`install.sh`, `recomp/host/launcher.m`) |
| Tests | `recomp/tests/run_tests.sh`: present shader, launch window, the game end to end |

## Next

- Play further: Acts 2-5, waypoints, every class's skills (each new code path may reach an unsupported instruction or
  an unimplemented import; the crash report names it).
- Open Battle.net / Battle.net: untested (needs the player's own CD keys for Battle.net).
- Optional: the palette lookup on the GPU (upload 8-bit pictures and the palette: a quarter of the bandwidth).
- Optional: a Glide-style high-resolution mode is out of scope (it would need Glide; the game's 3dfx mode loads
  `glide3x.dll`).
- Mouse: the game's pointer follows the Mac's relative motion; an absolute mode (the Mac pointer's position as the
  game's) may suit windowed play better.
