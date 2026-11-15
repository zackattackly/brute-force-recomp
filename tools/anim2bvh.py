"""Brute Force (Xbox) animation exporter: animation clips -> BVH (opens in Blender).

Reads the skeletons from the mesh libraries (objects-*.xml: archetypes made of
<PART>s joined by <joint>s, e.g. the 3ds Max Biped 'Bip01 ...' bones) and the
animation libraries (animations-*.xml + .chnl), and writes one .bvh per clip
into animations-NAME_bvh/<animation set>/ next to each library. Each clip is paired
with the skeleton of the character type that uses the clip's animation set
(or, failing that, the smallest skeleton that has every bone it animates).

Usage:
    python anim2bvh.py <data folder>

In Blender: File > Import > Motion Capture (.bvh). The game is left-handed;
the exporter mirrors Z so motions play the right way round. Units: metres,
30 frames per second.
Pure Python standard library (uses anim2json.py from the same folder).
"""
import math, os, re, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim2json as A

FPS = 30.0

# ------------------------------------------------------------ skeletons
def find_skeletons(root):
    skels = []
    for d, _, files in os.walk(root):
        for f in files:
            if not (f.startswith('objects-') and f.endswith('.xml')): continue
            try: tree = ET.parse(os.path.join(d, f))
            except ET.ParseError: continue
            for arch in tree.iter('archetype'):
                joints = []
                for j in arch.iter('joint'):
                    p = j.find('parent-part'); c = j.find('child-part')
                    g = None
                    for k in j:
                        if k.find('parent-point') is not None: g = k
                    if p is None or c is None or g is None: continue
                    try:
                        off = [float(v) for v in g.find('parent-point').text.split()]
                        q = [float(v) for v in g.find('ORIENTATION').text.split()] if g.find('ORIENTATION') is not None else [1, 0, 0, 0]
                    except (ValueError, AttributeError):
                        continue
                    joints.append((A.nice((p.text or '').strip()), A.nice((c.text or '').strip()), off, q))
                if len(joints) < 3: continue
                parent = {c: p for p, c, _, _ in joints}
                roots = {p for p, _, _, _ in joints if p not in parent}
                if len(roots) != 1: continue
                nm = arch.find('name')
                skels.append({'name': (nm.text or '').strip() if nm is not None else '?',
                              'root': roots.pop(), 'parent': parent,
                              'offset': {c: o for _, c, o, _ in joints},
                              'orient': {c: q for _, c, _, q in joints},
                              'bones': set(parent) | set(parent.values())})
    return skels

def type_links(root, set_names):
    """animation-set name -> list of skeleton archetype names, from the object types:
    a character type entry (has a BASE child) names its mesh archetype (mesh-name)
    and, in some field, its animation set."""
    links = {}
    for d, _, files in os.walk(root):
        for f in files:
            if not (f.startswith('objecttypes-') and f.endswith('.xml')): continue
            try: tree = ET.parse(os.path.join(d, f))
            except ET.ParseError: continue
            for e in tree.iter():
                if e.find('BASE') is None: continue
                meshes = [(m.text or '').strip() for m in e.iter('mesh-name') if (m.text or '').strip()]
                sets = {(x.text or '').strip() for x in e.iter() if (x.text or '').strip() in set_names}
                for sname in sets:
                    for mname in meshes:
                        if mname not in links.setdefault(sname, []): links[sname].append(mname)
    return links

def motion_links(root):
    """Character mesh archetype -> (label, [animation-set names]).
    A character type names its skeleton (BASE/mesh-name) and its motion sequence
    (motion-archetype, e.g. ms_hf -> characters/ms_hf.xml); the motion sequence's
    <name> is the name of the animation set holding its clips."""
    ms = {}
    for d, _, files in os.walk(root):
        for f in files:
            if f.startswith('ms_') and f.endswith('.xml'):
                try: r = ET.parse(os.path.join(d, f)).getroot()
                except ET.ParseError: continue
                n = r.find('name')
                if n is not None and (n.text or '').strip(): ms[f[:-4].lower()] = (n.text or '').strip()
    links = {}
    for d, _, files in os.walk(root):
        for f in files:
            if not (f.startswith('objecttypes-') and f.endswith('.xml')): continue
            try: tree = ET.parse(os.path.join(d, f))
            except ET.ParseError: continue
            for e in tree.iter():
                mn = e.find('BASE/mesh-name'); ma = e.find('motion-archetype')
                if mn is None or ma is None: continue
                mesh = (mn.text or '').strip(); mname = (ma.text or '').strip().lower()
                if not mesh or mname not in ms: continue
                label, sets = links.setdefault(mesh, [None, []])
                if ms[mname] not in sets: sets.append(ms[mname])
                lab = mname[3:] if mname.startswith('ms_') else mname
                if lab.startswith('dm_'): lab = lab[3:]
                if label is None or len(lab) < len(label): links[mesh][0] = lab
    return {k: (v[0], v[1]) for k, v in links.items()}

# ------------------------------------------------------------ math
def qnorm(q):
    n = math.sqrt(sum(v * v for v in q)) or 1.0
    return [v / n for v in q]

def nlerp(a, b, t):
    if sum(x * y for x, y in zip(a, b)) < 0: b = [-v for v in b]
    return qnorm([x + (y - x) * t for x, y in zip(a, b)])

def sample(track, t):
    ts, vs = track['times'], track['values']
    if not ts: return None
    if t <= ts[0]: return vs[0]
    if t >= ts[-1]: return vs[-1]
    lo, hi = 0, len(ts) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if ts[mid] <= t: lo = mid
        else: hi = mid
    f = (t - ts[lo]) / ((ts[hi] - ts[lo]) or 1)
    a, b = vs[lo], vs[hi]
    if track['type'] == 'rotation': return nlerp(a, b, f)
    if track['type'] == 'transform':
        return {'position': [x + (y - x) * f for x, y in zip(a['position'], b['position'])],
                'rotation': nlerp(a['rotation'], b['rotation'], f)}
    if track['type'] == 'vector': return [x + (y - x) * f for x, y in zip(a, b)]
    return a

def mirror_q(q):            # (x, y, z, w) mirrored on Z
    return [-q[0], -q[1], q[2], q[3]]

def q_to_zxy(q):
    """Quaternion (x,y,z,w) -> BVH 'Zrotation Xrotation Yrotation' degrees (R = Rz Rx Ry)."""
    x, y, z, w = q
    m00 = 1 - 2 * (y * y + z * z); m01 = 2 * (x * y - z * w); m02 = 2 * (x * z + y * w)
    m10 = 2 * (x * y + z * w); m11 = 1 - 2 * (x * x + z * z); m12 = 2 * (y * z - x * w)
    m20 = 2 * (x * z - y * w); m21 = 2 * (y * z + x * w); m22 = 1 - 2 * (x * x + y * y)
    sx = max(-1.0, min(1.0, m21))
    rx = math.asin(sx)
    if abs(sx) < 0.99999:
        rz = math.atan2(-m01, m11); ry = math.atan2(-m20, m22)
    else:
        rz = math.atan2(m10, m00); ry = 0.0
    return [math.degrees(rz), math.degrees(rx), math.degrees(ry)]

# ------------------------------------------------------------ BVH
def write_bvh(path, skel, clip):
    tracks = {t['target']: t for t in clip['tracks']}
    children = {}
    for c, p in skel['parent'].items(): children.setdefault(p, []).append(c)
    order = []
    moving = {b for b, t in tracks.items() if t['type'] in ('transform', 'vector')} | {skel['root']}
    lines = ['HIERARCHY']

    def emit(b, depth):
        ind = '  ' * depth
        off = skel['offset'].get(b, [0, 0, 0])
        lines.append('%s%s %s' % (ind, 'ROOT' if depth == 0 else 'JOINT', b.replace(' ', '_')))
        lines.append(ind + '{')
        lines.append('%s  OFFSET %.6f %.6f %.6f' % (ind, off[0], off[1], -off[2]))
        if b in moving:
            lines.append(ind + '  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation')
        else:
            lines.append(ind + '  CHANNELS 3 Zrotation Xrotation Yrotation')
        order.append(b)
        kids = sorted(children.get(b, []))
        if kids:
            for k in kids: emit(k, depth + 1)
        else:
            lines.append(ind + '  End Site')
            lines.append(ind + '  {')
            lines.append(ind + '    OFFSET 0.000000 0.050000 0.000000')
            lines.append(ind + '  }')
        lines.append(ind + '}')
    emit(skel['root'], 0)

    dur = max(clip.get('duration') or 0.0, max((t['times'][-1] for t in clip['tracks'] if t['times']), default=0.0))
    frames = max(1, int(round(dur * FPS)) + 1)
    lines += ['MOTION', 'Frames: %d' % frames, 'Frame Time: %.6f' % (1.0 / FPS)]
    for fi in range(frames):
        t = fi / FPS
        row = []
        for b in order:
            tr = tracks.get(b)
            v = sample(tr, t) if tr else None
            pos = list(skel['offset'].get(b, [0, 0, 0])) if b != skel['root'] else [0, 0, 0]
            q = [0, 0, 0, 1]
            if tr is not None and v is not None:
                if tr['type'] == 'rotation': q = v
                elif tr['type'] == 'transform': q = v['rotation']; pos = v['position']
                elif tr['type'] == 'vector': pos = v
            if b == skel['root'] and clip.get('root-height', -1e5) > -1000:
                pos = [pos[0], pos[1] + clip['root-height'], pos[2]]   # stand on y = 0
            if b in moving:
                row += ['%.6f' % pos[0], '%.6f' % pos[1], '%.6f' % -pos[2]]
            row += ['%.4f' % a for a in q_to_zxy(mirror_q(q))]
        lines.append(' '.join(row))
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')

# ------------------------------------------------------------ driver
def clips_of(xml_path):
    """Decode every clip of a library using anim2json's channel decoder."""
    text = open(xml_path, encoding='latin-1').read()
    m = re.search(r'data-file="([^"]+)"', text)
    if not m: return []
    data_path = os.path.join(os.path.dirname(xml_path), m.group(1))
    if not os.path.isfile(data_path): return []
    data = open(data_path, 'rb').read()
    channels = {}
    for cm in re.finditer(r'<channel ([^>]*?)/?>', text):
        a = dict(A.ATTR.findall(cm.group(1)))
        if 'name' in a: channels[a['name']] = a
    out = []
    set_at = [(m.start(), dict(A.ATTR.findall(m.group(1))).get('name', ''))
              for m in re.finditer(r'<archetype-set ([^>]*?)>', text)]
    for am in re.finditer(r'<archetype ([^>]*?)>(.*?)</archetype>', text, re.S):
        a = dict(A.ATTR.findall(am.group(1)))
        sname = ''
        for pos, n in set_at:
            if pos < am.start(): sname = n
        clip = {'name': A.nice(a.get('name', '')), 'set': sname,
                'duration': float(a.get('duration', 0) or 0),
                'root-height': float(a.get('root-height', -1e5) or -1e5), 'tracks': []}
        for tm in re.finditer(r'<target ([^>]*?)/?>', am.group(2)):
            t = dict(A.ATTR.findall(tm.group(1)))
            ch = channels.get(t.get('data-channel'))
            dec = A.decode_channel(ch, data) if ch else None
            if dec and dec['type'] in ('rotation', 'transform', 'vector'):
                dec['target'] = A.nice(t.get('name')); clip['tracks'].append(dec)
        if clip['tracks']: out.append(clip)
    return out

def main(args):
    if not args:
        print(__doc__); return 1
    root = args[0]
    print('finding skeletons...', flush=True)
    skels = find_skeletons(root)
    print('  %d skeletons' % len(skels))
    libs = []
    for d, _, files in os.walk(root):
        libs += [os.path.join(d, f) for f in files if f.startswith('animations-') and f.endswith('.xml')]
    set_names = set()
    for x in libs:
        set_names |= set(re.findall(r'<archetype-set name="([^"]+)"', open(x, encoding='latin-1').read()))
    links = {}
    for mesh, (_, sets) in motion_links(root).items():
        for sname in sets: links.setdefault(sname, []).append(mesh)
    for sname, meshes in type_links(root, set_names).items():
        for m in meshes:
            if sname not in links: links.setdefault(sname, []).append(m)
    by_name = {}
    for sk in skels: by_name.setdefault(sk['name'], sk)
    print('  %d animation sets linked to a character skeleton' % sum(1 for v in links.values() if any(m in by_name for m in v)))
    labels = A.set_labels(root)
    done = unmatched = 0
    for i, x in enumerate(sorted(libs), 1):
        print('[%d/%d] %s' % (i, len(libs), x), flush=True)
        out_dir = os.path.splitext(x)[0] + '_bvh'
        for clip in clips_of(x):
            need = {t['target'] for t in clip['tracks']}
            linked = [by_name[m] for m in links.get(clip['set'], []) if m in by_name]
            cands = [s for s in linked if need <= s['bones']]
            if not cands:       # no character type link: smallest skeleton with every bone
                cands = sorted((s for s in skels if need <= s['bones']), key=lambda s: len(s['bones']))[:1]
            if not cands:
                unmatched += 1; continue
            skel = cands[0]
            sub = os.path.join(out_dir, A.set_folder(clip['set'], labels))
            os.makedirs(sub, exist_ok=True)
            fn = re.sub(r'[\\/:*?"<>|#]', '', clip['name']) or 'clip'
            write_bvh(os.path.join(sub, fn + '.bvh'), skel, clip)
            done += 1
    print('wrote %d BVH clips; %d clips had no matching skeleton' % (done, unmatched))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
