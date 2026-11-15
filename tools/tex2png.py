"""Brute Force (Xbox) texture exporter.

Reads a texture library (textures-*.xml, made by xmb2xml / convert_all) and its
data file (textures-*.tex) and writes every texture's top mip level as a PNG.

Usage:
    python tex2png.py <folder>              (all texture libraries under folder)
    python tex2png.py <textures-x.xml> ...  (specific libraries)

Output goes next to each library, in a folder named after it
(textures-x.xml -> textures-x_png/). Pure Python standard library, no extra
packages needed.

Formats (Xbox D3DFORMAT numbers, as stored in the library's format attribute):
    0  L8        swizzled, 8-bit grey
    6  A8R8G8B8  swizzled
    7  X8R8G8B8  swizzled
    11 P8        swizzled 8-bit palette indices; no palette is stored with
                 the texture, so it is written as greyscale indices
    12 DXT1
    14 DXT2/3
    15 DXT4/5
    18 LIN_A8R8G8B8 linear
    25 A8        swizzled, 8-bit alpha (written as grey)
    negative     cube map of format -n: the six faces are written side by side
    46           empty render-target placeholder (skipped)
    animated textures (<animated-texture>) only list frames of the above
"""
import os, re, struct, sys, zlib

# ---------------------------------------------------------------- PNG writer
def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 6)))
        f.write(chunk(b'IEND', b''))

# ---------------------------------------------------------------- swizzle
def swizzle_table(w, h):
    """Xbox swizzle: texel (x, y) is at Morton index with x in the lowest bit."""
    xs = [0] * w
    ys = [0] * h
    bit = 1; ww, hh = w, h; xb = yb = 0
    while ww > 1 or hh > 1:
        if ww > 1:
            for x in range(w):
                if (x >> xb) & 1: xs[x] |= bit
            bit <<= 1; xb += 1; ww >>= 1
        if hh > 1:
            for y in range(h):
                if (y >> yb) & 1: ys[y] |= bit
            bit <<= 1; yb += 1; hh >>= 1
    return xs, ys

def unswizzle(data, w, h, bpp):
    xs, ys = swizzle_table(w, h)
    out = bytearray(w * h * bpp)
    o = 0
    for y in range(h):
        yo = ys[y]
        for x in range(w):
            i = (yo | xs[x]) * bpp
            out[o:o + bpp] = data[i:i + bpp]
            o += bpp
    return out

# ---------------------------------------------------------------- DXT
def _c565(c):
    r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

def decode_dxt(data, w, h, kind):
    out = bytearray(w * h * 4)
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    bs = 8 if kind == 1 else 16
    p = 0
    for by in range(bh):
        for bx in range(bw):
            blk = data[p:p + bs]; p += bs
            if len(blk) < bs: return out
            if kind == 1:
                cb = blk
                alpha = None
            else:
                cb = blk[8:]
                if kind == 3:
                    a64 = int.from_bytes(blk[:8], 'little')
                    alpha = [((a64 >> (4 * i)) & 15) * 17 for i in range(16)]
                else:
                    a0, a1 = blk[0], blk[1]
                    if a0 > a1:
                        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                    else:
                        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                    bits = int.from_bytes(blk[2:8], 'little')
                    alpha = [pal[(bits >> (3 * i)) & 7] for i in range(16)]
            c0, c1 = struct.unpack_from('<HH', cb)
            r0, r1 = _c565(c0), _c565(c1)
            if c0 > c1 or kind != 1:
                cols = [r0 + (255,), r1 + (255,),
                        tuple((2 * a + b) // 3 for a, b in zip(r0, r1)) + (255,),
                        tuple((a + 2 * b) // 3 for a, b in zip(r0, r1)) + (255,)]
            else:
                cols = [r0 + (255,), r1 + (255,),
                        tuple((a + b) // 2 for a, b in zip(r0, r1)) + (255,), (0, 0, 0, 0)]
            idx = struct.unpack_from('<I', cb, 4)[0]
            for i in range(16):
                y = by * 4 + (i >> 2); x = bx * 4 + (i & 3)
                if x >= w or y >= h: continue
                c = cols[(idx >> (2 * i)) & 3]
                o = (y * w + x) * 4
                out[o:o + 3] = bytes(c[:3])
                out[o + 3] = c[3] if alpha is None else alpha[i]
    return out

# ---------------------------------------------------------------- formats
BPP = {0: 1, 6: 4, 7: 4, 11: 1, 18: 4, 25: 1}
DXT = {12: 1, 14: 3, 15: 5}

def level0_size(fmt, w, h):
    if fmt in DXT:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if DXT[fmt] == 1 else 16)
    return w * h * BPP[fmt]

def chain_size(fmt, w, h, mips):
    return sum(level0_size(fmt, max(1, w >> i), max(1, h >> i)) for i in range(mips))

def decode(fmt, data, w, h):
    if fmt in DXT:
        return decode_dxt(data, w, h, DXT[fmt])
    if fmt in (6, 7):
        px = unswizzle(data, w, h, 4)
        out = bytearray(w * h * 4)
        for i in range(w * h):
            b, g, r, a = px[4 * i:4 * i + 4]
            out[4 * i:4 * i + 4] = bytes((r, g, b, 255 if fmt == 7 else a))
        return out
    if fmt == 18:
        out = bytearray(w * h * 4)
        for i in range(w * h):
            b, g, r, a = data[4 * i:4 * i + 4]
            out[4 * i:4 * i + 4] = bytes((r, g, b, a))
        return out
    if fmt in (0, 11, 25):
        px = unswizzle(data, w, h, 1)
        out = bytearray(w * h * 4)
        for i in range(w * h):
            v = px[i]
            out[4 * i:4 * i + 4] = bytes((v, v, v, 255))
        return out
    return None

# ---------------------------------------------------------------- libraries
ATTR = re.compile(r'(\S+?)="([^"]*)"')

def load_names():
    try:
        import json
        here = os.path.dirname(os.path.abspath(__file__))
        # keys may be written with or without leading zeros: normalise to hex()
        return {hex(int(k, 16)): v for k, v in json.load(open(os.path.join(here, 'names.json'))).items()}
    except Exception:
        return {}

NAMES = load_names()

def tex_name(h):
    if h.startswith('#'):
        key = hex(int(h[1:], 16))
        n = NAMES.get(key)
        if n: return re.sub(r'[\\/:*?"<>|]', '_', n)
        return h[1:]
    return re.sub(r'[\\/:*?"<>|]', '_', h) or 'unnamed'

def export_library(xml_path):
    text = open(xml_path, encoding='latin-1').read()
    m = re.search(r'<texture-library ([^>]*)>', text)
    if not m:
        return 0, 0, 0
    lib = dict(ATTR.findall(m.group(1)))
    data_path = os.path.join(os.path.dirname(xml_path), lib.get('data-file', ''))
    if not os.path.isfile(data_path):
        print('  data file missing:', data_path)
        return 0, 0, 0
    data = open(data_path, 'rb').read()
    out_dir = os.path.splitext(xml_path)[0] + '_png'
    os.makedirs(out_dir, exist_ok=True)
    ok = skipped = failed = 0
    for tm in re.finditer(r'<optimized-texture ([^>]*?)/?>', text):
        a = dict(ATTR.findall(tm.group(1)))
        try:
            fmt = int(a['format']); w = int(a['width']); h = int(a['height'])
            mips = int(a.get('number-lods', a.get('_120336ef', a.get('mip-levels', '1'))) or 1)
            off = int(a['offset']); size = int(a['size'])
        except (KeyError, ValueError):
            skipped += 1; continue
        name = tex_name(a.get('name', ''))
        blob = data[off:off + size]
        try:
            if fmt < 0:
                base = -fmt
                if base not in BPP and base not in DXT:
                    skipped += 1; continue
                face = chain_size(base, w, h, mips)
                stride = (face + 127) & ~127
                faces = [decode(base, blob[i * stride:i * stride + level0_size(base, w, h)], w, h) for i in range(6)]
                rgba = bytearray()
                for y in range(h):
                    for f in faces:
                        rgba += f[y * w * 4:(y + 1) * w * 4]
                write_png(os.path.join(out_dir, name + '_cube.png'), w * 6, h, rgba)
                ok += 1; continue
            if fmt not in BPP and fmt not in DXT:
                skipped += 1; continue
            rgba = decode(fmt, blob[:level0_size(fmt, w, h)], w, h)
            suffix = '_p8' if fmt == 11 else ''
            write_png(os.path.join(out_dir, name + suffix + '.png'), w, h, rgba)
            ok += 1
        except Exception as e:
            failed += 1
            print('  failed', name, fmt, w, h, e)
    return ok, skipped, failed

def main(args):
    if not args:
        print(__doc__); return 1
    libs = []
    for p in args:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                libs += [os.path.join(root, f) for f in files if f.startswith('textures-') and f.endswith('.xml')]
        else:
            libs.append(p)
    tot = [0, 0, 0]
    for i, x in enumerate(sorted(libs), 1):
        print('[%d/%d] %s' % (i, len(libs), x), flush=True)
        r = export_library(x)
        for k in range(3): tot[k] += r[k]
    print('wrote %d textures, %d in formats not supported, %d failed' % tuple(tot))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
