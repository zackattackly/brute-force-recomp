r"""Brute Force (Xbox) dialogue browser: chatter libraries and mission dialogue -> HTML.

Writes common\dialogue.html with
  - every chatter library (what the squad, enemies and creatures say in combat): for each
    situation (line tag) the voice lines the game picks from, the facial expression,
    the chance and the priority
  - the scripted mission dialogue of each level, in order (dxen_<speaker>_<level><number>)
Each line has a player for its .wav file from export_sounds.bat. Browsers only play plain
PCM .wav files, so export the sounds with the PCM option (answer y) to listen in the page;
the links open the files either way.

Usage:
    python chatter2html.py <data folder> [language]

Chatter format (reverse-engineered, characters\<name>_chatter.xmb):
  <chatter> holds entries with attributes line_tag (the situation: ENEMY_SEEN, OUT_OF_AMMO,
  ORDER_FOLLOW ... stored as the name hash), priority, leastnon, a chance in percent
  (_ead503b2) and two timings in seconds (_ea21ae4b, _183b88ed), with <sound_id> children
  (the hash of the sound cue name, which is the wave name) and an <expression> (EFE_*).
  Character types name their library in <chatter-library>.
Pure Python standard library.
"""
import collections, csv, glob, html, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bfhash import bfhash

# Chatter situations recovered from the executable's event names and word lists.
TAGS = '''AFFIRMATIVE SQUAD_REFORM ORDER_FOLLOW ORDER_ALL ORDER_GUARD ORDER_SHADOON ORDER_HOUND ORDER_FIREHOUND
ORDER_PRIEST ORDER_BYPASS ORDER_DISARM ORDER_FLINT ORDER_FOLLOWER ORDER_HAWK ORDER_BRUTUS ORDER_TEX ORDER_HUNTERLORD
ORDER_MOVE_OUT SHOT_BLOCKED DAMAGED_SELF KILLED_ENEMY FRIEND_KILLED RELOAD_WEAPON FRIENDLY_FIRE TARGET_GROUP
TARGET_ENTRENCHED SUPPRESSING_FIRE THROWING_GRENADE ME_TOO MOVING_BACK MOVING_FORWARD TAUNT_ENEMY FALLING_BACK
FLINT_KILLED HAWK_KILLED BRUTUS_KILLED TEX_KILLED SPECIAL_ATTACK_KILL OUT_OF_AMMO OUT_OF_CHARGE SEEN_DEAD_ENEMY
SEEN_DEAD_FRIENDLY TARGET_LOST_SIGHT HEARD_WEAPON_FIRE ENEMY_NOISE ENEMY_SEEN GRENADE_NEAR SHOT_AT LEADER_KILLED
SOUND_ALARM'''.split()
TAG_NAMES = {'#%08x' % bfhash(t): t for t in TAGS}


def load_names():
    import json
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'names.json')
    try: return {'#%08x' % int(k, 16): v for k, v in json.load(open(p)).items()}   # any key padding
    except Exception: return {}


def wav_index(root, lang):
    """sound name -> path of its .wav relative to common\\ (from sounds_wav\\_sounds.csv)."""
    idx = {}
    p = os.path.join(root, 'sounds_wav', '_sounds.csv')
    if os.path.exists(p):
        for row in csv.DictReader(open(p, encoding='utf-8')):
            if row.get('wav') and row['sound'] not in idx:
                idx[row['sound']] = '../sounds_wav/' + row['wav'].replace('\\', '/')
    return idx


def player(name, wavs):
    p = wavs.get(name)
    if not p: return '<span class="nm">%s</span>' % html.escape(name)
    return ('<span class="nm"><a href="%s">%s</a></span><audio controls preload="none" src="%s"></audio>'
            % (html.escape(p), html.escape(name), html.escape(p)))


def chatter_libs(root):
    libs = {}
    for f in sorted(glob.glob(os.path.join(root, '*', 'characters', '*_chatter.xml')) +
                    glob.glob(os.path.join(root, '*', '*_chatter.xml'))):
        name = os.path.basename(f)[:-4]
        if name in libs and os.sep + 'common' + os.sep not in f: continue
        libs[name] = f
    return libs


def parse_chatter(path, names):
    t = open(path, encoding='iso-8859-1').read()
    out = []
    for m in re.finditer(r'<_f74e6a16 ([^>]*)>(.*?)</_f74e6a16>', t, re.S):
        a = dict(re.findall(r'([\w-]+)="([^"]*)"', m.group(1)))
        body = m.group(2)
        sounds = [names.get(s, s) for s in re.findall(r'sound_id="([^"]*)"', body)]
        sounds = [names.get(s, s) if s.startswith('#') else s for s in sounds]
        expr = re.findall(r'expression_type="([^"]*)"', body)
        tag = a.get('line_tag', '')
        out.append({'tag': TAG_NAMES.get(tag, tag), 'priority': a.get('priority', ''),
                    'chance': a.get('_ead503b2', ''), 'expr': ', '.join(e.replace('EFE_', '') for e in expr),
                    'sounds': sounds})
    return out


CSS = '''body{font:14px/1.4 system-ui,sans-serif;margin:16px auto;max-width:1100px;padding:0 16px;background:#15171b;color:#ddd}
h2{margin-top:30px;border-bottom:1px solid #333}h3{margin:16px 0 4px}a{color:#8cb4ff}.dim{color:#888}
table{border-collapse:collapse;width:100%}td,th{padding:3px 8px;border-bottom:1px solid #262a31;vertical-align:top;text-align:left}
.line{display:flex;align-items:center;gap:8px;margin:1px 0}.nm{min-width:220px;font:12px ui-monospace,monospace}
audio{height:26px}nav a{margin-right:10px}details{margin:6px 0}summary{cursor:pointer}'''


def main(argv):
    if not argv:
        print(__doc__); return 1
    root = argv[0]; lang = argv[1] if len(argv) > 1 else 'en'
    names = load_names()
    wavs = wav_index(root, lang)
    libs = chatter_libs(root)
    parts = ['<h1>Brute Force: dialogue</h1><p>Voice lines from the chatter libraries (combat talk) and the '
             'scripted mission dialogue. %s</p>' % (
                 'Players need the sounds exported as plain PCM (export_sounds.bat, answer y).' if wavs else
                 'Run export_sounds.bat (answer y to PCM) and then this exporter again to get players for every line.')]
    groups = collections.OrderedDict([('Campaign', []), ('Deathmatch (dm_)', []), ('Squad deathmatch (sdm_)', [])])
    for n in sorted(libs):
        groups['Deathmatch (dm_)' if n.startswith('dm_') else 'Squad deathmatch (sdm_)' if n.startswith('sdm_')
               else 'Campaign'].append(n)
    parts.append('<nav><a href="#chatter">Chatter</a><a href="#missions">Mission dialogue</a></nav>'
                 '<h2 id="chatter">Chatter libraries</h2>')
    n_lines = n_tags = 0
    for g, lst in groups.items():
        parts.append('<h3>%s</h3>' % html.escape(g))
        for lib in lst:
            entries = parse_chatter(libs[lib], names)
            rows = []
            for e in entries:
                n_tags += 1; n_lines += len(e['sounds'])
                rows.append('<tr><td><b>%s</b><div class="dim">%s</div></td><td>%s%%</td><td>%s</td><td>%s</td></tr>' % (
                    html.escape(e['tag']), html.escape(e['expr']), html.escape(e['chance']), html.escape(e['priority']),
                    ''.join('<div class="line">%s</div>' % player(s, wavs) for s in e['sounds'])))
            parts.append('<details><summary>%s <span class="dim">(%d situations)</span></summary><table><tr><th>'
                         'Situation</th><th>Chance</th><th>Priority</th><th>Lines</th></tr>%s</table></details>' % (
                             html.escape(lib.replace('_chatter', '')), len(entries), ''.join(rows)))
    # scripted mission dialogue: dxen_/dxmi_ cues per level, from the sound libraries
    parts.append('<h2 id="missions">Mission dialogue</h2><p class="dim">The scripted lines of each level, in the '
                 'order of their numbers (speaker, then level and line number).</p>')
    per = collections.defaultdict(set)
    for f in glob.glob(os.path.join(root, '*', 'sounds-*.xml')):
        lvl = os.path.basename(f)[len('sounds-'):-4]
        for sid in re.findall(r'<SOUND id="([^"]*)"', open(f, encoding='iso-8859-1').read()):
            nm = names.get(sid, sid)
            m = re.match(r'dx(?:en|mi)_([a-z0-9]+)_([a-z]\d\d)(\d+[a-z]?)$', nm)
            if m: per[lvl].add((m.group(2), m.group(3), m.group(1), nm))
    for lvl in sorted(per):
        lines = sorted(per[lvl])
        parts.append('<details><summary>%s <span class="dim">(%d lines)</span></summary>%s</details>' % (
            html.escape(lvl), len(lines), ''.join('<div class="line"><b style="min-width:90px">%s</b>%s</div>' % (
                html.escape(spk.title()), player(nm, wavs)) for _, _, spk, nm in lines)))
    out = os.path.join(root, 'common', 'dialogue.html')
    open(out, 'w', encoding='utf-8').write('<!doctype html><html><head><meta charset="utf-8"><title>Brute Force '
                                           'dialogue</title><style>%s</style></head><body>%s</body></html>' % (
                                               CSS, ''.join(parts)))
    print('%d chatter libraries, %d situations, %d chatter lines, mission dialogue for %d levels -> %s' % (
        len(libs), n_tags, n_lines, len(per), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
