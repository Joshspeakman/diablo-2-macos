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
        e = dict(os.environ, DS_TEST='1', W32_AUDIO_MUTE='1', DS_CMDFILE=self.cmd, DS_PEEKFILE=os.path.join(self.dir, 'peek.txt'),
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
