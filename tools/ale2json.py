r"""Brute Force (Xbox) effects exporter: Alchemy effect library (.ale) -> JSON.

Brute Force's particle effects (muzzle flashes, explosions, blood, smoke, beams,
shields, ...) are all in common\effects-common.ale. This writes one JSON file per
effect into effects-common_json\ next to it, with the effect's node tree, every
node's class and named properties, and links to the texture PNGs from
export_textures.bat, plus an index of all effects (_index.json).

Usage:
    python ale2json.py <data folder>

Format (reverse-engineered; it is the "Alchemy" effect system Digital Anvil also
used in Freelancer):
  The file is a UTF tree ('UTF ' header, 44-byte nodes, string block, data block)
  with two leaves:
  AlchemyNodeLibrary (version 1.2): node count, then per node its class (hash of the
    class name, e.g. FxConeEmitter) and properties until a 0 type word. A property is
    type (u16), name hash (u32), value. Types: 1 bool (value in bit 15), 2 int,
    3 float, 4 enum, 0x103 string (u16 length, padded to even), 0x104 pair of ints
    (blend: D3DBLEND source, destination), 0x105 transform (u32 flags + 9 curves:
    translate x y z, rotate x y z, scale x y z), 0x200 float animation, 0x201 colour
    animation, 0x202 curve.
  ALEffectLib (version 1.1): effect count, then per effect: name, bounding sphere
    (x, y, z, radius), node references (flags, node-name hash, parent index or
    0x8000 for none, index) and pairs (emitter index -> appearance/field index).
  Node, class, effect and texture names are hashed with the game's usual name hash
  (bfhash); the property names come from the property tables in default.xbe.
Pure Python standard library.
"""
import json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bfhash import bfhash

PROPS = {
    0x10423ceb: 'AirField_Approach',
    0xe5e3524c: 'AirField_Magnitude',
    0x0c53b008: 'Appearance_LODCurve',
    0x04fc9016: 'BasicApp_Alpha',
    0x1506eb6c: 'BasicApp_BlendInfo',
    0x048767e8: 'BasicApp_Color',
    0xf52b8dd5: 'BasicApp_CommonTexFrame',
    0xf3d52ee4: 'BasicApp_FlipTexU',
    0xe8dc7f5e: 'BasicApp_FlipTexV',
    0xe0e97650: 'BasicApp_HToVAspect',
    0xe91467f1: 'BasicApp_MotionBlur',
    0xf87b5fd5: 'BasicApp_QuadTexture',
    0x05db630e: 'BasicApp_Rotate',
    0xf7c2eba9: 'BasicApp_Size',
    0x015a6f47: 'BasicApp_TexFrame',
    0x1ba23359: 'BasicApp_TexName',
    0xf863872e: 'BasicApp_TriTexture',
    0xe2f60eeb: 'BasicApp_UseCommonTexFrame',
    0x03503b61: 'BeamApp_ConnectSegments',
    0x1c65b7b9: 'BeamApp_LineAppearance',
    0x0ba0b3bb: 'BeamApp_TargetTransform',
    0x0abe0402: 'BeamApp_UseSNormalizedVel',
    0x11336c98: 'CollideField_Height',
    0xf2468210: 'CollideField_Reflectivity',
    0x138bdb51: 'CollideField_Width',
    0xf636f07f: 'ConeEmitter_MaxRadius',
    0xfa4b5e41: 'ConeEmitter_MaxSpread',
    0x1c73d16b: 'ConeEmitter_MinRadius',
    0x100e7f55: 'ConeEmitter_MinSpread',
    0x14b74eb2: 'CubeEmitter_Depth',
    0xefb0f8cf: 'CubeEmitter_Height',
    0x08c7217f: 'CubeEmitter_MaxSpread',
    0xe282006b: 'CubeEmitter_MinSpread',
    0xe2999ffd: 'CubeEmitter_OffsetHeight',
    0xfd22a11c: 'CubeEmitter_Width',
    0xe7221f95: 'Emitter_EmitCount',
    0x023c350c: 'Emitter_Frequency',
    0x0a635880: 'Emitter_InitLifeSpan',
    0x0f9a9d52: 'Emitter_InitialParticles',
    0xfe077e05: 'Emitter_LODCurve',
    0x0ab180c5: 'Emitter_Pressure',
    0x0681233e: 'Emitter_VelocityApproach',
    0xf8e7645a: 'GeneralApp_Alpha',
    0x0d645074: 'GeneralApp_AngleFadeOut',
    0x04ca8bbc: 'GeneralApp_ApplyEnvironmentColor',
    0x0d36e978: 'GeneralApp_ApplyParticleTransform',
    0x1daea8c0: 'GeneralApp_BlendInfo',
    0xf89c93a4: 'GeneralApp_Color',
    0xe3542300: 'GeneralApp_FacingType',
    0xfe7c2071: 'GeneralApp_MotionBlur',
    0x1f2cb4d7: 'GeneralApp_ParticleTransform',
    0xf24d7541: 'GeneralApp_Rotate',
    0x00fac601: 'GeneralApp_Scale',
    0x04b5fc1b: 'GeneralApp_ScaleX',
    0x0db2cc8d: 'GeneralApp_ScaleY',
    0x17d45550: 'GeneralApp_ShapeType',
    0x18657e4a: 'GeneralApp_TexFrame',
    0xf736f94e: 'GeneralApp_TexName',
    0xe02b8bd4: 'GravityField_Gravity',
    0x1bf425dd: 'KillField_Height',
    0x09df410d: 'KillField_Width',
    0x0b7ee41c: 'MeshApp_MeshId',
    0x18dadca8: 'MeshApp_MeshName',
    0xf0004d58: 'MeshApp_ParticleTransform',
    0x001fc112: 'MeshApp_UseParticleTransform',
    0xf54ef296: 'Node_ClassName',
    0xf27fde7d: 'Node_LifeSpan',
    0xefa8ce01: 'Node_Name',
    0xe13a59a1: 'Node_Transform',
    0xf267b8e1: 'OrientedApp_Height',
    0xfa03992f: 'OrientedApp_Width',
    0x03b76e6c: 'ParticleApp_DeathName',
    0x0ec7a290: 'ParticleApp_LifeName',
    0x049a6dbe: 'ParticleApp_SmoothRotation',
    0xe34c3c55: 'ParticleApp_UseDynamicRotation',
    0xf5d84839: 'PerpApp_AppearanceTransform',
    0x098cea00: 'PerpApp_UseAppearanceVel',
    0xf3cf5ea5: 'RadialField_Approach',
    0x1a433168: 'RadialField_Attenuation',
    0x0064b3b9: 'RadialField_Magnitude',
    0xff28f620: 'RadialField_Radius',
    0xfb7958c1: 'RectApp_CenterOnPos',
    0xe48ddae2: 'RectApp_Length',
    0xfba203b8: 'RectApp_Scale',
    0xf9febf0d: 'RectApp_ViewingAngleFade',
    0x1266735f: 'RectApp_Width',
    0x1874de74: 'SphereEmitter_MaxRadius',
    0xf231ff60: 'SphereEmitter_MinRadius',
    0x1b970b9a: 'TurbulenceField_Approach',
    0x1ceac6d1: 'TurbulenceField_Magnitude',
}

CLASSES = {bfhash(n): n for n in (
    'FxGeneralAppearance', 'FxParticleAppearance', 'BFBeamAppearance',
    'FxConeEmitter', 'FxCubeEmitter', 'BFCubeEmitter', 'FxSphereEmitter',
    'FxGravityField', 'FxTurbulenceField', 'FxAirField', 'FxRadialField', 'BFKillField')}
BLEND = {1: 'ZERO', 2: 'ONE', 3: 'SRCCOLOR', 4: 'INVSRCCOLOR', 5: 'SRCALPHA', 6: 'INVSRCALPHA',
         7: 'DESTALPHA', 8: 'INVDESTALPHA', 9: 'DESTCOLOR', 10: 'INVDESTCOLOR'}
AXES = ['translate_x', 'translate_y', 'translate_z', 'rotate_x', 'rotate_y', 'rotate_z',
        'scale_x', 'scale_y', 'scale_z']
NODE_NAME = 0xefa8ce01

def utf_leaves(b):
    magic, ver, nbo, nbs, _, nsz, sbo, sba, sbu, dbo = struct.unpack_from('<4s9I', b, 0)
    if magic != b'UTF ': raise ValueError('not a UTF file')
    strs = b[sbo:sbo + sbu]
    name = lambda o: strs[o:strs.index(b'\0', o)].decode('latin-1')
    out = {}
    def walk(off, path):
        while True:
            peer, no, flags, _, child, alloc, size, size2 = struct.unpack_from('<8I', b, nbo + off)
            n = name(no)
            if flags & 0x80: out[n] = b[dbo + child:dbo + child + size]
            elif child: walk(child, path + '/' + n)
            if not peer: break
            off = peer
    walk(0, '')
    return out

class Reader:
    def __init__(self, b): self.b = b; self.p = 0
    def take(self, fmt):
        v = struct.unpack_from('<' + fmt, self.b, self.p); self.p += struct.calcsize('<' + fmt)
        return v if len(v) > 1 else v[0]
    def string(self):
        n = self.take('H'); s = self.b[self.p:self.p + n]; self.p += n + (n & 1)
        return s.rstrip(b'\0').decode('latin-1')

def r4(x): return round(x, 5)

def curve(r):
    interp, n = r.take('BB'); items = []
    for _ in range(n):
        s, value, loop, count = r.take('ffHH')
        keys = [[r4(v) for v in r.take('4f')] for _ in range(count)]
        it = {'at': r4(s), 'value': r4(value)}
        if keys: it.update(loop=loop, keys=keys)   # keys: time, value, in, out
        items.append(it)
    return {'interp': interp, 'items': items}

def keyed(r, colour):
    interp, n = r.take('BB'); items = []
    for _ in range(n):
        s, t, c = r.take('fBB')
        keys = []
        for _ in range(c):
            if colour:
                k = r.take('4f'); keys.append([r4(k[0]), [r4(v) for v in k[1:]]])
            else:
                keys.append([r4(v) for v in r.take('2f')])
        items.append({'at': r4(s), 'interp': t, 'keys': keys})
    return {'interp': interp, 'items': items}

def value(r, typ):
    t = typ & 0x7fff
    if t == 1: return bool(typ & 0x8000)
    if t in (2, 4): return r.take('I')
    if t == 3: return r4(r.take('f'))
    if t == 0x103: return r.string()
    if t == 0x104:
        a, b = r.take('II'); return {'source': BLEND.get(a, a), 'dest': BLEND.get(b, b)}
    if t == 0x105:
        flags = r.take('I'); return {'flags': flags, **{ax: curve(r) for ax in AXES}}
    if t == 0x200: return keyed(r, False)
    if t == 0x201: return keyed(r, True)
    if t == 0x202: return curve(r)
    raise ValueError('unknown property type %#x at %#x' % (typ, r.p))

def read_nodes(data):
    r = Reader(data); version, n = r.take('fI'); nodes = {}
    for _ in range(n):
        cls = r.take('I'); props = {}
        while True:
            typ = r.take('H')
            if typ == 0: break
            h = r.take('I')
            props[PROPS.get(h, '#%08x' % h)] = value(r, typ)
        name = props.get('Node_Name', '')
        nodes[bfhash(name)] = {'name': name, 'class': CLASSES.get(cls, '#%08x' % cls), 'properties': props}
    return round(version, 3), nodes

def read_effects(data):
    r = Reader(data); version, n = r.take('fI'); fx = []
    for _ in range(n):
        name = r.string(); bound = [r4(v) for v in r.take('4f')]
        refs = [r.take('4I') for _ in range(r.take('I'))]
        pairs = [r.take('2I') for _ in range(r.take('I'))]
        fx.append((name, bound, refs, pairs))
    return round(version, 3), fx

def texture_index(root):
    """Texture name hash -> PNG path (PNGs from tex2png.py, named by hash or name)."""
    out = {}
    for d, _, files in os.walk(root):
        if d.endswith('_png'):
            for f in files:
                key = os.path.splitext(f)[0].replace('_p8', '').replace('_cube', '')
                try: h = int(key, 16) if re.fullmatch(r'[0-9a-f]{8}', key) else bfhash(key)
                except ValueError: continue
                out.setdefault(h, os.path.join(d, f))
    # animated textures: a list of frames (uv rectangles) on frame-texture sheets
    anim = {}
    for d, _, files in os.walk(root):
        for f in files:
            if f.startswith('textures-') and f.endswith('.xml'):
                text = open(os.path.join(d, f), encoding='latin-1').read()
                for m in re.finditer(r'<animated-texture name="#([0-9a-f]{8})"[^>]*>(.*?)</animated-texture>', text, re.S):
                    sheet = re.search(r'<frame-texture name="#([0-9a-f]{8})"', m.group(2))
                    if sheet:
                        anim.setdefault(int(m.group(1), 16), (int(sheet.group(1), 16), m.group(2).count('<frame ')))
    for h, (sheet, frames) in anim.items():
        if h not in out and sheet in out: out[h] = (out[sheet], frames)
    return out

def export(path, pngs):
    leaves = utf_leaves(open(path, 'rb').read())
    nver, nodes = read_nodes(leaves['AlchemyNodeLibrary'])
    ever, effects = read_effects(leaves['ALEffectLib'])
    out_dir = os.path.splitext(path)[0] + '_json'
    os.makedirs(out_dir, exist_ok=True)
    index, missing, used = [], 0, set()
    for name, bound, refs, pairs in effects:
        items = []
        for flags, h, parent, idx in refs:
            n = nodes.get(h)
            it = {'index': idx}
            if parent != 0x8000: it['parent'] = parent
            if n:
                used.add(h)
                it.update(name=n['name'], **{'class': n['class']})
                props = dict(n['properties'])
                for key in ('GeneralApp_TexName', 'BasicApp_TexName'):
                    tex = props.get(key)
                    if tex:
                        png = pngs.get(bfhash(tex)) or pngs.get(bfhash(os.path.splitext(tex)[0]))
                        if isinstance(png, tuple):          # animated: frame sheet + frame count
                            it['texture_frames'] = png[1]; png = png[0]
                        if png: it['texture_png'] = os.path.relpath(png, out_dir).replace('\\', '/')
                it['properties'] = props
            elif flags & 1:
                it['group'] = True        # a grouping/attachment node with no definition of its own
            else:
                it['missing'] = '#%08x' % h; missing += 1
            items.append(it)
        doc = {'name': name, 'bounding_sphere': {'center': bound[:3], 'radius': bound[3]},
               'nodes': items, 'links': [{'from': a, 'to': b} for a, b in pairs]}
        fn = re.sub(r'[\\/:*?"<>|#]', '_', name) or 'effect'
        with open(os.path.join(out_dir, fn + '.json'), 'w') as f:
            json.dump(doc, f, indent=1)
        index.append({'name': name, 'file': fn + '.json', 'nodes': len(refs),
                      'classes': sorted({i.get('class') for i in items if i.get('class')})})
    with open(os.path.join(out_dir, '_index.json'), 'w') as f:
        json.dump(index, f, indent=1)
    unused = len(nodes) - len(used)
    return len(effects), len(nodes), missing, unused

def main(args):
    if not args:
        print(__doc__); return 1
    root = args[0]
    pngs = texture_index(root)
    for d, _, files in os.walk(root):
        for f in files:
            if f.endswith('.ale'):
                p = os.path.join(d, f)
                n, nn, miss, unused = export(p, pngs)
                print('%s: %d effects, %d nodes; %d references to nodes not in the file, %d nodes unused'
                      % (p, n, nn, miss, unused))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
