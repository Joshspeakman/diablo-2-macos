# Diablo II for Apple Silicon Macs

Diablo II and Lord of Destruction (patch 1.14d), statically recompiled from your own copy into native arm64 code and
run on Metal and Core Audio, the way [dungeon-siege-macos](https://github.com/Joshspeakman/dungeon-siege-macos) and
dungeon-siege-2-macos do for the Dungeon Siege games. No Wine, no Rosetta, no emulator. Nothing from the game is in
this repository: the build regenerates everything locally from your copy, and the game folder is only read.

**State: Beta 2.** The game is playable natively:

- the Blizzard intro and the cinematics (Bink, with sound), the title, the menus;
- single player: new characters of every class, all five acts (their towns and waypoints), every class's skills, the
  HUD, dialogue with speech, saving and loading;
- TCP/IP games (Other Multiplayer → TCP/IP): host on this Mac, join from another, play together;
- all three of the game's renderers: DirectDraw (the classic 8-bit picture), its windowed mode, and Direct3D with
  both of its views (see [Graphics](#graphics));
- an optional remastered Direct3D picture: drawn at the display's resolution, smooth textures, 60/120 fps or the
  display's refresh rate, widescreen or ultra-wide, and zooming out with the mouse wheel or a trackpad pinch, each a switch in the launch
  window;
- sound through DirectSound on Core Audio: music, effects, speech, and the game's **3D Sound** and **Environmental
  Effects** (see [Sound](#sound)).

Battle.net and Open Battle.net are untested. What's next: [docs/PLAN.md](docs/PLAN.md).

## Graphics

Diablo II has two looks, and both are here:

- **Isometric** ("2D"): the classic flat view. DirectDraw always shows it; Direct3D shows it with View → Isometric.
- **Perspective** ("3D"): the game's Direct3D mode tilts the ground away toward the horizon, with depth as you walk.
  Direct3D → View → Perspective (or the game's own Options → Video → Perspective).

On top of the game's Direct3D renderer, the launch window's **Direct3D** group can bring the picture up to a modern
display. Every option is off (the original) unless chosen:

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
there to bring characters over). The game runs in macOS full screen with Game Mode on; with Notch → Around the Notch
it is a borderless window over the whole display instead (macOS full screen always keeps a picture below the camera).
The app's icon is drawn at install from the same art as the launch window (`recomp/tools/d2_icon.m`).

The launch window (in the game's own look: the main menu's backdrop and logo, decoded from your archives at install)
has:

| Setting | Choices |
|---|---|
| Video | **DirectDraw** (the classic renderer, plays the cinematics) · Direct3D (the game's 3D renderer) · Windowed Mode (the game's `-w` renderer; no cinematics) |
| Direct3D → View | **Game's Choice** (the game's Options → Video → Perspective, as it last saved it) · Isometric · Perspective |
| Direct3D → Resolution | **Original** (800×600, scaled to the display like DirectDraw) · Display (the game's frame drawn at the display's resolution: crisp edges and text) |
| Direct3D → Textures | **Sharp** (the original pixels) · Smooth (bilinear filtering; cut-out sprites keep clean edges) |
| Direct3D → Frame Rate | **Original** (25, as the game draws) · 60 · 120 · Display (its refresh rate): movement blended between the game's ticks |
| Direct3D → Width | **Original** (4:3) · Display (the display's shape at the same height: more of the world to the sides; others in multiplayer see less) |
| Direct3D → Zoom | **Off** (the game's own view) · Wheel or Pinch, 1.5× · Wheel or Pinch, 2× (wheel down or pinch to see more of the world, wheel up or spread the fingers to come back) |
| Display → Scaling | **Sharp** (square pixels, smoothed only at their edges) · Smooth (bilinear) · Pixel (nearest) · CRT (sharp with scanlines) |
| Display → Notch | Hide the Notch (the picture below the camera, in macOS full screen) · Around the Notch (the whole display, the camera over the picture's top centre) |
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
- **EAX reverb** (`eax_reverb_test.c`): measured from impulse responses: the decay time (0.8–5 s, ±20%), the faster
  treble decay with a low decay HF ratio, nothing before the reflections delay, levels in millibels (−600 mB = −6 dB of
  energy; the late reverb's whole energy equals its level within 1.5 dB from a 2 m room to a 100 m space), silence when
  the room is off, decorrelated stereo.
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
  - *d3d-fps*: the same character walking at 60 frames a second: 12 consecutive frames (read back from the GPU) about
    16 ms apart, each moved on from the one before by an even step (blended, not repeated);
  - *zoom*: Direct3D with Zoom 2×: ten wheel notches out draw the world to the screen's corners with the bottom bar
    and orbs as they were, and ten back return to the game's own view;
  - *d3d-wide*: the same character 1400 wide: the menus in the middle with black bars at the sides; in game a
    1400×600 frame with the orbs at its edges and the inventory opening at the right edge (not at its 800×600 place);
  - *sound3d*: with 3D Sound and Environmental Effects chosen, the game finds the 3D voices and EAX 2.0 at start,
    plays its effects through 3D buffers it places, sets each one's EAX properties and the area's environment, and
    the mix in the game stays below full scale;
  - *tcpip*: two instances, one hosting and one joining by this Mac's LAN address, both in Act 1.
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

Notices and credits: [NOTICE.md](NOTICE.md).
