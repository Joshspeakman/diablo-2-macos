"""Diablo II 1.10+ character files (.d2s, version 96), for the coverage test: read a character the game saved and
give it what a test needs (every class skill, mana, function-key hotkeys, a starting act, all waypoints). Only scratch
characters the tests create themselves are edited.

  ch = D2S(open(path, 'rb').read()); ch.set_skills([20] * 30); ch.set_stat('mana', 2000); open(path, 'wb').write(ch.save())
"""
import struct

CLASSES = ['Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin']
FIRST_SKILL = [6, 36, 66, 96, 126, 221, 251]           # each class's 30 skills, in the order of the file's skill list
# the character stats (ItemStatCost's first rows): id -> (name, bits); life, mana and stamina are 8-bit fixed point
STATS = {0: ('strength', 10), 1: ('energy', 10), 2: ('dexterity', 10), 3: ('vitality', 10), 4: ('statpts', 10),
         5: ('newskills', 8), 6: ('hitpoints', 21), 7: ('maxhp', 21), 8: ('mana', 21), 9: ('maxmana', 21),
         10: ('stamina', 21), 11: ('maxstamina', 21), 12: ('level', 7), 13: ('experience', 32), 14: ('gold', 25),
         15: ('goldbank', 25)}
FIXED = {'hitpoints', 'maxhp', 'mana', 'maxmana', 'stamina', 'maxstamina'}
OFF_CHECKSUM, OFF_SIZE, OFF_NAME, OFF_STATUS, OFF_CLASS, OFF_LEVEL = 12, 8, 20, 36, 40, 43
OFF_HOTKEYS, OFF_RIGHT, OFF_ACTS, OFF_QUESTS, OFF_WAYPOINTS, OFF_STATS = 56, 124, 168, 345, 633, 765

class Bits:
    def __init__(self, data=b''): self.d = data; self.pos = 0; self.out = []
    def get(self, n):
        v = 0
        for k in range(n):
            byte = self.d[(self.pos + k) >> 3]
            v |= ((byte >> ((self.pos + k) & 7)) & 1) << k
        self.pos += n; return v
    def put(self, v, n): self.out.extend((v >> k) & 1 for k in range(n))
    def bytes(self):
        b = bytearray((len(self.out) + 7) // 8)
        for i, bit in enumerate(self.out): b[i >> 3] |= bit << (i & 7)
        return bytes(b)

class D2S:
    def __init__(self, data):
        if struct.unpack_from('<I', data, 0)[0] != 0xaa55aa55: raise ValueError('not a character file')
        if struct.unpack_from('<I', data, 4)[0] != 96: raise ValueError('not a 1.10+ character (version %d)' % struct.unpack_from('<I', data, 4)[0])
        self.head = bytearray(data[:OFF_STATS])
        if data[OFF_STATS:OFF_STATS + 2] != b'gf': raise ValueError('no stats section')
        bits = Bits(data[OFF_STATS + 2:]); self.stats = {}
        while True:
            sid = bits.get(9)
            if sid == 0x1ff: break
            if sid not in STATS: raise ValueError('unknown stat %d' % sid)
            name, n = STATS[sid]; self.stats[name] = bits.get(n)
        end = OFF_STATS + 2 + (bits.pos + 7) // 8
        if data[end:end + 2] != b'if': raise ValueError('no skills section')
        self.skills = list(data[end + 2:end + 32]); self.rest = data[end + 32:]
    # ---- reading ----
    @property
    def name(self): return self.head[OFF_NAME:OFF_NAME + 16].split(b'\0')[0].decode('latin-1')
    @property
    def cls(self): return self.head[OFF_CLASS]
    def stat(self, name): v = self.stats.get(name, 0); return v >> 8 if name in FIXED else v
    # ---- changing ----
    def set_stat(self, name, value): self.stats[name] = value << 8 if name in FIXED else value
    def set_skills(self, levels): self.skills = [max(0, min(99, int(v))) for v in levels][:30] + [0] * (30 - len(levels))
    def set_hotkeys(self, skill_ids):
        """F1, F2, ...: the right mouse button's skill for each (0xffff: unassigned)"""
        for k in range(16):
            struct.pack_into('<I', self.head, OFF_HOTKEYS + 4 * k, skill_ids[k] if k < len(skill_ids) else 0xffff)
    def set_right_skill(self, skill_id): struct.pack_into('<I', self.head, OFF_RIGHT, skill_id)
    def set_act(self, act, difficulty=0):
        """the act (0-4) the character starts in: the game loads it in that act's town"""
        self.head[OFF_ACTS + difficulty] = 0x80 | (act & 7)
    def all_waypoints(self, difficulty=0):
        p = OFF_WAYPOINTS + 8 + 24 * difficulty + 2           # after "WS", 6 bytes, and the difficulty's 0x02 0x01
        for k in range(5): self.head[p + k] = 0xff
    def all_quests(self, difficulty=0):
        """every quest of the difficulty done and every act reached (48 words: each act's introduction, quests and the
        travel to the next act), so the waypoints list every act"""
        for k in range(48): struct.pack_into('<H', self.head, OFF_QUESTS + 96 * difficulty + 2 * k, 1)
        if self.head[OFF_QUESTS - 10:OFF_QUESTS - 6] != b'Woo!': raise ValueError('no quest section')
    def save(self):
        b = Bits()
        for sid, (name, n) in STATS.items():
            if name in self.stats and (self.stats[name] or name in ('level', 'strength', 'energy', 'dexterity', 'vitality')):
                b.put(sid, 9); b.put(self.stats[name] & ((1 << n) - 1), n)
        b.put(0x1ff, 9)
        if 'level' in self.stats: self.head[OFF_LEVEL] = self.stats['level']
        data = bytearray(self.head) + b'gf' + b.bytes() + b'if' + bytes(self.skills) + self.rest
        struct.pack_into('<I', data, OFF_SIZE, len(data)); struct.pack_into('<I', data, OFF_CHECKSUM, 0)
        s = 0
        for byte in data: s = (((s << 1) | (s >> 31)) + byte) & 0xffffffff
        struct.pack_into('<I', data, OFF_CHECKSUM, s)
        return bytes(data)

def checksum_ok(data):
    d = bytearray(data); want = struct.unpack_from('<I', d, OFF_CHECKSUM)[0]; struct.pack_into('<I', d, OFF_CHECKSUM, 0); s = 0
    for byte in d: s = (((s << 1) | (s >> 31)) + byte) & 0xffffffff
    return s == want

if __name__ == '__main__':
    import sys
    raw = open(sys.argv[1], 'rb').read(); c = D2S(raw)
    print(c.name, CLASSES[c.cls], 'level', c.stat('level'), 'stats', {k: c.stat(k) for k in c.stats}, 'skills', c.skills,
          'checksum ok' if checksum_ok(raw) else 'BAD CHECKSUM', 'round trip identical' if c.save() == raw else 'round trip differs')
