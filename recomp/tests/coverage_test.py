"""Gameplay coverage on the player's own copy: every class's 30 skills cast outside town, and every act's town and the
area beyond it. Slower than game_test.py (about half an hour); for after changes to the runtime or the lifter.

  coverage_test.py --game <Diablo II folder> [--work <scratch folder>] [--classes Amazon,Sorceress,...] [--acts 1,2,3,4,5]

For each class: a new character is made through the menus, saved, and given every class skill at level 20 and plenty
of life and mana (tests/d2s.py; only these scratch characters are edited). Loaded again, it leaves the town by its
waypoint (or walks out: the town's and the next area's positions read from the game's level list), and each skill is made the right-hand
skill (the game's own skill list, through memory) and cast at the ground. After each: the game still runs, the right
skill is the one chosen. For each act: a character (all quests done, all waypoints) starts in that act's town (the
save's act), takes the town's waypoint to the act's first destination (found among the map's presets) and casts.
The game reaches each stage through the window's own input path, as in game_test.py."""
import argparse, glob, os, shutil, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import d2game, d2s
from d2game import *

ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True); ap.add_argument('--app', default=os.path.join(ROOT, 'work/full/app/Diablo2Native'))
ap.add_argument('--exe', default=os.path.join(ROOT, 'work/Game.exe')); ap.add_argument('--work', default=None)
ap.add_argument('--classes', default=','.join(d2s.CLASSES)); ap.add_argument('--acts', default='1,2,3,4,5')
a = ap.parse_args()
WORK = a.work or os.path.join('/tmp', 'd2-coverage-%d' % os.getpid()); os.makedirs(WORK, exist_ok=True)
d2game.CONFIG.update(app=a.app, exe=a.exe, game=a.game, work=WORK)
TOWNS = {1: 1, 2: 40, 3: 75, 4: 103, 5: 109}
HERO_AT = {'Amazon': (114, 309), 'Assassin': (203, 309), 'Necromancer': (295, 300), 'Barbarian': (400, 303),
           'Paladin': (526, 303), 'Sorceress': (615, 320), 'Druid': (718, 314)}             # on the hero selection
results = []
def report(name, ok, detail): results.append((name, ok, detail)); print('%-22s %s  %s' % (name, 'ok  ' if ok else 'FAIL', detail), flush=True)
def saves(g): return os.path.join(g.dir, 'drive_c', 'Users', 'player', 'Saved Games', 'Diablo II')

# ---- the player and the map, read from the game ----
def where(g):
    x, y = peek(g, PLAYER + ',2c,0'), peek(g, PLAYER + ',2c,4')
    return (x >> 16, y >> 16) if x is not None and y is not None else (0, 0)
def levels(g):
    """{level number: (x, y, w, h) in subtiles} for the levels the act has set up (its list, from the act's data)"""
    out = {}; lv = peek(g, PLAYER + ',2c,1c,10,58')
    misc = peek(g, '%x' % (lv + 0x1b4)) if lv else None
    lv = peek(g, '%x' % (misc + 0x47c)) if misc else lv
    while lv and len(out) < 40:
        x, y, w, h, no, nxt = [peek(g, '%x' % (lv + o)) for o in (0x1c, 0x20, 0x24, 0x28, 0x1d0, 0x1ac)]
        if no is None: break
        if w and h: out[no] = (5 * x, 5 * y, 5 * w, 5 * h)
        lv = nxt
    return out
def click_toward(g, tx, ty, wait=2.0):
    x, y = where(g); dx, dy = tx - x, ty - y
    sx, sy = (dx - dy) * 16, (dx + dy) * 8                   # subtiles to the screen (isometric, 32x16 per subtile)
    k = max(sx / 280 if sx > 0 else -sx / 330, sy / 200 if sy > 0 else -sy / 240, 1.0)   # stay clear of the Help button and the HUD
    g.send('click:%d,%d' % (400 + sx / k, 290 + sy / k), wait)
def leave_town(g):
    """walks from the town into a neighbouring area: towards their shared border (a little past both its ends, where
    a gate or bridge may be), at two depths beyond it, until the area changes"""
    s = g.shot('town')
    if gold_fraction(s, (60, 40, 740, 230)) > 0.02 and brightness(s, (60, 40, 740, 230)) < 40: g.send('key:72', 1.5)   # the Help screen: H closes it
    town = area(g); lv = levels(g); t = lv.get(town)
    if not t: return None
    tx, ty, tw, th = t; targets = []
    for no, (x, y, w, h) in lv.items():
        if no == town: continue
        for depth in (30, 70):
            lo, hi = max(ty, y), min(ty + th, y + h)
            if hi > lo and x == tx + tw: targets += [(tx + tw + depth, yy) for yy in range(lo - 30, hi + 31, 20)]      # east
            if hi > lo and x + w == tx: targets += [(tx - depth, yy) for yy in range(lo - 30, hi + 31, 20)]            # west
            lo, hi = max(tx, x), min(tx + tw, x + w)
            if hi > lo and y == ty + th: targets += [(xx, ty + th + depth) for xx in range(lo - 30, hi + 31, 20)]      # south
            if hi > lo and y + h == ty: targets += [(xx, ty - depth) for xx in range(lo - 30, hi + 31, 20)]            # north
    tried = 0
    while targets and tried < 60:
        px, py = where(g); targets.sort(key=lambda p: abs(p[0] - px) + abs(p[1] - py)); target = targets.pop(0)
        stuck = 0
        for _ in range(8):
            before = where(g); click_toward(g, *target); tried += 1
            now = area(g)
            if now is not None and now != town: return now
            after = where(g)
            if abs(after[0] - target[0]) + abs(after[1] - target[1]) < 6: break
            stuck = stuck + 1 if abs(after[0] - before[0]) + abs(after[1] - before[1]) < 2 else 0
            if stuck >= 2: break
    return None
WAYPOINTS = (119, 156, 237, 398, 429)                        # the towns' waypoint objects (acts 1-5)
def presets(g, kind=2):
    """[(class, x, y)] of the map's preset units (objects) in the rooms of the player's level that are set up"""
    lv = peek(g, PLAYER + ',2c,1c,10,58'); r2 = peek(g, '%x' % (lv + 0x10)) if lv else None; n = 0; out = []
    while r2 and n < 120:
        rx, ry, pu, m = peek(g, '%x' % (r2 + 0x34)), peek(g, '%x' % (r2 + 0x38)), peek(g, '%x' % (r2 + 0x5c)), 0
        while pu and m < 60:
            if peek(g, '%x' % (pu + 0x14)) == kind:
                out.append((peek(g, '%x' % (pu + 4)), rx * 5 + peek(g, '%x' % (pu + 8)), ry * 5 + peek(g, '%x' % (pu + 0x18))))
            pu = peek(g, '%x' % (pu + 0xc)); m += 1
        r2 = peek(g, '%x' % (r2 + 0x24)); n += 1
    return out
def walk_to(g, x, y, near=8, steps=12):
    for _ in range(steps):
        px, py = where(g)
        if abs(px - x) + abs(py - y) <= near: return True
        click_toward(g, x, y)
    px, py = where(g); return abs(px - x) + abs(py - y) <= near
def take_waypoint(g, act, entry=1):
    """walks to the town's waypoint (found among the map's presets, exploring the town until its room is set up),
    opens it and travels to the act's entry'th destination; returns the new area"""
    town = area(g); t = levels(g).get(town)
    wp = None
    for target in [None] + ([(t[0] + t[2] // 2, t[1] + t[3] // 2), (t[0] + t[2] // 4, t[1] + t[3] // 4), (t[0] + 3 * t[2] // 4, t[1] + t[3] // 4),
                             (t[0] + t[2] // 4, t[1] + 3 * t[3] // 4), (t[0] + 3 * t[2] // 4, t[1] + 3 * t[3] // 4)] if t else []):
        if target: walk_to(g, *target, near=20)
        wp = next(((x, y) for c, x, y in presets(g) if c in WAYPOINTS), None)
        if wp: break
    if not wp: return None
    walk_to(g, wp[0] + 3, wp[1] + 3, near=6)
    px, py = where(g); dx, dy = wp[0] - px, wp[1] - py
    g.send('click:%d,%d' % (400 + (dx - dy) * 16, 290 + (dx + dy) * 8), 2.5)   # the waypoint: its list opens
    g.shot('waypoints-act%d' % act)
    g.send('click:%d,76' % (127, 187, 240, 304, 363)[act - 1], 1.5)     # the act's tab
    g.send('click:227,%d' % (137 + 35 * entry), 6)                  # its destination
    now = area(g); return now if now != town else None
def skill_list(g):
    info = peek(g, PLAYER + ',a8'); sk = peek(g, '%x' % (info + 4)) if info else None; out = []
    while sk and len(out) < 80:
        out.append((peek(g, '%x,0' % sk) & 0xffff, sk)); sk = peek(g, '%x' % (sk + 4))
    return out
def cast(g, skill_ptr):
    info = peek(g, PLAYER + ',a8')
    with open(os.path.join(g.dir, 'peek.txt'), 'a') as f: f.write('%x=%x\n' % (info + 0xc, skill_ptr))   # the right-hand skill
    time.sleep(0.6)
    chosen = peek(g, PLAYER + ',a8,c,0,0')
    for spot in ('rclick:520,240', 'rclick:300,330'): g.send(spot, 1.2)
    return chosen & 0xffff if chosen is not None else None

def new_character(cls, name):
    g = Game('make-' + cls, {}); time.sleep(5)
    for _ in range(5): g.send('click:400,300', 3)                # videos, title, main menu -> Single Player
    g.send('click:%d,%d' % HERO_AT[cls], 2); g.send('type:' + name, 2); g.send('click:690,555', 12)
    ok = area(g) == 1
    g.send('key:27', 1.5)
    for _ in range(3):
        if gold_fraction(g.shot('menu'), (190, 245, 610, 275)) > 0.04: break
        g.send('key:27', 1.5)
    g.send('click:400,259', 6); g.stop()
    f = os.path.join(saves(g), name + '.d2s')
    return f if ok and os.path.exists(f) else None
def load(name, src, edit):
    c = d2s.D2S(open(src, 'rb').read()); edit(c)
    g = Game('play-' + name, {}); os.makedirs(saves(g), exist_ok=True)
    open(os.path.join(saves(g), os.path.basename(src)), 'wb').write(c.save())
    time.sleep(5)
    for _ in range(5): g.send('click:400,300', 3)                # videos, title, main menu -> Single Player: the list
    g.send('click:690,555', 12)
    return g
def strong(c, act=0):
    c.set_skills([20] * 30); c.set_act(act); c.all_waypoints(); c.all_quests()
    for s, v in (('mana', 20000), ('maxmana', 20000), ('hitpoints', 20000), ('maxhp', 20000)): c.set_stat(s, v)

try:
    for cls in [x for x in a.classes.split(',') if x]:
        name = 'Cov' + cls[:9]
        src = new_character(cls, name)
        if not src: report('%s create' % cls, False, 'the new character did not reach Act 1 or was not saved'); continue
        made = d2s.D2S(open(src, 'rb').read()); report('%s create' % cls, d2s.CLASSES[made.cls] == cls, 'saved as %s (%s)' % (made.name, d2s.CLASSES[made.cls]))
        g = load(name, src, strong)
        out = (take_waypoint(g, 1) or leave_town(g)) if area(g) == 1 else None
        if not out: report('%s skills' % cls, False, 'could not leave town (area %s)' % area(g)); g.stop(); continue
        sk = [(sid, p) for sid, p in skill_list(g) if d2s.FIRST_SKILL[made.cls] <= sid < d2s.FIRST_SKILL[made.cls] + 30]
        cast_ok, wrong = 0, []
        for sid, p in sk:
            got = cast(g, p)
            if fault(g) or not g.alive(): break
            if got == sid: cast_ok += 1
            else: wrong.append(sid)
        g.shot('casting'); f = fault(g)
        report('%s skills' % cls, not f and cast_ok == len(sk) == 30 and alive_in_game(g),
               '%d of %d class skills chosen and cast in area %d%s%s' % (cast_ok, len(sk), out, '; not chosen: %s' % wrong if wrong else '', '; ' + f if f else ''))
        g.stop()
        if cls == a.classes.split(',')[0]: first = src
    for act in [int(x) for x in a.acts.split(',') if x]:
        if 'first' not in dir(): break
        g = load('act%d' % act, first, lambda c: strong(c, act - 1))
        town = area(g); out = take_waypoint(g, act) if town == TOWNS[act] else None
        sk = skill_list(g)[-6:] if out else []
        for sid, p in sk: cast(g, p)
        s = g.shot('act%d' % act); f = fault(g)
        report('act %d' % act, town == TOWNS[act] and out is not None and not f and hud(s)[0],
               'town %s (expected %d), left into area %s, %d skills cast%s' % (town, TOWNS[act], out, len(sk), '; ' + f if f else ''))
        g.stop()
except Exception as e:
    report('error', False, repr(e))
finally:
    failed = [r for r in results if not r[1]]
    print('%d of %d checks passed%s' % (len(results) - len(failed), len(results), '' if not failed else ' (failed: %s)' % ', '.join(r[0] for r in failed)))
    print('screenshots and logs: %s' % WORK)
    sys.exit(1 if failed else 0)
