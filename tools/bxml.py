"""Brute Force (Xbox) BXML (.xmb) reader - reverse-engineered from default.xbe.
Token decompressor: FUN_00281bf0/FUN_00281d30; parser: FUN_00254860/FUN_00255330."""
import struct, json, os
HERE = os.path.dirname(os.path.abspath(__file__))

class Reader:
    def __init__(self, b, pos, ver):
        self.b, self.p, self.ver = b, pos, ver
        self.blk = []; self.bi = 0
        self.win = [0]*512; self.wpos = 1
        self.state = 3; self.mlen = 0; self.moff = 0
        _, self.flags = self._getword(); self.bit = 0
    def _getword(self):
        new = 0
        if self.bi == len(self.blk):
            n = struct.unpack_from('<H', self.b, self.p)[0]
            words = struct.unpack_from(f'<{n-1}H', self.b, self.p+2) if n > 1 else ()
            self.blkstart = self.p; self.p += 2*n
            self.blk = list(words)
            if self.ver == 0x101: self.remaining = words[0] | (words[1] << 16); self.bi = 2
            else: self.remaining = words[0]; self.bi = 1
            self.wpos = 1; self.state = 3; new = 1
        w = self.blk[self.bi]; self.bi += 1
        return new, w
    def word(self):
        if self.state == 4:
            w = self.win[self.moff & 0x1ff]; self.moff += 1
            self.win[self.wpos] = w; self.wpos = (self.wpos+1) & 0x1ff
            self.mlen -= 1
            if self.mlen == 0: self.state = 3
            return w
        if self.bit == 16:
            _, self.flags = self._getword(); self.bit = 0
        isref = self.flags & (1 << self.bit); self.bit += 1
        new, w = self._getword()
        if new:
            self.flags = w; isref = w & 1; self.bit = 1
            _, w = self._getword()
        if not isref:
            self.win[self.wpos] = w; self.wpos = (self.wpos+1) & 0x1ff
            return w
        self.moff = w & 0x1ff; self.mlen = (w >> 9) + 2; self.state = 4
        return self.word()

def varint(b, p):
    v = s = 0
    while True:
        c = b[p]; p += 1; v |= (c & 0x7f) << s; s += 7
        if not c & 0x80: return v, p

def parse_header(b):
    assert b[:4] == b'BXML', 'not a BXML file'
    ver = (b[4] << 8) | b[5]; flags = b[6]
    nameoff, cksum = struct.unpack_from('<II', b, 8)
    n, p = varint(b, 16); ns = []
    for _ in range(n):
        ns.append(struct.unpack_from('<II', b, p)); p += 8
    cnt, p = varint(b, nameoff)
    names = list(struct.unpack_from(f'<{cnt}I', b, p)); p += 4*cnt
    return dict(ver=ver, flags=flags, cksum=cksum, ns=ns, names=names, tokpos=p)
