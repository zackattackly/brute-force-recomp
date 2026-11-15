"""Brute Force (Xbox) vertex shader disassembler.

The game's vertex shaders are NV2A microcode programs stored in default.xbe: a header
word (0x2078 | instruction count << 16) followed by 16-byte instructions. This prints
them as readable assembly (field layout as in the xemu emulator's vsh.c).

Usage:
    python vshdis.py <default.xbe>                list every shader program
    python vshdis.py <default.xbe> 0x3D8BF8       disassemble the program at that address

Registers: v0..v15 inputs, r0..r11 temporaries, c0..c191 constants (hardware numbering,
the same numbers the game passes when it sets them: e.g. c23..c25), oPos/oD0/oT0.. outputs.
Pure Python standard library.
"""
import struct, sys
F = dict(ILU=(1,25,3),MAC=(1,21,4),CONST=(1,13,8),V=(1,9,4),
 A_NEG=(1,8,1),A_SX=(1,6,2),A_SY=(1,4,2),A_SZ=(1,2,2),A_SW=(1,0,2),A_R=(2,28,4),A_MUX=(2,26,2),
 B_NEG=(2,25,1),B_SX=(2,23,2),B_SY=(2,21,2),B_SZ=(2,19,2),B_SW=(2,17,2),B_R=(2,13,4),B_MUX=(2,11,2),
 C_NEG=(2,10,1),C_SX=(2,8,2),C_SY=(2,6,2),C_SZ=(2,4,2),C_SW=(2,2,2),C_RH=(2,0,2),C_RL=(3,30,2),C_MUX=(3,28,2),
 MAC_MASK=(3,24,4),OUT_R=(3,20,4),ILU_MASK=(3,16,4),O_MASK=(3,12,4),ORB=(3,11,1),OADDR=(3,3,8),OMUX=(3,2,1),A0X=(3,1,1),FINAL=(3,0,1))
MAC=['nop','mov','mul','add','mad','dp3','dph','dp4','dst','min','max','slt','sge','arl']
ILU=['nop','mov','rcp','rcc','rsq','exp','log','lit']
ONAME={0:'oPos',3:'oD0',4:'oD1',5:'oFog',6:'oPts',7:'oB0',8:'oB1',9:'oT0',10:'oT1',11:'oT2',12:'oT3'}
def fld(w,n):
    i,s,l=F[n]; return (w[i]>>s)&((1<<l)-1)
def mask(m): return ''.join(c for b,c in zip((8,4,2,1),'xyzw') if m&b)
def src(w,p,a0):
    mux=fld(w,p+'_MUX')
    if p=='C': r=(fld(w,'C_RH')<<2)|fld(w,'C_RL')
    else: r=fld(w,p+'_R')
    if mux==1: s='r%d'%r if r!=12 else 'oPos'
    elif mux==2: s='v%d'%fld(w,'V')
    elif mux==3: s=('c[a0.x+%d]' if a0 else 'c%d')%fld(w,'CONST')
    else: s='?'
    sw=''.join('xyzw'[fld(w,p+'_S'+c)] for c in 'XYZW')
    if sw!='xyzw': s+='.'+(sw[0] if len(set(sw))==1 else sw)
    return ('-' if fld(w,p+'_NEG') else '')+s
NARGS={'mov':1,'mul':2,'add':2,'mad':3,'dp3':2,'dph':2,'dp4':2,'dst':2,'min':2,'max':2,'slt':2,'sge':2,'arl':1}
def dis(words):
    out=[]
    for k in range(0,len(words),4):
        w=words[k:k+4]; a0=fld(w,'A0X')
        mac=MAC[fld(w,'MAC')] if fld(w,'MAC')<len(MAC) else '?'; ilu=ILU[fld(w,'ILU')]
        args={'mov':['A'],'mul':['A','B'],'add':['A','C'],'mad':['A','B','C'],'dp3':['A','B'],'dph':['A','B'],'dp4':['A','B'],
              'dst':['A','B'],'min':['A','B'],'max':['A','B'],'slt':['A','B'],'sge':['A','B'],'arl':['A']}
        parts=[]
        if mac!='nop':
            dsts=[]
            if fld(w,'MAC_MASK'): dsts.append('r%d.%s'%(fld(w,'OUT_R'),mask(fld(w,'MAC_MASK'))))
            if fld(w,'O_MASK') and fld(w,'OMUX')==0:
                dsts.append(('%s' if fld(w,'ORB') else 'c%d')%(ONAME.get(fld(w,'OADDR'),'o%d'%fld(w,'OADDR')) if fld(w,'ORB') else fld(w,'OADDR'))+'.'+mask(fld(w,'O_MASK')))
            if mac=='arl': dsts=['a0.x']
            parts.append('%s %s, %s'%(mac,'+'.join(dsts) or '_',', '.join(src(w,p,a0) for p in args[mac])))
        if ilu!='nop':
            dsts=[]
            if fld(w,'ILU_MASK'): dsts.append('r%d.%s'%(1 if mac!='nop' else fld(w,'OUT_R'),mask(fld(w,'ILU_MASK'))))
            if fld(w,'O_MASK') and fld(w,'OMUX')==1:
                dsts.append(ONAME.get(fld(w,'OADDR'),'o%d'%fld(w,'OADDR'))+'.'+mask(fld(w,'O_MASK')))
            parts.append('%s %s, %s'%(ilu,'+'.join(dsts) or '_',src(w,'C',a0)))
        out.append(' : '.join(parts)+(' ;final' if fld(w,'FINAL') else ''))
    return out
class Xbe:
    """Reads an XBE by virtual address (section table at the header's +0x120)."""
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        base, = struct.unpack_from('<I', self.b, 0x104)
        n, hdr = struct.unpack_from('<II', self.b, 0x11C)
        self.secs = []
        for i in range(n):
            _, va, vs, raw, rs = struct.unpack_from('<5I', self.b, hdr - base + 56 * i)
            self.secs.append((va, vs, raw, rs))
    def read(self, va, n):
        for sva, vs, raw, rs in self.secs:
            if sva <= va and va + n <= sva + rs:
                return self.b[raw + va - sva: raw + va - sva + n]
        raise ValueError('address %#x is not in the file' % va)
    def scan(self):
        for sva, vs, raw, rs in self.secs:
            for o in range(0, rs - 8, 4):
                w, = struct.unpack_from('<I', self.b, raw + o)
                if (w & 0xffff) == 0x2078 and 1 <= (w >> 16) <= 136:
                    yield sva + o, w >> 16

def program(x, va):
    h, = struct.unpack('<I', x.read(va, 4)); n = h >> 16
    return dis(struct.unpack('<%dI' % (4 * n), x.read(va + 4, 16 * n)))

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    x = Xbe(sys.argv[1])
    if len(sys.argv) == 2:
        import re
        for va, n in x.scan():
            try: txt = '\n'.join(program(x, va))
            except Exception: continue
            ins = sorted(set(re.findall(r'v\d+', txt)), key=lambda s: int(s[1:]))
            print('%#x  %3d instructions  inputs %s' % (va, n, ' '.join(ins)))
    else:
        for i, l in enumerate(program(x, int(sys.argv[2], 16))): print('%3d  %s' % (i, l))
