r"""Brute Force (Xbox) data index: one start page for everything the exporters made.

Writes index.html at the top of the data folder. It links the overview pages (mission guide,
game data sheet, dialogue browser, effect viewer, menus) and lists every level with its
mission name and links to its exported files: 3D scene, mission logic and graph, models,
textures, animations, music, dialogue and effect sounds. Only files that exist are linked,
so run it last (export_all.bat does).

Usage:
    python make_index.py <data folder>
Pure Python standard library.
"""
import html, os, re, sys
import xml.etree.ElementTree as ET


def mission_names(root):
    """level folder -> (mission name, kind, mission number, section number)."""
    out = {}
    camp = os.path.join(root, 'common', 'campaign-bf.xml')
    st = {}
    p = os.path.join(root, 'common', 'stringtable', 'string-table-en.xml')
    if os.path.exists(p):
        t = open(p, encoding='iso-8859-1').read()
        for i, v in re.findall(r'<id>([^<]*)</id>\s*<value>(.*?)</value>', t, re.S):
            st[i.strip()] = html.unescape(v)
    if not os.path.exists(camp): return out
    kinds = {'#17506391': 'Campaign', '#120785ef': 'Deathmatch', '#efb18d28': 'Squad deathmatch'}
    count = {}
    for lv in ET.parse(camp).getroot().findall('level'):
        kind = kinds.get(lv.get('type'), '')
        count[kind] = count.get(kind, 0) + 1
        name = re.sub(r'\^c\d|\^f\(\d?|\^\)', '', st.get(lv.get('display-name-id'), lv.get('display-name-id') or ''))
        for j, sec in enumerate(lv.findall('zone') or lv.findall('_0ef96a81'), 1):
            fn = (sec.findtext('file-name') or '').strip().lower()
            if fn: out[fn] = (name, kind, count[kind], j)
    return out


PAGES = [('common/campaign.html', 'Mission guide', 'every mission: levels, briefings, objectives, credits', 'export_campaign.bat'),
         ('common/game_data.html', 'Game data', 'weapons, characters and items with their stats', 'export_game_data.bat'),
         ('common/dialogue.html', 'Dialogue', 'every voice line, with players', 'export_dialogue.bat'),
         ('common/sound_effects.html', 'Sound effects', 'every sound effect by where it is used: weapons, materials, objects, levels', 'export_sound_effects.bat'),
         ('common/effects_viewer.html', 'Effect viewer', 'the 603 particle effects, playing in 3D', 'export_effect_viewer.bat'),
         ('common/game-options-en_menus.html', 'Menus', 'the 64 front-end menus, rebuilt', 'export_menus.bat'),
         ('characters_glb', 'Characters', 'rigged characters with their animations (.glb)', 'export_characters.bat'),
         ('sounds_wav', 'Sounds', 'music, dialogue and effects as .wav, plus _sounds.csv', 'export_sounds.bat'),
         ('common/effects-common_json', 'Effects (JSON)', 'the effect library as JSON', 'export_effects.bat')]

LEVEL_FILES = [('scene', '{l}/{l}_scene.glb'), ('logic', '{l}/levels-{l}_logic.txt'), ('graph', '{l}/levels-{l}_logic.dot'),
               ('models', '{l}/objects-{l}.obj'), ('textures', '{l}/textures-{l}_png'),
               ('animations', '{l}/animations-{l}_bvh'), ('music', 'sounds_wav/music/{l}'),
               ('dialogue', 'sounds_wav/en/{l}'), ('effect sounds', 'sounds_wav/memory/{l}')]

CSS = '''body{font:14px/1.45 system-ui,sans-serif;margin:16px auto;max-width:1100px;padding:0 16px;background:#15171b;color:#ddd}
h1{font-size:24px}h2{margin-top:30px;border-bottom:1px solid #333;padding-bottom:4px}a{color:#8cb4ff}.dim{color:#888}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:10px}
.card{background:#1c1f25;border-radius:8px;padding:10px 12px}.card b{display:block;margin-bottom:2px}
table{border-collapse:collapse;width:100%}td,th{padding:4px 8px;border-bottom:1px solid #262a31;text-align:left;vertical-align:top}
td.l a{margin-right:8px;white-space:nowrap}.wrap{overflow-x:auto}'''


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]
    names = mission_names(root)
    parts = ['<h1>Brute Force: exported data</h1><p class="dim">Start page for the files the Brute Force recomp '
             'exporters wrote into this folder. Links appear for what has been exported; the batch file that makes '
             'each one is named next to anything missing.</p><h2>Overview</h2><div class="cards">']
    for path, title, desc, bat in PAGES:
        if os.path.exists(os.path.join(root, path)):
            parts.append('<div class="card"><b><a href="%s">%s</a></b>%s</div>' % (html.escape(path), title, desc))
        else:
            parts.append('<div class="card"><b>%s</b>%s<div class="dim">run %s</div></div>' % (title, desc, bat))
    parts.append('</div>')
    levels = sorted(d for d in os.listdir(root) if os.path.isfile(os.path.join(root, d, 'levels-%s.xml' % d)))
    order = {'Campaign': 0, 'Deathmatch': 1, 'Squad deathmatch': 2, '': 3}
    levels.sort(key=lambda l: (order.get(names.get(l, ('', '', 0, 0))[1], 3), names.get(l, ('', '', 99, 0))[2],
                               names.get(l, ('', '', 0, 0))[3], l))
    parts.append('<h2>Levels (%d)</h2><div class="wrap"><table><tr><th>Level</th><th>Mission</th><th>Files</th></tr>' % len(levels))
    for l in levels:
        nm, kind, num, sec = names.get(l, ('', '', 0, 0))
        mission = ('%s %d: %s' % (kind, num, nm) + (' (part %d)' % sec if sec > 1 else '')) if nm else ''
        links = []
        for label, pat in LEVEL_FILES:
            rel = pat.format(l=l)
            if os.path.exists(os.path.join(root, rel)):
                links.append('<a href="%s">%s</a>' % (html.escape(rel), label))
        parts.append('<tr><td><b>%s</b></td><td>%s</td><td class="l">%s</td></tr>' % (
            html.escape(l), html.escape(mission), ''.join(links) or '<span class="dim">nothing exported yet</span>'))
    parts.append('</table></div>')
    out = os.path.join(root, 'index.html')
    open(out, 'w', encoding='utf-8').write('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" '
                                           'content="width=device-width,initial-scale=1"><title>Brute Force data</title>'
                                           '<style>%s</style></head><body>%s</body></html>' % (CSS, ''.join(parts)))
    print('index of %d levels written to %s' % (len(levels), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
