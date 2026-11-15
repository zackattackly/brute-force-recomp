"""Brute Force (Xbox) animation exporter: animation libraries -> JSON.

Reads each animation library (animations-*.xml, made by xmb2xml / convert_all)
and its channel data (animations-*.chnl) and writes one JSON file per clip
into animations-NAME_json/<animation set>/ next to it.

Usage:
    python anim2json.py <folder>                 (all libraries under folder)
    python anim2json.py <animations-x.xml> ...

Channel format (reverse-engineered):
  Each <channel> has data-type, a sampling kind, offset/size in the .chnl
  file, num-frames and, for evenly sampled channels, the frame time.
  Sampling kinds:
    #f8b95953  evenly sampled: frame i is at i * frame-time
    #00ad833c  keyed: every key starts with its time (float32)
  Data types (value layout per frame/key):
    #06677bcb  rotation, compressed quaternion: x, y, z as int16 / 32767,
               w = sqrt(1 - x^2 - y^2 - z^2)
    #06901441  transform: position float3, then compressed quaternion
    #f36d8810  vector float3 (e.g. position)
    #fed99c09, #e0b5650b  single float
    #007162cc  event track: event name hash (uint32)
  A clip (<archetype>) lists targets (bone name hash, or 'root') and the
  channel that drives each.
Pure Python standard library.
"""
import json, math, os, re, struct, sys

ATTR = re.compile(r'(\S+?)="([^"]*)"')
EVEN, KEYED = '#f8b95953', '#00ad833c'
ROT, XFORM, VEC3, EVENT = '#06677bcb', '#06901441', '#f36d8810', '#007162cc'
FLOATS = ('#fed99c09', '#e0b5650b')

def load_names():
    try:
        # keys may be written with or without leading zeros (0x0abc... / 0xabc...): normalise to hex()
        return {hex(int(k, 16)): v for k, v in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'names.json'))).items()}
    except Exception:
        return {}
NAMES = load_names()

def nice(h):
    if h and h.startswith('#'):
        try: return NAMES.get(hex(int(h[1:], 16)), h)
        except ValueError: return h
    return h

def quat(b, o):
    x, y, z = (v / 32767.0 for v in struct.unpack_from('<3h', b, o))
    w2 = 1.0 - (x * x + y * y + z * z)
    return [round(x, 5), round(y, 5), round(z, 5), round(math.sqrt(w2) if w2 > 0 else 0.0, 5)]

def value(dt, b, o):
    if dt == ROT: return quat(b, o), 6
    if dt == XFORM:
        p = [round(v, 5) for v in struct.unpack_from('<3f', b, o)]
        return {'position': p, 'rotation': quat(b, o + 12)}, 18
    if dt == VEC3: return [round(v, 5) for v in struct.unpack_from('<3f', b, o)], 12
    if dt == EVENT: return nice('#%08x' % struct.unpack_from('<I', b, o)[0]), 4
    if dt in FLOATS: return round(struct.unpack_from('<f', b, o)[0], 5), 4
    return None, 0

def decode_channel(a, data):
    dt = a.get('data-type'); kind = None
    for k, v in a.items():
        if v in (EVEN, KEYED): kind = v
    try:
        n = int(a['num-frames']); off = int(a['offset']); size = int(a['size'])
    except (KeyError, ValueError):
        return None
    b = data[off:off + size]
    keyed = kind == KEYED or (n and size // n == {ROT: 10, XFORM: 22, VEC3: 16, EVENT: 8}.get(dt, -1))
    if dt == EVENT and not keyed:
        keyed = True          # event tracks always carry times
    step = None
    for k, v in a.items():
        if k not in ('offset', 'size', 'num-frames') and re.fullmatch(r'-?\d+\.\d+', v or ''):
            step = float(v)
    times, vals, o = [], [], 0
    for i in range(n):
        if keyed:
            if o + 4 > len(b): break
            t = struct.unpack_from('<f', b, o)[0]; o += 4
        else:
            t = i * step if step else i
        v, used = value(dt, b, o)
        if used == 0: return None
        o += used
        times.append(round(t, 5)); vals.append(v)
    kind_name = {ROT: 'rotation', XFORM: 'transform', VEC3: 'vector', EVENT: 'events'}.get(dt, 'float' if dt in FLOATS else dt)
    return {'type': kind_name, 'times': times, 'values': vals}

def set_labels(root):
    """Animation-set name -> readable label from the motion sequences that use it
    (characters/ms_hf.xml names set #1cb6373d -> 'hf')."""
    labels = {}
    for d, _, files in os.walk(root):
        for f in files:
            if f.startswith('ms_') and f.endswith('.xml'):
                m = re.search(r'<name>([^<]+)</name>', open(os.path.join(d, f), encoding='latin-1').read())
                if not m: continue
                lab = f[3:-4]
                if lab.startswith('dm_'): lab = lab[3:]
                old = labels.get(m.group(1).strip())
                if old is None or len(lab) < len(old): labels[m.group(1).strip()] = lab
    return labels

def set_folder(sname, labels):
    n = labels.get(sname) or nice(sname)
    return re.sub(r'[\\/:*?"<>|#]', '', n) or 'clips'

def export_library(xml_path, labels=None):
    text = open(xml_path, encoding='latin-1').read()
    m = re.search(r'data-file="([^"]+)"', text)
    if not m: return 0, 0
    data_path = os.path.join(os.path.dirname(xml_path), m.group(1))
    if not os.path.isfile(data_path):
        print('  data file missing:', data_path); return 0, 0
    data = open(data_path, 'rb').read()
    channels = {}
    for cm in re.finditer(r'<channel ([^>]*?)/?>', text):
        a = dict(ATTR.findall(cm.group(1)))
        if 'name' in a: channels[a['name']] = a
    out_root = os.path.splitext(xml_path)[0] + '_json'
    # clips of different animation sets share names (every set has its own walk),
    # so each set gets its own sub-folder
    set_at = [(sm.start(), dict(ATTR.findall(sm.group(1))).get('name', ''))
              for sm in re.finditer(r'<archetype-set ([^>]*?)>', text)]
    clips = bad = 0
    for am in re.finditer(r'<archetype ([^>]*?)>(.*?)</archetype>', text, re.S):
        a = dict(ATTR.findall(am.group(1)))
        name = nice(a.get('name', 'clip%d' % clips))
        sname = ''
        for pos, n in set_at:
            if pos < am.start(): sname = n
        out_dir = os.path.join(out_root, set_folder(sname, labels or {}))
        os.makedirs(out_dir, exist_ok=True)
        clip = {'name': name, 'set': nice(sname), 'duration': float(a.get('duration', 0) or 0),
                'root-height': float(a.get('root-height', 0) or 0), 'tracks': []}
        if 'event-channel' in a and a['event-channel'] in channels:
            ev = decode_channel(channels[a['event-channel']], data)
            if ev: clip['events'] = ev
        for tm in re.finditer(r'<target ([^>]*?)/?>', am.group(2)):
            t = dict(ATTR.findall(tm.group(1)))
            ch = channels.get(t.get('data-channel'))
            if not ch: bad += 1; continue
            dec = decode_channel(ch, data)
            if not dec: bad += 1; continue
            dec['target'] = nice(t.get('name'))
            clip['tracks'].append(dec)
        fn = re.sub(r'[\\/:*?"<>|#]', '', name) or 'clip%d' % clips
        with open(os.path.join(out_dir, fn + '.json'), 'w') as f:
            json.dump(clip, f, separators=(',', ':'))
        clips += 1
    return clips, bad

def main(args):
    if not args:
        print(__doc__); return 1
    libs = []
    for p in args:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                libs += [os.path.join(root, f) for f in files if f.startswith('animations-') and f.endswith('.xml')]
        else:
            libs.append(p)
    labels = {}
    for p in args:
        if os.path.isdir(p): labels.update(set_labels(p))
    tc = tb = 0
    for i, x in enumerate(sorted(libs), 1):
        print('[%d/%d] %s' % (i, len(libs), x), flush=True)
        c, b = export_library(x, labels)
        tc += c; tb += b
    print('wrote %d animation clips; %d tracks could not be decoded' % (tc, tb))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
