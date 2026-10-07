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
import argparse, glob, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import d2game, d2s
from d2game import *

ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True); ap.add_argument('--app', default=os.path.join(ROOT, 'work/full/app/Diablo2Native'))
ap.add_argument('--exe', default=os.path.join(ROOT, 'work/Game.exe')); ap.add_argument('--work', default=None)
ap.add_argument('--classes', default=','.join(d2s.CLASSES)); ap.add_argument('--acts', default='1,2,3,4,5')
ap.add_argument('--character', default=None, help='a .d2s for the acts (else the first class made)')
ap.add_argument('--panels', default='1', help='0: skip the panels stage')
ap.add_argument('--env', action='append', default=[], help='NAME=value for the game (e.g. D2_RENDERER=d3d D2_FPS=120); repeatable')
a = ap.parse_args()
GAME_ENV = dict(e.split("=", 1) for e in a.env)                     # settings for every game the test starts
if a.character: first = a.character
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
    x, y = peek_many(g, [PLAYER + ',2c,0', PLAYER + ',2c,4'])
    return (x >> 16, y >> 16) if x is not None and y is not None else (0, 0)
def levels(g):
    """{level number: (x, y, w, h) in subtiles} for the levels the act has set up (its list, from the act's data)"""
    out = {}; lv = peek(g, PLAYER + ',2c,1c,10,58')
    misc = peek(g, '%x' % (lv + 0x1b4)) if lv else None
    lv = peek(g, '%x' % (misc + 0x47c)) if misc else lv
    while lv and len(out) < 40:
        x, y, w, h, no, nxt = peek_many(g, ['%x' % (lv + o) for o in (0x1c, 0x20, 0x24, 0x28, 0x1d0, 0x1ac)])
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
def waypoint_list_open(path):
    """the waypoint list's flat dark-grey panel fills the left of the screen (0.94 of these pixels; the world <= 0.1)"""
    w, h, rows = png.load(path); n = k = 0
    for y in range(130, 420, 3):
        for x in range(150, 380, 3):
            p = rows[y][x]; n += 1
            if max(p) < 70 and max(p) - min(p) < 14: k += 1
    return k / n > 0.6
WAYPOINTS = (119, 156, 237, 398, 429)                        # the towns' waypoint objects (acts 1-5)
def presets(g, kind=2):
    """[(class, x, y)] of the map's preset units (objects) in the rooms of the player's level that are set up"""
    lv = peek(g, PLAYER + ',2c,1c,10,58'); r2 = peek(g, '%x' % (lv + 0x10)) if lv else None; n = 0; out = []
    while r2 and n < 120:
        rx, ry, pu, nxt = peek_many(g, ['%x' % (r2 + o) for o in (0x34, 0x38, 0x5c, 0x24)]); m = 0
        while pu and m < 60:
            k, cid, x, y, pnext = peek_many(g, ['%x' % (pu + o) for o in (0x14, 4, 8, 0x18, 0xc)])
            if k == kind and None not in (cid, x, y, rx, ry): out.append((cid, rx * 5 + x, ry * 5 + y))
            pu = pnext; m += 1
        r2 = nxt; n += 1
    return out
def walk_to(g, x, y, near=8, steps=16):
    """clicks towards (x, y); when a step gets no closer, a detour: steps turned 45, 90 and 135 degrees either way"""
    import math
    turns = [0, 45, -45, 90, -90, 135, -135]; k = 0
    for _ in range(steps):
        px, py = where(g); d = abs(px - x) + abs(py - y)
        if d <= near: return True
        a = math.atan2(y - py, x - px) + math.radians(turns[k % len(turns)]); r = min(40, d)
        click_toward(g, px + r * math.cos(a), py + r * math.sin(a))
        nx, ny = where(g)
        k = 0 if abs(nx - x) + abs(ny - y) < d - 3 else k + 1        # closer: straight on again; else the next turn
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
    for attempt in range(5):
        px, py = where(g); dx, dy = wp[0] - px, wp[1] - py; sx, sy = 400 + (dx - dy) * 16, 290 + (dx + dy) * 8
        if not (40 <= sx <= 690 and 40 <= sy <= 500):               # off screen: closer first
            walk_to(g, wp[0] + 3, wp[1] + 3, near=6)
            for _ in range(10):                                     # until the character stands still
                a1 = where(g); time.sleep(0.5); a2 = where(g)
                if a1 == a2: break
            px, py = a2; dx, dy = wp[0] - px, wp[1] - py; sx, sy = 400 + (dx - dy) * 16, 290 + (dx + dy) * 8
        g.send('click:%d,%d' % (sx, sy), 6)                         # the waypoint: the game walks there (its own path) and opens the list
        if attempt and not waypoint_list_open(g.shot('waypoints-act%d' % act)): walk_to(g, wp[0] + 3, wp[1] + 3, near=6, steps=24)
        s = g.shot('waypoints-act%d' % act)
        if waypoint_list_open(s): break
    else:
        return None
    g.send('click:%d,76' % (127, 187, 240, 304, 363)[act - 1], 1.5)     # the act's tab
    g.send('click:227,%d' % (137 + 35 * entry), 6)                  # its destination
    now = area(g); return now if now != town else None
def skill_list(g):
    info = peek(g, PLAYER + ',a8'); sk = peek(g, '%x' % (info + 4)) if info else None; out = []
    while sk and len(out) < 80:
        sid, nxt = peek_many(g, ['%x,0' % sk, '%x' % (sk + 4)]); out.append(((sid or 0) & 0xffff, sk)); sk = nxt
    return out
def cast(g, skill_ptr):
    info = peek(g, PLAYER + ',a8')
    with open(os.path.join(g.dir, 'peek.txt'), 'a') as f: f.write('%x=%x\n' % (info + 0xc, skill_ptr))   # the right-hand skill
    time.sleep(0.6)
    chosen = peek(g, PLAYER + ',a8,c,0,0')
    for spot in ('rclick:520,240', 'rclick:300,330'): g.send(spot, 1.2)
    return chosen & 0xffff if chosen is not None else None

def new_character(cls, name):
    g = Game('make-' + cls, dict(GAME_ENV)); time.sleep(5)
    for _ in range(5): g.send('click:400,300', 3)                # videos, title, main menu -> Single Player
    g.send('click:%d,%d' % HERO_AT[cls], 2); g.send('type:' + name, 2); g.send('click:690,555', 12)
    ok = area(g) == 1
    g.send('key:27', 1.5)
    for _ in range(3):
        if gold_fraction(g.shot('menu'), (190, 245, 610, 275)) > MENU_GOLD: break
        g.send('key:27', 1.5)
    g.send('click:400,259', 6); g.stop()
    f = os.path.join(saves(g), name + '.d2s')
    return f if ok and os.path.exists(f) else None
def load(name, src, edit):
    c = d2s.D2S(open(src, 'rb').read()); edit(c)
    g = Game('play-' + name, dict(GAME_ENV)); os.makedirs(saves(g), exist_ok=True)
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
        try:
            name = 'Cov' + cls[:9]
            src = new_character(cls, name)
            if not src: report('%s create' % cls, False, 'the new character did not reach Act 1 or was not saved'); continue
            if 'first' not in globals(): first = src                # the acts' character
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
        except Exception as e:
            report('%s error' % cls, False, repr(e))
            subprocess.run(['pkill', '-f', WORK], capture_output=True)   # its game, if still running
    for act in [int(x) for x in a.acts.split(',') if x]:
        try:
            if 'first' not in globals(): break
            g = load('act%d' % act, first, lambda c: strong(c, act - 1))
            town = area(g); out = (take_waypoint(g, act) or leave_town(g)) if town == TOWNS[act] else None
            sk = skill_list(g)[-6:] if out else []
            for sid, p in sk: cast(g, p)
            s = g.shot('act%d' % act); f = fault(g)
            report('act %d' % act, town == TOWNS[act] and out is not None and not f and hud(s)[0],
                   'town %s (expected %d), left into area %s, %d skills cast%s' % (town, TOWNS[act], out, len(sk), '; ' + f if f else ''))
            g.stop()
        except Exception as e:
            report('act %d error' % act, False, repr(e))
            subprocess.run(['pkill', '-f', WORK], capture_output=True)   # its game, if still running
    # ---- the game's panels, opened by their keys in the field and closed with Escape ----
    if a.panels != '0' and 'first' in globals():
        try:
            g = load('panels', first, strong)
            out = (take_waypoint(g, 1) or leave_town(g)) if area(g) == 1 else None
            opened = []; base = g.shot('panels-none')
            for key, what in ((ord('I'), 'inventory'), (ord('C'), 'character'), (ord('T'), 'skill tree'), (ord('Q'), 'quests'),
                              (9, 'automap'), (ord('H'), 'help')):                # (single player has no party screen)
                g.send('key:%d' % key, 1.2); s1 = g.shot('panel-%s' % what.replace(' ', '-'))
                shown = gold_fraction(s1, (560, 5, 800, 60)) > gold_fraction(base, (560, 5, 800, 60)) + 0.004 if key == 9 else differs(base, s1) > 0.05
                if shown and not fault(g): opened.append(what)   # the automap: its area name at the top right
                g.send('key:%d' % (9 if key == 9 else 27), 1.2)
                if key not in (9,):                                       # Escape closes a panel; one more would open the menu
                    if gold_fraction(g.shot('closed'), (190, 245, 610, 275)) > MENU_GOLD: g.send('key:27', 1)
            f = fault(g)
            report('panels', out is not None and len(opened) == 6 and not f and alive_in_game(g),
                   'opened %s in area %s%s' % (', '.join(opened), out, '; ' + f if f else ''))
            g.stop()
        except Exception as e:
            report('panels error', False, repr(e)); subprocess.run(['pkill', '-f', WORK], capture_output=True)
except Exception as e:
    report('error', False, repr(e))
finally:
    failed = [r for r in results if not r[1]]
    print('%d of %d checks passed%s' % (len(results) - len(failed), len(results), '' if not failed else ' (failed: %s)' % ', '.join(r[0] for r in failed)))
    print('screenshots and logs: %s' % WORK)
    sys.exit(1 if failed else 0)
