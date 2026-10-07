# Diablo II for Apple Silicon Macs

Diablo II and Lord of Destruction (patch 1.14d), statically recompiled from your own copy into native arm64 code and
run on Metal and Core Audio, the way [dungeon-siege-macos](https://github.com/Joshspeakman/dungeon-siege-macos) and
dungeon-siege-2-macos do for the Dungeon Siege games. No Wine, no Rosetta, no emulator. Nothing from the game is in
this repository: the build regenerates everything locally from your copy, and the game folder is only read.

**State: Beta 1.** The game is playable natively:

- the Blizzard intro and the cinematics (Bink, with sound), the title, the menus;
- single player: new characters of every class, Act 1 and onwards, the HUD, dialogue with speech, saving and loading;
- TCP/IP games (Other Multiplayer → TCP/IP): host on this Mac, join from another, play together;
- the game's own DirectDraw renderer (default) or its windowed mode, both drawing the original 8-bit pictures, shown
  with a choice of scaling filters (sharp pixels, smooth, nearest, CRT scanlines);
- sound through DirectSound on Core Audio: music, effects, speech.

Battle.net and Open Battle.net are untested. 1.14d's Direct3D mode is not provided (its DirectDraw mode is chosen
instead, see below). What's next: [docs/PLAN.md](docs/PLAN.md).

## Install

Needs an Apple Silicon Mac, the Xcode command line tools (`xcode-select --install`), python3, and Diablo II updated to
**1.14d** (Blizzard's last classic patch: `Game.exe` and the game's `.mpq` archives in one folder, as a Windows,
Wine or CrossOver installation has them).

```sh
git clone <this repository> && cd diablo-2-macos
./install.sh                                         # finds the game in the usual places (Wine/CrossOver bottles, /Applications)
./install.sh --game-dir "/path/to/Diablo II"         # or names the folder
./install.sh --nightly                               # "Diablo II Nightly": the nightly branch, beside the stable app
```

This checks the game's `Game.exe` (1.14d), recompiles it with the game's Bink, Smacker and JPEG libraries (about two
minutes), and installs **Diablo II Native** in `~/Applications`. Settings, characters and logs live in
`~/Games/Diablo2Native` (characters in `drive_c/Users/player/Saved Games/Diablo II`, as on Windows; copy `.d2s` files
there to bring characters over). The game runs in macOS full screen with Game Mode on.

The launch window (in the game's own look: the main menu's backdrop and logo, decoded from your archives at install)
has:

| Setting | Choices |
|---|---|
| Video | **DirectDraw** (the classic renderer, plays the cinematics) · Windowed Mode (the game's `-w` renderer; no cinematics) |
| Display → Scaling | **Sharp** (square pixels, smoothed only at their edges) · Smooth (bilinear) · Pixel (nearest) · CRT (sharp with scanlines) |
| Display → Notch | Hide the Notch (the picture below the camera) · Around the Notch |
| Updates | rebuilds the app from the newest commit of its branch, from your own copy |

The game's own options (gamma, contrast, sound and music volume, light quality, ...) are in its Options menu and are
kept between sessions.

## Tests

```sh
recomp/tests/run_tests.sh                        # no game files needed: the present shader and the launch window
recomp/tests/run_tests.sh "/path/to/Diablo II"   # and the game end to end (after ./install.sh or recomp/build.sh)
```

- **Present shader** (`present_shader_test.m`): the Metal renderer draws a known picture into an offscreen drawable;
  every pixel is checked for each scaling filter (whole-pixel blocks, blends only at block edges, bilinear, scanline
  shading) and for the gamma ramp.
- **Launch window** (`launcher_test.m`): its settings reach the runtime and are remembered; it draws with the art.
- **The game** (`game_test.py`), on your copy in a scratch folder, through the window's own input path:
  - *video*: the Bink intro is shown (640×480 16-bit pictures that move) with its sound;
  - *menus, ingame, gdi*: every 8-bit picture on screen is exactly the game's palette for that frame (no colour lost
    between the game's pixels and the screen), in both renderers; the life and mana orbs are drawn in Act 1;
  - *speech*: talking to Warriv plays his introduction, and the recorded output matches the same speech decoded
    independently from `d2speech.mpq` (`tools/mpq.py`: Huffman + ADPCM + implode) with correlation ≥ 0.8 (0.95–0.97
    measured) at the same offset in several windows (right speed, no drift);
  - *walk, save, load*: a click moves the character; Save and Exit writes a valid `.d2s`; Single Player lists it and
    loads it back into the game;
  - *tcpip*: two instances, one hosting and one joining by this Mac's LAN address, both in Act 1.

## How it works

`recomp/build.sh <Game.exe>` analyses the executable (`tools/analyze.py`: 18,414 functions, 95.6% of `.text`),
lifts every function to C (`tools/lift.py`: x86 to C with lazy flags, x87, SEH), does the same for `binkw32.dll`,
`SmackW32.dll` (moved off Bink's address by `tools/rebase_pe.py`) and `ijl11.dll`, and compiles it all for arm64
(about 20,000 functions). The runtime (`recomp/runtime/win32`) is a macOS implementation of the Windows APIs the game
uses (615 functions: kernel32, user32, gdi32, advapi32, DirectDraw, DirectSound, Winsock, ...), and the host
(`recomp/host`) runs the window, input, the launch window and the Metal renderer (`src/renderer`).

Notes on Diablo II:

- 1.14d always runs Direct3D when not windowed. A start-up hook (`tools/lift.py` HOOKS, `runtime/win32/hooks.c`) picks
  among the game's own renderers instead (`D2_RENDERER`): its DirectDraw renderer (default) or its windowed GDI mode.
  Both draw 8-bit pictures in software; `runtime/win32/ddraw_pal.c` (palettized DirectDraw 1-7) and the DIB sections
  in `gdi32.c` turn them into BGRA pictures for the renderer.
- The game's language file `D2.LNG` is loaded as a resource-only module; characters are kept where 1.14d keeps them on
  Windows (`SHGetKnownFolderPath` Saved Games); settings the game keeps in the registry persist in
  `<data>/registry.txt`.
- WS2_32's ordinals 10–12 differ from WSOCK32's (ioctlsocket, inet_addr, inet_ntoa); the host's own client in a TCP/IP
  game reaches the game's server through loopback.

## Other commands

```sh
recomp/build.sh "/path/to/Diablo II/Game.exe"        # recompile (recomp/work; never committed)
recomp/tools/build_app.sh recomp/work/full           # link the app executable
recomp/run.sh "/path/to/Diablo II"                    # play from the build (data in ~/Games/Diablo2Native)
```

Environment for development: `DS_TEST=1` (background window, no launch window), `D2_RENDERER=ddraw|gdi`,
`DSR_FILTER=sharp|smooth|pixel|crt`, `W32_DDLOG=1`, `W32_NETLOG=1`, `W32_SEHLOG=1`, `W32_TRACE=<file>`,
`W32_AUDIO_DUMP=<file.wav>`, `DS_CMDFILE=<file>` (scripted clicks, keys, typing and screenshots).

Notices and credits: [NOTICE.md](NOTICE.md).
