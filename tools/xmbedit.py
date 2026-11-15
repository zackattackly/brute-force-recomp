"""Edit Brute Force .xmb (BXML) files at the token level (Milestone 96).

load(path) decodes the token stream (via xmb2xml's Decoder) into a list of Tok(word, value_bytes): each token keeps the
exact value bytes the game reads for it, so nothing is re-interpreted. save(path, doc) writes the tokens back as one
uncompressed block (flag words all 0 = no back-references) followed by the value bytes in token order; the header up to
the token stream (names table, namespaces) is kept as it was. The files carry no checksum (0).

Token types (high nibble): 0x0 element start (children follow), 0xa element with a value, 0xb empty element,
0x9 repeat count, 0xd list of values, 0xc end of element. Low 12 bits: index into the names table."""
import sys, os, struct, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmb2xml
from bxml import Reader

class Tok:
    __slots__ = ('w', 'v')
    def __init__(self, w, v=b''): self.w = w; self.v = v
    def __repr__(self): return 'Tok(%04x,%s)' % (self.w, self.v.hex())

class Doc:
    def __init__(self, head, toks, names, ver): self.head = head; self.toks = toks; self.names = names; self.ver = ver

_SCH = None
def schemas(data_dir=None):
    global _SCH
    if _SCH is None:
        d = data_dir or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'loader', 'xbox_hdd', 'p5', 'common')
        _SCH = xmb2xml.Schemas(sorted(glob.glob(os.path.join(d, '**', '*.xsb'), recursive=True)))
    return _SCH

def load(path):
    b = open(path, 'rb').read()
    d = xmb2xml.Decoder(b, schemas())
    toks = []
    orig_word = Reader.word
    depth = [0]
    def word(self):   # Reader.word calls itself for a back-reference: record only the outermost call
        depth[0] += 1
        try: w = orig_word(self)
        finally: depth[0] -= 1
        if depth[0] == 0: toks.append(Tok(w))
        return w
    orig_val = xmb2xml.Decoder.val
    def val(self, bt, et=None):
        p0 = self.r.p; r = orig_val(self, bt, et); toks[-1].v += b[p0:self.r.p]; return r
    Reader.word = word; xmb2xml.Decoder.val = val
    try:
        d.r = Reader(b, d.h['tokpos'], d.h['ver'])   # re-create so the patched word() is used from the start
        toks.clear(); d.run()
    finally:
        Reader.word = orig_word; xmb2xml.Decoder.val = orig_val
    return Doc(b[:d.h['tokpos']], toks, d.h['names'], d.h['ver'])

BLOCK_TOKENS = 3696   # tokens per block: 3696 + 231 flag words + 2 = 3929 words; the game's block buffer holds ~4100 (its own
                      # files never exceed 4115 words per block; one 6,000-word block crashed it at 0x258B98)
def save(path, doc):
    """Blocks of at most BLOCK_TOKENS tokens; each block: word count, the number of tokens it holds (two words for version
    0x101), then groups of a flags word (0 = all literal) and 16 tokens; the value bytes of its tokens follow it."""
    parts = [doc.head]
    for s in range(0, len(doc.toks), BLOCK_TOKENS):
        chunk = doc.toks[s:s + BLOCK_TOKENS]; ws = [t.w for t in chunk]; out = []
        out += [len(ws) & 0xffff, len(ws) >> 16] if doc.ver == 0x101 else [len(ws)]
        for i in range(0, len(ws), 16): out.append(0); out += ws[i:i + 16]
        parts.append(struct.pack('<H', len(out) + 1) + struct.pack('<%dH' % len(out), *out))
        parts.append(b''.join(t.v for t in chunk))
    open(path, 'wb').write(b''.join(parts))

# ---- structure helpers ----
def match_end(toks, i):
    """index of the 0xc end token closing the element that starts at toks[i] (type 0x0)"""
    depth = 0
    for j in range(i, len(toks)):
        t = toks[j].w & 0xf000
        if t == 0x0000: depth += 1
        elif t == 0xc000:
            depth -= 1
            if depth == 0: return j
    raise ValueError('unbalanced')

def name_hash(doc, tok): return doc.names[tok.w & 0xfff]

def children(doc, i):
    """(start, end) index pairs of the direct child elements of the element starting at i"""
    end = match_end(doc.toks, i); res = []; j = i + 1
    while j < end:
        t = doc.toks[j].w & 0xf000
        if t == 0x0000: e = match_end(doc.toks, j); res.append((j, e)); j = e + 1
        elif t in (0xa000, 0xb000): res.append((j, j)); j += 1
        else: j += 1
    return res

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    doc = load(src); save(dst, doc)
    print('tokens', len(doc.toks), 'value bytes', sum(len(t.v) for t in doc.toks), 'written', os.path.getsize(dst))
