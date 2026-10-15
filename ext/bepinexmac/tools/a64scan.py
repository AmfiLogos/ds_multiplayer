#!/usr/bin/env python3
"""Offline check of Il2CppInterop's injector hook lookups on the arm64 slice of a GameAssembly.dylib.
Runs the same xref chains as Il2CppInterop.Runtime (Injection/Hooks/*.cs) with the arm64 JumpTargets rules
used in our fork (BL = call; B = tail call only when it lands on a function start; scan stops at RET or at
the next function start). Prints every step so a wrong pick is visible.
usage: a64scan.py GameAssembly.dylib [--dis ADDR N]"""
import struct, subprocess, sys, bisect
import capstone

path = sys.argv[1]
data = open(path, 'rb').read()

def arm64_slice(d):
    magic = struct.unpack('>I', d[:4])[0]
    if magic in (0xcafebabe, 0xcafebabf):
        n = struct.unpack('>I', d[4:8])[0]
        for i in range(n):
            if magic == 0xcafebabe:
                ct, cs, off, sz, al = struct.unpack('>5I', d[8 + i * 20: 28 + i * 20])
            else:
                ct, cs, off, sz, al, _ = struct.unpack('>2I2QII', d[8 + i * 32: 40 + i * 32])
            if ct == 0x0100000c:
                return off, sz
        raise SystemExit('no arm64 slice')
    return 0, len(d)

SOFF, SSZ = arm64_slice(data)
m = data[SOFF:SOFF + SSZ]
ncmds = struct.unpack('<I', m[16:20])[0]
p = 32
segs, fstarts = [], None
for _ in range(ncmds):
    cmd, size = struct.unpack('<II', m[p:p + 8])
    if cmd == 0x19:
        name = m[p + 8:p + 24].rstrip(b'\0').decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack('<4Q', m[p + 24:p + 56])
        segs.append((name, vmaddr, vmsize, fileoff, filesize))
    elif cmd == 0x26:
        dataoff, datasize = struct.unpack('<II', m[p + 8:p + 16])
        fstarts = (dataoff, datasize)
    p += size
text = [s for s in segs if s[0] == '__TEXT'][0]

def rd(addr, n):
    for name, va, vs, fo, fs in segs:
        if va <= addr < va + fs:
            o = fo + addr - va
            return m[o:o + n]
    return b''

starts = []
o, end = fstarts[0], fstarts[0] + fstarts[1]
a = text[1]
while o < end:
    v, sh = 0, 0
    while True:
        b = m[o]; o += 1
        v |= (b & 0x7f) << sh; sh += 7
        if not b & 0x80:
            break
    if v == 0:
        break
    a += v
    starts.append(a)
startset = set(starts)

exports = {}
for line in subprocess.run(['nm', '-arch', 'arm64', '-g', path], capture_output=True, text=True).stdout.splitlines():
    f = line.split()
    if len(f) == 3 and f[1] == 'T':
        exports[f[2].lstrip('_')] = int(f[0], 16)

def next_start(addr):
    i = bisect.bisect_right(starts, addr)
    return starts[i] if i < len(starts) else addr + 0x10000

def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

def jump_targets(start, ignore_ret=False):
    out, first_flow = [], True
    stop = next_start(start)
    a = start
    while a < stop:
        w = struct.unpack('<I', rd(a, 4))[0]
        flow = False
        if (w & 0xFFFFFC1F) == 0xD65F0000:          # RET
            if not ignore_ret:
                break
            flow = True
        elif (w & 0xFC000000) == 0x94000000:        # BL
            out.append(a + sx(w & 0x3FFFFFF, 26) * 4); flow = True
        elif (w & 0xFC000000) == 0x14000000:        # B
            t = a + sx(w & 0x3FFFFFF, 26) * 4
            flow = True
            if t in startset and t != start:
                out.append(t)
                if first_flow:
                    break
        elif (w & 0xFF000010) == 0x54000000 or (w & 0x7E000000) in (0x34000000, 0x36000000) \
                or (w & 0xFFFFFC1F) in (0xD61F0000, 0xD63F0000):
            flow = True                              # B.cond / CBZ / CBNZ / TBZ / TBNZ / BR / BLR
        if flow:
            first_flow = False
        a += 4
    return out

def tail_calls(start):
    """B instructions that land on another function start, in address order (whole function)."""
    out, a, stop = [], start, next_start(start)
    while a < stop:
        w = struct.unpack('<I', rd(a, 4))[0]
        if (w & 0xFC000000) == 0x14000000:
            t = a + sx(w & 0x3FFFFFF, 26) * 4
            if t in startset and t != start:
                out.append(t)
        a += 4
    return out

def name(addr):
    for k, v in exports.items():
        if v == addr:
            return k
    return ''

def show(label, addr):
    print(f'{label:42s} 0x{addr:x} size {next_start(addr) - addr:#x} {name(addr)}')

if '--dis' in sys.argv:
    i = sys.argv.index('--dis')
    a, n = int(sys.argv[i + 1], 16), int(sys.argv[i + 2])
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    for ins in md.disasm(rd(a, n * 4), a):
        print(f'  {ins.address:x}: {ins.mnemonic} {ins.op_str}')
    sys.exit()

print(f'arm64 slice at {SOFF:#x}, {len(starts)} function starts, {len(exports)} exports')
# Class::FromIl2CppType, Class::FromName
for api in ('il2cpp_class_from_il2cpp_type', 'il2cpp_class_from_name'):
    t = jump_targets(exports[api]); show(api, exports[api]); print('   targets', [hex(x) for x in t])
# GenericMethod::GetMethod (Unity 6 hook)
api = exports['il2cpp_object_get_virtual_method']
gvm = jump_targets(api)[0]; show('Object::GetVirtualMethod', gvm)
x = jump_targets(gvm); print('   targets', [hex(v) for v in x])
tails = tail_calls(gvm); print('   tail calls', [hex(v) for v in tails])
ggvm = tails[-1]; show('MetadataCache::GetGenericVirtualMethod (tail)', ggvm)
fx = jump_targets(ggvm); print('   targets', [hex(v) for v in fx])
show('=> GenericMethod::GetMethod(gmethod) (last)', fx[-1])
# MetadataCache::GetTypeInfoFromTypeDefinitionIndex (2018.3+ branch, metadata v29+)
api = exports['il2cpp_image_get_class']
igt = jump_targets(api)[0]; show('Image::GetType', igt)
x = jump_targets(igt); print('   targets', [hex(v) for v in x])
cand = igt if not x else x[0]
if x and len(x) > 1:
    h = x[-1] if len(x) == 2 else x[0]; show('getTypeInfoFromHandle', h)
    hx = jump_targets(h); print('   targets', [hex(v) for v in hx])
    if len(hx) != 1:
        cand = h
    else:
        cand = hx[0]
        while len(jump_targets(cand)) == 1:
            cand = jump_targets(cand)[0]
show('=> GetTypeInfoFromTypeDefinitionIndex', cand)
# Class::GetDefaultFieldValue (xref path)
api = exports['il2cpp_field_static_get_value']
sgv = jump_targets(api)[0]; show('Field::StaticGetValue', sgv)
x = jump_targets(sgv); print('   targets', [hex(v) for v in x])
sgvi = x[-1]; show('Field::StaticGetValueInternal', sgvi)
x = jump_targets(sgvi); print('   targets', [hex(v) for v in x])
show('   x86 rule (3 ? last : first), WRONG on clang', x[-1] if len(x) == 3 else x[0])
def call_with_stack_x1(start):
    found, mark, a, stop = 0, False, start, next_start(start)
    while a < stop:
        w = struct.unpack('<I', rd(a, 4))[0]
        if (w & 0xFFFFFC1F) == 0xD65F0000:
            break
        if (w & 0xFC000000) == 0x94000000:
            if mark:
                if found: return 0
                found = a + sx(w & 0x3FFFFFF, 26) * 4
            mark = False
        elif (w & 0xFF800000) == 0x91000000 and (w & 0x1F) == 1:
            mark = ((w >> 5) & 0x1F) in (29, 31)
        elif (w & 0x1F) == 1:
            mark = False
        a += 4
    return found
show('=> GetDefaultFieldValue (BL with x1 = stack address)', call_with_stack_x1(sgvi))
