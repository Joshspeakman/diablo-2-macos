"""A small MPQ (Mo'PaQ, format 0/1) reader for the tests: an independent way to read the game's archives, to check what
the recompiled game reads and plays against. Hash and block tables, file-key encryption, single-unit and sectored
files, sector CRC flags, and these compressions: zlib (0x02), bzip2 (0x10), PKWARE DCL implode (0x08 flag or 0x08
compression), Huffman (0x01) and IMA ADPCM mono/stereo (0x40/0x80), which Blizzard used for WAV sound.

  mpq.py <archive.mpq> list                      the names in "(listfile)" that are present
  mpq.py <archive.mpq> get <name> <out file>     extract one file
"""
import struct, sys, zlib, bz2

# ---- crypto ----
_T = [0] * 0x500
def _init():
    seed = 0x00100001
    for i in range(0x100):
        j = i
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB; a = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB; _T[j] = a | (seed & 0xFFFF); j += 0x100
_init()
def hash_str(s, kind):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in s.upper().encode('latin-1'):
        s1 = _T[(kind << 8) + ch] ^ ((s1 + s2) & 0xFFFFFFFF)
        s2 = (ch + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1
def decrypt(data, key):
    out = bytearray(len(data) // 4 * 4); seed = 0xEEEEEEEE
    for i in range(len(data) // 4):
        seed = (seed + _T[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = struct.unpack_from('<I', data, 4 * i)[0] ^ ((key + seed) & 0xFFFFFFFF)
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (v + seed + (seed << 5) + 3) & 0xFFFFFFFF
        struct.pack_into('<I', out, 4 * i, v)
    return bytes(out) + data[len(data) // 4 * 4:]

# ---- PKWARE DCL "explode" ----
_LEN_BITS = [3, 2, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7]
_LEN_CODE = [5, 3, 1, 6, 10, 2, 12, 20, 4, 24, 8, 48, 16, 32, 64, 0]
_EX_LEN_BITS = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]
_LEN_BASE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 14, 22, 38, 70, 134, 262]
_DIST_BITS = [2, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
              7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8]
_DIST_CODE = [0x03, 0x0D, 0x05, 0x19, 0x09, 0x11, 0x01, 0x3E, 0x1E, 0x2E, 0x0E, 0x36, 0x16, 0x26, 0x06, 0x3A, 0x1A, 0x2A,
              0x0A, 0x32, 0x12, 0x22, 0x42, 0x02, 0x7C, 0x3C, 0x5C, 0x1C, 0x6C, 0x2C, 0x4C, 0x0C, 0x74, 0x34, 0x54, 0x14,
              0x64, 0x24, 0x44, 0x04, 0x78, 0x38, 0x58, 0x18, 0x68, 0x28, 0x48, 0x08, 0xF0, 0x70, 0xB0, 0x30, 0xD0, 0x50,
              0x90, 0x10, 0xE0, 0x60, 0xA0, 0x20, 0xC0, 0x40, 0x80, 0x00]
class _Bits:
    def __init__(self, d, pos): self.d, self.pos, self.buf, self.n = d, pos, 0, 0
    def need(self, k):
        while self.n < k:
            if self.pos >= len(self.d): raise EOFError
            self.buf |= self.d[self.pos] << self.n; self.pos += 1; self.n += 8
    def peek(self, k): self.need(k); return self.buf & ((1 << k) - 1)
    def drop(self, k): self.need(k); self.buf >>= k; self.n -= k
    def get(self, k): v = self.peek(k); self.drop(k); return v
def _table(bits, codes):
    t = {}
    for sym, (b, c) in enumerate(zip(bits, codes)): t[(b, c)] = sym
    return t
_LEN_T, _DIST_T = _table(_LEN_BITS, _LEN_CODE), _table(_DIST_BITS, _DIST_CODE)
def _decode(bs, table, maxbits):
    for b in range(1, maxbits + 1):
        v = bs.peek(b) if True else 0
        if (b, v) in table: bs.drop(b); return table[(b, v)]
    raise ValueError('bad code')
def explode(data, out_size):
    ctype, dict_bits = data[0], data[1]
    if ctype != 0: raise NotImplementedError('PKWARE binary-coded literals only')
    bs = _Bits(data, 2); out = bytearray()
    while len(out) < out_size:
        if bs.get(1) == 0: out.append(bs.get(8)); continue
        sym = _decode(bs, _LEN_T, 8); length = _LEN_BASE[sym] + (bs.get(_EX_LEN_BITS[sym]) if _EX_LEN_BITS[sym] else 0) + 2
        if length == 519: break
        dsym = _decode(bs, _DIST_T, 8)
        low = bs.get(2 if length == 2 else dict_bits)
        dist = ((dsym << (2 if length == 2 else dict_bits)) | low) + 1
        for _ in range(length): out.append(out[-dist])
    return bytes(out[:out_size])

# ---- IMA ADPCM (Blizzard variant) ----
_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
         130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
         1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
         7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
_IDX = [-1, 0, -1, 4, -1, 2, -1, 6, -1, 1, -1, 5, -1, 3, -1, 7, -1, 1, -1, 5, -1, 3, -1, 7, -1, 2, -1, 4, -1, 6, -1, 8]
def adpcm_decompress(data, channels):
    out = bytearray(); pos = 0
    shift = data[1] - 1; pos = 2
    pred = []; idx = [0x2C] * channels
    for ch in range(channels):
        v = struct.unpack_from('<h', data, pos)[0]; pos += 2; pred.append(v); out += struct.pack('<h', v)
    ch = channels - 1
    while pos < len(data):
        b = data[pos]; pos += 1
        ch = (ch + 1) % channels if channels == 2 else 0
        if b & 0x80:
            if b == 0x80:
                if idx[ch] != 0: idx[ch] -= 1
                out += struct.pack('<h', pred[ch])
            elif b == 0x81:
                idx[ch] = min(idx[ch] + 8, 0x58)
                if channels == 2: ch = (ch + 1) % 2
            else:                                              # 0x82..: nothing (kept for completeness)
                if channels == 2: ch = (ch + 1) % 2
            continue
        step = _STEP[idx[ch]]; diff = step >> shift
        if b & 1: diff += step >> 0
        if b & 2: diff += step >> 1
        if b & 4: diff += step >> 2
        if b & 8: diff += step >> 3
        if b & 16: diff += step >> 4
        if b & 32: diff += step >> 5
        p = pred[ch] - diff if b & 0x40 else pred[ch] + diff
        p = max(-32768, min(32767, p)); pred[ch] = p
        out += struct.pack('<h', p)
        idx[ch] = max(0, min(0x58, idx[ch] + _IDX[b & 0x1F]))
    return bytes(out)
# NOTE: Blizzard's ADPCM uses the low six bits of a sample byte as the magnitude bits (shifted per bit) and bit 6 as the
# sign; the per-bit shifts above follow StormLib's DecompressADPCM (step >> 0 .. >> 5 for bits 0..5, plus step >> shift).

# ---- Huffman (Blizzard's adaptive Huffman, used together with ADPCM for WAV sound) ----
# The nine weight tables (one per compression type, 0x102 bytes each) are constants of Storm; the game's own copy is
# read from Game.exe (set_huffman_tables). The tree: a doubly linked list of items sorted by weight (highest first),
# where the two children of a node are neighbours in the list (child_hi = child_lo.prev).
_HUFF_TABLES = None
def set_huffman_tables(blob):
    global _HUFF_TABLES
    _HUFF_TABLES = [blob[0x102 * k:0x102 * (k + 1)] for k in range(9)]
def huffman_tables_from_exe(exe_path, va=0x6cf3a0):
    """Diablo II 1.14d Game.exe keeps Storm's tables at 0x6cf3a0."""
    sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'tools'))
    from pe import PE
    set_huffman_tables(PE(exe_path).bytes_at(va, 9 * 0x102))

class _Item:
    __slots__ = ('prev', 'next', 'value', 'weight', 'parent', 'lo')
    def __init__(self, value, weight): self.prev = self.next = self.parent = self.lo = None; self.value = value; self.weight = weight

class _Huff:
    def __init__(self, ctype):
        self.head = _Item(-1, 0); self.head.prev = self.head.next = self.head    # the list's head (sentinel)
        self.by_byte = [None] * 0x102
        w = _HUFF_TABLES[ctype & 0x0F]; maxw = 0
        for i in range(0x100):
            if w[i]:
                it = _Item(i, w[i]); self.by_byte[i] = it; self._insert_after(it, self.head)
                if w[i] >= maxw: maxw = w[i]
                else: self._move_after(it, self._higher_or_equal(self.head.prev, w[i]))
        for v in (0x100, 0x101):
            it = _Item(v, 1); self.by_byte[v] = it; self._insert_after(it, self.head.prev)
        lo = self.head.prev
        while lo is not self.head:
            hi = lo.prev
            if hi is self.head: break
            parent = _Item(0, hi.weight + lo.weight); parent.lo = lo; lo.parent = hi.parent = parent
            self._insert_after(parent, self._higher_or_equal(self.head.prev, parent.weight))
            lo = hi.prev
    # list helpers
    def _remove(self, it): it.prev.next = it.next; it.next.prev = it.prev
    def _insert_after(self, it, where): it.prev = where; it.next = where.next; where.next.prev = it; where.next = it
    def _move_after(self, it, where): self._remove(it); self._insert_after(it, where)
    def _higher_or_equal(self, it, weight):
        while it is not self.head:
            if it.weight >= weight: return it
            it = it.prev
        return self.head
    def decode(self, bits):
        it = self.head.next                            # the root: the heaviest item
        while it.lo is not None:
            it = it.lo.prev if bits.get(1) else it.lo
        return it.value
    def inc_weights(self, it):
        while it is not None:
            it.weight += 1
            higher = self._higher_or_equal(it.prev, it.weight); other = higher.next
            if other is not it:                        # swap it with the first item of its old weight
                self._move_after(other, it); self._move_after(it, higher)
                if other.parent.lo is other: other.parent.lo = it
                if it.parent.lo is it: it.parent.lo = other
                it.parent, other.parent = other.parent, it.parent
            it = it.parent
    def new_branch(self, value):
        """0x101 (a byte not in the tree): the lightest item splits into itself and the new byte (weight 0)"""
        last = self.head.prev
        hi = _Item(last.value, last.weight); hi.parent = last; self.by_byte[last.value] = hi
        lo = _Item(value, 0); lo.parent = last; self.by_byte[value] = lo
        self._insert_after(hi, self.head.prev); self._insert_after(lo, self.head.prev)
        last.lo = lo
        self.inc_weights(lo)

class _LsbBits:
    def __init__(self, d): self.d, self.pos, self.buf, self.n = d, 0, 0, 0
    def get(self, k):
        while self.n < k:
            b = self.d[self.pos] if self.pos < len(self.d) else 0
            self.buf |= b << self.n; self.pos += 1; self.n += 8
        v = self.buf & ((1 << k) - 1); self.buf >>= k; self.n -= k; return v

def huffman_decompress(data, out_size):
    if _HUFF_TABLES is None: raise NotImplementedError('Huffman (0x01): call huffman_tables_from_exe(Game.exe) first')
    bits = _LsbBits(data); ctype = bits.get(8); h = _Huff(ctype); cmp0 = ctype == 0
    out = bytearray()
    while len(out) < out_size:
        v = h.decode(bits)
        if v == 0x100: break
        if v == 0x101:
            v = bits.get(8); h.new_branch(v)
            if not cmp0: h.inc_weights(h.by_byte[v])
        out.append(v)
        if cmp0: h.inc_weights(h.by_byte[v])
    return bytes(out)

def decompress(data, out_size):
    if len(data) == out_size: return data
    method = data[0]; body = data[1:]
    if method & 0x10: body = bz2.decompress(body); method &= ~0x10
    if method & 0x08: body = explode(body, out_size); method &= ~0x08
    if method & 0x02: body = zlib.decompress(body); method &= ~0x02
    if method & 0x01: body = huffman_decompress(body, out_size * 2); method &= ~0x01
    if method & 0x80: body = adpcm_decompress(body, 2); method &= ~0x80
    if method & 0x40: body = adpcm_decompress(body, 1); method &= ~0x40
    if method: raise NotImplementedError('compression %#x' % method)
    return body

class MPQ:
    def __init__(self, path):
        self.f = open(path, 'rb'); d = self.f.read(0x200); off = 0
        while True:
            self.f.seek(off); h = self.f.read(32)
            if h[:4] == b'MPQ\x1a': break
            if h[:4] == b'MPQ\x1b': off += struct.unpack_from('<I', h, 8)[0]; continue
            off += 0x200
            if off > 0x10000000: raise ValueError('not an MPQ')
        self.base = off
        _, hsize, asize, ver, sshift, htpos, btpos, hn, bn = struct.unpack_from('<4sIIHHIIII', h)
        self.sector = 512 << sshift
        self.f.seek(off + htpos); ht = decrypt(self.f.read(16 * hn), hash_str('(hash table)', 3))
        self.f.seek(off + btpos); bt = decrypt(self.f.read(16 * bn), hash_str('(block table)', 3))
        self.hashes = [struct.unpack_from('<IIHHI', ht, 16 * i) for i in range(hn)]
        self.blocks = [struct.unpack_from('<IIII', bt, 16 * i) for i in range(bn)]
    def find(self, name):
        n = len(self.hashes); i = hash_str(name, 0) % n; a, b = hash_str(name, 1), hash_str(name, 2)
        for _ in range(n):
            h = self.hashes[i]
            if h[4] == 0xFFFFFFFF: return None
            if h[0] == a and h[1] == b and h[4] != 0xFFFFFFFE: return self.blocks[h[4]]
            i = (i + 1) % n
        return None
    def read(self, name):
        blk = self.find(name)
        if not blk: raise KeyError(name)
        pos, csize, size, flags = blk
        if not flags & 0x80000000: raise KeyError(name)
        self.f.seek(self.base + pos); raw = self.f.read(csize)
        key = 0
        if flags & 0x10000:
            key = hash_str(name.replace('/', '\\').split('\\')[-1], 3)
            if flags & 0x20000: key = ((key + pos) ^ size) & 0xFFFFFFFF
        comp = flags & 0x200 or flags & 0x100
        if flags & 0x1000000:                                  # a single unit
            if key: raw = decrypt(raw, key)
            return decompress(raw, size) if comp and csize < size else raw
        if not comp:
            if key:
                out = b''
                for s in range(0, len(raw), self.sector): out += decrypt(raw[s:s + self.sector], (key + s // self.sector) & 0xFFFFFFFF)
                return out[:size]
            return raw[:size]
        nsec = (size + self.sector - 1) // self.sector
        ntab = nsec + 1 + (1 if flags & 0x4000000 else 0)
        tab = raw[:4 * ntab]
        if key: tab = decrypt(tab, (key - 1) & 0xFFFFFFFF)
        offs = struct.unpack_from('<%dI' % ntab, tab)
        out = bytearray()
        for s in range(nsec):
            chunk = raw[offs[s]:offs[s + 1]]
            if key: chunk = decrypt(chunk, (key + s) & 0xFFFFFFFF)
            want = min(self.sector, size - s * self.sector)
            if flags & 0x100: out += explode(chunk, want) if len(chunk) < want else chunk
            else: out += decompress(chunk, want)
        return bytes(out)
    def names(self):
        try: lst = self.read('(listfile)').decode('latin-1').split()
        except KeyError: return []
        return [n for n in lst if self.find(n)]

if __name__ == '__main__':
    import os
    exe = os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), 'Game.exe')
    if os.path.exists(exe): huffman_tables_from_exe(exe)
    m = MPQ(sys.argv[1])
    if sys.argv[2] == 'list':
        for n in m.names(): print(n)
    elif sys.argv[2] == 'get':
        d = m.read(sys.argv[3]); open(sys.argv[4], 'wb').write(d); print(len(d), 'bytes')
