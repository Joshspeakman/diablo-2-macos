"""Driving the native Diablo II from a test: a game in test mode from a fresh data folder, played through the window's
own input path (DS_CMDFILE clicks, keys, typing), screenshots, memory peeks, and checks on what it shows."""
import os, struct, subprocess, time, wave, math
import png

CONFIG = {'app': None, 'exe': None, 'game': None, 'work': None}     # set by the test
PLAYER = '7a6a70'                                                     # 1.14d: the player's unit (a pointer)

class Game:
    def __init__(self, name, env=None, saves=()):
        self.dir = os.path.join(CONFIG['work'], name); os.makedirs(self.dir, exist_ok=True)
        if saves:                                                     # characters to start with (copied into Saved Games)
            sg = os.path.join(self.dir, 'drive_c', 'Users', 'player', 'Saved Games', 'Diablo II'); os.makedirs(sg, exist_ok=True)
            for f in saves: open(os.path.join(sg, os.path.basename(f)), 'wb').write(open(f, 'rb').read())
        self.cmd = os.path.join(self.dir, 'cmd.txt'); open(self.cmd, 'w').close(); open(os.path.join(self.dir, 'peek.txt'), 'w').close()
        self.log = os.path.join(self.dir, 'run.log'); self.wav = os.path.join(self.dir, 'mix.wav'); self.shots = 0
        e = dict(os.environ, DS_TEST='1', W32_AUDIO_MUTE='1', DS_NO_PORTMAP='1', D2_NO_P2P='1', DS_CMDFILE=self.cmd, DS_PEEKFILE=os.path.join(self.dir, 'peek.txt'),
                 W32_AUDIO_DUMP=self.wav, DS_ARGS='')
        e.update(env or {})
        self.p = subprocess.Popen([CONFIG['app'], '--exe', CONFIG['exe'], '--game', CONFIG['game'], '--data', self.dir], stdout=open(self.log, 'w'),
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
    w, h, rows = png.load(path); f = w / 800                    # (a frame drawn larger: the same places, scaled)
    life, mana = png.mean(rows, *(int(v * f) for v in (40, 520, 110, 590))), png.mean(rows, *(int(v * f) for v in (700, 520, 770, 590)))
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
MENU_GOLD = 0.012     # the game menu's gold text in its box: DirectDraw about 0.065, Direct3D 0.028; without the menu under 0.004
def gold_fraction(path, box):
    w, h, rows = png.load(path); n = g = 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            p = rows[y][x]; n += 1
            if p[0] > 140 and p[1] > 110 and p[2] < 120 and p[0] > p[2] + 50: g += 1
    return g / max(n, 1)
def differs(p1, p2, box=None):
    """the share of samples (every 4th pixel) that changed between two screenshots, in a box (else above the bottom bar)"""
    w, h, r1 = png.load(p1); _, _, r2 = png.load(p2); d = n = 0
    x0, y0, x1, y1 = box or (0, 0, w, h - 100)
    for y in range(y0, y1, 4):
        for x in range(x0, x1, 4):
            n += 1
            if sum(abs(r1[y][x][k] - r2[y][x][k]) for k in range(3)) > 60: d += 1
    return d / max(n, 1)
def log_sizes(game):
    return {l.split('(')[1].split(')')[0] for l in open(game.log) if ': frame ' in l and ' fps' in l}
def wav_rms(path, t0, t1):
    w = wave.open(path); r = w.getframerate(); w.setpos(int(t0 * r)); n = int((t1 - t0) * r)
    d = w.readframes(n); x = struct.unpack('<%dh' % (len(d) // 2), d)
    return math.sqrt(sum(v * v for v in x) / max(len(x), 1)) if x else 0

def wav_peak(path, t0=0):
    """the loudest sample of a 16-bit recording from t0 seconds on, 1.0 = full scale"""
    w = wave.open(path); w.setpos(min(int(t0 * w.getframerate()), w.getnframes())); d = w.readframes(w.getnframes())
    x = struct.unpack('<%dh' % (len(d) // 2), d)
    return max((abs(v) for v in x), default=0) / 32768.0

def to_hero_select(g):
    """clicks past the intro videos, the title and the main menu (Single Player) to the hero selection"""
    time.sleep(max(0.5, 5 - (time.time() - g.t0)))
    for _ in range(5): g.send('click:400,300', 3)

def peek_many(g, chains):
    """32-bit values through chains of pointers ("7a6a70,2c,1c"), read by the running game's peek thread (it reads
    <data>/peek.txt every quarter second, all new lines at once); None for any not answered"""
    if not chains: return []
    log = g.log; start = os.path.getsize(log)
    with open(os.path.join(g.dir, 'peek.txt'), 'a') as f: f.write(''.join(c + '\n' for c in chains))
    want = {c: None for c in chains}; left = set(chains)
    for _ in range(60):
        time.sleep(0.1)
        with open(log, 'rb') as f: f.seek(start); text = f.read().decode('latin-1')
        for l in text.split('\n'):
            if 'peek ' in l and ' = 0x' in l:
                c = l.split('peek ', 1)[1].split(' = ')[0]
                if c in left: want[c] = int(l.split('= 0x')[1][:8], 16); left.discard(c)
        if not left: break
    return [want[c] for c in chains]
def peek(g, chain): return peek_many(g, [chain])[0]
def area(g):
    """the area the player is in (the level number: 1 Rogue Encampment, 40 Lut Gholein, 75 Kurast Docks, 103 Pandemonium
    Fortress, 109 Harrogath), or None outside a game"""
    return peek(g, PLAYER + ',2c,1c,10,58,1d0')
def alive_in_game(g): return g.alive() and area(g) not in (None, 0)
def fault(g):
    for l in open(g.log):
        if 'game ended' in l: return l.strip()
    return None

def burst(g, wait=6.0):
    """the next 12 frames the game presents, as (milliseconds, 800x600 BGRA bytes) (DS_TEST with W32_BURST_DIR = <dir>/burst)"""
    import glob, re
    d = os.path.join(g.dir, 'burst'); open(os.path.join(d, 'go'), 'w').close()
    for _ in range(int(wait * 10)):
        time.sleep(0.1)
        fs = sorted(glob.glob(os.path.join(d, 'f*.bgra')))
        if len(fs) >= 12: break
    time.sleep(0.3); fs = sorted(glob.glob(os.path.join(d, 'f*.bgra')))[:12]
    return [(int(re.search(r'-(\d+)\.bgra$', f).group(1)), open(f, 'rb').read()) for f in fs]
def scroll(a, b, w=800, h=600):
    """how far the picture moved from frame a to frame b (dx, dy within +-14, +-10 pixels): the best match of the
    middle of the screen, compared as brightness on a 7-pixel grid"""
    lum = lambda d: [d[i] + d[i + 1] + d[i + 2] for i in range(0, len(d), 4)]
    A, B = lum(a), lum(b); pts = [(x, y) for y in range(120, 400, 7) for x in range(170, 630, 7)]; best = None
    for dy in range(-10, 11):
        for dx in range(-14, 15):
            e = sum(abs(A[y * w + x] - B[(y + dy) * w + x + dx]) for x, y in pts)
            if best is None or e < best[0]: best = (e, dx, dy)
    return best[1], best[2]
