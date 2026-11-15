import struct, sys
d = open(sys.argv[1], 'rb').read()
base = struct.unpack_from('<I', d, 0x104)[0]
n, sh = struct.unpack_from('<II', d, 0x11C)
img = bytearray(0x4D4000 - base)
img[:0x1000] = d[:0x1000]
for i in range(n):
    fl, va, vs, ra, rs = struct.unpack_from('<5I', d, sh - base + i * 0x38)
    img[va - base:va - base + rs] = d[ra:ra + rs]
    print(hex(va), hex(vs), hex(fl))
open(sys.argv[2], 'wb').write(img)
