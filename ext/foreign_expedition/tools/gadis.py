#!/usr/bin/env python3
"""dis.py <rva hex> [n]: disassemble GameAssembly at an RVA, name BL targets from the dump (0.4.29)."""
import sys, re, capstone, struct
GA = '~/ds_multiplayer/instances/A/DungeonSettlers.app/Contents/Frameworks/GameAssembly.dylib'
DUMP = '~/ds_skill_truth/dump_25846389/dump.cs'
names = {}; cls = ''
for line in open(DUMP, errors='replace'):
    m = re.match(r'^(?:public |internal |private |sealed |abstract |static )*(?:class|struct) (\S+)', line)
    if m: cls = m.group(1)
    m = re.search(r'RVA: (0x[0-9A-F]+)', line)
    if m: last = int(m.group(1), 16); continue
    if 'last' in dir() and last is not None and '(' in line:
        names[last] = cls + '.' + line.strip()[:110]; last = None
data = open(GA, 'rb').read()
# arm64 slice: fat or thin
off = 0
magic = struct.unpack('>I', data[:4])[0]
if magic == 0xCAFEBABE:
    n = struct.unpack('>I', data[4:8])[0]
    for i in range(n):
        ct, cs, o, sz, al = struct.unpack('>5I', data[8 + i * 20: 28 + i * 20])
        if ct == 0x0100000C: off = o
rva = int(sys.argv[1], 16); N = int(sys.argv[2]) if len(sys.argv) > 2 else 120
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
for ins in md.disasm(data[off + rva: off + rva + N * 4], rva):
    s = f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}'
    if ins.mnemonic in ('bl', 'b'):
        t = int(ins.op_str.lstrip('#'), 16); s += '   ; ' + names.get(t, '?')
    print(s)
    if ins.mnemonic == 'ret' and N > 0: pass
