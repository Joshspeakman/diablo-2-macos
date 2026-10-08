#!/bin/bash
# run_tests.sh [Diablo II folder]: the test suite. Without a game folder, the parts that need no game files; with
# one (and the game built: build.sh + tools/build_app.sh), also the end-to-end game test (tests/game_test.py).
#   present shader   the Metal renderer's present pass: scaling filters (sharp, smooth, pixel, crt) and the gamma ramp
#   launcher         the launch window's settings reaching the runtime, and its drawing
#   eax reverb       the environment reverb behind 3D sound's environmental effects: decay, HF decay, timing, levels
#   game             video, palette-exact graphics, sound (speech matched to the game's archive), input, saving,
#                    loading, the windowed renderer and TCP/IP games, on the player's own copy
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; RECOMP="$(dirname "$HERE")"; ROOT="$(dirname "$RECOMP")"
T="$(mktemp -d /tmp/d2-tests.XXXXXX)"; FAIL=0
step() { echo "== $1"; }
step "present shader"
clang -fobjc-arc -framework Metal -framework Foundation -framework QuartzCore -I "$ROOT/src/renderer" "$HERE/present_shader_test.m" "$ROOT/src/renderer/renderer.m" -o "$T/present_shader_test" \
  && (cd "$ROOT/src/renderer" && "$T/present_shader_test") || FAIL=1
step "eax reverb"
clang -O2 -I "$RECOMP/runtime/win32" "$HERE/eax_reverb_test.c" "$RECOMP/runtime/win32/eax_reverb.c" -o "$T/eax_reverb_test" && "$T/eax_reverb_test" || FAIL=1
step "launcher"
clang -fobjc-arc -framework AppKit -framework Metal -framework ImageIO -lz "$HERE/launcher_test.m" -o "$T/launcher_test" && "$T/launcher_test" || FAIL=1
if [ $# -ge 1 ]; then
  step "game (a few minutes; the game runs in the background)"
  python3 "$HERE/game_test.py" --game "$1" --work "$T/game" || FAIL=1
fi
echo; [ $FAIL = 0 ] && echo "all tests passed" || echo "SOME TESTS FAILED (details above; files in $T)"
exit $FAIL
