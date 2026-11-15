"""Brute Force BXSD (.xsb) schema reader. Loader: FUN_00281fe0, tree: FUN_00281f10, node: FUN_00254620."""
import struct
from bxml import varint

def qname(b, p):
    t = b[p]; p += 1
    if t == 0: return None, p
    if t == 0x21: return (None, struct.unpack_from('<I', b, p)[0]), p + 4
    if t == 0x22:
        ns, n = struct.unpack_from('<II', b, p); return (ns, n), p + 8
    raise ValueError(f'bad qname tag {t:#x} at {p-1:#x}')

def parse(b):
    assert b[:4] == b'BXSD'
    body = struct.unpack_from('<I', b, 8)[0]
    # namespace table (just before body)
    p = 16
    tns, p = qname(b, p) if b[p] in (0, 0x21, 0x22) else (None, p)
    def node(p):
        kind = struct.unpack_from('<H', b, p)[0]; p += 2
        name, p = qname(b, p); typ, p = qname(b, p)
        n, p = varint(b, p); kids = []
        for _ in range(n):
            k, p = node(p); kids.append(k)
        return dict(kind=kind, name=name, type=typ, kids=kids), p
    n, p = varint(b, body); roots = []
    for _ in range(n):
        r, p = node(p); roots.append(r)
    return dict(header=b[16:body], roots=roots, end=p, size=len(b))
