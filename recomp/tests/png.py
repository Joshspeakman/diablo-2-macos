"""Just enough PNG for the tests: 8-bit RGB / RGBA (the game's screenshots), all five row filters.
  load(path) -> (width, height, rows) with rows[y][x] = (r, g, b)"""
import struct, zlib

def load(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n': raise ValueError('%s: not a PNG' % path)
    pos, idat, w = 8, b'', 0
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8]); body = d[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            if depth != 8 or ctype not in (2, 6): raise ValueError('%s: %d-bit type %d not supported' % (path, depth, ctype))
            bpp = 3 if ctype == 2 else 4
        elif t == b'IDAT': idat += body
        elif t == b'IEND': break
    raw = zlib.decompress(idat); stride = w * bpp; rows = []; prev = bytearray(stride); at = 0
    for y in range(h):
        f = raw[at]; line = bytearray(raw[at + 1:at + 1 + stride]); at += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 0xff
            elif f == 2: line[x] = (line[x] + b) & 0xff
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 0xff
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xff
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)]); prev = line
    return w, h, rows

def mean(rows, x0, y0, x1, y1):
    """average (r, g, b) of the rectangle [x0, x1) x [y0, y1)"""
    n = 0; s = [0, 0, 0]
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = rows[y][x]; s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; n += 1
    return tuple(v / max(n, 1) for v in s)
