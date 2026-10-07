#!/bin/bash
# build.sh <Game.exe> [outdir]
# Recompile the user's own Diablo II 1.14d Game.exe (and the Bink, Smacker and Intel JPEG DLLs in its folder) to arm64:
# analysis -> C -> libgame.dylib.
# Everything generated is derived from the executable and stays local (work/ is git-ignored).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EXE="${1:?usage: build.sh <Game.exe> [outdir]}"; OUT="${2:-$HERE/work/full}"
SHA=631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf          # Diablo II + Lord of Destruction 1.14d
case "$(shasum -a 256 "$EXE" | cut -d' ' -f1)" in "$SHA") ;;
  *) echo "not the original Diablo II 1.14d Game.exe (update the game to 1.14d first)"; exit 1;; esac
PY="$HERE/.venv/bin/python"
[ -x "$PY" ] || { python3 -m venv "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
mkdir -p "$HERE/work"
cp "$EXE" "$HERE/work/Game.exe"                  # a copy (no patches so far)
EXE="$HERE/work/Game.exe"
echo "== analysis"; "$PY" "$HERE/tools/analyze.py" "$EXE" "$HERE/work/analysis.json" | head -6
echo "== lifting";  "$PY" "$HERE/tools/lift.py" "$EXE" "$HERE/work/analysis.json" "$OUT"
# the DLLs Game.exe imports from its own folder: Bink and Smacker (videos) and the Intel JPEG Library, recompiled the
# same way; game folder paths, so the build needs the folder the executable came from
GAME="$(cd "$(dirname "$1")" && pwd)"; IMGS=("=")
mkdir -p "$HERE/work/dlls"; NIMP=$("$PY" -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["imports"]))' "$OUT/image.json")
# Smacker asks for the same base as Bink (0x10000000): a copy moved to 0x11000000 is lifted instead (tools/rebase_pe.py)
for rel in binkw32.dll SmackW32.dll ijl11.dll; do
  tag=$(basename "$rel" | tr . _ | tr A-Z a-z); src="$GAME/$rel"; [ -f "$src" ] || { echo "missing $src"; exit 1; }
  if [ "$rel" = SmackW32.dll ]; then "$PY" "$HERE/tools/rebase_pe.py" "$src" "$HERE/work/dlls/$rel" 11000000; src="$HERE/work/dlls/$rel"; fi
  "$PY" "$HERE/tools/analyze.py" "$src" "$HERE/work/dlls/$tag.json" | head -1
  "$PY" "$HERE/tools/lift.py" "$src" "$HERE/work/dlls/$tag.json" "$OUT" --tag "$tag" --imp-base "$NIMP" | grep -E 'lifted|internal'
  NIMP=$((NIMP + $("$PY" -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["imports"]))' "$OUT/${tag}_image.json")))
  IMGS+=("$tag=$rel")
done
"$PY" "$HERE/tools/link_tables.py" "$OUT" "${IMGS[@]}"
echo "== compiling"
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
printf '%s\n' "$OUT"/lift_*.c "$OUT"/*_lift_*.c "$OUT/fntab.c" | xargs -n 1 -P "$(sysctl -n hw.ncpu)" "$HERE/tools/cc1.sh" "$OUT"
rm -f "$OUT/obj/rt.o"
"$HERE/tools/build_runtime.sh" "$OUT"            # runtime + Win32 layer, then the link
echo "== $OUT/libgame.dylib"
