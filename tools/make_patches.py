"""Milestone 97: patches for the game files this project edits, for the download (which must not carry game data).

Each edited file in loader\\xbox_hdd becomes loader\\patches\\<path with \\ as _>.bfp: a list of "copy these bytes from the
player's own original" and "insert these new bytes" steps. The loader (patch.c) applies a patch when the game opens the file
and it is still the original (size and FNV-1a hash match); the original is kept as <file>.orig.

Format (little endian): "BFP1", orig size, orig hash, new size, new hash, then ops: 1 = copy (offset, length), 2 = insert
(length, bytes), 0 = end."""
import os, struct, difflib, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HDD = os.path.join(HERE, '..', 'loader', 'xbox_hdd')
OUT = os.path.join(HERE, '..', 'loader', 'patches')
FILES = [   # (edited file, its untouched original)
    ('p5/common/stringtable/string-table-en.xmb', 'p5/common/stringtable/string-table-en.xmb.orig'),
    ('p5/common/stringtable/string-table-de.xmb', 'p5/common/stringtable/string-table-de.xmb.orig'),
    ('p5/common/stringtable/string-table-es.xmb', 'p5/common/stringtable/string-table-es.xmb.orig'),
    ('p5/common/stringtable/string-table-it.xmb', 'p5/common/stringtable/string-table-it.xmb.orig'),
    ('p5/common/game-options-en.xmb', 'p5/common/game-options-en.xmb.kb_orig'),
]

def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h

def make(new_rel, orig_rel):
    a = open(os.path.join(HDD, orig_rel), 'rb').read(); b = open(os.path.join(HDD, new_rel), 'rb').read()
    # rsync-style: every 12-byte run of the original is indexed; the edited file is walked, taking the longest run that matches
    # the original (near the last match first) and inserting only bytes found nowhere in the original
    K = 12; idx = {}
    for i in range(len(a) - K + 1): idx.setdefault(a[i:i + K], []).append(i)
    ops = bytearray(); added = 0; j = 0; pend = bytearray(); last = 0
    def flush():
        nonlocal pend, ops, added
        if pend: ops += struct.pack('<BI', 2, len(pend)) + pend; added += len(pend); pend = bytearray()
    while j < len(b):
        best, bl = -1, 0
        for i in idx.get(b[j:j + K], ()) if j + K <= len(b) else ():
            n = K
            while i + n < len(a) and j + n < len(b) and a[i + n] == b[j + n]: n += 1
            if n > bl or (n == bl and abs(i - last) < abs(best - last)): best, bl = i, n
            if bl > 4096: break
        if bl >= K: flush(); ops += struct.pack('<BII', 1, best, bl); j += bl; last = best + bl
        else: pend.append(b[j]); j += 1
    flush()
    ops += b'\0'
    out = b'BFP1' + struct.pack('<IIII', len(a), fnv(a), len(b), fnv(b)) + bytes(ops)
    os.makedirs(OUT, exist_ok=True)
    name = new_rel.replace('/', '_') + '.bfp'
    open(os.path.join(OUT, name), 'wb').write(out)
    # check: applying it to the original gives the edited file
    p = 20; r = bytearray()
    while out[p]:
        if out[p] == 1: o, n = struct.unpack_from('<II', out, p + 1); r += a[o:o + n]; p += 9
        else: n, = struct.unpack_from('<I', out, p + 1); r += out[p + 5:p + 5 + n]; p += 5 + n
    assert bytes(r) == b, new_rel
    print('%-48s %7d bytes, new bytes %6d (%.1f%% of the file)' % (name, len(out), added, 100.0 * added / len(b)))

if __name__ == '__main__':
    for n, o in FILES: make(n, o)
