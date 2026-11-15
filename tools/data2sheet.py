r"""Brute Force (Xbox) game data sheet: weapons, characters and items -> HTML + CSV.

Reads every object type (objecttypes-*.xml in common, mp_common and each level) and
writes the game's design numbers as tables:
  common\game_data.html            one page, sortable tables (click a column header)
  common\game_data_weapons.csv     weapons
  common\game_data_characters.csv  characters (squad, enemies, creatures)
  common\game_data_items.csv       pickups, ammo, health, grenades and other items

Usage:
    python data2sheet.py <data folder> [language]     (language: en, de, fr, es, it)

How it reads the data (reverse-engineered):
  Each object type is an element whose <BASE> chain holds the shared fields (name,
  object-type, combat-target hit points and shield). Weapons add weapon-type, a <Damage>
  block (min, max, damage-type, radius), a <bullet> block (speed, range, bullet-type),
  reload-time, ammo-count, ammo-type, accuracy, burst-delay, burst-spread, recoil-per-shot.
  Characters add max-stamina, four movement speeds, radar-range, an <inventory> with
  weapon-name entries and an <ai> block. The on-screen name is the stringtable-name
  looked up in the string table.
  Many types are copied into every level that uses them; when a level's copy has different
  numbers, the table shows the most common value and marks the cell with an asterisk
  (the tooltip lists every value and the levels).
Pure Python standard library.
"""
import collections, csv, glob, html, os, re, sys
import xml.etree.ElementTree as ET


def text(el, path):
    if el is None: return ''
    c = el.find(path)
    return (c.text or '').strip() if c is not None else ''


def load_strings(root, lang):
    st = {}
    for p in glob.glob(os.path.join(root, 'common', '**', 'string-table-%s.xml' % lang), recursive=True):
        t = open(p, encoding='iso-8859-1').read()
        for i, v in re.findall(r'<id>([^<]*)</id>\s*<value>(.*?)</value>', t, re.S):
            st[i.strip()] = re.sub(r'\^c\d|\^f\(|\^\)', '', html.unescape(v)).strip()
    return st


def collect_types(root):
    """name -> list of (level, object-type, outer element)"""
    out = collections.defaultdict(list)
    files = sorted(glob.glob(os.path.join(root, '*', 'objecttypes-*.xml')))
    for f in files:
        level = os.path.basename(os.path.dirname(f))
        try: r = ET.parse(f).getroot()
        except ET.ParseError: continue
        par = {c: p for p in r.iter() for c in p}
        for b in r.iter('BASE'):
            ot = text(b, 'object-type')
            if not ot or b.find('name') is None: continue
            e = b
            while e in par and par[e].tag == 'BASE': e = par[e]
            e = par.get(e, e)
            out[text(b, 'name')].append((level, ot, e))
    return out


def first(e, tag):
    for x in e.iter(tag): return x
    return None


def name_of(n):
    return n.lstrip('#') if n.startswith('#') else n


def merged(rows_by_level):
    """[(level, value)] -> (most common value, note or '')"""
    vals = collections.Counter(v for _, v in rows_by_level)
    if not vals: return '', ''
    best = vals.most_common(1)[0][0]
    if len(vals) == 1: return best, ''
    by = collections.defaultdict(list)
    for lv, v in rows_by_level: by[v].append(lv)
    return best, '; '.join('%s: %s' % (v or '-', ', '.join(sorted(set(l)))) for v, l in by.items())


WEAPON_COLS = [
    ('display', 'Name'), ('type', 'Type name'), ('weapon-type', 'Class'), ('damage-min', 'Damage min'),
    ('damage-max', 'Damage max'), ('damage-type', 'Damage type'), ('radius', 'Splash radius'),
    ('bullet-type', 'Bullet'), ('speed', 'Bullet speed'), ('range', 'Range'), ('reload-time', 'Reload (s)'),
    ('ammo-count', 'Max ammo'), ('ammo-type', 'Ammo'), ('accuracy-min', 'Accuracy min'),
    ('accuracy-max', 'Accuracy max'), ('burst-delay', 'Burst delay'), ('burst-spread', 'Burst spread'),
    ('recoil', 'Recoil per shot'), ('levels', 'Levels')]


def weapon_row(e):
    acc = e.find('accuracy')
    if acc is None: acc = e.find('_fb327295')      # same field before the names were recovered
    return {
        'weapon-type': text(e, 'weapon-type').replace('WTYPE_', ''),
        'damage-min': text(e, 'Damage/min'), 'damage-max': text(e, 'Damage/max'),
        'damage-type': text(e, 'Damage/damage-type').replace('DTYPE_', ''),
        'radius': text(e, 'Damage/radius'),
        'bullet-type': text(e, 'bullet/bullet-type').replace('BTYPE_', ''),
        'speed': text(e, 'bullet/speed'), 'range': text(e, 'bullet/range'),
        'reload-time': text(e, 'reload-time'), 'ammo-count': text(e, 'ammo-count'),
        'ammo-type': re.sub(r'^AMMO_\d+_', '', text(e, 'ammo-type')),
        'accuracy-min': text(acc, 'min'), 'accuracy-max': text(acc, 'max'),
        'burst-delay': text(e, 'burst-delay') or text(e, '_e6c60892'),
        'burst-spread': text(e, 'burst-spread') or text(e, '_e704fd69'),
        'recoil': text(acc, 'recoil-per-shot') or text(acc, '_10b6f49b')}


CHAR_COLS = [
    ('display', 'Name'), ('type', 'Type name'), ('hitpoints', 'Hit points'), ('target-type', 'Target type'),
    ('shield', 'Shield'), ('shield-charge', 'Shield charge'), ('stamina', 'Max stamina'),
    ('speed1', 'Speed 1'), ('speed2', 'Speed 2'), ('speed3', 'Speed 3'), ('speed4', 'Speed 4'),
    ('radar-range', 'Radar range'), ('weapons', 'Weapons'), ('motion', 'Animation set'),
    ('skill', 'Combat skill'), ('levels', 'Levels')]


def char_row(e, wname):
    speeds, blk = [], None
    for c in e:
        if len(c) and all(x.tag == 'speed' for x in c): blk = c
    if blk is not None: speeds = [(x.text or '').strip() for x in blk]
    speeds += [''] * (4 - len(speeds))
    inv = e.find('inventory')
    weapons = [wname((w.text or '').strip()) for w in inv.iter('weapon-name')] if inv is not None else []
    return {
        'hitpoints': text(e, './/combat-target/hitpoints'),
        'target-type': text(e, './/combat-target/target-type').replace('TTYPE_', ''),
        'shield': text(e, './/combat-target/shield/shield-type').replace('EST_', ''),
        'shield-charge': text(e, './/combat-target/shield/max-charge'),
        'stamina': text(e, 'max-stamina'),
        'speed1': speeds[0], 'speed2': speeds[1], 'speed3': speeds[2], 'speed4': speeds[3],
        'radar-range': text(e, './/radar-range'),
        'weapons': ', '.join(w for w in weapons if w),
        'motion': text(e, 'motion-archetype'),
        'skill': text(e, 'combat-skill/skill-set')}


ITEM_COLS = [
    ('display', 'Name'), ('type', 'Type name'), ('object-type', 'Kind'), ('class', 'Class'), ('group', 'Group'),
    ('stack', 'Stack limit'), ('damage-max', 'Damage'), ('damage-type', 'Damage type'), ('radius', 'Radius'),
    ('levels', 'Levels')]


def item_row(e, ot):
    dmg = first(e, 'Damage')
    return {'object-type': ot.replace('OT_', ''), 'class': text(e, './/class-type').replace('ICLASS_', ''),
            'group': text(e, './/group-type').replace('IG_', ''), 'stack': text(e, './/stack-limit'),
            'damage-max': text(dmg, 'max'), 'damage-type': text(dmg, 'damage-type').replace('DTYPE_', ''),
            'radius': text(dmg, 'radius')}


def build(types, kinds, rowfn, strings):
    rows = []
    for name, copies in sorted(types.items()):
        copies = [c for c in copies if c[1] in kinds]
        if not copies: continue
        per = collections.defaultdict(list)
        disp = ''
        for level, ot, e in copies:
            r = rowfn(e, ot)
            for k, v in r.items(): per[k].append((level, v))
            sn = text(e, './/stringtable-name')
            if sn and sn != 'unknown' and not disp:
                disp = strings.get(sn, strings.get('#' + sn, ''))
        row, notes = {'type': name_of(name), 'display': disp,
                      'levels': str(len(set(l for l, _, _ in copies)))}, {}
        for k, lst in per.items():
            row[k], notes[k] = merged(lst)
        rows.append((row, notes))
    return rows


def write_csv(path, cols, rows):
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow([h for _, h in cols])
        for r, _ in rows: w.writerow([r.get(k, '') for k, _ in cols])


def table(title, cols, rows, intro):
    h = ['<h2>%s <small>(%d)</small></h2><p>%s</p><div class="wrap"><table class="sort"><thead><tr>' % (
        html.escape(title), len(rows), intro)]
    h += ['<th>%s</th>' % html.escape(t) for _, t in cols] + ['</tr></thead><tbody>']
    for r, notes in rows:
        h.append('<tr>')
        for k, _ in cols:
            v = html.escape(r.get(k, '') or '')
            n = notes.get(k, '')
            h.append('<td title="%s">%s*</td>' % (html.escape(n), v) if n else '<td>%s</td>' % v)
        h.append('</tr>')
    h.append('</tbody></table></div>')
    return ''.join(h)


PAGE = '''<!doctype html><html><head><meta charset="utf-8"><title>Brute Force game data</title><style>
body{font:13px system-ui,sans-serif;margin:16px;background:#14161a;color:#ddd}h1{font-size:20px}h2{margin-top:28px}
small{color:#888;font-weight:normal}p{color:#aaa;max-width:900px}.wrap{overflow-x:auto}
table{border-collapse:collapse;white-space:nowrap}th,td{padding:3px 8px;border-bottom:1px solid #2a2e35;text-align:left}
th{position:sticky;top:0;background:#20242b;cursor:pointer;user-select:none}tr:hover td{background:#1d2128}
td[title]{color:#f2c46d}</style></head><body><h1>Brute Force: game data</h1>
<p>Read from the game's object types. Click a column header to sort. A value with an asterisk differs between
levels: the most common value is shown, and its tooltip lists every value with its levels.</p>%s
<script>document.querySelectorAll('table.sort th').forEach(function(th,i){th.onclick=function(){
var tb=th.closest('table').tBodies[0],rows=[].slice.call(tb.rows),d=th.dataset.d=th.dataset.d=='1'?'-1':'1';
rows.sort(function(a,b){var x=a.cells[i].textContent.replace('*',''),y=b.cells[i].textContent.replace('*','');
var nx=parseFloat(x),ny=parseFloat(y);if(!isNaN(nx)&&!isNaN(ny))return (nx-ny)*d;return x.localeCompare(y)*d});
rows.forEach(function(r){tb.appendChild(r)})}})</script></body></html>'''


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]; lang = argv[1] if len(argv) > 1 else 'en'
    strings = load_strings(root, lang)
    types = collect_types(root)
    disp_of = {}
    for name, copies in types.items():
        for _, _, e in copies:
            sn = text(e, './/stringtable-name')
            if sn and sn != 'unknown':
                disp_of[name] = strings.get(sn, ''); break
    wname = lambda n: disp_of.get(n) or name_of(n)
    weapons = build(types, {'OT_WEAPON'}, lambda e, ot: weapon_row(e), strings)
    weapons.sort(key=lambda r: (r[0]['display'] == '', r[0]['weapon-type'], r[0]['display'] or r[0]['type']))
    chars = build(types, {'OT_CHARACTER'}, lambda e, ot: char_row(e, wname), strings)
    chars.sort(key=lambda r: (r[0]['display'] == '', r[0]['display'] or r[0]['type']))
    items = build(types, {'OT_INVENTORY', 'OT_INVENTORY_GRENADE'}, item_row, strings)
    items.sort(key=lambda r: (r[0]['display'] == '', r[0]['object-type'], r[0]['display'] or r[0]['type']))
    out = os.path.join(root, 'common')
    write_csv(os.path.join(out, 'game_data_weapons.csv'), WEAPON_COLS, weapons)
    write_csv(os.path.join(out, 'game_data_characters.csv'), CHAR_COLS, chars)
    write_csv(os.path.join(out, 'game_data_items.csv'), ITEM_COLS, items)
    body = (table('Weapons', WEAPON_COLS, weapons,
                  'Weapon types carried by players and enemies. Type names without a display name are built-in '
                  'attacks (explosions, creature attacks, the power blade).')
            + table('Characters', CHAR_COLS, chars,
                    'The squad, enemies and creatures. The four speeds are the movement speeds of the character '
                    'type (their exact order, walk to dash, is inferred).')
            + table('Items', ITEM_COLS, items, 'Pickups, ammunition, health packs, grenades and other inventory items.'))
    open(os.path.join(out, 'game_data.html'), 'w', encoding='utf-8').write(PAGE % body)
    print('%d weapons, %d characters, %d items written to %s' % (len(weapons), len(chars), len(items),
                                                                 os.path.join(out, 'game_data.html')))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
