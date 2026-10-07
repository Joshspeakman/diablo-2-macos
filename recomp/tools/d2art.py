#!/usr/bin/env python3
"""d2art.py <Diablo II folder> <out folder>: the launch window's art, made at install time from the player's own game
archives (nothing from the game is kept in this project): the main menu's backdrop (backdrop.png, 800x600) and the
Diablo II logo (logo.png), decoded from the front end's DC6 sprites with the Act 1 palette."""
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mpq

def palette(archives):
    d = read(archives, 'data\\global\\palette\\ACT1\\pal.dat')
    return [(d[3 * k + 2], d[3 * k + 1], d[3 * k]) for k in range(256)]          # stored B, G, R
def read(archives, name):
    for a in archives:
        try: return a.read(name)
        except KeyError: pass
    raise KeyError(name)
def dc6_frames(data):
    """[(width, height, offset x, offset y, rows of palette indices or None for transparent)] for direction 0"""
    ver, flags, enc, term, dirs, fpd = struct.unpack_from('<6I', data, 0)
    if ver != 6: raise ValueError('not a DC6')
    ptrs = struct.unpack_from('<%dI' % (dirs * fpd), data, 24); out = []
    for p in ptrs[:fpd]:
        flip, w, h, ox, oy, _, _, length = struct.unpack_from('<8I', data, p)
        rows = [[None] * w for _ in range(h)]; body = data[p + 32:p + 32 + length]
        x, y, i = 0, (0 if flip else h - 1), 0
        while i < len(body) and 0 <= y < h:
            b = body[i]; i += 1
            if b == 0x80: x = 0; y += 1 if flip else -1
            elif b & 0x80: x += b & 0x7f
            else:
                for k in range(b):
                    if x < w: rows[y][x] = body[i + k]
                    x += 1
                i += b
        out.append((w, h, ox, oy, rows))
    return out
def canvas(w, h): return [[(0, 0, 0, 0)] * w for _ in range(h)]
def blit(dst, frame, x0, y0, pal, mode='over'):
    w, h, _, _, rows = frame
    for y in range(h):
        if not 0 <= y0 + y < len(dst): continue
        r = dst[y0 + y]
        for x in range(w):
            v = rows[y][x]
            if v is None or not 0 <= x0 + x < len(r): continue
            c = pal[v]
            if mode == 'add':                                  # the logo's flames: light added over the letters
                o = r[x0 + x]; r[x0 + x] = (min(255, o[0] + c[0]), min(255, o[1] + c[1]), min(255, o[2] + c[2]), max(o[3], max(c)))
            else: r[x0 + x] = c + (255,)
def write_png(path, rows):
    h, w = len(rows), len(rows[0]); raw = b''.join(b'\0' + bytes(v for p in r for v in p) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def main(game, out):
    os.makedirs(out, exist_ok=True)
    archives = [mpq.MPQ(os.path.join(game, n)) for n in ('d2exp.mpq', 'd2data.mpq') if os.path.exists(os.path.join(game, n))]
    pal = palette(archives)
    # the main menu's backdrop: 800x600 in 256x256 tiles, four to a row (the expansion's, else the original's)
    for name in ('data\\global\\ui\\FrontEnd\\gameselectscreenEXP.dc6', 'data\\global\\ui\\FrontEnd\\gameselectscreen.dc6'):
        try: frames = dc6_frames(read(archives, name)); break
        except KeyError: frames = None
    if frames:
        c = canvas(800, 600)
        for k, f in enumerate(frames): blit(c, f, 256 * (k % 4), 256 * (k // 4), pal)
        write_png(os.path.join(out, 'backdrop.png'), c)
    # the logo: the dark letters (left and right halves) with the first frame of their flames added over them
    halves = []
    for side in ('Left', 'Right'):
        black = dc6_frames(read(archives, 'data\\global\\ui\\FrontEnd\\D2logoBlack%s.DC6' % side))[0]
        fire = dc6_frames(read(archives, 'data\\global\\ui\\FrontEnd\\D2logoFire%s.DC6' % side))[0]
        halves.append((black, fire))
    w = sum(max(b[0], f[0]) for b, f in halves); h = max(max(b[1], f[1]) for b, f in halves)
    c = canvas(w, h); x = 0
    for black, fire in halves:
        blit(c, black, x, h - black[1], pal); blit(c, fire, x, h - fire[1], pal, 'add'); x += max(black[0], fire[0])
    write_png(os.path.join(out, 'logo.png'), c)

if __name__ == '__main__':
    exe = os.path.join(sys.argv[1], 'Game.exe')
    if os.path.exists(exe): mpq.huffman_tables_from_exe(exe)
    main(sys.argv[1], sys.argv[2]); print('launcher art in', sys.argv[2])
