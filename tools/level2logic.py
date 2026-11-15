"""Brute Force (Xbox) mission logic exporter: level trigger networks -> text, JSON, Graphviz.

A level's missions are built from triggers placed in levels-NAME.xml: spawn, timer,
calculator, router, sound, dialog, AI, script, anim, warp, letterbox, level-over and
other trigger objects, wired together by signals. For every level this writes:
    levels-NAME_logic.txt    readable listing, one block per trigger
    levels-NAME_logic.json   the same as data
    levels-NAME_logic.dot    signal graph (open with Graphviz or an online .dot viewer)

Usage:
    python level2logic.py <data folder>

How triggers work (from the game code, Milestone 24):
  * Every trigger has a list of receivers (_e91cf6a8, "receiver") it sends its
    signals to, and the mirror list of senders (_0884288c, "sender") it listens to.
  * A third list (_0b6b92ba, name not recovered) holds the objects the trigger acts on:
    the characters an AI trigger commands, the units a warp trigger moves, the speaker of
    a dialog trigger (a spawn trigger there stands for the character it spawned).
  * Its responses (_031cd9f5) say what to do when a signal arrives:
    "on TRIG_SIG_x [match-id m] do TRIG_ACT_y", at most max-count times
    (_f136d22d; 2147483647 = unlimited).
  * Every trigger understands ENABLE / DISABLE / TOGGLE_ENABLE (and then sends
    TRIG_SIG_ENABLED / TRIG_SIG_DISABLED) and USER_SIG1..8 (re-sent as
    TRIG_SIG_USER_SIGNAL1..8); each trigger type adds its own actions (a timer:
    OPERATE, DISPLAY_TIMER, TIMER_FULL_RESET...; a calculator: INC/DEC/MODIFY_COUNTER).
Object names are hashed in the files; names recovered so far come from names.json
(about half of all level objects: trigger_spawn612, trigger_router523, wpoint88, ...).
Pure Python standard library.
"""
import json, os, re, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bfhash import bfhash

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    NAMES = {hex(int(k, 16)): v for k, v in json.load(open(os.path.join(HERE, 'names.json'))).items()}  # any key padding
except Exception:
    NAMES = {}

RESPONSES, RECEIVERS, SENDERS, ACTS_ON = '_031cd9f5', '_e91cf6a8', '_0884288c', '_0b6b92ba'
FIELD = {'_f136d22d': 'max-count', '_170a9b80': 'flag', '_e2da3955': 'extra'}
COMMON = {'object-type-name', 'name', 'enabled', 'is-destroyed', 'id', RESPONSES, RECEIVERS,
          SENDERS, ACTS_ON, '_137926ff', '_1eec0e75', '_f657f799', '_0a82e377', 'transform'}


# What each trigger type does, from its code (Milestones 24-26). Shown at the top of each
# level's listing for the types that level uses.
TYPE_NOTES = {
    '*': 'Every trigger: ENABLE / DISABLE / TOGGLE_ENABLE (then sends ENABLED / DISABLED to its '
         'receivers) and USER_SIG1..8 (re-sent as USER_SIGNAL1..8). While disabled it only reacts to '
         'these and sends nothing. A rule fires at most max-count times. Detectors (box-detector? with '
         'size, proximity-detector with radius) send ENTER_DETECTOR / EXIT_DETECTOR / EMPTIED_DETECTOR '
         'when objects passing their filter (alignment) come and go; pickups send PICKUP_OBJECT and usable '
         'objects USE_OBJECT. Kinds ending in "?" have unrecovered names and are labelled by what they do.',
    'trigger_router': 'Relays signals. With _f0d6857f false it forwards every signal it receives to its '
         'receivers unchanged; with it true, only signals whose rule says OPERATE, re-sent as '
         'out-signal when that is set.',
    'trigger_spawn': 'OPERATE spawns spawned-object-name at location-world-point (with home-point, '
         'home-range-radius/height and goal-id for its AI), after spawn-delay seconds, then sends '
         'OPERATED (carrying the spawned object) to its receivers. SPAWN_RESET re-arms it.',
    'trigger_timer': 'OPERATE starts a countdown of current-value seconds; at zero it sends TIMER_PULSE '
         'and, with enable-looping, starts again from initial-value. DISPLAY_TIMER / UNDISPLAY_TIMER '
         'show it on screen; TIMER_FULL_RESET and RESET_COUNTDOWN_TIME reset it.',
    'trigger_calculator': 'INC_COUNTER / DEC_COUNTER / MODIFY_COUNTER (by value) / '
         'SET_COUNTER_FROM_SIG_DATA change counter, which is then compared with target using op; '
         'when the test passes it sends CALCULATOR and TRUE (FALSE otherwise, inferred).',
    'trigger_ai': 'OPERATE applies its AI settings to the characters it acts on (personality, '
         'alignment, event, squad command, destination world-point), replacing their current goals.',
    'trigger_warp': 'OPERATE teleports the characters it acts on (or those spawned by the spawn '
         'triggers it acts on) to world-point, fading out over warp-fade-time seconds.',
    'trigger_dialog': 'PLAY_DIALOG / OPERATE makes the character it acts on speak dialog; if that '
         'character is dead it sends FINISHED at once. STOP / PAUSE / RESUME_DIALOG control the line.',
    'trigger_script': 'OPERATE starts the scripted sequence script-name (a cutscene, .gscr).',
    'trigger_level_over': 'OPERATE ends the level. With _18f66101 false it fades to black over half a '
         'second and finishes the level; with it true it hands over to the world object at +0xC4C '
         '(each level has one of each; the true one is probably the mission-failed ending, inferred).',
    'trigger_gamevar': 'OPERATE does out = A op B on game variables (persistent flags such as '
         'missions.m03.<objective>.state): op GVOP_ASSIGN / ADD / SUBTRACT / MULTIPLY / DIVIDE / '
         'RESET_TO_DEFAULT write out; GVOP_EQUAL / LESS / GREATER ... compare A with B and, when out '
         'is "signal", send TRUE or FALSE. Objective flags missions.<mission>.<objective> have '
         '.enabled (1 = shown in the objective list) and .state (0 = open, 1 = done, -1 = failed; '
         'inferred from how the levels use them); their text is shown in [brackets].',
    'trigger_letterbox': 'OPERATE slides the cinematic black bars in over show-duration seconds; STOP '
         'takes them away over hide-duration.',
    'trigger_radar': 'OPERATE gives the objects it acts on a radar marker of type radar-contact '
         '(RCONTACT_OBJECTIVE, _ALIGNMENT, _BOSS_ALIGNMENT, _IMPORTANT_PICKUP, _INVISIBLE ...).',
    'trigger_menu': 'OPERATE opens the menu "menu" and sends OPERATED when the player accepts (inferred).',
    'trigger_zone': 'OPERATE hands over to another level section: target-level-name (field '
         '_14b0089f before Milestone 55, e.g. m05_x).',
    'trigger_event': 'Sends EVENT_MATCH when the game event event-id (filtered by event-filter) '
         'happens (inferred).',
    'trigger_textbox': 'OPERATE shows an on-screen text box (text id first-string-id, field _170f888b before Milestone 56), optionally waiting for a '
         'button (TBP_PRESS / TBP_HOLD of a VB_* button); sends OPERATED when it closes (inferred).',
    'trigger_score': 'OPERATE changes the squad score by amount (inferred).',
    'trigger_squad': 'Changes the player squad (leader-number; inferred).',
    'trigger_damage': 'A proximity detector that damages objects within radius (damage-type; inferred).',
    'trigger_evaluate': 'OPERATE runs a test and sends TRUE or FALSE; random-test is the chance of '
         'TRUE (0.5 = half the time), used for random branches.',
    'trigger_sound': 'PLAY_SOUND / STOP_SOUND / PAUSE_SOUND / RESUME_SOUND control its sound; it '
         'sends FINISHED when the sound ends (inferred).',
    'trigger_mover': 'MOVE_FORWARD / MOVE_BACKWARD (and their _LOOP / _OSCILLATE forms) and STOP '
         'move an object part (part-name) along move-axis.',
    'trigger_anim': 'OPERATE plays animation-archetype (command, time-scale, weight) and sends '
         'ANIM_START / ANIM_BACK_START (inferred from its code; details not yet traced).',
}

def nice(s):
    s = (s or '').strip()
    if s.startswith('#'):
        try: return NAMES.get(hex(int(s[1:], 16)), s)
        except ValueError: return s
    return NAMES.get(s, s) if s.startswith('_') else s

# object kinds whose tag name is not recovered yet, named from their fields (guesses, marked '?')
GUESS = {'_e02db47a': 'box-detector?', '_0cb602d0': 'pickup?', '_14cccfaa': 'use-trigger?',
         '_e5e648e6': 'box-detector-2?', '_130818c0': 'damage-box-detector?',
         '_e2b9c3a2': 'controller-trigger?', '_f7db632b': 'test-trigger?'}

# tags that older conversions named wrongly (hash collisions in earlier name lists)
RENAMED = {'fleeima': 'destroyer-trigger', 'spreaddirtpuff': 'world-point-trigger', 'types_wrath': '_14cccfaa'}

def tagname(t):
    t = RENAMED.get(t, t)
    if t in GUESS and NAMES.get(hex(int(t[1:], 16)), '').endswith(('trigger', 'detector')) is False:
        return GUESS[t]
    if t.startswith('_') and re.fullmatch(r'_[0-9a-f]{8}', t):
        return NAMES.get(hex(int(t[1:], 16)), t)
    return t

def canon(x):
    """Name for a hashed or named reference; also corrects names that older conversions
    got wrong (hash collisions since replaced in names.json)."""
    h = hval(x)
    return NAMES.get(hex(h), nice(x)) if h is not None else nice(x)

def objlist(el):
    return [canon(o.text) for o in el.iter('objects') if (o.text or '').strip()] if el is not None else []

def response(el):
    """A response element: a BASE child (signal/action/...) plus type-specific fields."""
    base = el.find('BASE') if el.tag != 'BASE' else el
    if base is None: return None
    r = {}
    for c in base:
        k = FIELD.get(c.tag, c.tag)
        if c.tag == 'type': continue
        v = (c.text or '').strip()
        r[k] = v.replace('TRIG_SIG_', '').replace('TRIG_ACT_', '') if k in ('signal', 'action') else v
    if r.get('max-count') == '2147483647': r.pop('max-count')
    if r.get('match-id') == '0': r.pop('match-id')
    if r.get('flag') == 'false': r.pop('flag')
    if r.get('extra') == '0': r.pop('extra')
    for c in (el if el.tag != 'BASE' else []):
        if c.tag != 'BASE' and len(c) == 0 and (c.text or '').strip():
            r[tagname(c.tag)] = nice(c.text)
    return r

OBJECTIVES = {}   # game-flag path (tuple of hashes) -> 'primary objective of M03_a: "..."'

def hval(x):
    x = (x or '').strip()
    if x.startswith('#'):
        try: return int(x[1:], 16)
        except ValueError: return None
    return bfhash(x)

def load_objectives(root):
    """Mission objectives from common/campaign-bf.xml (paths into the game flags) with their
    English text from the string table."""
    camp = st = None
    for d, _, files in os.walk(root):
        if 'campaign-bf.xml' in files: camp = os.path.join(d, 'campaign-bf.xml')
        if 'string-table-en.xml' in files: st = os.path.join(d, 'string-table-en.xml')
    if not camp: return
    strings = {}
    if st:
        t = open(st, encoding='latin-1').read()
        for i, v in re.findall(r'<id>#([0-9a-f]{8})</id>\s*<value>(.*?)</value>', t, re.S):
            strings[int(i, 16)] = v.replace('\x00', '').strip()
    t = open(camp, encoding='latin-1').read()
    for m in re.finditer(r'<level [^>]*>(.*?)</level>', t, re.S):
        body = m.group(1)
        fn = re.search(r'<file-name>([^<]*)</file-name>', body)
        for kind, ob in re.findall(r'<(primary|secondary)-objectives>(.*?)</\1-objectives>', body, re.S):
            path = tuple(hval(x) for x in re.findall(r'<PATH>([^<]*)</PATH>', ob))
            did = re.search(r'<display-id>#([0-9a-f]{8})', ob)
            text = strings.get(int(did.group(1), 16), '') if did else ''
            OBJECTIVES[path] = '%s objective of %s: "%s"' % (kind, fn.group(1) if fn else '?', text)

def operand(c):
    """A game-variable operand: <selector> GVS_CONSTANT / GVS_VARIABLE (gameflag path) / GVS_SIGNAL."""
    sel = (c.findtext('selector') or '').strip()
    if sel == 'GVS_CONSTANT': return (c.findtext('constant') or '').strip()
    if sel == 'GVS_VARIABLE':
        flags = c.findall('gameflag')
        s = '.'.join(canon(g.text) for g in flags)
        obj = OBJECTIVES.get(tuple(hval(g.text) for g in flags[:-1]))
        return s + ('  [' + obj + ']' if obj else '')
    if sel == 'GVS_SIGNAL': return 'signal'
    return sel

def settings(el):
    out = {}
    for c in el:
        if c.tag in COMMON: continue
        if c.find('selector') is not None:
            out[{'_1554ca7a': 'A', 'in-a': 'A', '_0e5d9bc0': 'B', 'in-b': 'B'}.get(c.tag, tagname(c.tag))] = operand(c)
            continue
        if len(c) == 0:
            v = (c.text or '').strip()
            if v: out[tagname(c.tag)] = nice(v)
        else:
            lst = objlist(c)
            if lst and all(len(x) == 0 for x in c): out[tagname(c.tag)] = lst
    return out

def read_level(path):
    root = ET.parse(path).getroot()
    objs = []
    for el in root.iter():
        nm = el.find('name'); ot = el.find('object-type-name')
        if nm is None or ot is None or el.find(RESPONSES) is None and el.find(RECEIVERS) is None \
                and el.find(ACTS_ON) is None:
            continue
        resp_el = el.find(RESPONSES)
        resps = []
        if resp_el is not None:
            for c in resp_el:
                r = response(c)
                if r: resps.append(r)
        objs.append({'name': canon(nm.text), 'kind': tagname(el.tag), 'type': nice(ot.text),
                     'enabled': (el.findtext('enabled') or '').strip() != 'false',
                     'responses': resps, 'senders': objlist(el.find(SENDERS)),
                     'receivers': objlist(el.find(RECEIVERS)), 'acts_on': objlist(el.find(ACTS_ON)),
                     'settings': settings(el)})
    return objs

def describe(o):
    head = '%s  [%s%s]%s' % (o['name'], o['kind'], ', ' + o['type'] if o['type'] != o['kind'] else '',
                             '' if o['enabled'] else '  (starts disabled)')
    lines = [head]
    if o['senders']: lines.append('  listens to: ' + ', '.join(o['senders']))
    for r in o['responses']:
        if r.get('signal') == 'INVALID' and r.get('action') == 'OPERATE' and len(r) == 2: continue
        extra = ', '.join('%s=%s' % (k, v) for k, v in r.items() if k not in ('signal', 'action'))
        lines.append('  on %s -> %s%s' % (r.get('signal'), r.get('action'), ('  (' + extra + ')') if extra else ''))
    if o['acts_on']: lines.append('  acts on: ' + ', '.join(o['acts_on']))
    if o['receivers']: lines.append('  signals: ' + ', '.join(o['receivers']))
    if o['settings']:
        s = ', '.join('%s=%s' % (k, (', '.join(v) if isinstance(v, list) else v)) for k, v in o['settings'].items())
        lines.append('  settings: ' + s)
    return '\n'.join(lines)

def dot(objs, title):
    def q(s): return '"%s"' % s.replace('"', "'")
    lines = ['digraph %s {' % q(title), '  rankdir=LR; node [shape=box, fontsize=10];']
    linked = {o['name'] for o in objs if o['receivers'] or o['senders'] or o['acts_on']}
    for o in objs:
        if o['name'] in linked:
            lines.append('  %s [label=%s];' % (q(o['name']), q('%s\\n%s' % (o['name'], o['kind']))))
    for o in objs:
        acts = {}
        for r in o['responses']:
            acts.setdefault(r.get('signal'), []).append(r.get('action'))
        for s in o['senders']:
            label = '; '.join('%s>%s' % (k, '/'.join(v)) for k, v in acts.items() if k != 'INVALID')
            lines.append('  %s -> %s [label=%s, fontsize=8];' % (q(s), q(o['name']), q(label)))
        for a in o['acts_on']:
            lines.append('  %s -> %s [style=dashed, color=gray];' % (q(o['name']), q(a)))
    lines.append('}')
    return '\n'.join(lines) + '\n'

def main(args):
    if not args:
        print(__doc__); return 1
    total = 0
    load_objectives(args[0])
    for d, _, files in os.walk(args[0]):
        for f in sorted(files):
            if not (f.startswith('levels-') and f.endswith('.xml')): continue
            p = os.path.join(d, f)
            try: objs = read_level(p)
            except ET.ParseError as e:
                print('  cannot read', p, e); continue
            base = os.path.splitext(p)[0]
            links = sum(len(o['receivers']) for o in objs)
            with open(base + '_logic.txt', 'w') as out:
                out.write('%s: %d triggers and objects with signal links, %d links\n\n' % (f, len(objs), links))
                used = sorted({o['type'] for o in objs if o['type'] in TYPE_NOTES})
                for t in ['*'] + used:
                    out.write('%s: %s\n' % ('all triggers' if t == '*' else t, TYPE_NOTES[t]))
                out.write('\n')
                for o in sorted(objs, key=lambda o: (o['kind'], o['name'])):
                    out.write(describe(o) + '\n\n')
            json.dump(objs, open(base + '_logic.json', 'w'), indent=1)
            open(base + '_logic.dot', 'w').write(dot(objs, f))
            print('%s: %d triggers/objects, %d signal links' % (p, len(objs), links))
            total += 1
    print('wrote the mission logic of %d levels' % total)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
