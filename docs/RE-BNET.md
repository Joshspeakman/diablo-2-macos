# Battle.net → serverless game list: reverse-engineering notes (Game.exe 1.14d)

Branch `re-bnet` (worktree `build/re-bnet`). All addresses are Game.exe virtual addresses (sha256 631066c1…, image base
0x400000). Static facts were read with capstone (`recomp/.venv`); each was then checked in the running native build in
test mode (`DS_TEST=1 W32_AUDIO_MUTE=1 DS_NO_PORTMAP=1`, scratch data folders under `$TMPDIR`, `W32_NETLOG=1`). Nothing
contacted the internet: every `net: connect` in the logs is `127.0.0.1:4000`.

Proof of concept on this branch: `recomp/runtime/win32/d2bnet.c` (+ hooks in `hooks.c`, `lift.py`), turned on with
`D2_NO_BNET=1`. Debug aids added: `find:<text>` / `find:#<dword>` / `dump:<chain>,<hexlen>` lines in the test-mode
peek file (`host/main.m`), `net: close` in the `W32_NETLOG` output (`winsock.c`).

## 0. How the front end is built (the key to everything below)

Every front-end screen is assembled from one static table of control descriptions in writable `.data`:

| | |
|---|---|
| table | `0x708d10`, 0x30 bytes per entry, entry `id` at `0x708d10 + id*0x30` |
| entry | `+0` type (2 image, 4 text, 6 button, 1 edit, 8 timer…), `+4` x, `+8` y (**bottom** edge), `+0xc` w, `+0x10` h, `+0x14` hotkey (VK), `+0x18` string-table id, `+0x1c` cell-file global, **`+0x20` click callback** (`BOOL __stdcall cb(Control*)`, all end `ret 4`), `+0x24..` extra |
| create control `id` on the screen layer | `0x42f430` (`ecx = id`; pushes onto `0x779350[n]`, n = `0x779944`) |
| create on the popup layer | `0x42f3a0` (`ecx = id`; layer `0x779450`, n = `0x779948`) |
| destroy the whole screen | `0x42f480` (popup layer only: `0x42f3f0`) |

Because the callbacks are read from the table when a control is created, changing a callback dword in the table (before
the screen is built) changes what the button does — no code patch, no relift.

## 1. Main menu buttons

Correction to the brief: **"Open Battle.net" is not on the main menu** in 1.14d — it is the first button of the
*Other Multiplayer* screen. The main menu has Single Player, Battle.net, the gateway line under it ("GATEWAY: U.S.
WEST", a button too), Other Multiplayer, Credits, Cinematics, Exit.

| button (string id) | table entry (classic / LoD layout) | callback | what it does |
|---|---|---|---|
| Single Player (5106) | 0x09 / 0x10 | `0x435cd0` | `[0x7795ec]=0`, hero selection |
| **Battle.net** (5107) | 0x0a @`0x708ef0` / **0x11 @`0x709040`** | **`0x435d30`** | `mov [0x7795ec],1` ; `call 0x435330` (Battle.net log-on) |
| gateway line (no string; label set by `0x431af0`) | 0x0b / 0x12 | `0x434600` | realm list screen (reads gateways via the bnclient object `[0x77abf0]`; no network) |
| Other Multiplayer (5108) | 0x0c / 0x13 | `0x430c50` | `0x43c4f0` (clear, background), create 0xff, 0x100, 0x101 |
| **Open Battle.net** (5115) — Other MP screen | 0xff @`0x70bce0` | **`0x435d10`** | `mov [0x7795ec],3` ; `call 0x435330` |
| **TCP/IP Game** (5116) — Other MP screen | 0x100 @`0x70bd10` | **`0x431a40`** | `mov [0x7795ec],2` ; `call 0x42ffe0` (build the TCP/IP screen) |
| Cancel — Other MP screen | 0x101 | `0x434df0` | `call 0x4336c0` (main menu) |

Main menu builder: `0x4336c0` (LoD branch creates 0x12, 0x11, 0x13, 0x10; classic 0xa, 0xb, 9, 0xc). The LoD layout is
the one in use: click points (bottom-edge y − h/2) Battle.net **400,349**, gateway 400,379, Other Multiplayer 400,416;
on Other Multiplayer, Open Battle.net **400,293**, TCP/IP Game 400,330.

TCP/IP screen (`0x42ffe0`, tail `jmp 0x42ff20`, which shows "Your IP address is" from `0x40df60` and disables Host/Join
if that is 127.0.0.1): controls 0x102–0x10b; **Host Game** 0x106 cb `0x435d80` (click 400,189), **Join Game** 0x107 cb
`0x431d20` (400,246), Cancel 0x10a @`0x70bef0` cb `0x430c50` (back to Other Multiplayer; click 103,553).

### State the TCP/IP path sets

* `0x7795ec` connection type: 0 single player, 1 Battle.net, 2 TCP/IP, 3 Open Battle.net. `0x431a40` sets 2.
* `0x42ffe0` sets `0x779954 = 1` itself and first calls `0x43b9a0` (if the Battle.net client object `[0x77abf0]`
  exists — it probably always does, `0x434600` uses it unconditionally — it clears two flags in `B` and calls its
  vtable `+0x10` close/reset; this ran harmlessly on every one of our TCP/IP screen openings, with no network
  traffic) and `0x42f480`, so it is safe to enter from *any* front-end
  screen, including straight from the main menu (the Other Multiplayer screen sets nothing the TCP/IP path needs;
  `0x43c4f0`'s tail `0x432ef0` only moves controls for one language).
* Host Game `0x435d80`: `[[0x7795d4]+0x19] = 8` (host), copies the local IP into `[0x7795d4]+0x37`, `[0x7795ec]=2`,
  `[[0x7795d4]+0x209] = 4`, then hero selection `0x43b080` (or new-hero `0x435580` if there are no .d2s).
* Join OK `0x434790` (success path `0x43490e`): `[0x7795ec]=2`, `[[0x7795d4]+0x19] = 9` (join), word `+0x1d = 1`.
* In game the client keeps its own game type at `0x7a0610` (8 host, 9 join — peeked: host 8, joiner 9).

So the redirect needs to set nothing beyond what `0x431a40` already does.

### Hook (implemented, verified)

`d2bnet_startup()` runs from the existing start-up hook `0x4065af` (before any menu exists) and rewrites table
callbacks (dword at entry+0x20):

| entry | address | was | now |
|---|---|---|---|
| 0x0a, 0x11 Battle.net | `0x708f10`, `0x709060` | 0x435d30 | **0x431a40** (TCP/IP Game) |
| 0x0b, 0x12 gateway line | `0x708f40`, `0x709090` | 0x434600 | 0x431a40 |
| 0xff Open Battle.net | `0x70bd00` | 0x435d10 | 0x431a40 |
| 0x10a TCP/IP Cancel | `0x70bf10` | 0x430c50 | 0x434df0 (back to the main menu, not Other MP) |

Alternatives considered: an OVERRIDE of `0x435d30`/`0x435d10` doing `[0x7795ec]=2; w32_callback(0x42ffe0); eax=1;
esp+=8` (works the same, needs a relift, and lets the host app know *which* button was clicked); patching the call at
`0x435d3a`/`0x435d1a`. The table poke is the smallest and needs no lifted-code change. If the host needs to know that
the TCP/IP screen was reached, the hook at **`0x42ffe0`** (TCP/IP screen built) is in place (`d2bnet: TCP/IP screen`).

Verification (run `$TMPDIR/rebnet/run1/menus`, screenshots copied to `re-bnet-evidence/`):
`01-main-menu.png` (labels "GAME LIST" — item 4), click 400,349 → `02-after-battlenet.png` = TCP/IP Options screen,
peek `7795ec` = 2, log `d2bnet: TCP/IP screen (connection type 2)`; Cancel → main menu (`03`); gateway line → TCP/IP
screen (`04`); Other Multiplayer (`05`, "OPEN GAME LIST") → Open Battle.net → TCP/IP screen (`06`), `7795ec` = 2. No
`net: connect` and no DNS lookups in the log. Then the full host/join below went through the redirected buttons.

## 2. Join Game box, host listening, game end

| what | address |
|---|---|
| Join Game callback (opens the box) | **`0x431d20`** |
| box built (all controls exist; regs dead) — hook | **`0x431de9`** (`mov eax,1` before `ret 4`) |
| edit control (pointer) | **`[0x779958]`** (control 0x10d: type 1 edit at 300,268, w213; Enter callback 0x434790) |
| the box's start text | **`0x7795f8`** `char[0x100]`; if `byte [0x7795f8]==0` it is first loaded from registry `HKCU\…\Diablo II\LastTcpIp` (`0x414e50`), then put in the edit with `0x4ffe10` |
| edit SetText (ANSI) | `0x4ffe10` `__fastcall(ecx=edit, edx=char*)` → `0x4ff5a0` (UTF-16) |
| edit GetText | `0x4fdd90` `__fastcall(ecx=edit)` → `wchar_t*` |
| OK button (0x111, hotkey VK_RETURN) and the edit's Enter | **`0x434790`** |
| Cancel (0x110) | `0x431e00` |

`0x434790` reads the **edit control** (not `0x7795f8`): GetText → ANSI `[ebp-0x410]`; empty → nothing; copies it to
`0x7795f8` and the registry (`0x415070`); `sscanf "%d.%d.%d.%d"`, otherwise resolves a host name with `0x40e0d0`
(failure clears the edit); copies the address to `[0x7795d4]+0x37`; "connecting" popup `0x42f6e0`; connects with
`0x52a750(0, addr)` (a thread `0x52a680` → `connect(addr:4000)`); failure → message 0x145b and back to the TCP/IP
screen (`0x4348a8`); success (`0x43490e`) → hero selection `0x43b080`.

**Filling and confirming programmatically (implemented, verified):** at the `0x431de9` hook `d2bnet_join_box()` writes
the chosen address to `0x7795f8` and calls `0x4ffe10` (via `w32_callback`, ecx = `[0x779958]`, edx = a guest heap
string); the host then sends Enter (`key:13`, the OK hotkey) — the game's own path. The address comes from
`d2bnet_set_join_address()` (any thread; consumed at the next box opening) or `D2_JOIN_ADDR` for tests.
Even simpler, without any hook: write the address into `0x7795f8` *before* Join Game is clicked — the box opens with
it. Calling `0x434790` directly from the hook also works in principle but runs the blocking connect inside the Join
button's callback; Enter is the robust choice.
Evidence (`run2/join`): `11-join-box.png` shows the box with `127.0.0.1`; log `d2bnet: join box open (edit control
0x0217b000, last address "")`, `join box filled with "127.0.0.1"`, `join OK pressed`, `joined: connection type 2`,
`net: connect 1000 to 127.0.0.1:4000`; both players in Act 1 (`13-host.png`, `14-join.png`, HUD check true).

**Host listening — port 4000 confirmed.** The server object is created by **`0x52b7a0`** (`push 0xfa0` = 4000 at
`0x52b7be` → `0x6bf760`; object stored in `0x882d08`). Client side, `0x44bc30` dispatches on the game type `0x7a0610`:
6/8 → `0x52b7a0(0, 1)` (at `0x44bcad`), then the host's own client connects to it. The listener is opened by
`0x6bf520`, which picks between two implementations on the server object's first dword: QServerNT `0x6c3360` →
`0x6c3120` (IOCP; `listen(s, 0xfa)` at `0x6c325a`) or **QServer98 `0x6c3b50`** (`bind` call at `0x6c3c26`, **`listen(s,
0x19)` at `0x6c3c4d`, success continues at `0x6c3c5e`**; socket at server `+0xb90`, port at `+0xba0`). The run logs show
`backlog 25` (0x19), so the TCP/IP host in this port runs the **QServer98** path (`0x6c3b50`); the NT path is not used.
Log (`run2/host`, `run3/host-nm`): `net: bind 1000 to 0.0.0.0:4000`, `net: listen 1000 backlog 25`, `net: connect 1001
to 127.0.0.1:4000` (the host's own client). Accepts are **not** a player count: run2 (host + one joiner) shows three
accepts — the host's own client, the joiner's probe connection made by the Join box's OK (`0x52a750` in `0x434790`,
closed again), and the joiner's game connection; run3 (host alone) shows one. Use the roster (item 3) for the count.

**Hosted game ends:** the client's game teardown **`0x44bd90`** calls **`0x52b2a0`** (server stop) for game types
0/1/6/8 → `0x6c0170` (QServer shutdown: closes the listeners at `0x6c01da`, `WSACleanup`), `0x882d08 = 0`. The player
unit `0x7a6a70` is cleared just before (watch: `0x039cbc00 -> 0` in `00409ab0 0040b3c0 0045f9f0 00465870 004659c0
0044c890 0044f360 …`), then `net: close 1000` (the listener), and the front end returns to the TCP/IP screen (hook
`0x42ffe0` fires again; `16-host-left.png`). Game start for comparison: `0x7a6a70` set in `00463a50 … 0044c6e0 0044efa0
… 0044f360` (`0x44f360` = the in-game loop). Recommended signals for the host app: started = listen OK (`0x6c3c5e`) or
`[0x7a6a70] != 0 && [0x7a0610] == 8`; ended = hook `0x52b2a0` (or the listener close seen in winsock).

Safety note: `0x44bc80`/`0x44bca2` hold hard-coded Blizzard addresses (207.82.87.139 / .243) for game types other than
0/1/6/8 (`0x7a0610 = 4`, …). Our paths never select those types, and the logs show only 127.0.0.1, but a
defence-in-depth block of 207.82.87.0/24 and ports 6112/6113 in `winsock.c` connect() would be cheap.

## 3. Host-side game details (any time during the hosted game)

`B = [0x7795d4]` — the front end's game/connection record (static heap block, the "BnetData" layout of earlier
versions; here `0x011ffa68`):

| field | where | verified |
|---|---|---|
| host's character name | `B+0xbd` char[16] (copied in `0x434a00`) | dump → "HostMac" |
| class | byte `B+0x1ee` (stored by `0x431e70`) 0 Ama, 1 Sor, 2 Nec, 3 Pal, 4 Bar, 5 Dru, 6 Asn | 4 (Barbarian) |
| char flags | word `B+0x1ef` (0x04 hardcore, 0x08 died, 0x20 expansion, bits 8-12 progression) | 0x0020 new hero; 0x0520 edited "Slayer" |
| difficulty chosen | byte **`B+0x210`** (0 Normal, 1 Nightmare, 2 Hell; set by `0x439af0`, cleared to 0 at hero OK `0x43986f`) | 0 in a Normal game; **1** after choosing Nightmare (`21-difficulty.png`, peek `7795d4,210` low byte 01) |
| host or join | dword `B+0x19` (8 host, 9 join); in game also `0x7a0610` | 8 / 9 |
| host IP shown / joined | `B+0x37` char[16] | |

Note: `B+0x3a1…0x3cb` (name/class/level block) is only filled on the Battle.net paths (types 1/3, `0x439957`) — not
for TCP/IP. Level is not in `B` for TCP/IP.

In-game client state:

| field | where | verified |
|---|---|---|
| player unit | `[0x7a6a70]`; class `unit+4`; name `[[unit+0x14]]+0` | class 4, "HostMac" |
| **roster (all players in the game)** | head **`[0x7bb5c0]`** → RosterUnit: `+0` name[16], `+0x10` unit id, `+0x14` party life %, `+0x1c` class, `+0x20` **word level**, `+0x22` party id, `+0x24` area, `+0x66` name copy, **`+0x80` next** | HostMac (id 1) → JoinMac (id 2) → 0 |
| **player count** | length of the `[0x7bb5c0]` list via `+0x80` | 2 with one joiner |
| level (alternative) | unit stat 12: stat list `[unit+0x5c]`; array at `+0x48` if `[sl+0x10] & 0x80000000` else `+0x24` (`{ptr, short count}`, 8-byte entries `{(stat<<16)|layer, value}`; sorted; see `0x625480`/`0x624f60`) | entry `000c0000 → 2`, stat 13 (experience) 500 |

Checks: run2 (new level-1 Barbarians HostMac hosting, JoinMac joined): `[0x7bb5c0]` → HostMac (+0x1c 4, +0x20 1)
→ `+0x80` JoinMac (id 2) → 0; `find:#2c1e400` → only `0x7bb5c0` points at the head. run3 (the scratch save edited
with `tests/d2s.py` to level 2, 500 experience, Nightmare open; hosted in Nightmare through the redirected Battle.net
button): roster `+0x20` = 2, `+0x80` = 0 (one player), `B+0xbd` "HostMac", `B+0x1ee` 4, `B+0x210` 1, `0x7a0610` 8,
unit stat 12 = 2. Evidence: `re-bnet-evidence/nm-log.txt`, `20-hero-select.png`, `21-difficulty.png`,
`22-in-game-nm.png`.

All of these are plain memory reads; the host app runs in the same process (`G_MEM + addr`), so it can read them at any
time (they are written by the game thread; a torn read only matters for the list walk — bound it, e.g. 8 entries).
The game name is not meaningful for TCP/IP games (no name is entered); the listing can use the host's character name.

## 4. Button labels

Labels are string-table ids in the control table (`+0x18`), looked up when a button is created by **`0x524a30`**
(`__fastcall ecx = id` → `wchar_t*`; ids ≥ 20000 expansionstring.tbl `[0x8829d4]`, ≥ 10000 patchstring.tbl
`[0x8829d0]`, else string.tbl `[0x8829b4]`). From `data\local\lng\eng\*.tbl` (d2data.mpq / Patch_D2.mpq / d2exp.mpq):
5106 "SINGLE PLAYER", **5107 "BATTLE.NET"**, 5108 "OTHER MULTIPLAYER", **5115 "OPEN BATTLE.NET"**, 5116 "TCP/IP GAME",
5117 "TCP/IP Options", 5118 "HOST GAME", 5119 "JOIN GAME"; 11003 (0x2b1f) is the Join box prompt. No code refers to
5107/5115 except the table (the `push 0x13fb` in `0x455f20` is an assert line number).

Implemented: an OVERRIDE at `0x524a30` returns a guest-heap UTF-16 string for 5107/5115 (`D2_BNET_LABEL`,
`D2_OPENBNET_LABEL`), otherwise runs the original; `01-main-menu.png` / `05-other-multiplayer.png` show "GAME LIST" /
"OPEN GAME LIST" in the game's font. Cheaper alternative with existing text only: poke the id at entry+0x18
(`0x709058` for 0x11, `0x70bcf8` for 0xff). The gateway line's text comes from `0x431af0` (registry gateway name); to
hide it, skip/disable control 0x12 (`[0x7795ac]`, e.g. `0x4f96f0(ecx, 0)` disables, as `0x4336c0` does for spawn
installs) or poke its type.

## Proposed design for the DHT game list

1. Launcher/host sets `D2_NO_BNET=1` (or make it the default): the Battle.net, gateway and Open Battle.net buttons
   open the TCP/IP screen; labels "GAME LIST" etc.
2. Host app hooks: `0x42ffe0` (TCP/IP screen up → show the DHT list overlay), `0x431de9` (Join box open),
   `0x435d80` (Host Game chosen), listen OK `0x6c3c5e` / stop `0x52b2a0` (announce / withdraw the DHT entry),
   `0x4348a8` / `0x43490e` (join failed / joined). Replace the `fprintf`s in `d2bnet_event` with a callback the host
   registers.
3. Join from the list: `d2bnet_set_join_address(ip)` then click Join Game (or the host clicks it) → box filled →
   Enter. Or set `0x7795f8` before clicking.
4. While hosting, announce every few seconds: name `B+0xbd`, class `B+0x1ee`, difficulty `B+0x210`, level and player
   count from the roster `[0x7bb5c0]`; TCP port 4000. Note: `portmap.c` (inherited from the DS2 port) maps UDP
   2302/6073 and nothing calls `portmap_open` in this port — internet hosting needs TCP 4000 mapped (NAT-PMP TCP opcode 2,
   UPnP protocol "TCP") at listen OK `0x6c3c5e`, removed at `0x52b2a0`.

## Proof-of-concept diff (branch re-bnet)

Commit `72c7f5a` on `re-bnet` (not pushed; nightly/main untouched):

* `recomp/runtime/win32/d2bnet.c` (new): `d2bnet_startup` (table pokes, `D2_NO_BNET=1`), `d2bnet_set_join_address`,
  `d2bnet_join_box` (hook `0x431de9`), `d2bnet_event` (hooks `0x42ffe0`, `0x435d80`, `0x434790`, `0x4348a8`,
  `0x43490e`: log lines `d2bnet: …`), `d2bnet_string` (OVERRIDE `0x524a30`, `D2_BNET_LABEL`, `D2_OPENBNET_LABEL`).
* `recomp/runtime/win32/hooks.c`: the cases above; `d2bnet_startup()` at the top of the `0x4065af` hook.
* `recomp/tools/lift.py`: the six HOOKS and the OVERRIDE (needs `build.sh`; a full rebuild took 1 min 50 s).
* `recomp/host/main.m`: peek-file `find:` / `dump:` (test mode, development).
* `recomp/runtime/win32/winsock.c`: `net: close` under `W32_NETLOG`.

Not committed (game-derived pictures / scratch tools): `re-bnet-evidence/` in this worktree — the screenshots and log
extracts named above, `bnet_test.py` (the driver: `bnet_test.py <app> <exe> <game> <work> menus|join|nm`, uses
`tests/d2game.py`; `nm` needs `saves/HostMac.d2s` beside it), `rx.py` (capstone helpers: `fdis`, `xref`, `calls`, `str`),
`forms.py` (dumps the control table with labels), `tbl.py` (string tables).

To reproduce: `recomp/build.sh <Game.exe>` and `recomp/tools/build_app.sh recomp/work/full` in this worktree, then run
with `D2_NO_BNET=1` (optionally `D2_BNET_LABEL=… D2_OPENBNET_LABEL=… D2_JOIN_ADDR=…`). Without `D2_NO_BNET` the build
behaves as nightly except for the label override (inactive unless the label variables are set) and the log hooks.
