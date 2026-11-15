"""Convert Brute Force .xmb (BXML) to XML using the game's .xsb schemas.
Reverse-engineered from default.xbe (see NOTES.md for function addresses)."""
import sys, os, json, glob, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xml.sax.saxutils import escape
from bxml import Reader, parse_header, varint
from bxsd import parse as parse_xsb
from bfhash import bfhash

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = {int(k,16): v for k, v in json.load(open(os.path.join(HERE, 'names.json'))).items()}
XS = bfhash('http://www.w3.org/2001/XMLSchema')
BUILTIN = {bfhash(t): t for t in ('float','int','unsignedInt','boolean','string','short','byte','unsignedShort','unsignedByte','double','long')}
STRINGID = bfhash('stringid'); ENUMVAL = bfhash('enum-value')
import re as _re
_XMLNAME = _re.compile(r'[A-Za-z_][A-Za-z0-9_.\-]*$')
def nm(h):
    """Tag/attribute name for a hash: the recovered name when it is a valid XML name,
    otherwise _xxxxxxxx (names with spaces or a leading digit would break the XML)."""
    n = NAMES.get(h)
    return n if n and _XMLNAME.match(n) and not n.lower().startswith('xml') else f'_{h:08x}'

_CTRL = _re.compile('[\x00-\x08\x0b\x0c\x0e-\x1f]')
def clean_text(s):
    """String values: the string tables, campaign and menu texts hold UTF-16LE text (every
    other byte 0); decode those, and drop control characters XML does not allow."""
    if len(s) >= 2 and len(s) % 2 == 0 and s[1::2].count(0) >= len(s) // 4:
        t = s.decode('utf-16-le', 'replace')
    else:
        t = s.decode('latin1')
    return _CTRL.sub('', t.rstrip('\0'))

class Schemas:
    def __init__(self, files):
        self.types = {}; self.globals = {}; self.qtypes = {}; self.qglobals = {}
        for f in files:
            raw = open(f,'rb').read(); x = parse_xsb(raw)
            tns = struct.unpack_from('<I', raw, 18)[0] if raw[17] == 0x21 else None
            for r in x['roots']:
                if r['name'] is None: continue
                key = r['name'][1]
                if r['kind'] & 0x8: self.globals.setdefault(key, r); self.qglobals[(tns, key)] = r
                else: self.types.setdefault(key, r); self.qtypes[(tns, key)] = r
    def type_node(self, q):
        if q is None: return None
        return self.qtypes.get((q[0], q[1])) or self.types.get(q[1])
    def global_el(self, ns, name):
        return self.qglobals.get((ns, name)) or self.globals.get(name)
    def children(self, node, depth=0):
        """Element declarations allowed under an element/type node, flattened."""
        out = []
        if node is None or depth > 20: return out
        t = node
        if node['kind'] & 0x8 and node['type'] is not None and node['kind'] != 0x9:
            if node['kind'] == 0x18: t = self.global_el(node['type'][0], node['type'][1]) or self.type_node(node['type'])
            else: t = self.type_node(node['type'])
            if t is None: return out
            if t['kind'] & 0x8: return self.children(t, depth+1)
        for k in t['kids']:
            if k['kind'] & 0x8 or k['kind'] == 0x80: out.append(k)
        if t['kind'] == 0x101 and t['type'] is not None:
            out += self.children(self.type_node(t['type']), depth+1)
        return out
    def builtin(self, node, depth=0):
        """Return (builtin_name, is_list) for a simple-valued element/type."""
        if node is None or depth > 20: return None, False
        q = node['type']
        if node['kind'] == 0x18 and q is not None:
            g = self.global_el(q[0], q[1]); return self.builtin(g, depth+1) if g else (None, False)
        if q is None:
            for k in node['kids']:
                if k['kind'] in (0x2, 0x6, 0x80):
                    b, l = self.builtin(k, depth+1); return b, l or k['kind'] == 0x6
            return None, False
        if q[1] == STRINGID: return 'stringid', node['kind'] == 0x6
        if q[1] == ENUMVAL: return 'enum', node['kind'] == 0x6
        if q[0] == XS or (q[1] in BUILTIN and q[1] not in self.types):
            return BUILTIN.get(q[1], nm(q[1])), node['kind'] == 0x6
        t = self.type_node(q)
        if t is None: return None, False
        b, l = self.builtin(t, depth+1)
        return b, l or node['kind'] == 0x6 or t['kind'] == 0x6

    def enum_type(self, node, depth=0):
        """Hash of the schema type that derives directly from enum-value (identifies the C++ enum)."""
        if node is None or depth > 20: return None
        q = node['type']
        if node['kind'] == 0x18 and q is not None: return self.enum_type(self.global_el(q[0], q[1]), depth+1)
        if q is None:
            for k in node['kids']:
                if k['kind'] in (0x2, 0x6, 0x80): return self.enum_type(k, depth+1)
            return None
        if q[1] == ENUMVAL: return node['name'][1] if node['name'] else None
        return self.enum_type(self.type_node(q), depth+1)

ATTRS = 0x1b8535f0   # name hash of the schema's attribute-group pseudo-element (kind 0x80), checked in FUN_00255330
_ep = os.path.join(HERE, 'enums.json')
ENUMS = {int(k,16): {int(v): n for v, n in t['values'].items()} for k, t in json.load(open(_ep)).items()} if os.path.exists(_ep) else {}
SEEN_ENUMS = {}   # enum type hash -> set of raw values seen (for validating the enum mapping)
SIZES = {'float': 4, 'int': 4, 'unsignedInt': 4, 'boolean': 1, 'short': 2, 'unsignedShort': 2, 'byte': 1, 'unsignedByte': 1, 'double': 8, 'long': 8}

class Decoder:
    def __init__(self, b, schemas):
        self.b = b; self.S = schemas
        self.h = parse_header(b)
        self.r = Reader(b, self.h['tokpos'], self.h['ver'])
        self.pending = 1
        self.special = bool(self.h['flags'] & 3)
    def val(self, bt, et=None):
        r = self.r; b = self.b
        if bt in ('float',): v = struct.unpack_from('<f', b, r.p)[0]; r.p += 4; return repr(round(v, 6))
        if bt in ('int',): v = struct.unpack_from('<i', b, r.p)[0]; r.p += 4; return str(v)
        if bt in ('unsignedInt',): v = struct.unpack_from('<I', b, r.p)[0]; r.p += 4; return str(v)
        if bt == 'boolean': v = b[r.p]; r.p += 1; return 'true' if v else 'false'
        if bt == 'stringid' and self.special:
            v = struct.unpack_from('<I', b, r.p)[0]; r.p += 4
            return NAMES.get(v, f'#{v:08x}') if v else ''
        if bt == 'enum' and self.special:
            v, r.p = varint(b, r.p); v -= 1
            SEEN_ENUMS.setdefault(et, set()).add(v)
            return ENUMS.get(et, {}).get(v, str(v))
        if bt in SIZES:
            n = SIZES[bt]; v = int.from_bytes(b[r.p:r.p+n], 'little', signed=not bt.startswith('unsigned')); r.p += n; return str(v)
        # strings: varint length + bytes
        n, r.p = varint(b, r.p); s = b[r.p:r.p+n]; r.p += n
        return clean_text(s)
    def count(self, t):
        n = t & 0xfff
        if t & 0x800:
            n &= 0x7ff; sh = 11
            while True:
                w = self.r.word(); n |= (w & 0x7fff) << sh; sh += 15
                if not w & 0x8000: break
        return n
    def element(self, decl, name, out, depth):
        ind = '  ' * depth
        kids = self.S.children(decl)
        bt, islist = self.S.builtin(decl) if decl is not None else (None, False)
        et = self.S.enum_type(decl) if bt == 'enum' else None
        attrs = ''
        body = []
        while True:
            t = self.r.word(); typ = t & 0xf000; idx = t & 0xfff
            if typ == 0xc000: break
            if typ == 0x9000: self.pending = self.count(t); continue
            if typ == 0xd000:
                n = self.count(t); vals = [self.val(bt, et) for _ in range(n)]
                body.append(escape(' '.join(vals)))
                continue
            cname = self.h['names'][idx]
            cdecl = next((k for k in kids if k['name'] and k['name'][1] == cname), None) or self.S.global_el(self.dns, cname)
            rep = self.pending; self.pending = 1
            if typ == 0xa000:
                cbt, _ = self.S.builtin(cdecl) if cdecl else (None, False)
                v = escape(self.val(cbt, self.S.enum_type(cdecl) if cbt == 'enum' else None))
                body.append(f'\n{ind}  <{nm(cname)}>{v}</{nm(cname)}>')
            elif typ == 0xb000:
                body.append(f'\n{ind}  <{nm(cname)}/>')
            elif typ == 0x0000 and cname == ATTRS:
                for an, av in self.attributes(cdecl):
                    attrs += f' {an}="{av}"'
            elif typ == 0x0000:
                sub = []
                self.element(cdecl, nm(cname), sub, depth+1)
                body.append('\n' + ''.join(sub))
            else:
                raise ValueError(f'unknown token {t:#06x}')
        content = ''.join(body)
        if not content: out.append(f'{ind}<{name}{attrs}/>')
        elif content.startswith('\n'): out.append(f'{ind}<{name}{attrs}>{content}\n{ind}</{name}>')
        else: out.append(f'{ind}<{name}{attrs}>{content}</{name}>')
    def attributes(self, decl):
        kids = decl['kids'] if decl is not None else []   # attribute decls are kind 0x2 under the 0x80 group
        res = []
        while True:
            t = self.r.word(); typ = t & 0xf000; idx = t & 0xfff
            if typ == 0xc000: return res
            if typ == 0x9000: self.count(t); continue
            cname = self.h['names'][idx]
            cdecl = next((k for k in kids if k['name'] and k['name'][1] == cname), None)
            if typ == 0xa000:
                cbt, _ = self.S.builtin(cdecl) if cdecl else (None, False)
                res.append((nm(cname), escape(self.val(cbt, self.S.enum_type(cdecl) if cbt == 'enum' else None), {'"': '&quot;'})))
            elif typ == 0xb000:
                res.append((nm(cname), ''))
            elif typ == 0xd000:
                pass
            else:
                raise ValueError(f'unexpected token {t:#06x} in attribute group')
    def run(self):
        t = self.r.word(); root = self.h['names'][t & 0xfff]
        self.dns = next((u for p, u in self.h['ns'] if p == 0), None)
        decl = self.S.global_el(self.dns, root)
        out = ['<?xml version="1.0" encoding="iso-8859-1"?>\n']
        self.element(decl, nm(root), out, 0)
        return ''.join(out) + '\n', self.r.p

def convert(path, schemas):
    b = open(path,'rb').read()
    d = Decoder(b, schemas)
    xml, end = d.run()
    return xml, end, len(b)

if __name__ == '__main__':
    sch = Schemas(sorted(glob.glob(os.path.join(sys.argv[1], '**/*.xsb'), recursive=True)))
    for f in sys.argv[2:]:
        xml, end, size = convert(f, sch)
        print(f'{f}: consumed {end}/{size}', file=sys.stderr)
        sys.stdout.write(xml)
