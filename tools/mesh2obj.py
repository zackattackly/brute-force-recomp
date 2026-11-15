"""Brute Force (Xbox) mesh exporter: mesh libraries -> Wavefront OBJ + MTL.

Reads each mesh library (objects-*.xml, made by xmb2xml / convert_all) and its
data file (objects-*.ivd), and writes objects-NAME.obj + objects-NAME.mtl next
to it. Materials are looked up in the materials-*.xml files of the whole data
folder, and their colour texture is linked to the PNG made by tex2png.py
(run export_textures.bat first to get textures in the viewer).

Usage:
    python mesh2obj.py <data folder>
    python mesh2obj.py <data folder> <objects-x.xml> ...   (only these)

Layout (reverse-engineered):
  .ivd = all 16-bit index data first, then the vertex buffers.
  Each geoset batch gives: primitive (strip / list), index count, index byte
  offset, vertex type, vertex count, vertex byte offset.
  Vertex types: 24 bytes = position float3, packed normal (11:11:10), uv float2;
  32 bytes = the same plus 8 more bytes (second set / colour / skin data);
  16 bytes = position + packed normal (no uv).
  Skinned meshes (#13d18cfc) store their indices as an Xbox push-buffer (read_indices)
  and are written in their rest pose.
  Terrain (#ef44f398, 32 bytes, one buffer shared by a whole geoset): the position is
  a packed grid cell plus height (terrain_vertex); the grid width and cell spacing come
  from the level's <Terrain> entry. Each terrain geoset (texture layer) is its own object.
Materials: a mesh's material (type BF_GameMaterial) lists no textures; the shader
material stored right after it in the materials file holds them. The shader type
names what its textures hold: BF_<letters>_rtMaterial with C colour, A alpha
(cut-out), S specular (kept in the colour texture's alpha), E emissive, B bump.
Pure Python standard library.
"""
import os, re, struct, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    from bfhash import bfhash
except Exception:
    bfhash = None

STRIDE = {0x1fe75072: 24, 0xe90647a9: 32, 0x13d18cfc: 32, 0xe7fd942a: 32, 0x0ade1ce7: 16}
TERRAIN = 0xef44f398
PRIM_STRIP, PRIM_LIST = 0x1d62e437, 0x0e7ab726

terrain_layers = 0
shaders = {}   # material name -> shader material type (filled by index_data)

def hval(s):
    """Hash value of a converted attribute: '#xxxxxxxx' or a recovered name."""
    s = (s or '').strip()
    if s.startswith('#'):
        try: return int(s[1:], 16)
        except ValueError: return None
    if s.lstrip('-').isdigit(): return int(s)
    return bfhash(s) if (bfhash and s) else None

def safe(s):
    return re.sub(r'[^A-Za-z0-9_.-]', '_', s.lstrip('#')) or 'unnamed'

def np3(u):
    x = u & 0x7ff; y = (u >> 11) & 0x7ff; z = (u >> 22) & 0x3ff
    if x & 0x400: x -= 0x800
    if y & 0x400: y -= 0x800
    if z & 0x200: z -= 0x400
    return x / 1023.0, y / 1023.0, z / 511.0

def children_text(el):
    return [(c.tag, (c.text or '').strip()) for c in el]

def find_batches(geoset):
    """Batch elements = elements that have a 'vertex-type' child."""
    for el in geoset.iter():
        if el.find('vertex-type') is not None:
            vals = [t for _, t in children_text(el)]
            # order: prim, index count, index offset, vertex-type, vertex count, vertex offset, ...
            try:
                vi = [c.tag for c in el].index('vertex-type')
                prim = hval(vals[vi - 3]); ic = int(vals[vi - 2]); io = int(vals[vi - 1])
                vt = hval(vals[vi]); vc = int(vals[vi + 1]); vo = int(vals[vi + 2])
            except (ValueError, IndexError):
                continue
            yield prim, ic, io, vt, vc, vo

def read_indices(data, prim, ic, io):
    """Index data of a batch -> (primitive, indices).
    Small counts are plain 16-bit index lists. When the count field is larger than
    0xFFFF it packs (push-buffer bytes << 16 | index count) and the data at io is an
    Xbox NV2A push-buffer: SET_BEGIN_END (0x17FC, 5 = triangle list, 6 = strip),
    ARRAY_ELEMENT16 (0x1800, two indices per dword), ARRAY_ELEMENT32 (0x1808)."""
    if ic <= 0xffff:
        if io + ic * 2 > len(data): return prim, None
        return prim, struct.unpack_from('<%dH' % ic, data, io)
    count = ic & 0xffff; end = io + (ic >> 16) * 2 + 8
    p, idx, nvprim = io, [], 5
    while p + 4 <= min(end, len(data)):
        w = struct.unpack_from('<I', data, p)[0]; p += 4
        cnt = (w >> 18) & 0x7ff; meth = w & 0x1ffc
        if meth == 0x17fc:
            val = struct.unpack_from('<I', data, p)[0]; p += 4 * cnt
            if val == 0: break
            nvprim = val
        elif meth == 0x1800:
            for _ in range(cnt):
                idx += struct.unpack_from('<2H', data, p); p += 4
        elif meth == 0x1808:
            for _ in range(cnt):
                idx.append(struct.unpack_from('<I', data, p)[0] & 0xffff); p += 4
        else:
            return prim, None
    return (PRIM_LIST if nvprim == 5 else PRIM_STRIP), idx[:count]

# ---------------------------------------------------------------- terrain
def terrain_params(root):
    """Terrain mesh name -> (width in blocks, cell spacing), from the levels-*.xml
    <Terrain> sections (width, length, spacing _e023cdc9, mesh-name)."""
    out = {}
    pat = re.compile(r'<Terrain>\s*<width>([^<]*)</width>\s*<length>[^<]*</length>\s*'
                     r'<(?:_e023cdc9|resolution)>([^<]*)</(?:_e023cdc9|resolution)>\s*<mesh-name>([^<]*)</mesh-name>')
    for d, _, files in os.walk(root):
        for f in files:
            if f.startswith('levels-') and f.endswith('.xml'):
                for m in pat.finditer(open(os.path.join(d, f), encoding='latin-1').read()):
                    try: out[m.group(3).strip()] = (int(m.group(1)), float(m.group(2)))
                    except ValueError: pass
    return out

def terrain_grid(a, radix):
    """Grid cell (x, z) of a terrain vertex; the sign of z comes from the height field."""
    m = abs(a)
    return m % radix, m // radix

def guess_radix(data, vo, vc):
    """For terrain without a level entry: the grid width (8 * blocks + 1) for which
    uv set 1 is an exact planar projection of the grid."""
    n = min(vc, 400); step = max(1, vc // n)
    rows = [struct.unpack_from('<2h', data, vo + 32 * k) for k in range(0, vc, step)]
    uv1 = [struct.unpack_from('<2f', data, vo + 32 * k + 16) for k in range(0, vc, step)]
    best = None
    for blocks in range(1, 33):
        R = blocks * 8 + 1
        pts = []
        for (a, b), (u, v) in zip(rows, uv1):
            x, z = terrain_grid(a, R)
            pts.append((x if a >= 0 else -x, z if b >= 0 else -z, u))
        # least squares u = p*x + q*z + r  (normal equations, 3x3)
        S = [[0.0] * 4 for _ in range(3)]
        for x, z, u in pts:
            r = (x, z, 1.0)
            for i in range(3):
                for j in range(3): S[i][j] += r[i] * r[j]
                S[i][3] += r[i] * u
        try:
            for i in range(3):
                piv = S[i][i]
                for j in range(i, 4): S[i][j] /= piv
                for k in range(3):
                    if k != i:
                        f = S[k][i]
                        for j in range(i, 4): S[k][j] -= f * S[i][j]
        except ZeroDivisionError:
            continue
        err = sum((S[0][3] * x + S[1][3] * z + S[2][3] - u) ** 2 for x, z, u in pts)
        if best is None or err < best[0]: best = (err, R)
    return best[1] if best else 65

def terrain_vertex(data, p, radix, spacing):
    """Terrain vertex (32 bytes, vertex type #ef44f398), as decoded from the game's
    terrain vertex shader (0x3D8BF8): SHORT2 (a, b) at 0, packed normal at 4, uv0 at 8,
    uv1 at 16, NORMSHORT3 at 24, PBYTE2 at 30.
      |a| = z_cell * radix + x_cell,  radix = 8 * terrain width in blocks + 1
      x = sign(a) * x_cell * spacing,  z = sign(b) * z_cell * spacing,  y = |b| / 16"""
    a, b = struct.unpack_from('<2h', data, p)
    xc, zc = terrain_grid(a, radix)
    return ((xc if a >= 0 else -xc) * spacing, abs(b) / 16.0, (zc if b >= 0 else -zc) * spacing)

def triangles(prim, idx):
    if prim == PRIM_LIST:
        for i in range(0, len(idx) - 2, 3):
            yield idx[i], idx[i + 1], idx[i + 2]
    else:
        for i in range(len(idx) - 2):
            a, b, c = idx[i], idx[i + 1], idx[i + 2]
            if a == b or b == c or a == c: continue
            yield (a, b, c) if i % 2 == 0 else (b, a, c)

# ---------------------------------------------------------------- materials / textures
def colour_texture(m):
    first = None
    for t in m.iter('texture'):
        pid = t.find('param-id'); tn = t.find('texture-name')
        if tn is None or not (tn.text or '').strip(): continue
        name = (tn.text or '').strip()
        if pid is not None and (pid.text or '').strip().lower() == 'color':
            return name
        if first is None: first = name
    return first

def index_data(root):
    mats, pngs = {}, {}
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            if f.endswith('.png') and d.endswith('_png'):
                pngs.setdefault(os.path.splitext(f)[0].replace('_p8', '').replace('_cube', ''), p)
            elif f.startswith('materials-') and f.endswith('.xml'):
                try: tree = ET.parse(p)
                except ET.ParseError: continue
                lst = list(tree.iter('Material'))
                texs = [colour_texture(m) for m in lst]
                for i, m in enumerate(lst):
                    n = m.find('name')
                    if n is None: continue
                    tex, shader = texs[i], m.find('type')
                    # Mesh materials (type BF_GameMaterial) carry no textures; the shader
                    # material stored right after each of them holds the textures.
                    if tex is None and i + 1 < len(lst):
                        tex, shader = texs[i + 1], lst[i + 1].find('type')
                    name = (n.text or '').strip()
                    if mats.get(name) is None:
                        mats[name] = tex
                        shaders[name] = (shader.text or '').strip() if shader is not None else ''
    return mats, pngs

def mesh_geometry(mesh, data, terrain, index):
    """Geometry of one <mesh>: yields (object name, material, vertices, triangles, is terrain), where a
    vertex is ((x, y, z), (nx, ny, nz), (u, v) or None) in the game's own (left-handed)
    coordinates and triangles index the vertex list. Bad batches are counted in index['skipped']."""
    nm = mesh.find('name'); mname = safe(nm.text if nm is not None and nm.text else 'mesh%d' % index['meshes'])
    for gi, geoset in enumerate(mesh.iter('geoset')):
        mat = geoset.find('material-name'); mat = (mat.text or '').strip() if mat is not None else ''
        batches = list(find_batches(geoset))
        tb = [b for b in batches if b[3] == TERRAIN]
        if tb:
            # terrain: all batches of the geoset share one vertex buffer
            vo = tb[0][5]; vc = max(b[4] for b in tb)
            if vo + vc * 32 > len(data):
                index['skipped'] += len(tb); continue
            width, spacing = terrain.get((nm.text or '').strip(), (None, 2.0)) if nm is not None else (None, 2.0)
            radix = width * 8 + 1 if width else guess_radix(data, vo, vc)
            verts = []
            for k in range(vc):
                p = vo + 32 * k
                verts.append((terrain_vertex(data, p, radix, spacing),
                              np3(struct.unpack_from('<I', data, p + 4)[0]),
                              struct.unpack_from('<2f', data, p + 8)))
            tris = []
            for prim, ic, io, vt, _, _ in tb:
                prim, idx = read_indices(data, prim, ic, io)
                if idx is None:
                    index['skipped'] += 1; continue
                tris += [t for t in triangles(prim, idx) if max(t) < vc]
            index['terrain'] += 1
            yield '%s_layer%d' % (mname, gi), mat, verts, tris, True
            batches = [b for b in batches if b[3] != TERRAIN]
        for prim, ic, io, vt, vc, vo in batches:
            stride = STRIDE.get(vt)
            prim, idx = read_indices(data, prim, ic, io)
            if not stride or vo + vc * stride > len(data) or idx is None:
                index['skipped'] += 1; continue
            verts = []
            for k in range(vc):
                p = vo + k * stride
                verts.append((struct.unpack_from('<3f', data, p),
                              np3(struct.unpack_from('<I', data, p + 12)[0]),
                              struct.unpack_from('<2f', data, p + 16) if stride >= 24 else None))
            yield mname, mat, verts, [t for t in triangles(prim, idx) if max(t) < vc], False

def load_library(xml_path):
    """-> (<mesh-library> element, its data file bytes) or (None, None)."""
    try: tree = ET.parse(xml_path)
    except ET.ParseError as e:
        print('  cannot read', xml_path, e); return None, None
    lib = None
    for el in tree.iter('mesh-library'): lib = el; break
    if lib is None: return None, None
    df = lib.find('data-file')
    data_path = os.path.join(os.path.dirname(xml_path), (df.text or '').strip() if df is not None else '')
    if not os.path.isfile(data_path):
        print('  data file missing:', data_path); return None, None
    return lib, open(data_path, 'rb').read()

def export(xml_path, mats, pngs, out_root, terrain=None):
    global terrain_layers
    terrain = terrain or {}
    lib, data = load_library(xml_path)
    if lib is None: return 0, 0
    base = os.path.splitext(xml_path)[0]
    obj_lines, used_mats = ['mtllib %s.mtl' % os.path.basename(base)], set()
    vbase = 1
    index = {'meshes': 0, 'skipped': 0, 'terrain': 0}
    for mesh in lib.iter('mesh'):
        wrote = False
        for oname, mat, verts, tris, terr in mesh_geometry(mesh, data, terrain, index):
            if terr or not wrote:
                obj_lines.append('o %s' % oname); wrote = True
            if mat:
                obj_lines.append('usemtl %s' % safe(mat)); used_mats.add(mat)
            for (x, y, z), (nx, ny, nz), uv in verts:
                obj_lines.append(('v %.4f %.4f %.4f' if terr else 'v %.6f %.6f %.6f') % (x, y, -z))
                obj_lines.append('vn %.4f %.4f %.4f' % (nx, ny, -nz))
                obj_lines.append('vt %.6f %.6f' % (uv[0], 1.0 - uv[1]) if uv else 'vt 0 0')
            for a, b, c in tris:
                a += vbase; b += vbase; c += vbase
                # flip winding because z was mirrored
                obj_lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d' % (a, a, a, c, c, c, b, b, b))
            vbase += len(verts)
        if wrote: index['meshes'] += 1
    terrain_layers += index['terrain']
    meshes, skipped = index['meshes'], index['skipped']
    if meshes == 0:
        return 0, skipped
    open(base + '.obj', 'w').write('\n'.join(obj_lines) + '\n')
    with open(base + '.mtl', 'w') as f:
        for m in sorted(used_mats):
            f.write('newmtl %s\nKd 1 1 1\n' % safe(m))
            png = texture_png(m, mats, pngs)
            if png:
                f.write('map_Kd %s\n' % os.path.relpath(png, os.path.dirname(base)).replace('\\', '/'))
            f.write('\n')
    return meshes, skipped

def texture_png(mat, mats, pngs):
    """Material name -> path of its colour texture's PNG (from tex2png.py), or None."""
    tex = mats.get(mat)
    if not tex: return None
    key = safe(tex)
    if tex.startswith('#'): key = tex[1:]
    return pngs.get(key)

def main(args):
    if not args:
        print(__doc__); return 1
    root = args[0]
    libs = args[1:]
    if not libs:
        for d, _, files in os.walk(root):
            libs += [os.path.join(d, f) for f in files if f.startswith('objects-') and f.endswith('.xml')]
    print('indexing materials and textures...', flush=True)
    mats, pngs = index_data(root)
    print('  %d materials, %d texture PNGs found' % (len(mats), len(pngs)))
    terrain = terrain_params(root)
    tm = ts = 0
    for i, x in enumerate(sorted(libs), 1):
        print('[%d/%d] %s' % (i, len(libs), x), flush=True)
        m, s = export(x, mats, pngs, root, terrain)
        tm += m; ts += s
    print('wrote %d meshes (%d terrain layers); %d batches skipped' % (tm, terrain_layers, ts))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
