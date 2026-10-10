# Recompiler: Diablo II as native arm64 code

This builds the user's own **Diablo II: Lord of Destruction 1.14d** `Game.exe` and its Bink, Smacker and Intel JPEG
DLLs into native Apple Silicon code. Generated game code stays in the ignored `work/` directory. Installation,
graphics options and detailed tests are described in the [main README](../README.md).

```sh
recomp/build.sh "/path/to/Diablo II/Game.exe"
recomp/tools/build_app.sh recomp/work/full
recomp/run.sh "/path/to/Diablo II"
```

After changes confined to the Win32 runtime or host, reuse the existing lifted objects:

```sh
recomp/tools/build_runtime.sh recomp/work/full
recomp/tools/build_app.sh recomp/work/full
```

Changes to the lifter, its hooks, or the input binaries require the full build. `build_runtime.sh` alone does not
refresh lifted game code.

## Rendering and timing

DirectDraw and windowed GDI use the game's 8-bit software renderer. `runtime/win32/ddraw_pal.c` expands its palette
and presents the resulting picture through Metal. Direct3D uses the game's DirectX 6, pre-transformed triangles
and the fixed-function Metal renderer in `src/renderer`; this game does not use Dungeon Siege II's HLSL effects.

The original simulation and sprite animation run at 25 ticks per second. `D2_FPS=60|120|display` enables the
`d2smooth.c` draw scheduler and interpolates moving paths without changing game ticks or saved positions. With
that scheduler active, the host defaults `DSR_FPSCAP` to zero: the generic automatic limiter would otherwise hold
120 FPS at 60 for about ten seconds, then add another wait to every frame. An explicit `DSR_FPSCAP` still applies.
Original-rate modes keep their existing limiter. That limiter now sleeps closer to its deadline before spinning.

`DS_FRAME_TRACE=/absolute/path.csv` records completed presentations. Shutdown detaches the trace under a mutex,
so delayed GPU callbacks cannot access freed samples. Unfinished or inconsistent timestamps are omitted.
A reduced `DSR_RINGMB` test buffer grows when an upload or complete draw would exceed it; the default remains
256 MiB. Wrapping alone cannot accommodate an allocation larger than the entire buffer.
`DS_TEST=1` keeps window activation changes from pausing or minimizing background tests; normal focus behavior
is unchanged. Screenshots and command-file input require test mode.

The runtime lock table preserves live entries past deleted hash slots, preventing a second host mutex from being
created for the same guest critical section.

## Validation

```sh
recomp/tests/run_tests.sh
python3 recomp/tests/game_test.py --game "/path/to/Diablo II" --only d3d,zoom
clang -O2 recomp/tests/frame_wait_test.c -o /tmp/d2-frame-wait && /tmp/d2-frame-wait
clang -O1 -g -fsanitize=thread recomp/tests/frame_trace_test.c -o /tmp/d2-frame-trace && /tmp/d2-frame-trace
```

The suite checks Metal scaling/gamma, renderer buffer source bounds and oversized upload/draw reservations under ASAN, concurrent trace shutdown,
critical-section collision/deletion semantics, launcher settings, audio reverb and the local game-list protocol. The Direct3D game test includes 60/120 FPS
movement readbacks shortly after loading, plus perspective, display resolution and wide-screen checks. Tests
use scratch characters and data directories, leaving installed saves alone. The pacing microbenchmark reports
CPU time and deadline lateness; it does not measure game FPS.

The October 2026 visual reference is tracked in [issue 1](https://github.com/Joshspeakman/diablo-2-macos/issues/1).
It uses the original 1.14d executable under Proton, 800×600 windowed software rendering, with no D2GL or FreeRes.
The camp's High/Medium lighting and blended-shadow settings can be compared against the native GDI mode using
the supplied isolated character. It is not a reference for the optional remastered or perspective modes.
