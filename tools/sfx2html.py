r"""Brute Force (Xbox) sound effect browser: where every sound cue is used -> one HTML page.

Writes common\sound_effects.html with
  - Weapons: the fire, reload and other sounds of every weapon type
  - Materials: bullet-hit sounds per material and ammo type, footsteps per foot type,
    landing, sliding and collision sounds (from world-materials.xml)
  - Objects: the sounds of every other object type (explosions, pickups, skills, jumps ...)
  - Levels: the placed sound objects of each level (global / positional / indoor / outdoor)
  - Unused: cues that are defined but referenced by none of the above
Each sound has a player for its .wav from export_sounds.bat (answer y to PCM so browsers
can play it). Unnamed cues show as #hash.

Usage:
    python sfx2html.py <data folder>
Pure Python standard library.
"""
import collections, csv, glob, html, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = {}
try:
    NAMES = {'#%08x' % int(k, 16): v for k, v in json.load(open(os.path.join(HERE, 'names.json'))).items()}
except Exception:
    pass


def norm(v):
    """A sound reference as written in the XML (a name or #hash) -> display name."""
    v = (v or '').strip()
    return NAMES.get(v.lower(), v) if v.startswith('#') else v


def wav_index(root):
    idx = {}
    p = os.path.join(root, 'sounds_wav', '_sounds.csv')
    if os.path.exists(p):
        for row in csv.DictReader(open(p, encoding='utf-8')):
            if row.get('wav'):
                path = '../sounds_wav/' + row['wav'].replace('\\', '/')
                for key in (row.get('sound', ''), norm(row.get('id', ''))):
                    if key and key not in idx: idx[key] = path
    return idx


def read(p):
    return open(p, encoding='iso-8859-1').read()


# ---------------------------------------------------------------- materials
def materials(root):
    files = sorted(glob.glob(os.path.join(root, '*', 'world-materials.xml')))
    out = collections.OrderedDict()
    if not files: return out
    t = read(files[0])
    for blk in re.split(r'\n  <world-material>', t)[1:]:
        m = re.search(r'<world-material-id>(\w+)<', blk)
        if not m: continue
        own = m.group(1).replace('WMAT_', '').lower()
        d = {'hit': collections.OrderedDict(), 'foot': collections.OrderedDict(), 'slide': [], 'land': [],
             'collide': collections.OrderedDict()}
        for ammo, hs in re.findall(r'<ammo-type>(\w+)</ammo-type>((?:\s*<hit-sound>[^<]*</hit-sound>)+)', blk):
            vals = tuple(norm(v) for v in re.findall(r'<hit-sound>([^<]*)</hit-sound>', hs) if v.strip())
            if vals:
                d['hit'].setdefault(vals, []).append(re.sub(r'^AMMO_\d+_', '', ammo).lower())
        for key, el in (('slide', 'slide'), ('land', 'jump-land')):
            s = re.search(r'<%s>(.*?)</%s>' % (el, el), blk, re.S)
            if s: d[key] = [norm(v) for v in re.findall(r'<sound-effect>([^<]+)</sound-effect>', s.group(1))]
        for other, body in re.findall(r'<\w+>\s*<world-material-id>(\w+)</world-material-id>\s*<collision>(.*?)</collision>', blk, re.S):
            vals = tuple(norm(v) for v in re.findall(r'<sound-effect>([^<]+)</sound-effect>', body))
            if vals: d['collide'].setdefault(vals, []).append(other.replace('WMAT_', '').lower())
        for idx, body in re.findall(r'<_030cac28>(\d+)</_030cac28>(.*?)</_eb5cba39>', blk, re.S):
            vals = tuple(norm(v) for v in re.findall(r'<sound-effect>([^<]+)</sound-effect>', body))
            if vals: d['foot'].setdefault(vals, []).append(int(idx))
        out[own] = d
    return out


# ---------------------------------------------------------------- object types
WEAPON_TAGS = {'fire-sound-name': 'fire', 'reload-sound-name': 'reload', 'projectile-sound-name': 'projectile',
               '_e6acaff7': 'fire (alt)', '_150df2e5': 'other', 'snipe-sound': 'snipe'}


def object_sounds(root):
    weapons = collections.defaultdict(lambda: collections.defaultdict(set))
    objects = collections.defaultdict(lambda: collections.defaultdict(set))
    for f in sorted(glob.glob(os.path.join(root, '*', 'objecttypes-*.xml'))):
        t = read(f)
        for blk in re.split(r'\n    <(?:_e275fb80|items)>', t)[1:]:
            nm = re.search(r'<name>([^<]+)</name>', blk)
            name = norm(nm.group(1)) if nm else '?'
            for tg, v in re.findall(r'<([\w\-]+)>([^<]+)</\1>', blk):
                if not v.strip() or not (v.startswith('#') or tg in WEAPON_TAGS or 'sound' in tg or tg.endswith('skill')):
                    continue
                if tg in WEAPON_TAGS and tg not in ('snipe-sound',):
                    weapons[name][WEAPON_TAGS[tg]].add(norm(v))
                elif 'sound' in tg or tg.endswith('skill') or tg in ('_153f90c9', '_1629a90a', '_1b484e55'):
                    objects[name][tg.replace('-name', '')].add(norm(v))
    return weapons, objects


def level_sounds(root):
    out = collections.OrderedDict()
    for f in sorted(glob.glob(os.path.join(root, '*', 'levels-*.xml'))):
        lvl = os.path.basename(f)[len('levels-'):-4]
        t = read(f)
        rows = collections.Counter()
        for typ, snd in re.findall(r'<type>(SND_IS_\w+)</type>\s*<hardpoint>[^<]*</hardpoint>\s*<sound-object>([^<]+)</sound-object>', t):
            rows[(norm(snd), typ.replace('SND_IS_', '').lower())] += 1
        if rows: out[lvl] = rows
    return out


def defined_cues(root):
    cues = collections.defaultdict(set)
    for f in glob.glob(os.path.join(root, '*', 'sounds-*.xml')):
        lvl = os.path.basename(f)[len('sounds-'):-4]
        for sid in re.findall(r'<SOUND id="([^"]+)"', read(f)):
            cues[norm(sid)].add(lvl)
    return cues


CSS = '''body{font:14px/1.4 system-ui,sans-serif;margin:16px auto;max-width:1150px;padding:0 16px;background:#15171b;color:#ddd}
h2{margin-top:30px;border-bottom:1px solid #333}h3{margin:14px 0 4px}a{color:#8cb4ff}.dim{color:#888}
table{border-collapse:collapse;width:100%}td,th{padding:3px 8px;border-bottom:1px solid #262a31;vertical-align:top;text-align:left}
.s{display:inline-flex;align-items:center;gap:4px;margin:1px 10px 1px 0}.nm{font:12px ui-monospace,monospace}
audio{height:24px;width:150px}nav a{margin-right:12px}details{margin:6px 0}summary{cursor:pointer}.wrap{overflow-x:auto}'''


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]
    wavs = wav_index(root)
    used = set()

    def many(vals):
        """Each sound of a list once, with xN when the list repeats it."""
        c = collections.Counter(vals)
        return ''.join(snd(n) + ('<span class="dim">x%d</span> ' % c[n] if c[n] > 1 else '')
                       for n in dict.fromkeys(vals))

    def snd(n):
        used.add(n)
        p = wavs.get(n)
        label = '<span class="nm">%s</span>' % html.escape(n)
        if not p: return '<span class="s">%s</span>' % label
        return '<span class="s"><a class="nm" href="%s">%s</a><audio controls preload="none" src="%s"></audio></span>' % (
            html.escape(p), html.escape(n), html.escape(p))

    mats = materials(root)
    weapons, objects = object_sounds(root)
    levels = level_sounds(root)
    cues = defined_cues(root)
    P = ['<h1>Brute Force: sound effects</h1><p>Every sound cue, listed by where the game uses it. %s Unnamed cues show as #hash.</p>' % (
        'Players need the sounds exported as plain PCM (export_sounds.bat, answer y).' if wavs else
        'Run export_sounds.bat (answer y to PCM), then this exporter again, to get players.'),
         '<nav><a href="#weapons">Weapons</a><a href="#materials">Materials</a><a href="#objects">Objects</a>'
         '<a href="#levels">Levels</a><a href="#unused">Not referenced</a></nav>']
    # weapons
    P.append('<h2 id="weapons">Weapons (%d)</h2><div class="wrap"><table><tr><th>Weapon</th><th>Sounds</th></tr>' % len(weapons))
    for w in sorted(weapons):
        parts = ''.join('<div><span class="dim">%s:</span> %s</div>' % (k, ''.join(snd(n) for n in sorted(v)))
                        for k, v in sorted(weapons[w].items()))
        P.append('<tr><td><b>%s</b></td><td>%s</td></tr>' % (html.escape(w), parts))
    P.append('</table></div>')
    # materials
    P.append('<h2 id="materials">Materials (%d)</h2><p class="dim">From world-materials.xml. Foot types are the '
             '<code>_030cac28</code> index of each footstep set: 2 is the ferals and 4 the Shrikes (from the names of '
             'their sounds); 0, 3 and 5 share one set.</p>' % len(mats))
    for m, d in mats.items():
        rows = []
        for vals, ammo in d['hit'].items():
            rows.append('<tr><td>hit: %s</td><td>%s</td></tr>' % (html.escape(', '.join(ammo)), many(vals)))
        for vals, feet in d['foot'].items():
            rows.append('<tr><td>footsteps, foot type %s</td><td>%s</td></tr>' % (
                html.escape(', '.join(map(str, sorted(feet)))), many(vals)))
        for key in ('land', 'slide'):
            if d[key]: rows.append('<tr><td>%s</td><td>%s</td></tr>' % ('landing' if key == 'land' else 'sliding',
                                                                       many(d[key])))
        for vals, others in d['collide'].items():
            rows.append('<tr><td>collision with %s</td><td>%s</td></tr>' % (html.escape(', '.join(others)),
                                                                            many(vals)))
        P.append('<details><summary><b>%s</b> <span class="dim">(%d entries)</span></summary><table>%s</table></details>' % (
            html.escape(m), len(rows), ''.join(rows)))
    # objects
    P.append('<h2 id="objects">Objects (%d)</h2><div class="wrap"><table><tr><th>Object type</th><th>Sounds</th></tr>' % len(objects))
    for o in sorted(objects):
        parts = ''.join('<div><span class="dim">%s:</span> %s</div>' % (html.escape(k), ''.join(snd(n) for n in sorted(v)))
                        for k, v in sorted(objects[o].items()))
        P.append('<tr><td>%s</td><td>%s</td></tr>' % (html.escape(o), parts))
    P.append('</table></div>')
    # levels
    P.append('<h2 id="levels">Placed sounds by level</h2><p class="dim">Sound objects placed in each level, with how many '
             'times each is placed and how it plays (global, positional, indoor, outdoor).</p>')
    for lvl, rows in levels.items():
        body = ''.join('<tr><td>%s</td><td>%s</td><td class="dim">%d</td></tr>' % (snd(n), html.escape(t), c)
                       for (n, t), c in sorted(rows.items()))
        P.append('<details><summary>%s <span class="dim">(%d sounds)</span></summary><table>%s</table></details>' % (
            html.escape(lvl), len(rows), body))
    # unused (ignoring dialogue, which the dialogue page lists)
    rest = sorted(n for n in cues if n not in used and not n.startswith('dx'))
    P.append('<h2 id="unused">Not referenced (%d)</h2><p class="dim">Defined in a level\'s sound list but not used by any '
             'weapon, material, object or placed sound object. Scripts and triggers may still play them. Dialogue '
             '(dx...) is on the dialogue page.</p><div>%s</div>' % (len(rest), ''.join(snd(n) for n in rest)))
    out = os.path.join(root, 'common', 'sound_effects.html')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'w', encoding='utf-8').write('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" '
                                           'content="width=device-width,initial-scale=1"><title>Brute Force sound effects'
                                           '</title><style>%s</style></head><body>%s</body></html>' % (CSS, ''.join(P)))
    print('%d weapons, %d materials, %d object types, %d levels, %d unreferenced cues -> %s' % (
        len(weapons), len(mats), len(objects), len(levels), len(rest), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
