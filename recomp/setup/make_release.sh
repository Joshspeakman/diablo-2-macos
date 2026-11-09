#!/bin/bash
# make_release.sh <game folder> <version tag>: builds the drag-and-drop app and disk image for a public release.
#
# The app holds our runtime and launcher, compiled here against Apple's SDK; the compiler and Python that build the game on
# the player's Mac; and address lists saying where the functions are in each exact game file. It holds no game code or data:
# check_release.py fails the release otherwise. The game piece is a separate library the executable links weakly, so the app
# runs before the game exists and binds it once setup has built it (setup_ui.m restarts itself with the library path set).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"; S="$HERE/setup"; GAME="$(cd "${1:?usage: make_release.sh <game folder> <tag>}" && pwd)"; TAG="${2:?tag}"
j() { python3 -c 'import json,sys; v=json.load(open(sys.argv[1])); v=v.get(sys.argv[2],sys.argv[3] if len(sys.argv)>3 else ""); print(" ".join(map(str,v)) if isinstance(v,list) else v)' "$S/game.json" "$@"; }
NAME="$(j name)"; BIN="$(j binary)"; SLUG="${NAME// /-}"; OUTREL="$(j out work/full)"
REPO_NAME="$(basename "$ROOT")"; B="$HOME/ProjectBuilds/$REPO_NAME"; REL="$B/release"; mkdir -p "$REL" "$B/recomp/work"
[ -e "$HERE/work" ] || { ln -s "$B/recomp/work" "$HERE/work"; C="$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir)"; grep -qxF recomp/work "$C/info/exclude" || echo recomp/work >> "$C/info/exclude"; }
[ -x "$HERE/.venv/bin/python" ] || { python3 -m venv "$HERE/.venv"; "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
TK="$HOME/ProjectBuilds/_toolkit/toolkit.tar.xz"; [ -f "$TK" ] || "$S/make_toolkit.sh" "$TK"
if [ -z "${SKIP_FULL_BUILD:-}" ]; then echo "== full build (for the runtime's objects and to prove this tree builds)"; (cd "$HERE"; GAME="$GAME" bash -c "$(j build)" 2>&1 | tail -2); fi
OUT="$HERE/$OUTREL"; O="$OUT/obj/easy"; rm -rf "${O:?}"; mkdir -p "$O"
echo "== the runtime, split from the game"
clang -c $(j rt_opt -O1) -g -Wall -Wno-unused-function -fno-strict-aliasing -DRT_SPLIT $(j rt_cflags) -I "$HERE/runtime" -I "$HERE/runtime/win32" -o "$OUT/obj/rt/rt.o" "$HERE/runtime/rt.c"
clang -c -O2 -fobjc-arc -Wall -Wno-unused-function -DDS_EASY_INSTALL $(j host_cflags) -I "$HERE/runtime" -DDS_BUILD="\"$TAG\"" -o "$OUT/obj/host/main.o" "$HERE/host/main.m"
clang -c -O2 -fobjc-arc -Wall -o "$OUT/obj/host/setup_ui.o" "$S/setup_ui.m"
cat > "$O/stub.c" <<'STUB'
#include <stdint.h>
const char rt_fntab[1], rt_images[1], rt_imptab[1]; const unsigned rt_fntab_n, rt_images_n, rt_imptab_n; __thread uint32_t rt_host_rc;
STUB
clang -dynamiclib -arch arm64 -install_name @rpath/libgame.dylib -o "$O/libgame.dylib" "$O/stub.c"
FW=(); for f in $(j frameworks); do FW+=(-framework "$f"); done
find "$OUT/obj/rt" "$OUT/obj/host" -name '*.o' > "$O/objs.txt"
A="$REL/$NAME.app"; rm -rf "${A:?}"; mkdir -p "$A/Contents/MacOS" "$A/Contents/Resources/setup"
clang -o "$A/Contents/MacOS/$BIN" -filelist "$O/objs.txt" -Wl,-weak_library,"$O/libgame.dylib" -Wl,-export_dynamic -Wl,-rpath,@executable_path/../Frameworks "${FW[@]}" -lz $(j link_flags)
cp "$OUT/app/shaders.metal" "$OUT/app/dsr_snapshot.bin" "$A/Contents/MacOS/"
sed -e "s|@REPO@|$(j release_repo)|; s|@ASSET@|$SLUG.zip|" "$S/update.sh" > "$A/Contents/MacOS/update"; chmod +x "$A/Contents/MacOS/update"
echo "== setup files"
R="$A/Contents/Resources/setup"; mkdir -p "$R/tools" "$R/runtime" "$R/lists"
cp "$S/setup.py" "$S/game_tls.c" "$R/"; cp -R "$S/include" "$R/include"; cp "$HERE"/tools/*.py "$R/tools/"; cp "$HERE/runtime/rt.h" "$R/runtime/"; cp "$TK" "$R/toolkit.tar.xz"
CODE="$(cat "$HERE"/tools/*.py "$HERE/runtime/rt.h" "$S"/include/* "$S/game_tls.c" | shasum -a 256 | cut -d' ' -f1)"
python3 - "$S/game.json" "$R/game.json" "$CODE" "$TAG" <<'PY'
import json, sys
c = json.load(open(sys.argv[1])); c["code"] = sys.argv[3]; c["release"] = sys.argv[4]
for k in ("build", "out", "rt_cflags", "rt_opt", "host_cflags", "frameworks", "link_flags", "binary", "icon_label", "icon_color", "local_network", "release_repo", "bundle_id"): c.pop(k, None)
json.dump(c, open(sys.argv[2], "w"), indent=1)
PY
echo "== address lists"
LT="$(mktemp -d)"; "$HERE/.venv/bin/python" "$S/setup.py" --game "$GAME" --root "$LT" --setup "$R" --tools "$LT" --make-lists "$R/lists" | tail -1 >/dev/null; rm -rf "${LT:?}"
ls "$R/lists" | wc -l | awk '{print "lists:", $1}'
echo "== icon and bundle"
I="$(mktemp -d)"; clang -fobjc-arc -framework AppKit "$S/make_icon.m" -o "$I/mk"; "$I/mk" "$(j icon_label)" $(j icon_color) "$I/i.png"; mkdir "$I/i.iconset"
for n in 16 32 128 256 512; do sips -z $n $n "$I/i.png" --out "$I/i.iconset/icon_${n}x${n}.png" >/dev/null; sips -z $((n*2)) $((n*2)) "$I/i.png" --out "$I/i.iconset/icon_${n}x${n}@2x.png" >/dev/null; done
iconutil -c icns "$I/i.iconset" -o "$A/Contents/Resources/icon.icns"; rm -rf "${I:?}"
cat > "$A/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string><key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$(j bundle_id)</string><key>CFBundleExecutable</key><string>$BIN</string>
  <key>CFBundleIconFile</key><string>icon</string><key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${TAG#v}</string><key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>GCSupportsGameMode</key><true/><key>LSMinimumSystemVersion</key><string>13.0</string><key>NSHighResolutionCapable</key><true/>
  <key>NSLocalNetworkUsageDescription</key><string>$(j local_network)</string>
  <key>CFBundleDocumentTypes</key><array><dict><key>CFBundleTypeName</key><string>Game Folder</string><key>CFBundleTypeRole</key><string>Viewer</string>
    <key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.folder</string></array></dict></array>
</dict></plist>
PLIST
python3 "$S/check_release.py" "$A" "$GAME" "$S/game.json"
codesign --force --deep -s - "$A"; codesign -v "$A"
echo "== packaging"
D="$(mktemp -d)"; cp -R "$A" "$D/"; ln -s /Applications "$D/Applications"; sed "s/@NAME@/$NAME/g" "$S/ReadMe.txt" > "$D/Read Me First.txt"
rm -f "${REL:?}/${SLUG:?}.dmg" "${REL:?}/${SLUG:?}.zip"; MB=$(( $(du -sm "$D" | cut -f1) + 120 )); hdiutil create -quiet -size "${MB}m" -fs HFS+ -volname "$NAME" -srcfolder "$D" -ov -format UDZO "$REL/$SLUG.dmg"; rm -rf "${D:?}"
(cd "$REL"; ditto -c -k --keepParent "$NAME.app" "$SLUG.zip")
du -sh "$A" "$REL/$SLUG.dmg" | sed "s|$REL/||"
