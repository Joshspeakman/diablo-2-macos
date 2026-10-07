# Notices and credits

**Diablo II** and **Diablo II: Lord of Destruction** © Blizzard Entertainment. This project contains no part of the
game. The installer recompiles the executable of a copy the user owns, after verifying its checksum, and the app reads
the game's data from that copy. The launch window's art is decoded from the player's own archives at install time.

Used at build time (installed into `recomp/.venv` with pip, not redistributed here):
- **Capstone** (BSD) — disassembly for the recompiler. https://www.capstone-engine.org/
- **Unicorn** (GPL-2.0) — x86 emulation, used only by the development harness to compare the recompiled code with
  the original. https://www.unicorn-engine.org/

The recompiler, runtime and renderer started from the Dungeon Siege ports (dungeon-siege-macos,
dungeon-siege-2-macos) by the same author.

Formats this project reads were implemented from public descriptions: the MPQ archive format (as documented by the
modding community, e.g. StormLib's documentation), the DC6 sprite format, and Microsoft's DirectDraw, DirectSound,
GDI and Winsock documentation. No code was taken from other projects. `src/dsr/dsr_snapshot.bin` records device
capabilities reported by Wine's DirectDraw implementation (Wine, LGPL) for the Direct3D 7 path the Dungeon Siege port
uses; Diablo II does not use it.
