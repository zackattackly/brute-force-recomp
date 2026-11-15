"""Milestone 96: a KEY BINDINGS entry in the in-game pause menu, and the key-binding screen it opens.

Edits (token level, xmbedit.py) the menus file common\\game-options-<lang>.xmb and the string table
common\\stringtable\\string-table-<lang>.xmb in the loader's Xbox hard disk. Originals are kept as *.kb_orig and the patch is
always applied to those, so running it again is safe.

Pause menu (#fdcd29b4): a sixth button, copied from RESTART CHAPTER, named KB_BTN, text STR_KB_TITLE, at y 135 with an event
script (event 2, action 1 = open a menu, like PROFILE OPTIONS) that opens KB_MENU; QUIT moves to y 160, the two button hints to
y 200, the menu grows to 229 px.
Key-binding screen KB_MENU: a copy of the "Controls" help panel (#15d8de40): title STR_KB_TITLE, message STR_KB_BODY in a control
named KB_MSG (input.c finds it by that name and rewrites its text while the screen is open)."""
import sys, os, struct, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmbedit
from xmbedit import Tok

KB_BTN, KB_MENU, STR_KB_TITLE, STR_KB_BODY, KB_MSG = 0x4b42a001, 0x4b42a002, 0x4b42a003, 0x4b42a004, 0x4b42a005
PAUSE, CONTROLS, RESTART_CH, QUIT = 0xfdcd29b4, 0x15d8de40, 0x1ffd6617, 0x13d713e1
HERE = os.path.dirname(os.path.abspath(__file__))
COMMON = os.path.join(HERE, '..', 'loader', 'xbox_hdd', 'p5', 'common')

def u32(t): return struct.unpack('<I', t.v)[0] if len(t.v) == 4 else None
def f2(t): return struct.unpack('<ff', t.v)
def tagname(doc, t): return doc.names[t.w & 0xfff]
def NAME(s): import bfhash; return bfhash.bfhash(s)

def find_value(doc, value, start=0):
    for i in range(start, len(doc.toks)):
        if (doc.toks[i].w & 0xf000) == 0xa000 and u32(doc.toks[i]) == value: return i
    raise KeyError('%08x not found' % value)

def enclosing(doc, i, tag):
    """start index of the nearest enclosing element named `tag` (hash) around token i"""
    depth = 0
    for j in range(i, -1, -1):
        ty = doc.toks[j].w & 0xf000
        if ty == 0xc000 and j != i: depth += 1
        elif ty == 0x0000:
            if depth == 0 and tagname(doc, doc.toks[j]) == tag: return j
            if depth > 0: depth -= 1
    raise KeyError('no enclosing element')

def child(doc, start, tag):
    """first direct child (start, end) named tag"""
    for s, e in xmbedit.children(doc, start):
        if tagname(doc, doc.toks[s]) == tag: return s, e
    raise KeyError('child not found')

def set_pos(doc, item_start, x, y):
    s, e = child(doc, item_start, NAME('item-data')); ps, pe = child(doc, s, NAME('position'))
    doc.toks[ps + 1].v = struct.pack('<ff', x, y)

def clone(toks): return [Tok(t.w, t.v) for t in toks]

def patch_menus(path):
    doc = xmbedit.load(path)
    H = {n: NAME(n) for n in ('menus', 'items', 'data', 'name', 'position', 'size', 'item-data', 'display-string-id', 'text', 'event-script')}
    # ---- pause menu ----
    pm = enclosing(doc, find_value(doc, PAUSE), H['menus'])
    items, items_end = child(doc, pm, H['items'])
    rc = enclosing(doc, find_value(doc, RESTART_CH, pm), doc.toks[items + 2].w and tagname(doc, doc.toks[items + 2]))   # the button element (_16a61c91)
    btn_tag = tagname(doc, doc.toks[rc]); rc_end = xmbedit.match_end(doc.toks, rc)
    quit_btn = enclosing(doc, find_value(doc, QUIT, pm), btn_tag)
    # the event script template: the first event-script in the file whose action is "open" (action raw 2 = enum 1)
    es = next(i for i, t in enumerate(doc.toks) if (t.w & 0xf000) == 0 and tagname(doc, t) == H['event-script'] and doc.toks[i + 2].v == b'\x02')
    es_toks = clone(doc.toks[es:xmbedit.match_end(doc.toks, es) + 1])
    es_toks[1].v = b'\x03'; es_toks[2].v = b'\x01'; es_toks[3].v = struct.pack('<I', KB_MENU)   # event 2, action 0 = open a menu by name (action 1 opens a menu-sequence)
    nb = clone(doc.toks[rc:rc_end + 1])
    for t in nb:
        if (t.w & 0xf000) == 0xa000 and u32(t) == RESTART_CH: t.v = struct.pack('<I', KB_BTN)
        if (t.w & 0xf000) == 0xa000 and tagname(doc, t) == H['display-string-id']: t.v = struct.pack('<I', STR_KB_TITLE)
    nb = nb[:-1] + es_toks + nb[-1:]
    # positions: the new button at 135, QUIT to 160, hints (y 175) to 200, menu height 204 -> 229
    for i in range(pm, xmbedit.match_end(doc.toks, pm)):
        t = doc.toks[i]
        if (t.w & 0xf000) == 0xd000 and len(t.v) == 8:
            x, y = f2(t)
            if y == 175.0: t.v = struct.pack('<ff', x, 200.0)
            if (x, y) == (272.0, 204.0): t.v = struct.pack('<ff', 272.0, 229.0)
    set_pos(doc, quit_btn, 0.0, 160.0)
    for i, t in enumerate(nb):
        if (t.w & 0xf000) == 0xd000 and len(t.v) == 8 and f2(t) == (0.0, 85.0): t.v = struct.pack('<ff', 0.0, 135.0)
    cnt = doc.toks[items + 1]; assert (cnt.w & 0xf000) == 0x9000; cnt.w += 1    # 5 -> 6 buttons
    doc.toks[quit_btn:quit_btn] = nb
    # ---- the key-binding screen: a copy of the Controls panel ----
    cm = enclosing(doc, find_value(doc, CONTROLS), H['menus']); cm_end = xmbedit.match_end(doc.toks, cm)
    nm = clone(doc.toks[cm:cm_end + 1])
    texts = [t for t in nm if (t.w & 0xf000) == 0xa000 and tagname(doc, t) == H['text']]
    texts[0].v = struct.pack('<I', STR_KB_TITLE); texts[1].v = struct.pack('<I', STR_KB_BODY)
    for t in nm:
        if (t.w & 0xf000) == 0xa000 and u32(t) == CONTROLS: t.v = struct.pack('<I', KB_MENU)
        if (t.w & 0xf000) == 0xa000 and u32(t) == 0x02a91d98: t.v = struct.pack('<I', KB_MSG)   # the message control
        if (t.w & 0xf000) == 0xd000 and len(t.v) == 8 and f2(t) == (354.0, 315.0): t.v = struct.pack('<ff', 440.0, 270.0)
    doc.toks[cm_end + 1:cm_end + 1] = nm
    # a count token in front of the run of 'menus' elements, if there is one
    first = next(i for i, t in enumerate(doc.toks) if (t.w & 0xf000) == 0 and tagname(doc, t) == H['menus'])
    if (doc.toks[first - 1].w & 0xf000) == 0x9000:
        c = doc.toks[first - 1]; n = c.w & 0x7ff; assert not (c.w & 0x800); c.w += 1; print('menus count', n, '->', n + 1)
    xmbedit.save(path, doc)

def patch_strings(path, title, body):
    doc = xmbedit.load(path)
    cnt = doc.toks[1]; assert (cnt.w & 0xf000) == 0x9000 and not (cnt.w & 0x800)
    entry = clone(doc.toks[2:6])   # start, id, value, end
    def ent(i, s):
        e = clone(entry); e[1].v = struct.pack('<I', i); raw = s.encode('utf-16-le'); n = len(raw); vb = bytearray()
        while True:
            b = n & 0x7f; n >>= 7
            if n: vb.append(b | 0x80)
            else: vb.append(b); break
        e[2].v = bytes(vb) + raw; return e
    end = xmbedit.match_end(doc.toks, 0)
    doc.toks[end:end] = ent(STR_KB_TITLE, title) + ent(STR_KB_BODY, body)
    cnt.w += 2
    xmbedit.save(path, doc)

if __name__ == '__main__':
    lang = sys.argv[1] if len(sys.argv) > 1 else 'en'
    gm = os.path.join(COMMON, 'game-options-%s.xmb' % lang); st = os.path.join(COMMON, 'stringtable', 'string-table-%s.xmb' % lang)
    for p in (gm, st):
        if not os.path.exists(p + '.kb_orig'): shutil.copyfile(p, p + '.kb_orig')
        shutil.copyfile(p + '.kb_orig', p)
    patch_menus(gm)
    patch_strings(st, 'KEY BINDINGS', 'Loading key bindings...' + ' ' * 1200)
    print('patched', gm, st)
