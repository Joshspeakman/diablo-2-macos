# Test Lab

A panel for checking content and effects by hand: travel anywhere in the loaded map, spawn enemies and objects, give
and equip gear, and cast every spell and power, with a log of what was asked, what the game answered and what you saw.
It was written for the Linux port ([dungeon-siege-2-linux](https://github.com/Joshspeakman/dungeon-siege-2-linux));
the Mac version shares its content reader and its commands (`recomp/host/test_lab/lab_content.py` and
`lab_commands.py`, the same files) and has its own window.

## Starting it

Choose **Test Lab** in the launch window's Game row and press **Play**. The game opens in an ordinary window on the left of the screen, with the
Test Lab's window on the right. Load a single-player save through the game's Single Player menu (or create a
character), then **Prepare hero / unlock skills**.

- **Mouse:** click in the game to play; **F12** gives the mouse back for the Test Lab's window.
- **Saves:** the lab has its own data folder, `~/Games/DungeonSiege2Native/test-lab`. The first start copies your
  single-player saves and the game's options (`prefs.gas`, so your Shaders setting, and `DungeonSiege2.ini`) into it; **Fresh Save Copies, Then Start** (offered on later starts) replaces the lab's saves
  with your current ones, keeping the earlier lab saves in `test-lab/save-backups/`. Your own saves, settings, mods
  and stored CD key are never touched, and nothing in a normal Play enables the lab.
- The launcher's resolution, view distance and frame rate apply. Router port mapping is off in the lab.

## What it does

The lists are read from your own archives (`Logic.ds2res` and the others), including template inheritance; nothing of
the game is bundled. The base game gives 88 travel points, 450 spell/power templates, 37 active powers, 1,045 items,
1,646 enemies and 6,715 other objects. Counts include abstract and developer entries (hidden unless **Include bases /
developer content** is ticked); some unused retail definitions cannot be created.

- **Prepare hero / unlock skills:** the screen hero becomes invincible, the four combat skills go to 100, attributes to
  500, installed passive skills are unlocked, life and mana are restored. **Invincible / Vulnerable**, **Refill**,
  **Wound hero** (half life, for healing tests).
- **Locations:** travel the party to a starting point of the loaded map, including later acts. Wait for streaming and
  press Escape to skip a story movie. Destination scripts can start quests or cinematics; re-apply invincibility if a
  scene resets it. This is travel within the loaded map, not a Broken World loader or a quest tool.
- **Gear:** give, or give and equip, an item. Inventory space and equipment rules still apply.
- **Enemies / other objects:** spawn 1–16 nearby; the last one becomes the cast target. Invincible spawns allow repeated
  attacks; Friendly spawns allow friendly-target tests. Enemies run their own AI.
- **Spells / power templates:** cast at automatic, self, last-target or ground targeting; mana is refilled first.
  **Trigger effect** runs the game's magic directly, without the caster's animation; **Animated cast job** queues the
  normal cast (which can be interrupted). **Target party member…** picks a companion; **Kill test target** sets up
  resurrection tests.
- **Active powers:** selects, recharges and queues the real power. Its required stance is in the details; equip a
  suitable weapon. Their spell templates are listed separately for direct effect tests.
- **Cast filtered spells in sequence:** every listed spell in turn at the chosen interval, stopping on a rejected
  request. Stop cancels later casts, not effects already running.
- **Clean up test objects:** removes what the panel created (up to 512 per run), including equipped test gear. It does
  not undo quests, buffs, summons or other indirect effects; reload a fresh save copy for a clean scene.
- **Effect / shader definitions:** a searchable inventory of your files' effect scripts and `.fx` shaders.

## Records

Everything is in `~/Games/DungeonSiege2Native/test-lab`:

- `lab/history.jsonl`: each request, the game's reply, and your reviews. A successful reply only means the command ran
  or a job was queued: look at the result, then **Mark last test: looks correct** or **problem**, with a note.
- `frames-<time>.csv`: every presented frame's timing for that session (written when the game quits), in the Linux
  port's format: frame, time, interval, submit-to-present and present-to-GPU-done in milliseconds, and the size.
- `catalog.json`: the content list. **Export report…** writes the catalog, history and reviews as one file.

Reports and catalogs contain descriptions from your game files: keep them to yourself.

## Shader effects on the Mac

Unlike the Linux port (fixed-function graphics so far), the Mac runs the game's shader effects, translated to Metal,
when **Options → Video → Shaders** is on. Test effects with shaders both on and off: the game draws a different path
for each. The history's records say `macos-metal`.

## Checked on the Mac

With the Harker save and Shaders on Advanced, in test mode (no lab window; requests from `lab_tool.py`): status,
Prepare hero, spawning a Skath Disciple as the target, fireball, iceball, ice beam and lightning blast through the effect
path, clean-up, and travel to the Azunite Shrine of Life, Snowbrook Mountain and the Northern Greilyn Jungle all
answered and did what they said. The fire, ice and lightning effects, the snow and the glowing teleporter pylons drew
correctly. Once, a frame taken just after the first lightning blast showed the whole world white (the interface was
fine); it did not happen again in three repeats of the same casts: if you see a white flash with a spell, mark it
with a note. The lab's window itself (the launcher's Test Lab entry) has not been tried by hand yet.

## From a terminal

`recomp/host/test_lab/lab_tool.py` sends the same requests while a lab game is running, for scripted checks:

```sh
T=recomp/host/test_lab/lab_tool.py; LAB=~/Games/DungeonSiege2Native/test-lab
python3 $T find "/path/to/Dungeon Siege 2" spells fire            # catalog entries matching words
python3 $T send $LAB prepare
DS_GAME_DIR="/path/to/Dungeon Siege 2" python3 $T send $LAB cast spells <template> target=self
```

## How it works

`DS_TEST_LAB=1` (set when the launcher starts the Test Lab) registers a few functions with the game's script binder
(FuBi) before the engine starts, and the game thread checks `<lab data>/lab/request` up to five times a second from
its message loop, running the request through the game's own Skrit compiler, so objects, spells and travel follow the
normal engine paths (`recomp/runtime/win32/test_lab.c`). One request at a time; an unanswered request is reported as
unconfirmed after two minutes, never retried. Every request first checks that a single-player world is loaded, so
nothing is sent in a multiplayer game. There is no network listener.

`recomp/tests/test_lab_test.py` checks the archive reader, the parser and the mailbox with synthetic data (no game
files needed): `python3 recomp/tests/test_lab_test.py`.
