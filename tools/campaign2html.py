r"""Brute Force (Xbox) campaign guide: the mission list -> one HTML page.

Reads common\campaign-bf.xml (the game's mission list) and the string table, and writes
common\campaign.html: every campaign mission, deathmatch and squad deathmatch map in game
order, with
  - the mission name and the levels (map sections) it is made of
  - the movies played before and after each section, with the on-screen texts
  - the primary and secondary objectives, with their reward points
  - the credits shown for the mission
  - links to what the other exporters made for each level, when they exist: the 3D scene
    (export_levels.bat), the mission logic (export_mission_logic.bat), the textures, the
    models and the sounds.

Usage:
    python campaign2html.py <data folder> [language]     (language: en, de, fr, es, it)

The campaign file (reverse-engineered): <level display-name-id texture-name type> per mission,
type = campaign (#17506391), deathmatch (#120785ef) or squad deathmatch (#efb18d28). Each
section (zone; _0ef96a81 in data converted before Milestone 56) has a file-name, pre-movies / post-movies (bink-file plus timed texts:
text-string, start-delay, display-time) and the squad members it starts with. Objectives
(primary-objectives / secondary-objectives) give the game-flag path missions.<id>.<key>,
a display-id in the string table and a reward (points). <credits> holds title, group
headings and names.
Pure Python standard library.
"""
import glob, html, os, re, sys
import xml.etree.ElementTree as ET

KIND = {'#17506391': 'Campaign', '#120785ef': 'Deathmatch', '#efb18d28': 'Squad deathmatch'}


def strings(root, lang):
    st = {}
    for p in glob.glob(os.path.join(root, 'common', '**', 'string-table-%s.xml' % lang), recursive=True):
        t = open(p, encoding='iso-8859-1').read()
        for i, v in re.findall(r'<id>([^<]*)</id>\s*<value>(.*?)</value>', t, re.S):
            st[i.strip()] = html.unescape(v)   # the XML text still holds &lt; &amp; ...
    return st


def clean(s):
    """Remove the game's text markup (^c1 colours, ^f( flashing ^) )."""
    return re.sub(r'\^c\d|\^f\(\d?|\^\)', '', s or '')


def tx(el, tag):
    c = el.find(tag)
    return (c.text or '').strip() if c is not None else ''


def level_links(root, lvl):
    """(label, relative path from common\\) for exported files of one level that exist."""
    d = os.path.join(root, lvl)
    cands = [('3D scene', os.path.join(lvl, '%s_scene.glb' % lvl)),
             ('mission logic', os.path.join(lvl, 'levels-%s_logic.txt' % lvl)),
             ('logic graph', os.path.join(lvl, 'levels-%s_logic.dot' % lvl)),
             ('models', os.path.join(lvl, 'objects-%s.obj' % lvl)),
             ('textures', os.path.join(lvl, 'textures-%s_png' % lvl)),
             ('music', os.path.join('sounds_wav', 'music', lvl)),
             ('dialogue', os.path.join('sounds_wav', 'en', lvl)),
             ('effects sounds', os.path.join('sounds_wav', 'memory', lvl))]
    out = []
    for label, rel in cands:
        if os.path.exists(os.path.join(root, rel)):
            out.append((label, '../' + rel.replace('\\', '/')))
    return out if os.path.isdir(d) else []


def movie_html(m, st, when):
    bink = tx(m, 'bink-file')
    texts = []
    for t in m:
        if len(t) and t.find('text-string') is not None:
            s = clean(st.get(tx(t, 'text-string'), tx(t, 'text-string')))
            start = tx(t, 'start-delay') or tx(t, '_f724cb8c')
            dur = tx(t, 'display-time')
            texts.append('<div class="sub"><span class="t">%s s, %s s on screen</span><pre>%s</pre></div>' % (
                html.escape(start or '?'), html.escape(dur or '?'), html.escape(s.strip())))
    return '<li><b>%s</b> <span class="dim">(%s)</span>%s</li>' % (html.escape(bink), when, ''.join(texts))


def objectives(lv, st):
    rows = []
    for kind in ('primary-objectives', 'secondary-objectives'):
        for o in lv.findall(kind):
            path = '.'.join((p.text or '').strip() for p in o.findall('PATH'))
            text = clean(st.get(tx(o, 'display-id'), tx(o, 'display-id')))
            pts = tx(o, 'reward') or tx(o, '_f71c3e99')
            rows.append('<tr><td>%s</td><td>%s</td><td class="n">%s</td><td class="dim">%s</td></tr>' % (
                'Primary' if kind.startswith('p') else 'Secondary', html.escape(text), html.escape(pts),
                html.escape(path)))
    if not rows: return ''
    return ('<h4>Objectives</h4><table><tr><th></th><th>Objective</th><th>Points</th><th>Game flag</th></tr>%s'
            '</table>' % ''.join(rows))


def credits(lv, st):
    c = lv.find('credits')
    if c is None or not len(c): return ''
    out = []
    for e in c:
        if e.tag == 'title':
            out.append('<p><b>%s</b></p>' % html.escape(clean(st.get((e.text or '').strip(), e.text or ''))))
        elif e.tag == 'group':
            head = clean(st.get(tx(e, 'heading'), tx(e, 'heading')))
            names = [(n.text or '').strip() for n in e.findall('name')]
            out.append('<p><i>%s</i><br>%s</p>' % (html.escape(head), html.escape(', '.join(names))))
    return '<details><summary>Credits shown after this mission</summary>%s</details>' % ''.join(out)


CSS = '''body{font:14px/1.45 system-ui,sans-serif;margin:16px auto;max-width:980px;padding:0 16px;background:#15171b;color:#ddd}
h1{font-size:24px}h2{margin-top:36px;border-bottom:1px solid #333;padding-bottom:4px}h3{margin:22px 0 6px}
h4{margin:12px 0 4px;color:#bbb}.dim{color:#888}.n{text-align:right}a{color:#8cb4ff}
table{border-collapse:collapse;margin:4px 0}td,th{padding:2px 10px 2px 0;text-align:left;vertical-align:top}
.sec{margin:6px 0 6px 0;padding:6px 10px;background:#1c1f25;border-radius:6px}
.sub{margin:4px 0 4px 16px}.sub pre{white-space:pre-wrap;font:12px/1.4 ui-monospace,monospace;margin:2px 0;color:#cfc}
.t{font-size:12px;color:#999}ul{margin:4px 0;padding-left:20px}details{margin:8px 0;color:#aaa}
.mission{padding:4px 0 10px}nav a{margin-right:12px}'''


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]; lang = argv[1] if len(argv) > 1 else 'en'
    st = strings(root, lang)
    camp = os.path.join(root, 'common', 'campaign-bf.xml')
    r = ET.parse(camp).getroot()
    squad = [tx(s, 'char-id') for s in r.findall('squad-char')]
    parts = ['<h1>%s: mission guide</h1>' % html.escape(tx(r, 'display-name-text') or 'Brute Force'),
             '<p>From the game\'s mission list (<code>campaign-bf</code>). The squad: %s. Links open the files '
             'the other exporters made, when they have been run.</p>' % html.escape(', '.join(squad))]
    groups = {}
    for lv in r.findall('level'):
        groups.setdefault(KIND.get(lv.get('type'), lv.get('type')), []).append(lv)
    parts.append('<nav>%s</nav>' % ''.join('<a href="#%s">%s (%d)</a>' % (k.replace(' ', '_'), k, len(v))
                                            for k, v in groups.items()))
    n_missions = 0
    for kind, lvs in groups.items():
        parts.append('<h2 id="%s">%s</h2>' % (kind.replace(' ', '_'), kind))
        for i, lv in enumerate(lvs, 1):
            n_missions += 1
            name = clean(st.get(lv.get('display-name-id'), lv.get('display-name-id')))
            parts.append('<div class="mission"><h3>%d. %s</h3>' % (i, html.escape(name)))
            for sec in (lv.findall('zone') or lv.findall('_0ef96a81')):
                lvl = tx(sec, 'file-name').lower()
                links = level_links(root, lvl)
                starts = [(c.text or '').strip() for c in sec.findall('_ed54d255')]
                h = ['<div class="sec"><b>Level %s</b>' % html.escape(lvl)]
                if links:
                    h.append(' &nbsp;' + ' · '.join('<a href="%s">%s</a>' % (html.escape(p), html.escape(l))
                                                    for l, p in links))
                if starts:
                    h.append('<div class="dim">Squad at the start: %s</div>' % html.escape(', '.join(starts)))
                movies = [movie_html(m, st, 'before') for m in sec.findall('pre-movies')]
                movies += [movie_html(m, st, 'after') for m in sec.findall('post-movies')]
                if movies: h.append('<ul>%s</ul>' % ''.join(movies))
                h.append('</div>')
                parts.append(''.join(h))
            parts.append(objectives(lv, st))
            parts.append(credits(lv, st))
            parts.append('</div>')
    page = ('<!doctype html><html><head><meta charset="utf-8"><title>Brute Force mission guide</title>'
            '<meta name="viewport" content="width=device-width,initial-scale=1"><style>%s</style></head><body>%s'
            '</body></html>' % (CSS, ''.join(parts)))
    out = os.path.join(root, 'common', 'campaign.html')
    open(out, 'w', encoding='utf-8').write(page)
    print('%d missions and maps written to %s' % (n_missions, out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
