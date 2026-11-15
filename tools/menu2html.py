"""Brute Force (Xbox) menu viewer: the game's menus -> one HTML page.

The front-end and in-game menus (main menu, options, briefing, objectives, pause, squad
select, multiplayer setup ...) are defined in common/game-options-LANG.xmb, with their
texts in common/stringtable/string-table-LANG.xmb and their artwork in the texture
libraries. This rebuilds every menu on the Xbox's 640 x 480 screen as positioned
images, buttons and texts, and writes common/game-options-LANG_menus.html.

Usage:
    python menu2html.py <data folder> [language: en de fr es it]

Run export_textures.bat first so the artwork is there. Open the .html file in a browser.

Format (reverse-engineered):
  game-menus holds 64 <menus>, each with <data> (name, position, size on the 640x480
  screen) and its controls (_e275fb80): static-text (a string id), buttons (_16a61c91:
  display-string-id, event-script), button hints (_09dc0d2f: button-string), images
  (_0ac3cc5c: texture, colour, blend, position/size within the control, flip flags
  _e3e50283 (mirror sideways) / _06712921 (upright: without it the image is drawn upside
  down, which is how upside-down stored artwork such as the logo comes out right); or _0d802f88, a shared multi-part
  frame defined in _1544c137), option selectors (_fbe6d17c), list boxes, movie and music
  players. Controls use styles (_069866ab, referenced by _f3c6fdea): text colours, font
  size, alignment and a button background texture. menu-sequence lists which menus are
  shown together. Text codes: ^cN colour, ^f(N ... ^) flashing.
Pure Python standard library.
"""
import html, json, os, re, struct, sys
import xml.etree.ElementTree as ET
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bfhash import bfhash
try:
    NAMES = {hex(int(k, 16)): v for k, v in json.load(open(os.path.join(HERE, 'names.json'))).items()}  # any key padding
except Exception:
    NAMES = {}

def hval(s):
    s = (s or '').strip()
    if not s: return None
    if s.startswith('#'):
        try: return int(s[1:], 16)
        except ValueError: return None
    return bfhash(s)

def fl(s, n):
    try: v = [float(x) for x in (s or '').split()]
    except ValueError: v = []
    return (v + [0.0] * n)[:n]

def png_size(p):
    try:
        with open(p, 'rb') as f: b = f.read(24)
        return struct.unpack('>II', b[16:24])
    except Exception:
        return (0, 0)

def find(root, name):
    for d, _, files in os.walk(root):
        if name in files: return os.path.join(d, name)
    return None

def texture_index(root):
    out = {}
    for d, _, files in os.walk(root):
        if not d.endswith('_png'): continue
        for f in files:
            if not f.endswith('.png'): continue
            stem = f[:-4].replace('_p8', '').replace('_cube', '')
            h = int(stem, 16) if re.fullmatch(r'[0-9a-f]{8}', stem) else bfhash(stem)
            out.setdefault(h, os.path.join(d, f))
    return out

CODES = re.compile(r'\^c\d|\^f\(\d|\^\)|\^[a-z]')

def text_html(s):
    s = CODES.sub('', s or '')
    return html.escape(s).replace('\r', '').replace('\n', '<br>').replace('\t', '&emsp;')

class Menus:
    def __init__(self, root, lang):
        opt = find(root, 'game-options-%s.xml' % lang)
        st = find(root, 'string-table-%s.xml' % lang)
        if not opt: raise SystemExit('game-options-%s.xml not found under %s' % (lang, root))
        self.opt = opt
        self.strings = {}
        if st:
            for e in ET.parse(st).getroot():
                i = hval(e.findtext('id')); v = e.findtext('value') or ''
                if i is not None: self.strings[i] = v
        self.pngs = texture_index(root)
        r = ET.parse(opt).getroot()
        self.gm = r.find('game-menus')
        self.styles = {}
        for s in self.gm.findall('_069866ab'):
            self.styles[hval(s.findtext('id'))] = s
        self.frames = {}
        for s in self.gm.findall('_1544c137'):
            self.frames[hval(s.findtext('id'))] = s.findall('_1b74bf95')
        self.out_dir = os.path.dirname(opt)

    def string(self, ref):
        h = hval(ref)
        if h is None: return ''
        return self.strings.get(h, '[%s]' % (ref or '').strip())

    def png_url(self, ref):
        p = self.pngs.get(hval(ref))
        return os.path.relpath(p, self.out_dir).replace('\\', '/') if p else None

    def style(self, el):
        return self.styles.get(hval(el.findtext('_f3c6fdea'))) if el is not None else None

    def image(self, d, x, y, w, h):
        """One image part (texture, colour, flips) placed at x, y with size w, h (0 = natural)."""
        url = self.png_url(d.findtext('texture'))
        px, py = fl(d.findtext('position'), 2); sw, sh = fl(d.findtext('size'), 2)
        if url and (sw == 0 or sh == 0):
            nw, nh = png_size(os.path.join(self.out_dir, url))
            sw, sh = sw or nw, sh or nh
        sw, sh = sw or w, sh or h
        c = [int(float(v)) for v in (d.findtext('color') or '255 255 255 255').split()] + [255] * 4
        # the menu renderer draws textures with v running upwards, so an image appears upright
        # in its stored orientation only when _06712921 is set; _e3e50283 mirrors it sideways
        fx = (d.findtext('_e3e50283') or '') == 'true'; fy = (d.findtext('_06712921') or '') != 'true'
        tf = 'transform:scale(%d,%d);' % (-1 if fx else 1, -1 if fy else 1) if fx or fy else ''
        style = 'left:%dpx;top:%dpx;width:%dpx;height:%dpx;opacity:%.2f;%s' % (x + px, y + py, sw, sh, c[3] / 255.0, tf)
        if url:
            tint = '' if c[:3] == [255, 255, 255] else 'background:rgb(%d,%d,%d);' % tuple(c[:3])
            return '<img class="i" src="%s" style="%s%s" title="%s">' % (html.escape(url), style, '',
                                                                         html.escape((d.findtext('texture') or '')))
        return '<div class="i missing" style="%s" title="texture %s not exported">%s</div>' % (
            style, html.escape(d.findtext('texture') or ''), html.escape(d.findtext('texture') or ''))

    def text_box(self, txt, x, y, w, h, st, cls):
        color = '#6ec' ; size = 14; ha = 'left'; va = 'flex-start'
        if st is not None:
            c = (st.findtext('color-normal') or '').split()
            if len(c) >= 3: color = 'rgb(%s,%s,%s)' % tuple(c[:3])
            ps = st.find('font-1/point-size')
            if ps is not None and ps.text: size = int(float(ps.text)) * 0.75
            al = st.find('alignment')
            if al is not None:
                # alignment values: 0 = left / top, 1 = right / bottom, 2 = centred (inferred from the layouts)
                ha = {'0': 'left', '1': 'right', '2': 'center'}.get(al.findtext('_fc9aaf63'), 'left')
                va = {'0': 'flex-start', '1': 'flex-end', '2': 'center'}.get(al.findtext('_0c9d2ce1'), 'flex-start')
        bg = ''
        if st is not None and st.findtext('texture-1'):
            u = self.png_url(st.findtext('texture-1'))
            if u: bg = 'background-image:url(%s);background-size:100%% 100%%;' % html.escape(u)
        return ('<div class="%s" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx;color:%s;font-size:%.0fpx;'
                'justify-content:%s;align-items:%s;text-align:%s;%s">%s</div>'
                % (cls, x, y, w, h, color, size, {'left': 'flex-start', 'center': 'center', 'right': 'flex-end'}[ha],
                   va, ha, bg, text_html(txt)))

    def control(self, c):
        it = c.find('item-data')
        x, y = fl(it.findtext('position') if it is not None else '', 2)
        w, h = fl(it.findtext('size') if it is not None else '', 2)
        st = self.style(it)
        if c.tag == 'static-text':
            return self.text_box(self.string(c.findtext('text')), x, y, w, h, st, 't')
        if c.tag == '_16a61c91':
            return self.text_box(self.string(c.findtext('display-string-id')), x, y, w, h, st, 't b')
        if c.tag == '_09dc0d2f':
            return self.text_box(self.string(c.findtext('button-string')), x, y, w, h, st, 't hint')
        if c.tag == '_0ac3cc5c':
            parts = []
            d = c.find('data')
            if d is not None: parts.append(self.image(d, x, y, w, h))
            for fr in c.findall('_0d802f88'):
                for p in self.frames.get(hval(fr.text), []): parts.append(self.image(p, x, y, 0, 0))
            return ''.join(parts)
        if c.tag == '_fbe6d17c':
            entries = [self.string(e.findtext('display-string-id') or e.text) for e in c.findall('text-entry')]
            return self.text_box('< %s >' % (entries[0] if entries else ''), x, y, w, h, st, 't b')
        if c.tag == 'listbox':
            return '<div class="lb" style="left:%dpx;top:%dpx;width:%dpx;height:%dpx" title="list box">list</div>' % (x, y, w, h)
        if c.tag == 'movie-player':
            return '<div class="mv">movie: %s</div>' % html.escape(', '.join(m.text or '' for m in c.findall('movie-file')))
        return ''

    def render(self):
        menus = self.gm.findall('menus')
        blocks = []
        for n, m in enumerate(menus, 1):
            d = m.find('data')
            name = (d.findtext('name') or '').strip()
            nice = NAMES.get(hex(hval(name) or 0), name)
            mx, my = fl(d.findtext('position'), 2); mw, mh = fl(d.findtext('size'), 2)
            ctl = m.find('items')
            if ctl is None: ctl = m.find('_e275fb80')   # named in Milestone 55
            items = []; seen = {}
            for c in (ctl if ctl is not None else []):
                it = c.find('item-data')
                key = (c.tag, it.findtext('position') if it is not None else None, it.findtext('size') if it is not None else None)
                if c.tag == 'static-text' and key in seen:
                    # texts that share a place are alternatives the game swaps in (help line for the
                    # selected button ...): show the first, list the rest in its tooltip
                    items[seen[key]] = items[seen[key]].replace('<div class="t"', '<div class="t alt" title="%s"'
                        % html.escape('also shown here: ' + CODES.sub('', self.string(c.findtext('text')))[:200]), 1) \
                        if ' alt"' not in items[seen[key]] else items[seen[key]].replace('title="', 'title="%s | ' % html.escape(CODES.sub('', self.string(c.findtext('text')))[:200]), 1)
                    continue
                if c.tag == 'static-text': seen[key] = len(items)
                items.append(self.control(c))
            texts = [self.string(c.findtext('text') or c.findtext('display-string-id')) for c in (ctl if ctl is not None else [])
                     if c.tag in ('static-text', '_16a61c91')]
            title = next((CODES.sub('', t).strip() for t in texts if t.strip() and not t.startswith('[')), '')
            blocks.append('<section><h2>%d. %s <small>%s</small></h2><div class="screen"><div class="menu" '
                          'style="left:%dpx;top:%dpx;width:%dpx;height:%dpx">%s</div></div></section>'
                          % (n, html.escape(title[:60] or '(no text)'), html.escape(nice), mx, my, mw, mh, ''.join(items)))
        page = ('<!doctype html><html><head><meta charset="utf-8"><title>Brute Force menus</title><style>'
                'body{background:#111;color:#ccc;font-family:Arial,sans-serif;margin:16px}'
                'section{display:inline-block;margin:0 16px 24px 0;vertical-align:top}'
                'h2{font-size:14px;font-weight:normal;margin:4px 0}h2 small{color:#777}'
                '.screen{position:relative;width:640px;height:480px;background:#0b1622;overflow:hidden;border:1px solid #333}'
                '.menu{position:absolute;outline:1px dashed #234}'
                '.i,.t,.lb{position:absolute;box-sizing:border-box}.i.missing{border:1px dotted #555;color:#555;font-size:9px}'
                '.t{display:flex;overflow:hidden;line-height:1.15;padding:0 2px}.b{outline:1px solid rgba(80,160,220,.35)}'
                '.hint{font-size:11px!important}.lb{border:1px solid #357;color:#357;font-size:10px}'
                '.alt{text-decoration:underline dotted #357}.mv{position:absolute;right:4px;bottom:2px;font-size:9px;color:#456}'
                '</style></head><body><h1 style="font-size:18px">Brute Force menus (%d), rebuilt from %s</h1>%s</body></html>'
                % (len(menus), html.escape(os.path.basename(self.opt)), '\n'.join(blocks)))
        out = os.path.splitext(self.opt)[0] + '_menus.html'
        open(out, 'w', encoding='utf-8').write(page)
        return out, len(menus)

def main(args):
    if not args:
        print(__doc__); return 1
    m = Menus(args[0], args[1] if len(args) > 1 else 'en')
    out, n = m.render()
    print('wrote %d menus to %s' % (n, out))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
