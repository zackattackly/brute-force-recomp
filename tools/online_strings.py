"""Milestone 98: the game's "System Link" wording becomes online wording (the loader carries System Link over the internet).

Edits the English string table in the loader's Xbox hard disk, token by token (xmbedit.py). The file before this patch is kept
as string-table-en.xmb.ol_orig and the patch is always applied to that copy, so running it again is safe. Afterwards run
tools/make_patches.py so the download's patch matches."""
import os, re, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmbedit

HERE = os.path.dirname(os.path.abspath(__file__))
ST = os.path.join(HERE, '..', 'loader', 'xbox_hdd', 'p5', 'common', 'stringtable', 'string-table-en.xmb')

RULES = [   # (pattern, replacement), applied in order, case-sensitive
    (r'SYSTEM\s+LINK', 'PLAY ONLINE'),
    (r'Xbox System Link Cable may be disconnected or the network is unavailable\.', 'The online server may be unreachable or the network is unavailable.'),
    (r'over a System\s+Link network', 'online'),
    (r'over System\s+Link network', 'online'),
    (r'over System\s+Link', 'online'),
    (r'System\s+Link games', 'online games'),
    (r'System\s+Link Sessions', 'online sessions'),
    (r'System\s+Link game', 'online game'),
    (r'System\s+Link', 'online'),
    (r'LAN play', 'online play'),
]

def dec(raw):
    n = sh = k = 0
    while True:
        b = raw[k]; n |= (b & 0x7f) << sh; k += 1; sh += 7
        if not (b & 0x80): break
    return raw[k:k + n].decode('utf-16-le')

def enc(s):
    raw = s.encode('utf-16-le'); n = len(raw); vb = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        if n: vb.append(b | 0x80)
        else: vb.append(b); break
    return bytes(vb) + raw

def main():
    if not os.path.exists(ST + '.ol_orig'): shutil.copyfile(ST, ST + '.ol_orig')
    shutil.copyfile(ST + '.ol_orig', ST)
    doc = xmbedit.load(ST); changed = 0
    for t in doc.toks:
        if (t.w & 0xf000) != 0xa000 or len(t.v) < 4: continue
        try: s = dec(t.v)
        except Exception: continue
        n = s
        for pat, rep in RULES: n = re.sub(pat, rep, n)
        if n != s:
            t.v = enc(n); changed += 1; print(repr(s), '->', repr(n))
    xmbedit.save(ST, doc)
    print('changed', changed, 'strings')

if __name__ == '__main__':
    main()
