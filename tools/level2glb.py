r"""Brute Force (Xbox) level exporter: a whole level -> one glTF scene (.glb).

Puts the level's terrain, its sky and every placed object that has a model (buildings,
rocks, props, doors, crates, pickups, weapons, characters in their rest pose) where the
level file places it, with the models' textures embedded, plus the level's lights. Each placed object is a node named
after the object (or its hash) and its object type; each model is stored once and
shared by all its copies. Open in Blender (File > Import > glTF 2.0) or the Windows
3D Viewer.

Usage:
    python level2glb.py <data folder>                 (every level)
    python level2glb.py <data folder> e34 m05_a ...   (only these)

Output: <level>\<level>_scene.glb in the data folder. Run export_textures.bat first,
or the models come out untextured.

Format (reverse-engineered):
  levels-<level>.xml: every placed object (game-object, inventory-object, character-object,
    world-button-object, ...) has <object-type-name> and <transform>: 12 floats, rows
    R0 R1 R2 then the position, mapping object space to the world as
    world = x*R0 + y*R1 + z*R2 + position (row vectors, the Direct3D convention).
  objecttypes-*.xml: an object type's <mesh-name> names an archetype.
  objects-*.xml <archetype-library>: an archetype either lists <meshes> (levels of detail;
    the one with the highest lod-switch is the most detailed) or is built from <parts>,
    each part its own archetype, placed by its <joint> (parent-point in the parent part's
    space minus child-point; the rest rotations are identity).
  Materials: only shaders with alpha in their name (BF_CA_rtMaterial, BF_CAS_ ...) use the
    texture's alpha as a cut-out (foliage, fences); the others keep a specular mask there.
  The level's <Terrain> entry names the terrain mesh (see mesh2obj.py). Its texture layers
    overlap; the scene uses the first one (pass --all-layers for every layer).
  Lights (light-object): the type's light-type (LT_DIRECTIONALLIGHT / LT_POINTLIGHT), the
    instance's diffuse colour and range. A directional light shines along R2 of its transform
    (inferred: the brightest light of a level points downward this way in most levels).
    Brightness: directional 2, point range^2/4 (the game has no physical units).
  Sky: <sky><Object> names the sky mesh and its position. The background colour, the fog
    (colour, start and end distance) and the ambient colours are stored in the scene's extras
    (Blender: Scene properties > Custom Properties).
  Coordinates: the game is left-handed (Direct3D); like the OBJ export, z is mirrored.
Pure Python standard library.
"""
import json, math, os, re, struct, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mesh2obj as MO

SKIP_TAGS = {'spawn-trigger', 'sound-trigger', 'camera-object', 'path-object', 'light-object',
             'effect-object', 'blocker', 'points', 'proximity-detector'}

# ---------------------------------------------------------------- data index
def text(el, tag):
    c = el.find(tag)
    return (c.text or '').strip() if c is not None else ''

def floats(s):
    try: return [float(v) for v in s.split()]
    except ValueError: return []

class Library:
    """Object types, archetypes and meshes of the common folders plus one level."""
    def __init__(self, root, folders, terrain):
        self.types, self.arch, self.mesh = {}, {}, {}   # name -> (mesh-name) / element / (element, data)
        self.lights = {}                                # light type name -> (light-type, colour 0-255, range)
        self.terrain = terrain
        for folder in folders:
            d = os.path.join(root, folder)
            if not os.path.isdir(d): continue
            for f in sorted(os.listdir(d)):
                p = os.path.join(d, f)
                if f.startswith('objecttypes-') and f.endswith('.xml'):
                    self._types(p)
                elif f.startswith('objects-') and f.endswith('.xml'):
                    lib, data = MO.load_library(p)
                    if lib is None: continue
                    for m in lib.iter('mesh'):
                        if m.find('geoset') is not None or m.find('.//geoset') is not None:
                            self.mesh[text(m, 'name')] = (m, data)
                    try: tree = ET.parse(p)
                    except ET.ParseError: continue
                    for a in tree.iter('archetype'):
                        self.arch[text(a, 'name')] = a

    def _types(self, path):
        try: r = ET.parse(path).getroot()
        except ET.ParseError: return
        par = {c: p for p in r.iter() for c in p}
        for b in r.iter('BASE'):
            n = text(b, 'name')
            if not n or b not in par: continue
            self.types[n] = (text(par[b], 'mesh-name'), text(b, 'object-type'))
            if text(b, 'object-type') == 'OT_LIGHT':
                self.lights[n] = (text(par[b], 'light-type'), floats(text(par[b], 'diffuse-color')),
                                  floats(text(par[b], 'range')))

    def model(self, arch_name, depth=0):
        """Archetype -> list of (mesh name, offset (x, y, z)) in object space."""
        a = self.arch.get(arch_name)
        if a is None or depth > 8: return []
        out = []
        ms = a.find('meshes')
        if ms is not None:
            best = max(ms.findall('mesh'), key=lambda m: floats(text(m, 'lod-switch')) or [0.0], default=None)
            if best is not None and text(best, 'mesh-name') in self.mesh:
                out.append((text(best, 'mesh-name'), (0.0, 0.0, 0.0)))
        parts = a.find('parts')
        if parts is not None:
            pos = {'root': (0.0, 0.0, 0.0)}
            joints = {}
            for j in a.iter('joint'):
                g = next((k for k in j if k.find('parent-point') is not None), None)
                if g is None: continue
                pp = floats(text(g, 'parent-point')) or [0, 0, 0]
                cp = floats(text(g, 'child-point')) or [0, 0, 0]
                joints[text(j, 'child-part')] = (text(j, 'parent-part'), [pp[i] - cp[i] for i in range(3)])
            def where(part, seen=()):
                if part in pos: return pos[part]
                if part not in joints or part in seen: return (0.0, 0.0, 0.0)
                par, off = joints[part]
                b = where(par, seen + (part,))
                pos[part] = (b[0] + off[0], b[1] + off[1], b[2] + off[2])
                return pos[part]
            for p in parts.findall('PART'):
                pn, an = text(p, 'part-name'), text(p, 'archetype-name')
                o = where(pn)
                for mn, off in self.model(an, depth + 1):
                    out.append((mn, (o[0] + off[0], o[1] + off[1], o[2] + off[2])))
        return out

# Shader materials whose colour texture's alpha is a cut-out (the 'A' in BF_<letters>_rtMaterial).
# In the others (BF_CS_, BF_CSB_, BF_CES_ ...) the alpha holds the specular mask and is not transparency.
CUTOUT = {MO.bfhash(n) for n in ('BF_CA_rtMaterial', 'BF_CAS_rtMaterial', 'BF_CAN_rtMaterial', 'BF_CASB_rtMaterial',
                                 'BF_CAES_rtMaterial', 'BF_AE_rtMaterial', 'BF_CA1_rtMaterial', 'BF_A1E1_rtMaterial',
                                 'BF_A1E1Two_rtMaterial')}

def is_cutout(shader):
    return MO.hval(shader) in CUTOUT

# ---------------------------------------------------------------- glTF writer
class Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.g = {'asset': {'version': '2.0', 'generator': 'Brute Force recomp level2glb.py'},
                  'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [],
                  'textures': [], 'images': [], 'samplers': [{'magFilter': 9729, 'minFilter': 9987,
                                                              'wrapS': 10497, 'wrapT': 10497}],
                  'accessors': [], 'bufferViews': [], 'buffers': []}
        self.mat_index, self.img_index, self.mesh_index = {}, {}, {}

    def view(self, data, target=None):
        while len(self.bin) % 4: self.bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target: v['target'] = target
        self.bin += data
        self.g['bufferViews'].append(v)
        return len(self.g['bufferViews']) - 1

    def accessor(self, fmt, ctype, typ, count, data, target, minmax=None):
        a = {'bufferView': self.view(data, target), 'componentType': ctype, 'count': count, 'type': typ}
        if minmax: a['min'], a['max'] = minmax
        self.g['accessors'].append(a)
        return len(self.g['accessors']) - 1

    def material(self, name, png, cutout=False, sky=False):
        key = (name, sky)
        if key in self.mat_index: return self.mat_index[key]
        m = {'name': name, 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.9}}
        if png:
            if png not in self.img_index:
                bv = self.view(open(png, 'rb').read())
                self.g['images'].append({'bufferView': bv, 'mimeType': 'image/png',
                                         'name': os.path.splitext(os.path.basename(png))[0]})
                self.g['textures'].append({'source': len(self.g['images']) - 1, 'sampler': 0})
                self.img_index[png] = len(self.g['textures']) - 1
            m['pbrMetallicRoughness']['baseColorTexture'] = {'index': self.img_index[png]}
            if cutout: m['alphaMode'] = 'MASK'; m['doubleSided'] = True
        if sky:   # sky layers (dome, clouds): unlit and blended
            m['alphaMode'] = 'BLEND'; m['doubleSided'] = True
            m.setdefault('extensions', {})['KHR_materials_unlit'] = {}
            if 'KHR_materials_unlit' not in self.g.setdefault('extensionsUsed', []):
                self.g['extensionsUsed'].append('KHR_materials_unlit')
        self.g['materials'].append(m)
        self.mat_index[key] = len(self.g['materials']) - 1
        return self.mat_index[key]

    def mesh(self, key, name, prims):
        """prims: list of (material index, vertices, triangles) in game coordinates."""
        if key in self.mesh_index: return self.mesh_index[key]
        out = []
        for mi, verts, tris in prims:
            if not tris: continue
            P = [(x, y, -z) for (x, y, z), _, _ in verts]
            pos = struct.pack('<%df' % (3 * len(P)), *[c for p in P for c in p])
            nrm = struct.pack('<%df' % (3 * len(verts)), *[c for _, (nx, ny, nz), _ in verts for c in (nx, ny, -nz)])
            uv = struct.pack('<%df' % (2 * len(verts)), *[c for _, _, t in verts for c in ((t[0], t[1]) if t else (0.0, 0.0))])
            idx = struct.pack('<%dI' % (3 * len(tris)), *[i for a, b, c in tris for i in (a, c, b)])
            lo = [min(p[i] for p in P) for i in range(3)]; hi = [max(p[i] for p in P) for i in range(3)]
            attrs = {'POSITION': self.accessor('f', 5126, 'VEC3', len(P), pos, 34962, (lo, hi)),
                     'NORMAL': self.accessor('f', 5126, 'VEC3', len(P), nrm, 34962),
                     'TEXCOORD_0': self.accessor('f', 5126, 'VEC2', len(P), uv, 34962)}
            prim = {'attributes': attrs, 'indices': self.accessor('I', 5125, 'SCALAR', 3 * len(tris), idx, 34963)}
            if mi is not None: prim['material'] = mi
            out.append(prim)
        if not out:
            self.mesh_index[key] = None; return None
        self.g['meshes'].append({'name': name, 'primitives': out})
        self.mesh_index[key] = len(self.g['meshes']) - 1
        return self.mesh_index[key]

    def node(self, n, parent=None):
        self.g['nodes'].append(n)
        i = len(self.g['nodes']) - 1
        if parent is None: self.g['scenes'][0]['nodes'].append(i)
        else: self.g['nodes'][parent].setdefault('children', []).append(i)
        return i

    def light(self, light):
        ext = self.g.setdefault('extensions', {}).setdefault('KHR_lights_punctual', {'lights': []})
        if 'KHR_lights_punctual' not in self.g.setdefault('extensionsUsed', []):
            self.g['extensionsUsed'].append('KHR_lights_punctual')
        ext['lights'].append(light)
        return len(ext['lights']) - 1

    def save(self, path):
        while len(self.bin) % 4: self.bin.append(0)
        self.g['buffers'] = [{'byteLength': len(self.bin)}]
        for k in ('textures', 'images', 'materials', 'meshes'):
            if not self.g[k]: del self.g[k]
        js = json.dumps(self.g, separators=(',', ':')).encode()
        js += b' ' * (-len(js) % 4)
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
            f.write(struct.pack('<II', len(self.bin), 0x004E4942)); f.write(self.bin)

def placement(t):
    """Level transform (rows R0 R1 R2, position; world = x*R0 + y*R1 + z*R2 + pos) ->
    glTF column-major 4x4 matrix in the mirrored (right-handed) frame."""
    R = [t[0:3], t[3:6], t[6:9]]; p = t[9:12]
    S = (1, 1, -1)
    # column vectors: world = M v with M[i][j] = R[j][i]; mirrored: S M S
    M = [[R[j][i] * S[i] * S[j] for j in range(3)] for i in range(3)]
    T = [p[0], p[1], -p[2]]
    return [M[0][0], M[1][0], M[2][0], 0.0, M[0][1], M[1][1], M[2][1], 0.0,
            M[0][2], M[1][2], M[2][2], 0.0, T[0], T[1], T[2], 1.0]

def light_placement(t):
    """Like placement(), but for a directional light. The game puts a directional light's source
    5000 units from its position along the third column of its matrix (R0.z, R1.z, R2.z): Light
    vtable+0x44 (0x14EC80) for LT_DIRECTIONALLIGHT. So the light shines along minus that column.
    placement() mirrors z, so a node frame built on the shining direction makes the glTF light
    (local -Z) shine that way. The hand-edited matrices are often not orthonormal (glTF needs a
    clean rotation), so the frame is rebuilt around that direction."""
    def norm(v):
        n = math.sqrt(sum(c * c for c in v)) or 1.0
        return [c / n for c in v]
    def cross(a, b):
        return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    col = [-t[2], -t[5], -t[8]]   # the direction the light shines in
    z = norm(col) if any(col) else [0.0, 0.0, 1.0]
    up = [0.0, 1.0, 0.0] if abs(z[1]) < 0.99 else [1.0, 0.0, 0.0]
    x = norm(cross(up, z)); y = cross(z, x)
    return placement(x + y + z + list(t[9:12]))

def colour(v, scale=1.0):
    return [round(min(1.0, max(0.0, c / scale)), 4) for c in (v + [0, 0, 0])[:3]]

def level_settings(L):
    """Sky colour, fog and ambient light of a level (glTF has no fog or ambient: kept as extras)."""
    out = {}
    sky = L.find('sky')
    if sky is not None and floats(text(sky, 'background-color')):
        out['background-color'] = colour(floats(text(sky, 'background-color')))
    for tag in ('object-ambient', 'character-ambient'):
        if floats(text(L, tag)): out[tag] = colour(floats(text(L, tag)))
    for t in L.iter('Terrain'):
        if floats(text(t, 'ambient')): out['terrain-ambient'] = colour(floats(text(t, 'ambient')))
    fog = L.find('object-fog')
    if fog is not None:
        out['fog'] = {'enabled': text(fog, 'enable') == 'true',
                      'color': colour(floats(text(fog, 'color')), 255.0),
                      'start-distance': (floats(text(fog, 'start-distance')) or [0])[0],
                      'end-distance': (floats(text(fog, 'end-distance')) or [0])[0]}
    return out

# ---------------------------------------------------------------- one level
def export_level(root, level, mats, pngs, terrain, all_layers=False):
    folder = os.path.join(root, level)
    lp = os.path.join(folder, 'levels-%s.xml' % level)
    if not os.path.isfile(lp): return None
    common = ['common'] + (['mp_common'] if level.startswith(('mp', 'sdm')) else [])
    lib = Library(root, common + [level], terrain)
    try: L = ET.parse(lp).getroot()
    except ET.ParseError as e:
        print('  cannot read', lp, e); return None
    G = Gltf()
    stats = {'objects': 0, 'no model': 0, 'skipped': 0}
    geo_index = {'meshes': 0, 'skipped': 0, 'terrain': 0}

    def gl_mesh(mesh_name, terrain_mesh=False, sky=False):
        key = (mesh_name, terrain_mesh, sky)
        if key in G.mesh_index: return G.mesh_index[key]
        m, data = lib.mesh[mesh_name]
        prims, layer = [], 0
        for oname, mat, verts, tris, terr in MO.mesh_geometry(m, data, terrain, geo_index):
            if terr:
                layer += 1
                if layer > 1 and not all_layers: continue
            prims.append((G.material(mat, MO.texture_png(mat, mats, pngs), is_cutout(MO.shaders.get(mat, '')), sky)
                          if mat else None, verts, tris))
        return G.mesh(key, MO.safe(mesh_name), prims)

    # terrain
    for t in L.iter('Terrain'):
        mn = text(t, 'mesh-name')
        if mn in lib.mesh:
            mi = gl_mesh(mn, True)
            if mi is not None: G.node({'name': 'terrain', 'mesh': mi})
    # sky: a large mesh drawn around the level (the game turns it slowly, <rotation> per second)
    sky = L.find('sky')
    so = sky.find('Object') if sky is not None else None
    if so is not None and text(so, 'mesh-name'):
        parts = lib.model(text(so, 'mesh-name'))
        pos = floats(text(so, 'position')) or [0.0, 0.0, 0.0]
        kids = [k for k in (gl_mesh(mn, sky=True) for mn, _ in parts) if k is not None]
        if kids:
            si = G.node({'name': 'sky', 'translation': [pos[0], pos[1], -pos[2]]})
            for k in kids: G.node({'name': 'sky mesh', 'mesh': k}, si)
    # lights (KHR_lights_punctual): directional lights shine along R2 of their transform
    lights = G.node({'name': 'lights'})
    for e in L.iter('light-object'):
        t = floats(text(e, 'transform'))
        lt = lib.lights.get(text(e, 'object-type-name'))
        if len(t) != 12 or lt is None: continue
        kind, tcol, trange = lt
        col = colour(floats(text(e, 'diffuse'))) if floats(text(e, 'diffuse')) else colour(tcol, 255.0)
        rng = (floats(text(e, 'range')) or trange or [0.0])[0]
        name = text(e, 'name').lstrip('#') or 'light'
        if kind == 'LT_DIRECTIONALLIGHT':
            li = G.light({'name': name, 'type': 'directional', 'color': col, 'intensity': 2.0})
            G.node({'name': name, 'matrix': light_placement(t),
                    'extensions': {'KHR_lights_punctual': {'light': li}}}, lights)
        else:
            li = G.light({'name': name, 'type': 'point', 'color': col,
                          'intensity': round(max(rng, 1.0) ** 2 / 4, 2), **({'range': rng} if rng > 0 else {})})
            G.node({'name': name, 'translation': [t[9], t[10], -t[11]],
                    'extensions': {'KHR_lights_punctual': {'light': li}}}, lights)
        stats['lights'] = stats.get('lights', 0) + 1
    settings = level_settings(L)
    if settings: G.g['scenes'][0]['extras'] = settings
    G.g['scenes'][0]['name'] = level
    # placed objects
    group = G.node({'name': 'objects'})
    for e in L.iter():
        if e.tag in SKIP_TAGS: continue
        tr, tn = e.find('transform'), e.find('object-type-name')
        if tr is None or tn is None: continue
        t = floats(tr.text or '')
        if len(t) != 12: continue
        ty = lib.types.get((tn.text or '').strip())
        parts = lib.model(ty[0]) if ty and ty[0] else []
        if not parts:
            stats['no model'] += 1; continue
        name = text(e, 'name') or e.tag
        node = {'name': '%s (%s)' % (name.lstrip('#'), (tn.text or '').strip().lstrip('#')), 'matrix': placement(t)}
        kids = []
        for mn, off in parts:
            mi = gl_mesh(mn)
            if mi is None: continue
            kids.append({'name': MO.safe(mn), 'mesh': mi, 'translation': [off[0], off[1], -off[2]]}
                        if any(off) else {'name': MO.safe(mn), 'mesh': mi})
        if not kids:
            stats['no model'] += 1; continue
        if len(kids) == 1 and 'translation' not in kids[0]:
            node['mesh'] = kids[0]['mesh']
            G.node(node, group)
        else:
            ni = G.node(node, group)
            for k in kids: G.node(k, ni)
        stats['objects'] += 1
    out = os.path.join(folder, '%s_scene.glb' % level)
    if not G.g['meshes']:
        return {'objects': 0, 'models': 0, 'textures': 0, 'size': 0, 'skipped': 0, 'empty': True}
    G.save(out)
    stats['skipped'] = geo_index['skipped']
    stats['models'] = len(G.g.get('meshes', []))
    stats['textures'] = len(G.g.get('images', []))
    stats['size'] = os.path.getsize(out)
    return stats

def main(args):
    all_layers = '--all-layers' in args
    args = [a for a in args if not a.startswith('--')]
    if not args:
        print(__doc__); return 1
    root = args[0]
    levels = args[1:] or sorted(d for d in os.listdir(root)
                                if os.path.isfile(os.path.join(root, d, 'levels-%s.xml' % d)))
    print('indexing materials and textures...', flush=True)
    mats, pngs = MO.index_data(root)
    print('  %d materials, %d texture PNGs found' % (len(mats), len(pngs)))
    terrain = MO.terrain_params(root)
    for i, lv in enumerate(levels, 1):
        s = export_level(root, lv, mats, pngs, terrain, all_layers)
        if s is None:
            print('[%d/%d] %s: no level file' % (i, len(levels), lv)); continue
        if s.get('empty'):
            print('[%d/%d] %s: nothing to place' % (i, len(levels), lv)); continue
        print('[%d/%d] %-14s %4d objects placed, %3d lights, %3d models, %3d textures, %5.1f MB'
              % (i, len(levels), lv, s['objects'], s.get('lights', 0), s['models'], s['textures'], s['size'] / 1e6),
              flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
