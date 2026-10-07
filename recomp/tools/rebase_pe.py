#!/usr/bin/env python3
"""rebase_pe.py <in.dll> <out.dll> <new base, hex>

A copy of a 32-bit DLL moved to another preferred base, as `editbin /rebase` does: every location its base relocation
table lists (IMAGE_REL_BASED_HIGHLOW) gets the difference added, and the header's ImageBase is updated. Two DLLs that
want the same base (Bink and Smacker both ask for 0x10000000) can then be lifted side by side, each at its own address.
Only the copy in recomp/work is written; the game's own file is read."""
import struct, sys


def rebase(data: bytearray, new_base: int) -> int:
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0': raise ValueError('not a PE file')
    nsec = struct.unpack_from('<H', data, pe + 6)[0]; opt = pe + 24; optsz = struct.unpack_from('<H', data, pe + 20)[0]
    if struct.unpack_from('<H', data, opt)[0] != 0x10b: raise ValueError('not a 32-bit image')
    old_base = struct.unpack_from('<I', data, opt + 28)[0]; delta = (new_base - old_base) & 0xffffffff
    reloc_rva, reloc_size = struct.unpack_from('<II', data, opt + 96 + 8 * 5)
    secs = [struct.unpack_from('<IIII', data, opt + optsz + 40 * i + 8) for i in range(nsec)]   # vsize, va, rawsize, rawptr

    def off(rva):
        for vs, va, rs, rp in secs:
            if va <= rva < va + max(vs, rs): return rp + rva - va
        raise ValueError('rva 0x%x outside the sections' % rva)

    n = 0
    if delta and reloc_size:
        p, end = off(reloc_rva), off(reloc_rva) + reloc_size
        while p + 8 <= end:
            page, size = struct.unpack_from('<II', data, p)
            if size < 8: break
            for k in range((size - 8) // 2):
                e = struct.unpack_from('<H', data, p + 8 + 2 * k)[0]
                kind, ofs = e >> 12, e & 0xfff
                if kind == 0: continue                                 # padding
                if kind != 3: raise ValueError('relocation type %d not supported' % kind)
                o = off(page + ofs); v = struct.unpack_from('<I', data, o)[0]
                struct.pack_into('<I', data, o, (v + delta) & 0xffffffff); n += 1
            p += size
    struct.pack_into('<I', data, opt + 28, new_base)
    return n


if __name__ == '__main__':
    src, dst, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    d = bytearray(open(src, 'rb').read()); n = rebase(d, base)
    open(dst, 'wb').write(d)
    print('%s: moved to 0x%08x (%d relocations)' % (dst, base, n))
