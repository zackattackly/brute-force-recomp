"""Brute Force (Xbox) character exporter: rigged, skinned characters -> glTF (.glb).

For every character skeleton that carries a skinned mesh, writes a .glb with
the mesh (highest level of detail), its textures (linked to the PNGs made by
tex2png.py), the skeleton, the skin weights and all animation clips of the
character's animation set. Blender: File > Import > glTF 2.0.

Usage:
    python char2gltf.py <data folder>

Output: characters_glb/<archetype>.glb in the data folder (each character once,
even if several levels carry it).

Format (reverse-engineered):
  Skeleton  = archetype with <PART>s and <joint>s (parent-point = bone offset in
              the parent's space; rest rotations are identity).
  Each part's own archetype has a 12-float matrix (_e4b822af): rows R0 R1 R2 and
              t, mapping model space to that bone's space (bone-local = R v + t),
              i.e. the inverse bind matrix.
  Skinned vertex (#13d18cfc, 32 bytes): position float3, packed normal,
              uv float2, 4 weights (bytes, sum 255), 4 palette slots (byte 5*s+1).
  The mesh's palettes (_f1330520 > first child; one or more) map slots to part
  indices; each batch names its palette (the field after its vertex offset).
  Posed vertex = sum of weight * BoneWorld * (R v + t).
Pure Python standard library.
"""
import json, math, os, re, struct, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mesh2obj as MO
import anim2json as AJ
import anim2bvh as AB

SKIN = 0x13d18cfc

def mirror_m(R, t):
    S = [1, 1, -1]
    return ([[R[i][j] * S[i] * S[j] for j in range(3)] for i in range(3)],
            [t[0], t[1], -t[2]])

def mat_to_q(m):
    """3x3 rotation matrix -> unit quaternion (x, y, z, w)."""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        S = math.sqrt(tr + 1.0) * 2
        q = [(m[2][1] - m[1][2]) / S, (m[0][2] - m[2][0]) / S, (m[1][0] - m[0][1]) / S, 0.25 * S]
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        S = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        q = [0.25 * S, (m[0][1] + m[1][0]) / S, (m[0][2] + m[2][0]) / S, (m[2][1] - m[1][2]) / S]
    elif m[1][1] > m[2][2]:
        S = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        q = [(m[0][1] + m[1][0]) / S, 0.25 * S, (m[1][2] + m[2][1]) / S, (m[0][2] - m[2][0]) / S]
    else:
        S = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        q = [(m[0][2] + m[2][0]) / S, (m[1][2] + m[2][1]) / S, 0.25 * S, (m[1][0] - m[0][1]) / S]
    n = math.sqrt(sum(v * v for v in q)) or 1.0
    return [v / n for v in q]

def mirror_q(q):
    return [-q[0], -q[1], q[2], q[3]]

class Builder:
    def __init__(self):
        self.bin = bytearray(); self.views = []; self.accessors = []
    def add(self, fmt, rows, comp, typ, normalized=False, minmax=False, target=None):
        while len(self.bin) % 4: self.bin.append(0)
        off = len(self.bin)
        for r in rows: self.bin += struct.pack(fmt, *(r if isinstance(r, (list, tuple)) else [r]))
        view = {'buffer': 0, 'byteOffset': off, 'byteLength': len(self.bin) - off}
        if target: view['target'] = target
        self.views.append(view)
        acc = {'bufferView': len(self.views) - 1, 'componentType': comp, 'count': len(rows), 'type': typ}
        if normalized: acc['normalized'] = True
        if minmax:
            cols = list(zip(*[(r if isinstance(r, (list, tuple)) else [r]) for r in rows]))
            acc['min'] = [min(c) for c in cols]; acc['max'] = [max(c) for c in cols]
        self.accessors.append(acc)
        return len(self.accessors) - 1

def read_skeleton(arch, archs):
    parts = []
    for p in arch.iter('PART'):
        pn = p.find('part-name'); an = p.find('archetype-name')
        if pn is None: continue
        parts.append((AJ.nice((pn.text or '').strip()), (an.text or '').strip() if an is not None else ''))
    names = [p for p, _ in parts]
    parent, off = {}, {}
    for j in arch.iter('joint'):
        pp = j.find('parent-part'); cp = j.find('child-part')
        g = next((k for k in j if k.find('parent-point') is not None), None)
        if pp is None or cp is None or g is None: continue
        c = AJ.nice((cp.text or '').strip())
        parent[c] = AJ.nice((pp.text or '').strip())
        off[c] = [float(v) for v in g.find('parent-point').text.split()]
    inv = {}
    for n, an in parts:
        a = archs.get(an); e = a.find('_e4b822af') if a is not None else None
        if e is None:
            for k in (a if a is not None else []):
                if len((k.text or '').split()) == 12: e = k
        if e is not None:
            v = [float(x) for x in e.text.split()]
            inv[n] = ([v[0:3], v[3:6], v[6:9]], v[9:12])
    return names, parent, off, inv

def skin_batches(geoset):
    """Like mesh2obj.find_batches, plus the batch's bone palette index (the field
    after the vertex offset): meshes with many bones have several palettes."""
    for el in geoset.iter():
        if el.find('vertex-type') is None: continue
        tags = [c.tag for c in el]; vals = [(c.text or '').strip() for c in el]
        try:
            vi = tags.index('vertex-type')
            yield (MO.hval(vals[vi - 3]), int(vals[vi - 2]), int(vals[vi - 1]), MO.hval(vals[vi]),
                   int(vals[vi + 1]), int(vals[vi + 2]), int(vals[vi + 3]) if len(vals) > vi + 3 else 0)
        except (ValueError, IndexError):
            continue

def export_character(path, arch, archs, lib, data, mats, pngs, clips):
    names, parent, off, inv = read_skeleton(arch, archs)
    if len(names) < 2: return False
    # highest-detail mesh with skinned geosets
    meshes = {(m.find('name').text or '').strip(): m for m in lib.iter('mesh') if m.find('name') is not None}
    best = None
    for me in arch.iter('mesh'):
        mn = me.find('mesh-name'); ls = me.find('lod-switch')
        m = meshes.get((mn.text or '').strip()) if mn is not None else None
        if m is None: continue
        if not any(b[3] == SKIN for g in m.iter('geoset') for b in MO.find_batches(g)): continue
        lod = float(ls.text) if ls is not None and ls.text else 0
        if best is None or lod > best[0]: best = (lod, m)
    if best is None: return False
    mesh = best[1]
    palettes = []
    for k in mesh:
        if len(k) and (k[0].text or '').strip() and all(t.isdigit() for t in k[0].text.split()):
            palettes.append([int(t) for t in k[0].text.split()])
    if not palettes: return False
    B = Builder()
    gl = {'asset': {'version': '2.0', 'generator': 'Brute Force recomp char2gltf'},
          'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [],
          'textures': [], 'images': [], 'samplers': [{}], 'skins': [], 'animations': []}
    # Bind pose: each bone's bind matrix is the inverse of its inverse bind matrix;
    # the node's rest transform is its bind matrix relative to its parent's, so the
    # skeleton imports in the pose the mesh was modelled in (animations replace the
    # rotation, and the translation where a clip has a position track).
    idx = {n: i for i, n in enumerate(names)}
    bind = {}
    def bind_of(n):
        if n in bind: return bind[n]
        if n in inv:
            R, t = mirror_m(*inv[n])
            Rt = [[R[j][i] for j in range(3)] for i in range(3)]
            bind[n] = (Rt, [-sum(Rt[i][k] * t[k] for k in range(3)) for i in range(3)])
        else:
            o = off.get(n, [0, 0, 0]); o = [o[0], o[1], -o[2]]
            if n in parent and parent[n] in idx:
                PR, PT = bind_of(parent[n])
                bind[n] = (PR, [PT[i] + sum(PR[i][k] * o[k] for k in range(3)) for i in range(3)])
            else:
                bind[n] = ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], o)
        return bind[n]
    for n in names:
        R, T = bind_of(n)
        o = off.get(n, [0, 0, 0]); tl = [o[0], o[1], -o[2]]
        if n in parent and parent[n] in idx:
            PR, PT = bind_of(parent[n])
            PRt = [[PR[j][i] for j in range(3)] for i in range(3)]
            R = [[sum(PRt[i][k] * R[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
            # Bind-relative position. For Biped bones it equals the joint's parent-point;
            # face and attachment bones (fixed joints, type #e3be168e) have parent-point
            # 0 and only the bind matrix places them (eyes, brows, lips, jaw, backpack).
            tl = [sum(PRt[i][k] * (T[k] - PT[k]) for k in range(3)) for i in range(3)]
        node = {'name': n, 'translation': [round(v, 6) for v in tl]}
        q = mat_to_q(R)
        if abs(q[3]) < 0.999999: node['rotation'] = q
        gl['nodes'].append(node)
    for n in names:
        if n in parent and parent[n] in idx:
            gl['nodes'][idx[parent[n]]].setdefault('children', []).append(idx[n])
    roots = [idx[n] for n in names if n not in parent or parent[n] not in idx]
    ibms = []
    for n in names:
        R, T = bind_of(n)
        Ri = [[R[j][i] for j in range(3)] for i in range(3)]
        t = [-sum(Ri[i][k] * T[k] for k in range(3)) for i in range(3)]
        ibms.append([Ri[0][0], Ri[1][0], Ri[2][0], 0, Ri[0][1], Ri[1][1], Ri[2][1], 0,
                     Ri[0][2], Ri[1][2], Ri[2][2], 0, t[0], t[1], t[2], 1])
    ibm_acc = B.add('<16f', ibms, 5126, 'MAT4')
    gl['skins'].append({'joints': list(range(len(names))), 'inverseBindMatrices': ibm_acc, 'skeleton': roots[0]})
    # materials
    matidx = {}
    def material(mname):
        if mname in matidx: return matidx[mname]
        m = {'name': mname.lstrip('#'), 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.9}}
        tex = mats.get(mname)
        key = tex[1:] if tex and tex.startswith('#') else (MO.safe(tex) if tex else None)
        png = pngs.get(key) if key else None
        if png:
            gl['images'].append({'uri': os.path.relpath(png, os.path.dirname(path)).replace('\\', '/')})
            gl['textures'].append({'source': len(gl['images']) - 1, 'sampler': 0})
            m['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(gl['textures']) - 1}
        gl['materials'].append(m); matidx[mname] = len(gl['materials']) - 1
        return matidx[mname]
    prims = []
    for g in mesh.iter('geoset'):
        mt = g.find('material-name'); mname = (mt.text or '').strip() if mt is not None else ''
        for prim, ic, io, vt, vc, vo, pi in skin_batches(g):
            if vt != SKIN or vo + vc * 32 > len(data): continue
            palette = palettes[pi] if 0 <= pi < len(palettes) else palettes[0]
            prim, ind = MO.read_indices(data, prim, ic, io)
            if ind is None: continue
            pos, nrm, uv, jts, wts = [], [], [], [], []
            for k in range(vc):
                p = vo + 32 * k
                x, y, z = struct.unpack_from('<3f', data, p); pos.append((x, y, -z))
                nx, ny, nz = MO.np3(struct.unpack_from('<I', data, p + 12)[0])
                L = math.sqrt(nx * nx + ny * ny + nz * nz) or 1; nrm.append((nx / L, ny / L, -nz / L))
                u, v = struct.unpack_from('<2f', data, p + 16); uv.append((u, v))
                w = data[p + 24:p + 28]; s = data[p + 28:p + 32]
                jj, ww = [], []
                for q in range(4):
                    slot = (s[q] - 1) // 5
                    if w[q] and 0 <= slot < len(palette) and palette[slot] < len(names):
                        jj.append(palette[slot]); ww.append(w[q] / 255.0)
                    else:
                        jj.append(0); ww.append(0.0)
                tot = sum(ww) or 1.0
                jts.append(jj); wts.append([x / tot for x in ww])
            tris = [(a, c, b) for a, b, c in MO.triangles(prim, ind) if max(a, b, c) < vc]
            if not tris: continue
            attrs = {'POSITION': B.add('<3f', pos, 5126, 'VEC3', minmax=True, target=34962),
                     'NORMAL': B.add('<3f', nrm, 5126, 'VEC3', target=34962),
                     'TEXCOORD_0': B.add('<2f', uv, 5126, 'VEC2', target=34962),
                     'JOINTS_0': B.add('<4H', jts, 5123, 'VEC4', target=34962),
                     'WEIGHTS_0': B.add('<4f', wts, 5126, 'VEC4', target=34962)}
            flat = [i for t in tris for i in t]
            ia = B.add('<I', flat, 5125, 'SCALAR', target=34963)
            p = {'attributes': attrs, 'indices': ia, 'mode': 4}
            if mname: p['material'] = material(mname)
            prims.append(p)
    if not prims: return False
    gl['meshes'].append({'name': (mesh.find('name').text or '').lstrip('#'), 'primitives': prims})
    gl['nodes'].append({'name': 'skin', 'mesh': 0, 'skin': 0})
    gl['scenes'][0]['nodes'] = roots + [len(gl['nodes']) - 1]
    # The root sits at the pelvis; the game puts the ground root-height (per clip)
    # below it. Lift the root so the character stands on y = 0.
    ground = -min(B.accessors[p['attributes']['POSITION']]['min'][1] for p in prims)
    rnode = gl['nodes'][roots[0]]
    rest_t = list(rnode['translation']); rnode['translation'] = [rest_t[0], rest_t[1] + ground, rest_t[2]]
    # animations
    for clip in clips:
        chans, samps = [], []
        rh = clip.get('root-height', -1e5)
        lift = rh if rh > -1000 else ground
        root_moved = False
        for tr in clip['tracks']:
            n = tr['target']
            if n not in idx or not tr['times']: continue
            tin = B.add('<f', tr['times'], 5126, 'SCALAR', minmax=True)
            if tr['type'] in ('rotation', 'transform'):
                qs = [mirror_q(v if tr['type'] == 'rotation' else v['rotation']) for v in tr['values']]
                samps.append({'input': tin, 'output': B.add('<4f', qs, 5126, 'VEC4'), 'interpolation': 'LINEAR'})
                chans.append({'sampler': len(samps) - 1, 'target': {'node': idx[n], 'path': 'rotation'}})
            if tr['type'] in ('transform', 'vector'):
                ps = [(v['position'] if tr['type'] == 'transform' else v) for v in tr['values']]
                ps = [(p[0], p[1], -p[2]) for p in ps]
                if idx[n] == roots[0]:
                    ps = [(p[0], p[1] + lift, p[2]) for p in ps]; root_moved = True
                samps.append({'input': tin, 'output': B.add('<3f', ps, 5126, 'VEC3'), 'interpolation': 'LINEAR'})
                chans.append({'sampler': len(samps) - 1, 'target': {'node': idx[n], 'path': 'translation'}})
        if chans and not root_moved:
            end = max(max(tr['times']) for tr in clip['tracks'] if tr['times'])
            tin = B.add('<f', [0.0, max(end, 1e-3)], 5126, 'SCALAR', minmax=True)
            p0 = (rest_t[0], rest_t[1] + lift, rest_t[2])
            samps.append({'input': tin, 'output': B.add('<3f', [p0, p0], 5126, 'VEC3'), 'interpolation': 'LINEAR'})
            chans.append({'sampler': len(samps) - 1, 'target': {'node': roots[0], 'path': 'translation'}})
        if chans:
            gl['animations'].append({'name': clip['name'].lstrip('#'), 'channels': chans, 'samplers': samps})
    for k in ('textures', 'images', 'animations', 'materials'):
        if not gl[k]: del gl[k]
    if 'textures' not in gl: del gl['samplers']
    while len(B.bin) % 4: B.bin.append(0)
    gl['buffers'] = [{'byteLength': len(B.bin)}]
    gl['bufferViews'] = B.views; gl['accessors'] = B.accessors
    js = json.dumps(gl, separators=(',', ':')).encode()
    while len(js) % 4: js += b' '
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(B.bin)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(B.bin), 0x004E4942)); f.write(bytes(B.bin))
    return True

def main(args):
    if not args:
        print(__doc__); return 1
    root = args[0]
    print('indexing materials and textures...', flush=True)
    mats, pngs = MO.index_data(root)
    print('reading animation libraries...', flush=True)
    anim_libs = []
    for d, _, files in os.walk(root):
        anim_libs += [os.path.join(d, f) for f in files if f.startswith('animations-') and f.endswith('.xml')]
    links = AB.motion_links(root)
    labels = {}
    for mesh, (lab, _) in links.items(): labels.setdefault(lab, []).append(mesh)
    libs_of_set, clip_cache = {}, {}
    for x in anim_libs:
        for sname in set(re.findall(r'<archetype-set name="([^"]+)"', open(x, encoding='latin-1').read())):
            libs_of_set.setdefault(sname, []).append(x)
    def clips_for(sname):
        out, seen = [], set()
        for x in libs_of_set.get(sname, []):
            if x not in clip_cache: clip_cache[x] = AB.clips_of(x)
            for c in clip_cache[x]:
                if c.get('set') == sname and c['name'] not in seen:
                    seen.add(c['name']); out.append(c)
        return out
    libs = []
    for d, _, files in os.walk(root):
        libs += [os.path.join(d, f) for f in files if f.startswith('objects-') and f.endswith('.xml')]
    out_dir = os.path.join(root, 'characters_glb')
    total, done = 0, set()
    for i, x in enumerate(sorted(libs), 1):
        try: tree = ET.parse(x)
        except ET.ParseError: continue
        lib = next(tree.iter('mesh-library'), None)
        if lib is None: continue
        df = lib.find('data-file')
        dp = os.path.join(os.path.dirname(x), (df.text or '').strip() if df is not None else '')
        if not os.path.isfile(dp): continue
        archs = {}
        for a in tree.iter('archetype'):
            n = a.find('name')
            if n is not None: archs[(n.text or '').strip()] = a
        cands = [a for a in archs.values() if a.find('.//PART') is not None and a.find('.//joint') is not None
                 and a.find('.//mesh-name') is not None]
        if not cands: continue
        print('[%d/%d] %s' % (i, len(libs), x), flush=True)
        data = open(dp, 'rb').read()
        for a in cands:
            aname = (a.find('name').text or '').strip()
            if aname in done: continue          # the same character appears in many levels
            lab, sets = links.get(aname, (None, []))
            clips, seen = [], set()
            for s in sets:
                for c in clips_for(s):
                    if c['name'] not in seen: seen.add(c['name']); clips.append(c)
            stem = aname.lstrip('#')
            if lab: stem = lab if len(labels[lab]) == 1 else '%s_%s' % (lab, stem)
            os.makedirs(out_dir, exist_ok=True)
            fn = os.path.join(out_dir, stem + '.glb')
            if export_character(fn, a, archs, lib, data, mats, pngs, clips):
                total += 1; done.add(aname)
                print('   %s: %d animations' % (os.path.basename(fn), len(clips)))
    print('wrote %d rigged characters' % total)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
