"""End-to-end test of the native Diablo II on the player's own copy: video, graphics, sound, input, saving and loading.

  game_test.py --game <Diablo II folder> [--app recomp/work/full/app/Diablo2Native] [--exe recomp/work/Game.exe]
               [--work <scratch folder>] [--only <stage,...>] [--keep]

It runs the game in test mode (DS_TEST=1, sound muted but recorded) from a fresh data folder and plays it through
the window's own input path (DS_CMDFILE clicks and keys), checking each stage from screenshots, the run log, the
recorded sound and the files the game writes:

  video     the Blizzard intro plays (Bink through DirectDraw: 640x480 16-bit pictures, not black) with its sound
  menus     clicks reach the hero selection; the 8-bit picture is exactly the game's palette (no colour is lost
            between the game's pixels and the screen)
  ingame    a new Barbarian enters Act 1: the life and mana orbs are drawn (red, blue), every pixel from the palette
  speech    talking to Warriv plays his introduction: the recorded sound matches that speech decoded independently
            from the game's archive (tests/mpq.py) - same shape (correlation), same speed (no drift between windows)
  walk      a click moves the character (the picture changes, the HUD stays)
  save      Escape opens the menu; Save and Exit writes <Saved Games>/Diablo II/MacTest.d2s with a valid header
  load      Single Player lists MacTest; OK enters the game again with the HUD
  gdi       the game's windowed renderer (D2_RENDERER=gdi) reaches the main menu, also exactly through the palette
  d3d       the game's Direct3D renderer (D2_RENDERER=d3d) in each view (D2_PERSPECTIVE off, on): a new character in
            Act 1 with the HUD drawn, the game reading the view chosen, and the two views' pictures different; then
            at the display's resolution with smooth textures (D2_RESOLUTION=display, D2_TEXTURES=smooth): a larger
            frame with the HUD in its place and the same overall brightness; then at 60 frames a second (D2_FPS=60)
            while walking: 12 consecutive frames about 16 ms apart, each moved on from the one before, by even steps
            (the movement blended between the game's 25 ticks, not repeated frames); then 1400 wide (D2_WIDTH): the
            menus in the middle with bars at the sides, in game a 1400x600 frame with the orbs at its edges, and the
            inventory opening at the right edge (the middle of the screen untouched)
  zoom      Direct3D with the launch window's Zoom (D2_ZOOM_MAX=2): the mouse wheel zooms out, the world is drawn out to
            the screen's edges (the corners lit, not the black past the game's own view) with the bottom bar and orbs as
            they were; the wheel back up returns to the game's own view (the picture as before)
  sound3d   with 3D Sound and Environmental Effects chosen (the game's Sound Mixer setting 2): the game finds the
            device's 3D voices and EAX 2.0 at start, plays its sounds through 3D buffers it places, sets each one's EAX
            properties and the area's environment, and the mix in the game stays below full scale
  tcpip     two instances: one hosts a TCP/IP game, the other joins it by this Mac's LAN address; both are in Act 1
  tcpexit   then the host leaves (Save and Exit) with the joiner still in its game and quits through the game's own
            menus (ExitProcess): the game's TCP/IP thread had ended when the game waited for it (it blocks in select
            between packets instead of spinning on a closed connection), and the host ends without a fault or report
  gamelist  the same through the Battle.net button (ONLINE GAMES) and the game list: the joiner picks the host from it
            and the host's server has accepted the second player

The game folder is only read. Nothing is written outside the scratch folder."""
import argparse, glob, os, struct, subprocess, sys, time, wave, math
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import png, mpq
from d2game import *
import d2game

ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True); ap.add_argument('--app', default=os.path.join(ROOT, 'work/full/app/Diablo2Native'))
ap.add_argument('--exe', default=os.path.join(ROOT, 'work/Game.exe')); ap.add_argument('--work', default=None)
ap.add_argument('--only', default=None); ap.add_argument('--keep', action='store_true')
a = ap.parse_args()
WORK = a.work or os.path.join('/tmp', 'd2-game-test-%d' % os.getpid())
os.makedirs(WORK, exist_ok=True)
ONLY = set(a.only.split(',')) if a.only else None
d2game.CONFIG.update(app=a.app, exe=a.exe, game=a.game, work=WORK)
results = []

def save_and_exit(g):
    """Escape until the game menu shows (its gold text), then Save and Exit Game; returns how much menu text was seen"""
    menu = 0
    for k in range(4):
        g.send('key:27', 1.5); s = g.shot('menu%d' % k); menu = gold_fraction(s, (190, 245, 610, 275))
        if menu > MENU_GOLD: break
    g.send('click:400,259', 6)                                                               # Save and Exit Game
    return menu
def stage(name):
    return ONLY is None or name in ONLY
def report(name, ok, detail):
    results.append((name, ok, detail)); print('%-7s %s  %s' % (name, 'ok  ' if ok else 'FAIL', detail), flush=True)


try:
    # ---------------- a new character, end to end ----------------
    g = Game('new')
    if stage('video'):
        shots = []
        for k in range(3): time.sleep(1.2); shots.append(g.shot('video%d' % k))   # the logo animation, 1.2 s apart
        sizes = log_sizes(g); b = max(lit_fraction(s) for s in shots); moving = differs(shots[0], shots[-1])
        intro_rms = wav_rms(g.wav, 0.5, 3.0) if os.path.exists(g.wav) else 0
        report('video', '640x480' in sizes and b > 0.02 and moving > 0.01 and intro_rms > 100 and palette_exact(shots[-1]) is None,
               'pictures %s, lit %.0f%%, moving %.0f%%, intro sound rms %.0f, 16-bit (no palette): %s' % (
                   sorted(sizes), 100 * b, 100 * moving, intro_rms, palette_exact(shots[-1]) is None))
    to_hero_select(g)
    if stage('menus'):
        s = g.shot('heroes'); pe = palette_exact(s)
        report('menus', pe is not None and pe > 0.999 and brightness(s) > 10, 'hero selection: %.4f of pixels exactly from the palette' % (pe or 0))
    g.send('click:400,300', 2); g.send('type:MacTest', 2); g.send('click:690,555', 12)     # the Barbarian, a name, OK
    s = g.shot('act1'); ok, life, mana = hud(s); pe = palette_exact(s)
    if stage('ingame'):
        report('ingame', ok and pe is not None and pe > 0.999, 'life orb %s, mana orb %s, palette-exact %.4f' % (
            tuple(int(v) for v in life), tuple(int(v) for v in mana), pe or 0))
    if stage('speech'):
        t_click = time.time() - g.t0
        g.send('click:300,255', 16)                                                          # Warriv, at his wagon
        ref = os.path.join(WORK, 'war_intro.wav')
        if not os.path.exists(ref):
            exe = os.path.join(a.game, 'Game.exe'); mpq.huffman_tables_from_exe(exe if os.path.exists(exe) else a.exe)
            data = mpq.MPQ(os.path.join(a.game, 'd2speech.mpq')).read('data\\local\\sfx\\Act1\\Warriv\\War_act1_intro.wav')
            open(ref, 'wb').write(data)
        am = os.path.join(WORK, 'audiomatch')
        if not os.path.exists(am): subprocess.check_call(['clang', '-O2', '-o', am, os.path.join(HERE, 'audiomatch.c')])
        outs = []
        for off in ('1', '5', '9'):
            r = subprocess.run([am, ref, g.wav, '3', off], capture_output=True, text=True); outs.append(r.stdout.strip())
        lags = [float(o.split('lag=')[1].split()[0]) for o in outs if 'lag=' in o]
        corrs = [float(o.split('corr=')[1].split()[0]) for o in outs if 'corr=' in o]
        drift = max(lags) - min(lags) if lags else 99
        report('speech', len(corrs) == 3 and min(corrs) >= 0.8 and drift < 0.01,
               'correlation %s, lag %.3f s (spread %.4f s; talk at %.1f s)' % (corrs, lags[0] if lags else -1, drift, t_click))
        for _ in range(2): g.send('key:27', 1)                                              # the dialogue closed
    if stage('walk'):
        s1 = g.shot('walk0'); g.send('click:700,150', 3); s2 = g.shot('walk1'); d = differs(s1, s2); ok2 = hud(s2)[0]
        report('walk', d > 0.2 and ok2, 'picture changed in %.0f%% of samples; HUD %s' % (100 * d, 'kept' if ok2 else 'lost'))
    if stage('save') or stage('load'):
        menu = save_and_exit(g)
        saves = glob.glob(os.path.join(g.dir, 'drive_c', '**', 'MacTest.d2s'), recursive=True)
        hdr = open(saves[0], 'rb').read() if saves else b''
        good = len(hdr) > 300 and struct.unpack_from('<I', hdr, 0)[0] == 0xaa55aa55 and struct.unpack_from('<I', hdr, 8)[0] == len(hdr) and b'MacTest' in hdr[:40]
        report('save', menu > MENU_GOLD and good, 'menu text %.2f; %s (%d bytes, %s)' % (
            menu, os.path.relpath(saves[0], g.dir) if saves else 'no save file', len(hdr), 'valid header' if good else 'bad header'))
        if stage('load'):
            g.send('click:400,306', 4); s = g.shot('list')                                   # Single Player: the character list
            g.send('click:690,555', 10); s2 = g.shot('loaded'); ok, life, mana = hud(s2)
            report('load', ok and g.alive(), 'back in Act 1: life orb %s, mana orb %s' % (tuple(int(v) for v in life), tuple(int(v) for v in mana)))
    alive = g.alive(); g.stop()
    if not alive: report('run', False, 'the game ended early: ' + open(g.log).read()[-300:])

    # ---------------- the windowed (GDI) renderer ----------------
    if stage('gdi'):
        g = Game('gdi', {'D2_RENDERER': 'gdi'}); time.sleep(5)
        for _ in range(3): g.send('click:400,300', 3)
        s = g.shot('menu'); pe = palette_exact(s); sizes = log_sizes(g)
        report('gdi', pe is not None and pe > 0.999 and '800x600' in sizes and brightness(s) > 10,
               'windowed renderer: %s, palette-exact %.4f' % (sorted(sizes), pe or 0))
        g.stop()
    # ---------------- the Direct3D renderer, in its isometric and perspective views ----------------
    if stage('d3d'):
        # one character in both views: made and saved in Direct3D, then loaded by a fresh game in each view (its town is
        # laid out from the seed in its save, so the two pictures differ only by the view)
        g = Game('d3d-make', {'D2_RENDERER': 'd3d'}); to_hero_select(g)
        g.send('click:400,300', 2); g.send('type:MacView', 2); g.send('click:690,555', 12)          # the Barbarian, a name, OK
        made = hud(g.shot('d3d-new'))[0]; save_and_exit(g); g.stop()
        found = glob.glob(os.path.join(g.dir, 'drive_c', '**', 'MacView.d2s'), recursive=True)
        if not found or os.path.getsize(found[0]) < 500: raise RuntimeError('Direct3D: the character was not saved (only the file made at its creation)')
        shots, notes, good = {}, ['new character: HUD %s' % ('drawn' if made else 'missing')], made
        for view in ('off', 'on'):
            g = Game('d3d-' + view, {'D2_RENDERER': 'd3d', 'D2_PERSPECTIVE': view}, saves=found[:1]); to_hero_select(g)
            g.send('click:690,555', 12); shots[view] = s = g.shot('d3d-' + view)                      # OK: into Act 1
            ok, life, mana = hud(s); reg = open(os.path.join(g.dir, 'registry.txt')).read()
            chose = ('Perspective\t4\t%s' % ('01000000' if view == 'on' else '00000000')) in reg
            good = good and ok and chose and g.alive() and lit_fraction(s) > 0.05
            notes.append('%s: HUD %s, view %s' % ('perspective' if view == 'on' else 'isometric', 'drawn' if ok else 'missing', 'kept' if chose else 'not kept'))
            g.stop()
        d = differs(shots['off'], shots['on'])
        report('d3d', good and d > 0.05, '%s; the same place differs in %.0f%% of samples' % ('; '.join(notes), 100 * d))
        g = Game('d3d-display', {'D2_RENDERER': 'd3d', 'D2_PERSPECTIVE': 'on', 'D2_RESOLUTION': 'display', 'D2_TEXTURES': 'smooth'}, saves=found[:1])
        to_hero_select(g); g.send('click:690,555', 12); s = g.shot('d3d-display')
        w, h, _ = png.load(s); ok = hud(s)[0]; b, b0 = brightness(s), brightness(shots['on'])
        report('d3d-hd', w > 800 and abs(w * 3 - h * 4) <= 4 and ok and abs(b - b0) < 0.15 * b0 and g.alive(),
               '%dx%d frame, HUD %s, brightness %.1f (800x600: %.1f)' % (w, h, 'drawn' if ok else 'missing', b, b0))
        g.stop()
        for fps in (60, 120):
            name = 'd3d-fps%d' % fps
            g = Game(name, {'D2_RENDERER': 'd3d', 'D2_FPS': str(fps), 'W32_BURST_DIR': os.path.join(WORK, name, 'burst')}, saves=found[:1])
            os.makedirs(os.path.join(g.dir, 'burst'), exist_ok=True)
            to_hero_select(g); g.send('click:690,555', 4); g.send('click:780,300', 0.8)
            # Measure soon after loading: a second auto limiter used to hold 120 at 60 for ten seconds.
            fr = burst(g); gaps = [b[0] - a[0] for a, b in zip(fr, fr[1:])]
            steps = [scroll(a[1], b[1]) for a, b in zip(fr, fr[1:])]; xs = [abs(dx) for dx, dy in steps]
            gaps_sorted = sorted(gaps); median = gaps_sorted[len(gaps) // 2] if gaps else 99
            report(name, len(fr) == 12 and median <= 1200 / fps and min(xs, default=0) >= 1 and max(xs, default=99) - min(xs, default=0) <= 2 and g.alive(),
                   '%d frames, %s ms apart; the picture moved %s pixels each frame' % (len(fr), gaps, xs))
            g.stop()
        W = 1400
        g = Game('d3d-wide', {'D2_RENDERER': 'd3d', 'D2_WIDTH': str(W)}, saves=found[:1]); to_hero_select(g)
        menu = g.shot('wide-menu'); mw, mh, _ = png.load(menu)
        bars = max(brightness(menu, (0, 0, 290, 600)), brightness(menu, (W - 290, 0, W, 600))); mid = brightness(menu, (310, 0, W - 310, 600))
        g.send('click:690,555', 12); s0 = g.shot('wide-game'); w, h, _ = png.load(s0)
        life, mana = png.mean(png.load(s0)[2], 40, 520, 110, 590), png.mean(png.load(s0)[2], W - 100, 520, W - 30, 590)
        orbs = life[0] > 2 * (life[1] + life[2]) / 2 + 30 and mana[2] > 2 * (mana[0] + mana[1]) / 2 + 30
        g.send('key:73', 1.5); s1 = g.shot('wide-inventory')
        panel = brightness(s1, (W - 380, 80, W - 100, 300)); where = brightness(s1, (420, 80, 700, 300))   # stone grey there, world where it would be at 800x600
        report('d3d-wide', mw == W and bars < 1 and mid > 5 and (w, h) == (W, 600) and orbs and panel > 1.5 * where and g.alive(),
               'menus %dx%d (sides %.1f, middle %.1f); game %dx%d, orbs %s; inventory at the right edge %.1f, at its 800x600 place %.1f' % (
                   mw, mh, bars, mid, w, h, 'at the edges' if orbs else 'missing', panel, where))
        g.stop()
    # ---------------- zoomed out (Direct3D, the mouse wheel) ----------------
    if stage('zoom'):
        g = Game('zoom', {'D2_RENDERER': 'd3d', 'D2_ZOOM_MAX': '2'}); to_hero_select(g)
        g.send('click:400,300', 2); g.send('type:MacZoom', 2); g.send('click:690,555', 12)
        g.send('move:400,560', 1); s1 = g.shot('zoom-1')                                     # the cursor on the bottom bar, out of the picture
        g.send('wheel:-10', 3); s2 = g.shot('zoom-out')
        corners = min(brightness(s2, (10, 10, 170, 110)), brightness(s2, (630, 400, 790, 520)), brightness(s2, (10, 400, 170, 520)))
        ok_hud = hud(s2)[0]; changed = differs(s1, s2)
        g.send('wheel:10', 3); s3 = g.shot('zoom-back'); back = differs(s1, s3); g.stop()
        report('zoom', corners > 4 and ok_hud and changed > 0.1 and back < 0.05,
               'zoomed out: corners %.1f, HUD %s, the picture changed in %.0f%% of samples; back in, %.0f%% differ from before' % (
                   corners, 'kept' if ok_hud else 'lost', 100 * changed, 100 * back))
    # ---------------- 3D sound and environmental effects (DirectSound 3D, EAX) ----------------
    if stage('sound3d'):
        d = os.path.join(WORK, 'sound3d'); os.makedirs(d, exist_ok=True)
        open(os.path.join(d, 'registry.txt'), 'w').write('hkcu\\software\\blizzard entertainment\\diablo ii\tSound Mixer\t4\t02000000\n')
        g = Game('sound3d', {'W32_DSLOG': '1', 'W32_DS3DLOG': '1'}, saves=found[:1] if 'found' in globals() and found else ())
        to_hero_select(g)
        if 'found' in globals() and found: g.send('click:690,555', 12)                       # the character made for d3d: OK
        else: g.send('click:400,300', 2); g.send('type:MacSound', 2); g.send('click:690,555', 12)
        t_in = time.time() - g.t0                                                            # in Act 1 (the menus' own sounds peak before)
        g.send('click:700,150', 4); s = g.shot('sound3d'); ok_hud = hud(s)[0]; g.stop()
        log = open(g.log, errors='replace').read()
        eax2 = 'KsPropertySet QuerySupport set 1 id 1' in log
        voices = log.count('QueryInterface 279afa86 -> ok'); placed = log.count('dsound: 3D buffer')
        per_sound = 'EAX set 2 ' in log; env = 'EAX set 1 id 11' in log
        peak = wav_peak(g.wav, t_in) if os.path.exists(g.wav) else 1
        report('sound3d', eax2 and voices > 3 and placed > 10 and per_sound and env and ok_hud and peak < 1.0,
               'EAX 2.0 found: %s; %d 3D voices, %d placements; per-sound EAX: %s; environment set: %s; peak %.3f' % (
                   eax2, voices, placed, per_sound, env, peak))
    # ---------------- TCP/IP: one instance hosts, another joins ----------------
    if stage('tcpip') or stage('tcpexit'):
        def to_tcpip(g):
            time.sleep(5)
            for _ in range(4): g.send('click:400,300', 3)                                     # videos, title -> main menu
            g.send('click:400,416', 3); g.send('click:400,330', 3)                           # Other Multiplayer, TCP/IP Game
        hostenv = {'W32_NETLOG': '1', 'D2_MULTIPLAYER': 'lan'}
        if stage('tcpexit'):                                                                  # the TCP/IP thread's life, from its imports
            hostenv.update(W32_TRACE=os.path.join(WORK, 'tcpip-host', 'trace.txt'), W32_TRACE_ONLY='kernel32!CreateThread,kernel32!WaitForSingleObject')
            os.makedirs(os.path.join(WORK, 'tcpip-host'), exist_ok=True)
        host = Game('tcpip-host', hostenv); to_tcpip(host)                                    # LAN (Classic): the address typed
        host.send('click:400,189', 3)                                                         # Host Game: the hero selection
        host.send('click:400,300', 2); host.send('type:HostMac', 2); host.send('click:690,555', 12)
        import socket                                                                         # the address another PC would use
        try: lan = [x for x in socket.gethostbyname_ex(socket.gethostname())[2] if not x.startswith('127.')]
        except OSError: lan = []
        join = Game('tcpip-join', {'W32_NETLOG': '1', 'D2_MULTIPLAYER': 'lan'}); to_tcpip(join)
        join.send('click:400,246', 3)                                                         # Join Game: the address box
        join.send('type:' + (lan[0] if lan else '127.0.0.1'), 2); join.send('click:469,322', 5)
        join.send('click:140,495', 3)                                                         # Create New Character
        join.send('click:400,300', 2); join.send('type:JoinMac', 2); join.send('click:690,555', 5)
        join.send('click:690,555', 14)                                                        # OK: into the host's game
        hs, js = host.shot('host'), join.shot('join')
        accepts = sum(1 for l in open(host.log) if 'net: accept' in l)
        received = sum(1 for l in open(join.log) if 'net: recv' in l)
        if stage('tcpip'):
            report('tcpip', hud(hs)[0] and hud(js)[0] and accepts >= 2 and received > 10,
                   'host and joiner in Act 1: %s/%s; connections the host accepted: %d; packets the joiner received: %d (joined by %s)' % (
                       hud(hs)[0], hud(js)[0], accepts, received, lan[0] if lan else '127.0.0.1'))
        if stage('tcpexit'):
            menu = save_and_exit(host)                                                        # the joiner is still in the game
            host.send('key:27', 3); host.send('click:400,551', 1)                             # the main menu, Exit Diablo II
            for _ in range(300):
                if not host.alive(): break
                time.sleep(0.1)
            log = open(host.log, errors='replace').read(); worker = None; waits = []
            for l in open(os.path.join(host.dir, 'trace.txt'), errors='replace'):
                if 'CreateThread(0, 0, 6c3470' in l: worker = l.split('-> ')[1].strip()   # the game's TCP/IP thread (1.14d)
                elif worker and 'WaitForSingleObject(%s, 7d0' % worker in l: waits.append(l.split('-> ')[1].strip())
            reports = glob.glob(os.path.join(host.dir, 'CrashReports', 'crash-*'))
            ended = 'game ended: exit 0' in log and host.p.poll() == 0
            report('tcpexit', menu > MENU_GOLD and ended and waits == ['0'] and not reports and 'runtime fault' not in log,
                   'menu text %.2f; the host quit through its menus: %s; its wait for the TCP/IP thread at exit: %s (0 = it had '
                   'ended, 102 = still running after 2 s); crash reports: %d' % (menu, ended, waits or 'none', len(reports)))
        host.stop(); join.stop()
    # ---------------- Online games: the Battle.net button, the game list (no DHT, no router: local discovery) ----------------
    if stage('gamelist'):
        def to_online(g, shot=None):
            time.sleep(5)
            for _ in range(4): g.send('click:400,300', 3)                                     # videos, title -> main menu
            if shot: g.shot(shot)                                                             # the button reads ONLINE GAMES
            g.send('click:400,349', 3)                                                        # Battle.net (ONLINE GAMES) -> the TCP/IP screen
        env = {'W32_NETLOG': '1', 'D2_MULTIPLAYER': 'internet', 'D2_GAMELIST_AUTO': '1'}
        host = Game('list-host', env); to_online(host, 'main-menu')
        host.send('click:400,189', 3)                                                         # Host Game: the hero selection
        host.send('click:400,300', 2); host.send('type:HostMac', 2); host.send('click:690,555', 12)
        join = Game('list-join', env); to_online(join)
        join.send('click:400,246', 8)                                                         # Join Game: the list comes up and joins the game
        join.send('click:140,495', 3)                                                         # Create New Character
        join.send('click:400,300', 2); join.send('type:JoinMac', 2); join.send('click:690,555', 5)
        join.send('click:690,555', 14)                                                        # OK: into the host's game
        hs, js = host.shot('host'), join.shot('join')
        jl = open(join.log).read(); hl = open(host.log).read()
        listed = 'd2net: joining' in jl and 'HostMac' in jl.split('d2net: joining', 1)[1][:80]
        report('gamelist', hud(hs)[0] and hud(js)[0] and listed and 'd2bnet: TCP/IP screen' in jl and 'd2net: hosting' in hl,
               'host and joiner in Act 1: %s/%s; the joiner picked %s from the list; both went through the Battle.net button' % (
                   hud(hs)[0], hud(js)[0], 'HostMac' if listed else 'nothing'))
        host.stop(); join.stop()
except Exception as e:
    report('error', False, repr(e))
finally:
    failed = [r for r in results if not r[1]]
    print('%d of %d stages passed%s' % (len(results) - len(failed), len(results), '' if not failed else ' (failed: %s)' % ', '.join(r[0] for r in failed)))
    print('screenshots, logs and the recorded sound: %s' % WORK)
    sys.exit(1 if failed else 0)
