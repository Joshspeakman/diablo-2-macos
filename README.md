# Dungeon Siege II for Apple Silicon Macs

Dungeon Siege II (GOG release 2.3), statically recompiled from your own copy into native arm64 code and run on Metal,
as [dungeon-siege-macos](https://github.com/Joshspeakman/dungeon-siege-macos) does for Dungeon Siege 1. No Wine, no
Rosetta, no emulator. Nothing from the game is in this repository: the build regenerates everything locally from your
copy, and the game folder is only read.

**State: Beta 1.** Single player is playable: main menu, hero creation, the intro movie and opening cutscene,
gameplay (world, characters, HUD, dialogue), saving and loading, at the display's own resolution and up to 120 fps.
The game's shader effects (Options → Video → Shaders: water, refraction, glow, heat shimmer) are translated to Metal,
and a View Distance setting pushes out each area's fog like the SeeFar mods. Multiplayer is switched back on: LAN games
(host, join, play together) work without a CD key or any online service; the internet lobby signs in to OpenSpy with
an original retail CD key ([docs/MULTIPLAYER.md](docs/MULTIPLAYER.md)). What's next: [docs/PLAN.md](docs/PLAN.md).

## Install

Needs an Apple Silicon Mac, the Xcode command line tools (`xcode-select --install`) and python3.

```sh
git clone <this repository> && cd dungeon-siege-2-macos
./install.sh --gog-installer ~/Downloads/setup_dungeon_siege_2_*.exe   # GOG offline installer (brew install innoextract)
# or, with the game already unpacked or installed somewhere:
./install.sh --game-dir "/path/to/Dungeon Siege 2"
```

This recompiles the game (a few minutes) and installs **Dungeon Siege II Native** in `~/Applications`. Settings, saves
and logs live in `~/Games/DungeonSiege2Native`. The game runs in macOS full screen with **Game Mode** on; on a Mac with a notch the launch window's Notch setting puts the
picture below it or around it. **Interface Size** (125% to 300%) makes the game's interface larger at high resolutions while the world stays sharp: the
game runs at the resolution divided by it, and the screen is drawn at the full resolution. The launch window (laid out like the DS1 port's: the game's key art, the Game
row, then Display, Graphics, Online and Updates) sets the resolution, interface size, frame rate, view distance and gamma,
the game's own graphics options (shaders, shadows, texture filtering, object detail; written to its prefs.gas, so the
in-game Video page needn't be used), takes a CD key for online play, and offers updates: it rebuilds the app from the
newest version of this repository and your own copy of the game.

**Test Lab** (the launch window's Game row): the game in a window beside a panel that travels anywhere in the
map, spawns enemies, gives gear and casts every spell and power, with a record of each test, on copies of your saves.
From the Linux port; see [docs/TEST-LAB.md](docs/TEST-LAB.md).

`./install.sh --nightly` installs **Dungeon Siege II Nightly** next to it, built from the `nightly` branch (experimental
work), sharing the same saves and settings.

## How it works

`recomp/` holds the recompiler (x86 → C, then clang) and the runtime: a macOS implementation of the Windows APIs the
game uses, Direct3D 9 and D3DX on the Metal renderer in `src/`, Winsock, Miles audio and Bink video (both recompiled
from the DLLs in your game folder). Details in [recomp/README.md](recomp/README.md).

Dungeon Siege II is a trademark of its owners; this project is not affiliated with them. You need your own copy of the
game.
