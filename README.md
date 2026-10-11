# Diablo II for Apple Silicon Macs

Diablo II and Lord of Destruction (patch 1.14d), statically recompiled from your own copy into native arm64 code and
run on Metal and Core Audio, the way [dungeon-siege-macos](https://github.com/Joshspeakman/dungeon-siege-macos) and
dungeon-siege-2-macos do for the Dungeon Siege games. No Wine, no Rosetta, no emulator.

Nothing from the game is in this repository. `./install.sh` checks your copy's `Game.exe`, translates it (and the
Bink, Smacker and Intel JPEG libraries next to it) from x86 into C, compiles that for your Mac and installs an app.
The game folder is only read, and the app keeps loading the game's archives from it, so that folder has to stay.

## State

**Beta.** The latest release is [Beta 2](https://github.com/Joshspeakman/diablo-2-macos/releases/tag/beta-2);
`main` and `nightly` have moved on since (online games without Battle.net, pinch zoom, the best picture by default,
fixes). The game is playable natively:

- the Blizzard intro and the cinematics (Bink, with sound), the title, the menus;
- single player: new characters of every class, all five acts (their towns and waypoints), every class's skills, the
  HUD, dialogue with speech, saving and loading;
- multiplayer without Battle.net: an **online game list** that needs no server (the menus' Battle.net buttons lead
  to it), and the game's own **TCP/IP** games on a LAN, by address (see [Playing online](#playing-online));
- all three of the game's renderers: DirectDraw (the classic 8-bit picture), its windowed mode, and Direct3D with
  both of its views (see [Graphics](#graphics));
- an optional remastered Direct3D picture: drawn at the display's resolution, smooth textures, 60/120 fps or the
  display's refresh rate, widescreen or ultra-wide, and zooming out with the mouse wheel or a trackpad pinch, each a
  switch in the launch window;
- sound through DirectSound on Core Audio: music, effects, speech, and the game's **3D Sound** and **Environmental
  Effects** (see [Sound](#sound)).

Blizzard's Battle.net itself is not available (see [Playing online](#playing-online)). What's next:
[docs/PLAN.md](docs/PLAN.md).

## Requirements

- An Apple Silicon Mac (M1 or later). `install.sh` stops on an Intel Mac. The app asks for macOS 13 or later.
- The Xcode command line tools, for `clang`: `xcode-select --install`.
- `python3` (the command line tools include one). The first build makes `recomp/.venv` and installs Capstone and
  Unicorn into it with pip, so it needs the internet once.
- git, to clone this repository and for the in-app updates (the command line tools include it).
- Diablo II for **Windows**, updated to **1.14d** (Blizzard's last classic patch). Only that `Game.exe` is accepted
  (SHA-256 `631066c1…`): any other version is refused with "is not Diablo II 1.14d". Lord of Destruction is
  optional for the build; the port has been played and tested with it installed.
- About two minutes for the recompile, a few more for the whole install.

## Getting the game files

What the build needs is the folder a Windows installation of Diablo II 1.14d leaves: `Game.exe`, `D2.LNG`,
`binkw32.dll`, `SmackW32.dll`, `ijl11.dll` and the archives `d2data.mpq`, `d2char.mpq`, `d2sfx.mpq`, `d2music.mpq`,
`d2speech.mpq` and `d2video.mpq` (with Lord of Destruction also `d2exp.mpq`). `install.sh`
checks for each of them and says which one is missing.

- **From a Windows PC:** install Diablo II (and Lord of Destruction) from Blizzard's classic download on your
  Battle.net account or from your discs, make sure it is at 1.14d, and copy the whole `Diablo II` folder to the Mac
  (anywhere, e.g. `~/Games/Diablo II`).
- **On the Mac, with Wine or CrossOver:** install the Windows version into a Wine prefix or a CrossOver bottle and
  patch it to 1.14d there. `install.sh` finds it by itself in the usual places (see below). Wine is only needed for
  the installer; the app doesn't use it.
- **Blizzard's Mac version won't do:** the classic Mac client has no `Game.exe`, and this port recompiles the
  Windows program. The search skips it.

Characters from Windows are `.d2s` files in `Saved Games\Diablo II` (older versions kept them in the game's `save`
folder); see [Where things go](#where-things-go) for where they go here.

## Install

```sh
git clone https://github.com/Joshspeakman/diablo-2-macos.git
cd diablo-2-macos
./install.sh                                    # finds the game in the usual places
./install.sh --game-dir "/path/to/Diablo II"    # or names the folder (the one with Game.exe and the .mpq archives)
```

This checks `Game.exe` (1.14d) and the other files, recompiles the game with its Bink, Smacker and JPEG libraries
(about two minutes), and installs **Diablo II Native** in `~/Applications`. Open it from there (Finder, Launchpad or
Spotlight shows it as "Diablo II").

| Option | What it does |
|---|---|
| `--game-dir DIR` | The Diablo II folder. Without it, the folder an app installed earlier was set up with is used again; failing that, the first folder with the right `Game.exe` among `~/Games/*/prefix/drive_c/Program Files*/Diablo II*`, `~/Games/*/drive_c/Program Files*/Diablo II*`, CrossOver bottles (`~/Library/Application Support/CrossOver/Bottles/*/drive_c/Program Files*/Diablo II*`), `~/.wine/drive_c/Program Files*/Diablo II*`, `/Applications/Diablo II*` and `~/Games/Diablo II*` |
| `--app-dir DIR` | Where the app goes (default `~/Applications`) |
| `--data-dir DIR` | Where settings, characters and logs go (default `~/Games/Diablo2Native`) |
| `--nightly` | Installs **Diablo II Nightly** next to the stable app: the `nightly` branch, fetched from GitHub into `build/nightly` (this checkout is left as it is) and built from there. It shares the data folder with the stable app. Run it again to update it |

Running `./install.sh` again rebuilds and reinstalls the app; the data folder is kept.

The app's icon and the launch window's art (the main menu's backdrop and the flaming logo) are decoded from your own
archives at install time (`recomp/tools/d2art.py`, `recomp/tools/d2_icon.m`).

## Where things go

| What | Where |
|---|---|
| The app | `~/Applications/Diablo II Native.app` (and `Diablo II Nightly.app`) |
| The game's data | read from your Diablo II folder, never written to |
| Characters | `~/Games/Diablo2Native/drive_c/Users/player/Saved Games/Diablo II`, where 1.14d keeps them on Windows. Copy `.d2s` files there to bring characters over |
| The game's own settings (what it keeps in the registry on Windows) | `~/Games/Diablo2Native/registry.txt` |
| The launch window's settings | `~/Games/Diablo2Native/launcher.plist` |
| Log | `~/Games/Diablo2Native/Diablo2Native.log` (Nightly: `Diablo2Nightly.log`); past 5 MB the previous one is kept as `.old.log` |
| Crash and hang reports | `~/Games/Diablo2Native/CrashReports` |
| Build files | in the clone: `recomp/work` (the generated code), `recomp/.venv` (the recompiler's Python tools), `build/` (the updater's and `--nightly`'s checkouts) |

The app itself contains a copy of your `Game.exe` (`Contents/Resources/Game.exe`) and the code recompiled from it, so
it is a build of your copy for your Mac.

The game runs in macOS full screen with Game Mode on; with Notch → Around the Notch it is a borderless window over the
whole display instead (macOS full screen always keeps a picture below the camera).

## The launch window

The app opens with a launch window in the game's own look. **Play** starts the game; the buttons open each group of
settings. They are kept in `launcher.plist` and apply at the next Play. Defaults are in bold; a new install starts
with the remastered Direct3D picture, and the original look is a few switches away (see [Graphics](#graphics)).

| Setting | Choices |
|---|---|
| Video | DirectDraw (the classic renderer, 800×600) · **Direct3D** (the game's 3D renderer, with the options below; plays the cinematics too) · Windowed Mode (the game's `-w` renderer; no cinematics) |
| Direct3D → View | Game's Choice (the game's Options → Video → Perspective, as it last saved it) · Isometric · **Perspective** |
| Direct3D → Resolution | Original (800×600, scaled to the display like DirectDraw) · **Display** (the game's frame drawn at the display's resolution: crisp edges and text) |
| Direct3D → Textures | Sharp (the original pixels) · **Smooth** (bilinear filtering; cut-out sprites keep clean edges) |
| Direct3D → Frame Rate | Original (25, as the game draws) · 60 · 120 · **Display** (its refresh rate): movement blended between the game's ticks |
| Direct3D → Width | Original (4:3) · **Display** (the display's shape at the same height: more of the world to the sides; others in multiplayer see less) |
| Direct3D → Zoom | Off (the game's own view) · Wheel or Pinch, 1.5× · **Wheel or Pinch, 2×** (wheel down or pinch to see more of the world, wheel up or spread the fingers to come back) |
| Display → Scaling | **Sharp** (square pixels, smoothed only at their edges) · Smooth (bilinear) · Pixel (nearest) · CRT (sharp with scanlines) |
| Display → Notch (only on a display with a notch) | **Hide the Notch** (the picture below the camera, in macOS full screen) · Around the Notch (the whole display, the camera over the picture's top centre) |
| Multiplayer → Type | **Internet (Game List)** (the Battle.net buttons lead to the online game list) · LAN (Classic) (the game's own TCP/IP, as it was: type the host's address) |
| Multiplayer → Group | **Public** (anyone with this port sees your hosted games) · Private: *name* (press to set a group name: only copies with the same name see each other's games) |
| Multiplayer → Router | **Open Port When Hosting** (asks your router, by UPnP or NAT-PMP, for TCP and UDP 4000 while you host) · Leave Router Alone (forward them yourself) |
| Updates | shows whether a newer commit of the app's branch is on GitHub; press it to rebuild the app from it, from your own copy (see [Updating](#updating)) |

The Direct3D settings apply only when Video is Direct3D. The game's own options (gamma, contrast, sound and music
volume, light quality, ...) are in its Options menu and are kept between sessions.

## Graphics

Diablo II has two looks, and both are here:

- **Isometric** ("2D"): the classic flat view. DirectDraw always shows it; Direct3D shows it with View → Isometric.
- **Perspective** ("3D"): the game's Direct3D mode tilts the ground away toward the horizon, with depth as you walk.
  Direct3D → View → Perspective (or the game's own Options → Video → Perspective).

The launch window's **Direct3D** group offers both original and modern display settings. Its defaults are Display
resolution, Smooth textures, Display frame rate and width, and zoom up to 2×. For the original presentation, select
Original resolution/frame rate/width, Sharp textures, and Zoom Off:

| Option | What it does |
|---|---|
| Resolution → Display | The game's triangles are drawn at the display's resolution rather than as an 800×600 picture scaled up: crisp edges, text and detail |
| Textures → Smooth | Filtered textures (no stair-steps); sprites keep clean edges |
| Frame Rate → 60 · 120 · Display | Frames between the game's 25 ticks a second, with movement blended between them: smooth walking, scrolling, monsters and missiles |
| Width → Display | The display's shape (16:10, 16:9, 21:9, ...): more of the world to the sides, the interface at the screen's edges, menus kept 4:3 in the middle |
| Zoom → Wheel or Pinch | Zoom out with the mouse wheel (on a trackpad: scroll with two fingers, or pinch) to see more of the world around you, up to 1.5× or 2×; wheel up (or spreading the fingers) returns to the game's own view. The interface stays as it is |

A "remastered" setup: Video Direct3D, View Perspective (or Isometric), Resolution Display, Textures Smooth, Frame Rate
Display, Width Display, Zoom Wheel or Pinch.

Known limitations: with more frames, the picture runs up to one tick (40 ms) behind the game and character
animations stay at 25 frames a second; in Width → Display the help screen (H) still points at its 800×600 places, and
in multiplayer a wider or zoomed-out view shows more than other players see. Zoomed out, the GPU draws that much more
of the world (about 2.5× at 1.6×, so at the display's resolution the frame rate can halve), and at the farthest zoom in
a large outdoor area the ground can end at the rooms the game has loaded around you.

## Sound

The game's Options → Sound Options has two settings that a 2000s sound card unlocked, and both work here:

- **3D Sound**: each effect is a positioned voice (DirectSound 3D): placed left or right of your character as on screen
  and faded with distance. 3D Bias (the slider under it) then becomes available too.
- **Environmental Effects**: the room's acoustics, as Creative's EAX 2.0 cards gave them. Every area has its own
  environment in the game's data (`SoundEnviron.txt`): none in the towns, a faint wide space in the wilderness, a stone
  room's reverb in caves and the Catacombs, a hall in the Monastery, long tails in the tombs. A reverb in the runtime
  (`recomp/runtime/win32/eax_reverb.c`: early reflections and an 8-line feedback delay network) renders them from
  those numbers: decay time, high-frequency decay, reflections and reverb levels and delays, room size, diffusion.
  Each sound's own EAX settings (direct and room levels, obstruction, occlusion) are applied as well.

Both are off until chosen, and the game remembers the choice.

## Playing online

Blizzard's Battle.net servers won't take this copy, so the port never contacts them: the menus' **Battle.net**
button (and **Open Battle.net** under Other Multiplayer) leads to the game's own TCP/IP screen instead, and reads
**ONLINE GAMES** (or **LAN GAMES**). The gateway line under it is hidden. Connections to Blizzard's addresses that
the game has built in (207.82.87.x, ports 6112–6113) are refused (`recomp/runtime/win32/winsock.c`). The TCP/IP
screen's Cancel returns to the main menu.

The launch window's **Multiplayer → Type** picks how games are found:

- **Internet (Game List)**, the default. A game you host (TCP/IP screen → Host Game) is listed without any server:
  it is announced on BitTorrent's public DHT, the same way the Delta Force port's NovaWorld list works, and answers
  the list's questions on UDP 4000 with the host's character, class, level, difficulty and number of players.
  **Join Game** opens the game's address box with the list over it; it asks the hosts the DHT knows, the local
  network and this Mac every second. Pick a game and its address is typed into the box and confirmed, as a player
  would. The TCP/IP screen's "Your IP Address" shows your public address (your router's), which is what other
  players need.
- **LAN (Classic)**: the game's own TCP/IP, as it was. Host Game on one Mac; on the others Join Game and type the
  host's address (its TCP/IP screen shows it). Nothing of the list runs: no DHT, no router, nothing listening on
  UDP 4000.

**Group** makes a private list: only copies with the same group name (like a shared password) find each other's
games; it is part of the list's key, and hosts answer only questions that carry it. **Router**: with Open Port When
Hosting, a host asks the router (UPnP or NAT-PMP) to forward **TCP 4000** (the game) and **UDP 4000** (the list)
while it hosts; the forward is removed when hosting stops, and one left behind by a crash is removed at the next
start. If your router doesn't do UPnP or NAT-PMP, forward both ports to the hosting Mac yourself and choose Leave
Router Alone.

The first time, macOS asks whether the app may find devices on your local network: allow it for LAN games (System
Settings → Privacy & Security → Local Network, if you said no). If the macOS firewall is on, allow the app to accept
incoming connections when you host.

The game list is this port's own: it lists games hosted by this port. While you host on the public list, your public
address is visible to anyone who looks.

## Controls

The game's own: the mouse (left button to move and attack, right button for the right-hand skill) and its keys, as
on Windows; change them in the game's Options → Configure Controls. On the Mac:

- On a trackpad, a click is the left button and a two-finger click the right one (macOS's secondary click).
- With Direct3D → Zoom on, the mouse wheel (or two-finger scrolling) and a trackpad pinch zoom out and back in.
- Esc goes to the game (it doesn't leave full screen); keys with ⌘ go to macOS, so ⌘-Tab switches apps.

## Updating

The launch window's **Updates** row asks GitHub, each time it opens, whether the branch the app follows has a newer
commit than the one it was built from: **Diablo II Native** follows `main`, **Diablo II Nightly** follows `nightly`.
Press it to update: the branch is fetched into a worktree of the clone you installed from (`build/stable` or
`build/nightly`), the app is rebuilt from your own copy of the game (a few minutes), and the app restarts. Your
characters and settings are kept. A failed update changes nothing if it stopped before installing; the details are
in `~/Games/Diablo2Native/update.log`.

- `main` is the stable line; `nightly` has the newest, experimental work. Both apps can be installed side by side and share characters and settings.
- Keep the clone where it was when you installed: the updater runs in it. An app installed from a folder that isn't a
  git clone of this repository shows "Not available". It never asks for a password: "Couldn't check" means GitHub
  wasn't reachable (or git couldn't fetch without asking).
- From the terminal instead: `git pull` in the clone and `./install.sh` (it reuses the game folder of the app
  already installed), or `./install.sh --nightly` for the Nightly app.

## Uninstalling

```sh
rm -rf ~/Applications/"Diablo II Native.app" ~/Applications/"Diablo II Nightly.app"
rm -rf ~/Games/Diablo2Native        # your characters and settings: copy Saved Games/Diablo II somewhere first
rm -rf /path/to/diablo-2-macos      # the clone, with its build files (recomp/work, recomp/.venv, build/)
```

Your Diablo II folder was never changed.

## Known issues

- Battle.net itself isn't available: its buttons lead to the game list instead.
- Online games through the game list have been tested with a host and a player on the same Mac; not yet between two
  networks, or with more than two players.
- With more frames per second, the picture runs up to one tick (40 ms) behind the game, and character animations
  stay at 25 frames a second.
- With Width → Display, the help screen (H) still points at its 800×600 places; in multiplayer a wider or zoomed-out
  view shows more than other players see.
- Zoomed out, the GPU draws that much more of the world (about 2.5× at 1.6×): at the display's resolution the frame
  rate can halve. At the farthest zoom in a large outdoor area, the ground can end at the rooms the game has loaded
  around you (black patches at the far corners).
- Windowed Mode doesn't play the cinematics; DirectDraw and Direct3D do.
- Deeper play (bosses, the cube, trading, mercenaries) can reach code that hasn't run yet; the crash report names
  what was missing.
- The October 2026 comparison against the original 1.14d picture is tracked in
  [issue 1](https://github.com/Joshspeakman/diablo-2-macos/issues/1).

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| `... is not Diablo II 1.14d` | Update the game to 1.14d (Blizzard's last classic patch) first. Only the original 1.14d `Game.exe` is accepted; modified or cracked executables, older patches and the Mac client are refused |
| `the game folder has no ...` | Point `--game-dir` at a complete installation (see [Getting the game files](#getting-the-game-files)) |
| `usage: install.sh --game-dir ...` | No installation was found in the usual places: name the folder with `--game-dir` |
| `clang is missing` / `python3 is missing` | Install the Xcode command line tools: `xcode-select --install` |
| `the native build is for Apple Silicon Macs` | This port needs an M-series Mac |
| The launch window shows a plain title | Its art couldn't be read from your archives; the game itself is unaffected |
| "Diablo II stopped unexpectedly" | A report was written to `~/Games/Diablo2Native/CrashReports` (Show Report opens it). A hang you had to force-quit is reported the next time the app opens. Please attach the report and the log (`Diablo2Native.log`) to a [GitHub issue](https://github.com/Joshspeakman/diablo-2-macos/issues) |
| Nobody sees your hosted game | Check Multiplayer → Type is Internet on both sides, and the same Group; let the router forward TCP and UDP 4000 (or forward them yourself); allow the app through the macOS firewall |
| A LAN join doesn't connect | Allow Local Network access (System Settings → Privacy & Security → Local Network) and check the address on the host's TCP/IP screen |
| Updates says "Couldn't check" | GitHub wasn't reachable, or git couldn't fetch the clone's `origin` without a password; press it to retry |

## Tests

```sh
recomp/tests/run_tests.sh                        # no game files needed: renderer, launch window, reverb, game list
recomp/tests/run_tests.sh "/path/to/Diablo II"   # and the game end to end (after ./install.sh or recomp/build.sh)
```

- **Present shader** (`present_shader_test.m`): the Metal renderer draws a known picture into an offscreen drawable;
  every pixel is checked for each scaling filter (whole-pixel blocks, blends only at block edges, bilinear, scanline
  shading) and for the gamma ramp.
- **Launch window** (`launcher_test.m`): its settings reach the runtime and are remembered; it draws with the art.
- **EAX reverb** (`eax_reverb_test.c`): measured from impulse responses: the decay time (0.8–5 s, ±20%), the faster
  treble decay with a low decay HF ratio, nothing before the reflections delay, levels in millibels (−600 mB = −6 dB of
  energy; the late reverb's whole energy equals its level within 1.5 dB from a 2 m room to a 100 m space), silence when
  the room is off, decorrelated stereo.
- **Renderer bounds, frame trace, critical sections** (`renderer_init_test.m`, `renderer_ring_test.m`,
  `frame_trace_test.c`, `critical_sections_test.c`, under the address and undefined-behaviour sanitizers): see
  [recomp/README.md](recomp/README.md#validation).
- **Game list** (`d2net_test.c`, `p2p_dht_test.c`, `p2p_dht_fuzz.c`): the list's questions and answers, groups,
  malformed and oversized questions and the rate limit; the DHT client against fake nodes, and fuzzed with damaged
  answers. No internet, no router.
- **The game** (`game_test.py`), on your copy in a scratch folder, through the window's own input path:
  - *video*: the Bink intro is shown (640×480 16-bit pictures that move) with its sound;
  - *menus, ingame, gdi*: every 8-bit picture on screen is exactly the game's palette for that frame (no colour lost
    between the game's pixels and the screen), in both renderers; the life and mana orbs are drawn in Act 1;
  - *speech*: talking to Warriv plays his introduction, and the recorded output matches the same speech decoded
    independently from `d2speech.mpq` (`tools/mpq.py`: Huffman + ADPCM + implode) with correlation ≥ 0.8 (0.95–0.97
    measured) at the same offset in several windows (right speed, no drift);
  - *walk, save, load*: a click moves the character; Save and Exit writes a valid `.d2s`; Single Player lists it and
    loads it back into the game;
  - *d3d*: the Direct3D renderer: a new character is made and saved, then loaded in the Isometric and the Perspective
    view; the HUD is drawn in each, the game keeps the view chosen, and the same place looks different in the two;
  - *d3d-hd*: the same character at the display's resolution with smooth textures: a larger 4:3 frame, the HUD in
    its place, the same brightness as at 800×600;
  - *d3d-fps*: the same character walking at 60 and 120 frames a second: 12 consecutive frames read back from the GPU
    shortly after loading, each moved on from the one before by an even step (blended, not repeated);
  - *zoom*: Direct3D with Zoom 2×: ten wheel notches out draw the world to the screen's corners with the bottom bar
    and orbs as they were, and ten back return to the game's own view;
  - *d3d-wide*: the same character 1400 wide: the menus in the middle with black bars at the sides; in game a
    1400×600 frame with the orbs at its edges and the inventory opening at the right edge (not at its 800×600 place);
  - *sound3d*: with 3D Sound and Environmental Effects chosen, the game finds the 3D voices and EAX 2.0 at start,
    plays its effects through 3D buffers it places, sets each one's EAX properties and the area's environment, and
    the mix in the game stays below full scale;
  - *tcpip*: LAN (Classic): two instances, one hosting and one joining by this Mac's LAN address, both in Act 1;
  - *tcpexit*: a host quits through the game's menus with a client still connected, and exits cleanly
    ([issue 2](https://github.com/Joshspeakman/diablo-2-macos/issues/2));
  - *gamelist*: Internet: a host and a joiner through ONLINE GAMES, the joiner picking the host from the game list
    (found on this Mac: the tests never use the public DHT or the router), both in Act 1.
- **Coverage** (`coverage_test.py`, about an hour; run it on its own): a character of every class is made through the
  menus, given every class skill (`tests/d2s.py` edits these scratch characters only), leaves town by its waypoint and
  casts each of its 30 skills, chosen through the game's own skill list in memory; then each act's town is loaded and
  left by its waypoint (found among the map's presets) to the act's first destination, casting there; last, the
  inventory, character, skill tree, quest log, automap and help are opened by their keys in the field. The game must
  keep running and each chosen skill must be the one selected.

  ```sh
  python3 recomp/tests/coverage_test.py --game "/path/to/Diablo II"     # --classes Sorceress,Druid --acts 2,5 for a part
  ```

## How it works

`recomp/build.sh <Game.exe>` analyses the executable (`tools/analyze.py`: 18,414 functions, 95.6% of `.text`),
lifts every function to C (`tools/lift.py`: x86 to C with lazy flags, x87, SEH), does the same for `binkw32.dll`,
`SmackW32.dll` (moved off Bink's address by `tools/rebase_pe.py`) and `ijl11.dll`, and compiles it all for arm64
(about 20,000 functions). The runtime (`recomp/runtime/win32`) is a macOS implementation of the Windows APIs the game
uses (615 functions: kernel32, user32, gdi32, advapi32, DirectDraw, DirectSound, Winsock, ...), and the host
(`recomp/host`) runs the window, input, the launch window and the Metal renderer (`src/renderer`).

Notes on Diablo II:

- 1.14d always runs Direct3D when not windowed. A start-up hook (`tools/lift.py` HOOKS, `runtime/win32/hooks.c`) picks
  among the game's own renderers instead (`D2_RENDERER`): its DirectDraw renderer (default), its windowed GDI mode, or
  its Direct3D renderer. The first two draw 8-bit pictures in software; `runtime/win32/ddraw_pal.c` (palettized
  DirectDraw 1-7) and the DIB sections in `gdi32.c` turn them into BGRA pictures for the renderer.
- The Direct3D renderer is DirectX 6 (`IDirectDraw4` with `IDirect3D3`, `IDirect3DDevice3`, viewports, textures,
  vertex buffers). The game projects the scene itself, isometric or in perspective, and draws pre-transformed textured
  triangles; `ddraw_pal.c` turns its textures (16-bit, colour keys) into GPU textures and its draws, states and
  DirectX 5-style texture blending into the Metal renderer's commands. The view is the game's `Perspective` registry
  value, which the launch window can set at each start (`D2_PERSPECTIVE=on|off`).
- Display resolution (`D2_RESOLUTION=display`) uses the renderer's render scale (`DSR_RENDER_SCALE`): screen-sized
  targets are made that much larger and the game's screen-space triangles are rasterised into them, so geometry,
  text and texture detail are drawn at the display's pixels rather than scaled up afterwards. With sharp textures the
  scale is a whole number (each texel an even block; the present filter covers the rest), with smooth ones the
  display's exact size. Smooth textures (`D2_TEXTURES=smooth`) force bilinear filtering; transparent texels take
  their visible neighbours' colour and cut-outs are tested at half alpha, so sprites get no dark fringe.
- Frame rate (`D2_FPS`, `runtime/win32/d2smooth.c`): the game moves its units once a tick (40 ms) and in single player
  draws once a tick. Hooks in its frame function (`0x44efa0`) let it draw whenever a frame is due (paced there, so
  ticks are never held back), and around each draw set the moving units' paths (players, monsters, missiles) to
  positions blended between their last two ticks, putting the real ones back afterwards: only the picture sees them,
  and the camera follows the player's drawn position. Animation frames stay at 25 a second. The picture runs up to one
  tick (40 ms) behind the game, the cost of blending between known positions rather than guessing ahead.
- Width (`D2_WIDTH`, `runtime/win32/d2wide.c`): 1.14d already half-supports other widths (an unused 1344×700 mode;
  its bottom bar, orbs and right-hand panels' backgrounds follow the screen's width). The 800×600 mode reports the
  wider size to the game's graphics layer, its renderers and the client (hooks at `0x4f55aa`, `0x509c3f`,
  `0x51126f`, `0x6b5037`, `0x6c7b75`, `0x44ba50`, and the display mode). The rest is placed: the Inventory table's
  right-hand rows (panel, grid, equipment slots) and the Belts table's boxes move out once, so pictures, items and
  clicks agree; the right-hand panels' frame is drawn out by the extra width (a marked stretch of code); the 46
  places that take a side panel as a quarter of the screen get 200 (half its 400 pixels), so the view slides and
  the world ends where the panel does; the world is still drawn between two open panels; out of a game the menus are
  drawn in the middle and the cursor kept to their 800 pixels.
- Zoom (`D2_ZOOM_MAX`, `runtime/win32/d2zoom.c`): the in-game draw (`0x44c990`) draws the world, then the interface.
  For the world pass only, the view's rectangle, the rectangle its ground tiles are tested against and the screen's
  size where the game reads it (graphics layer and client) are made zoom times larger, and the rooms whose ground is
  drawn grow from the player's room and its neighbours to their neighbours too; the Direct3D draws made then are scaled
  back into the screen (`ddraw_pal.c`), and everything is put back before the interface. The camera the pass leaves is
  the larger view's, so input follows: a click in the world (`0x462d00`) and the search for the unit under the cursor
  (`0x467130`) take the point under the cursor in the larger view; the side panel and bottom bar tests keep the screen's.
  The mouse wheel sets a target the view eases to.
- Sound (`runtime/win32/dsound.c`): at start the game probes the sound device for 3D and EAX and greys the options it
  can't have. The device reports hardware 3D voices (the game wants 16), its 3D buffers answer `IKsPropertySet` with
  EAX 2.0 listener and buffer properties (and EAX 1.0 reverb), and those are rendered: 3D buffers placed against the
  listener, each sound's EAX direct/room/obstruction/occlusion levels as gains and treble filters, and its room send
  into the reverb (`eax_reverb.c`), whose settings follow the listener's environment.
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

Environment for development:

- the launch window's settings: `D2_RENDERER=ddraw|gdi|d3d`, `D2_PERSPECTIVE=on|off`, `D2_RESOLUTION=original|display`,
  `D2_TEXTURES=sharp|smooth`, `D2_FPS=<n>|display`, `D2_WIDTH=<n>|display`, `D2_ZOOM_MAX=<1-3>` (`D2_ZOOM=<z>`: a fixed
  zoom, for tests), `DSR_FILTER=sharp|smooth|pixel|crt`,
  `DS_NOTCH=hide|around`;
- testing: `DS_TEST=1` (background window, no launch window), `DS_CMDFILE=<file>` (scripted clicks, keys, typing and
  screenshots), `W32_BURST_DIR=<dir>` (12 consecutive frames when `<dir>/go` appears), `W32_AUDIO_DUMP=<file.wav>`,
  `DSR_RENDER_SCALE=<1-4>`;
- logs: `W32_DDLOG=1`, `W32_REGLOG=1`, `W32_NETLOG=1`, `W32_SEHLOG=1`, `W32_TRACE=<file>`, `W32_DSLOG=1` (DirectSound,
  with every EAX property set), `W32_DS3DLOG=1` (3D placements and their gains), `W32_EAXLOG=1` (each second: what is
  sent to the room and what the reverb adds), `DS_WINLOG=1` (the game
  window: shown and hidden, full screen, activation, and whether it is really on screen);
- the launch window: `DS_LAUNCHER_AUTOPLAY=1` presses Play (tests); Play starts the game in a fresh process (a game
  window shown by the process that ran the launch window could stay off screen in full screen), `DS_SAME_PROCESS=1`
  keeps it in the same one.

## Documentation

- [docs/PLAN.md](docs/PLAN.md): the port's plan and status, the frame-rate and ultra-wide research, what's next.
- [docs/RE-BNET.md](docs/RE-BNET.md): how the Battle.net buttons, the Join Game box and the host's game details were
  found in 1.14d, and the design of the game list.
- [recomp/README.md](recomp/README.md): building by hand, rendering and timing, validation.
- [NOTICE.md](NOTICE.md): notices and credits.

## Legal

Diablo, Diablo II and Lord of Destruction are trademarks of Blizzard Entertainment. This project is not affiliated
with or endorsed by Blizzard. It contains no part of the game: you need your own copy, the build reads it from your
disk and everything generated from it stays on your Mac (in the clone's `recomp/work` and in the app). Details and
credits: [NOTICE.md](NOTICE.md).

## License

The code in this repository is under the MIT License: [LICENSE](LICENSE).
