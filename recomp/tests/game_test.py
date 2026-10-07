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

The game folder is only read. Nothing is written outside the scratch folder."""
import argparse, glob, os, struct, subprocess, sys, time, wave, math
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import png, mpq

ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True); ap.add_argument('--app', default=os.path.join(ROOT, 'work/full/app/Diablo2Native'))
ap.add_argument('--exe', default=os.path.join(ROOT, 'work/Game.exe')); ap.add_argument('--work', default=None)
ap.add_argument('--only', default=None); ap.add_argument('--keep', action='store_true')
a = ap.parse_args()
WORK = a.work or os.path.join('/tmp', 'd2-game-test-%d' % os.getpid())
os.makedirs(WORK, exist_ok=True)
ONLY = set(a.only.split(',')) if a.only else None
results = []

def stage(name):
    return ONLY is None or name in ONLY
def report(name, ok, detail):
    results.append((name, ok, detail)); print('%-7s %s  %s' % (name, 'ok  ' if ok else 'FAIL', detail), flush=True)

class Game:
    def __init__(self, name, env=None):
        self.dir = os.path.join(WORK, name); os.makedirs(self.dir, exist_ok=True)
        self.cmd = os.path.join(self.dir, 'cmd.txt'); open(self.cmd, 'w').close(); open(os.path.join(self.dir, 'peek.txt'), 'w').close()
        self.log = os.path.join(self.dir, 'run.log'); self.wav = os.path.join(self.dir, 'mix.wav'); self.shots = 0
        e = dict(os.environ, DS_TEST='1', W32_AUDIO_MUTE='1', DS_CMDFILE=self.cmd, DS_PEEKFILE=os.path.join(self.dir, 'peek.txt'),
                 W32_AUDIO_DUMP=self.wav, DS_ARGS='')
        e.update(env or {})
        self.p = subprocess.Popen([a.app, '--exe', a.exe, '--game', a.game, '--data', self.dir], stdout=open(self.log, 'w'),
                                  stderr=subprocess.STDOUT, env=e)
        self.t0 = time.time()
    def send(self, line, wait=1.0):
        with open(self.cmd, 'a') as f: f.write(line + '\n')
        time.sleep(wait)
    def shot(self, name):
        path = os.path.join(self.dir, name + '.png')
        if os.path.exists(path): os.remove(path)
        self.send('shot:' + path, 0)
        for _ in range(80):
            if os.path.exists(path) and os.path.getsize(path) > 0: time.sleep(0.2); return path
            if self.p.poll() is not None: raise RuntimeError('the game ended: ' + open(self.log).read()[-400:])
            time.sleep(0.1)
        raise RuntimeError('no screenshot ' + name)
    def stop(self):
        if self.p.poll() is None: self.p.terminate()
        try: self.p.wait(10)
        except subprocess.TimeoutExpired: self.p.kill()
    def alive(self): return self.p.poll() is None

def palette_exact(path):
    """the fraction of the screenshot's pixels that are one of the 256 colours of the frame's palette (<shot>.pal)"""
    pal = os.path.splitext(path)[0] + '.png.pal'
    if not os.path.exists(pal): pal = path + '.pal'
    if not os.path.exists(pal): return None
    colours = {int(l, 16) for l in open(pal).read().split()}
    w, h, rows = png.load(path); good = 0
    for r in rows:
        for p in r:
            if (p[0] << 16 | p[1] << 8 | p[2]) in colours: good += 1
    return good / (w * h)
def hud(path):
    w, h, rows = png.load(path)
    life, mana = png.mean(rows, 40, 520, 110, 590), png.mean(rows, 700, 520, 770, 590)
    return life[0] > 2 * (life[1] + life[2]) / 2 + 30 and mana[2] > 2 * (mana[0] + mana[1]) / 2 + 30, life, mana
def brightness(path, box=None):
    w, h, rows = png.load(path); m = png.mean(rows, *(box or (0, 0, w, h))); return sum(m) / 3
def lit_fraction(path):
    """the share of pixels that are not near black"""
    w, h, rows = png.load(path); n = 0
    for r in rows[::2]:
        for p in r[::2]:
            if max(p) > 40: n += 1
    return n / ((h + 1) // 2 * ((w + 1) // 2))
def gold_fraction(path, box):
    w, h, rows = png.load(path); n = g = 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            p = rows[y][x]; n += 1
            if p[0] > 140 and p[1] > 110 and p[2] < 120 and p[0] > p[2] + 50: g += 1
    return g / max(n, 1)
def differs(p1, p2):
    w, h, r1 = png.load(p1); _, _, r2 = png.load(p2); d = 0
    for y in range(0, h - 100, 4):
        for x in range(0, w, 4):
            if sum(abs(r1[y][x][k] - r2[y][x][k]) for k in range(3)) > 60: d += 1
    return d / ((h - 100) // 4 * w // 4)
def log_sizes(game):
    return {l.split('(')[1].split(')')[0] for l in open(game.log) if ': frame ' in l and ' fps' in l}
def wav_rms(path, t0, t1):
    w = wave.open(path); r = w.getframerate(); w.setpos(int(t0 * r)); n = int((t1 - t0) * r)
    d = w.readframes(n); x = struct.unpack('<%dh' % (len(d) // 2), d)
    return math.sqrt(sum(v * v for v in x) / max(len(x), 1)) if x else 0

def to_hero_select(g):
    """clicks past the intro videos, the title and the main menu (Single Player) to the hero selection"""
    time.sleep(max(0.5, 5 - (time.time() - g.t0)))
    for _ in range(5): g.send('click:400,300', 3)

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
        menu = 0
        for k in range(4):
            g.send('key:27', 1.5); s = g.shot('menu%d' % k); menu = gold_fraction(s, (190, 245, 610, 275))
            if menu > 0.04: break
        g.send('click:400,259', 6)                                                           # Save and Exit Game
        saves = glob.glob(os.path.join(g.dir, 'drive_c', '**', 'MacTest.d2s'), recursive=True)
        hdr = open(saves[0], 'rb').read() if saves else b''
        good = len(hdr) > 300 and struct.unpack_from('<I', hdr, 0)[0] == 0xaa55aa55 and struct.unpack_from('<I', hdr, 8)[0] == len(hdr) and b'MacTest' in hdr[:40]
        report('save', menu > 0.04 and good, 'menu text %.2f; %s (%d bytes, %s)' % (
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
except Exception as e:
    report('error', False, repr(e))
finally:
    failed = [r for r in results if not r[1]]
    print('%d of %d stages passed%s' % (len(results) - len(failed), len(results), '' if not failed else ' (failed: %s)' % ', '.join(r[0] for r in failed)))
    print('screenshots, logs and the recorded sound: %s' % WORK)
    sys.exit(1 if failed else 0)
