#!/bin/bash
# run_tests.sh [Diablo II folder]: the test suite. Without a game folder, the parts that need no game files; with
# one (and the game built: build.sh + tools/build_app.sh), also the end-to-end game test (tests/game_test.py).
#   present shader   the Metal renderer's present pass: scaling filters (sharp, smooth, pixel, crt) and the gamma ramp
#   launcher         the launch window's settings reaching the runtime, and its drawing
#   eax reverb       the environment reverb behind 3D sound's environmental effects: decay, HF decay, timing, levels
#   game list        the serverless game list (d2net.c) and its DHT client (p2p_dht.c, against fake nodes; fuzzed
#                    with damaged answers under the address and undefined-behaviour sanitizers); no internet, no router
#   game             video, palette-exact graphics, sound (speech matched to the game's archive), input, saving,
#                    loading, the windowed renderer and TCP/IP games, on the player's own copy
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; RECOMP="$(dirname "$HERE")"; ROOT="$(dirname "$RECOMP")"
T="$(mktemp -d /tmp/d2-tests.XXXXXX)"; FAIL=0
step() { echo "== $1"; }
step "present shader"
clang -fobjc-arc -framework Metal -framework Foundation -framework QuartzCore -I "$ROOT/src/renderer" "$HERE/present_shader_test.m" "$ROOT/src/renderer/renderer.m" -o "$T/present_shader_test" \
  && (cd "$ROOT/src/renderer" && "$T/present_shader_test") || FAIL=1
step "renderer initialization bounds"
clang -O1 -g -fobjc-arc -fsanitize=address,undefined -framework Metal -framework Foundation -framework QuartzCore -I "$ROOT/src/renderer" "$HERE/renderer_init_test.m" "$ROOT/src/renderer/renderer.m" -o "$T/renderer_init_test" \
  && (cd "$ROOT/src/renderer" && "$T/renderer_init_test") || FAIL=1
step "upload ring bounds"
clang -O1 -g -fobjc-arc -fsanitize=address,undefined -framework Metal -framework Foundation -framework QuartzCore "$HERE/renderer_ring_test.m" -o "$T/renderer_ring_test" \
  && (cd "$ROOT/src/renderer" && "$T/renderer_ring_test") || FAIL=1
step "frame trace shutdown"
clang -O1 -g -fsanitize=address,undefined "$HERE/frame_trace_test.c" -o "$T/frame_trace_test" && "$T/frame_trace_test" || FAIL=1
step "critical-section collisions"
clang -O1 -g -fsanitize=address,undefined "$HERE/critical_sections_test.c" -o "$T/critical_sections_test" && "$T/critical_sections_test" || FAIL=1
step "eax reverb"
clang -O2 -I "$RECOMP/runtime/win32" "$HERE/eax_reverb_test.c" "$RECOMP/runtime/win32/eax_reverb.c" -o "$T/eax_reverb_test" && "$T/eax_reverb_test" || FAIL=1
step "game list"
SAN="-O1 -g -fsanitize=address,undefined"
clang $SAN -I "$RECOMP/runtime" -I "$RECOMP/runtime/win32" "$HERE/d2net_test.c" "$RECOMP/runtime/win32/p2p_dht.c" -lpthread -o "$T/d2net_test" && "$T/d2net_test" || FAIL=1
clang -O1 -g -I "$RECOMP/runtime/win32" "$HERE/p2p_dht_test.c" "$RECOMP/runtime/win32/p2p_dht.c" -lpthread -o "$T/p2p_dht_test" && "$T/p2p_dht_test" || FAIL=1
clang $SAN -I "$RECOMP/runtime/win32" "$HERE/p2p_dht_fuzz.c" -lpthread -o "$T/p2p_dht_fuzz" && "$T/p2p_dht_fuzz" 500000 || FAIL=1
step "launcher"
clang -fobjc-arc -framework AppKit -framework Metal -framework ImageIO -lz "$HERE/launcher_test.m" -o "$T/launcher_test" && "$T/launcher_test" || FAIL=1
if [ $# -ge 1 ]; then
  step "game (a few minutes; the game runs in the background)"
  python3 "$HERE/game_test.py" --game "$1" --work "$T/game" || FAIL=1
fi
echo; [ $FAIL = 0 ] && echo "all tests passed" || echo "SOME TESTS FAILED (details above; files in $T)"
exit $FAIL
