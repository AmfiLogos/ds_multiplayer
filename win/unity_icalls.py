#!/usr/bin/env python3
"""Find Unity native icall bodies (UnityPlayer) on Mac arm64 and Windows x64 and dump their code.

    python3 unity_icalls.py "UnityEngine.Random::Range" "UnityEngine.Random::get_value" ...

Registration code does: x0/rcx = name string, x1/rdx = function, call register. We find the string,
the instruction that loads its address, and the function address loaded next into x1 / rdx.
"""
import re, struct, sys, os
import numpy as np, lief, pefile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM, CS_ARCH_X86, CS_MODE_64

HOME = os.path.expanduser("~")
MAC = f"{HOME}/ds_multiplayer/win/mac_unityplayer/UnityPlayer_arm64.dylib"
WIN = f"{HOME}/ds_multiplayer/win/25846389/UnityPlayer.dll"
names = sys.argv[1:] or ["UnityEngine.Random::Range", "UnityEngine.Random::get_value", "UnityEngine.Mathf::PerlinNoise"]
N = int(os.environ.get("NINS", "60"))

# ---------- Mac ----------
mraw = open(MAC, "rb").read()
mb = lief.parse(MAC)
text = mb.get_section("__text")
def m_va2off(va):
    for s in mb.segments:
        if s.virtual_address <= va < s.virtual_address + s.virtual_size:
            return va - s.virtual_address + s.file_offset
words = np.frombuffer(mraw, dtype="<u4", count=text.size // 4, offset=text.offset)
pcs = text.virtual_address + np.arange(len(words), dtype=np.int64) * 4
is_adrp = (words & 0x9F000000) == 0x90000000
immlo = (words >> 29) & 3; immhi = (words >> 5) & 0x7FFFF
imm = ((immhi << 2) | immlo).astype(np.int64)
imm = np.where(imm & (1 << 20), imm - (1 << 21), imm)
adrp_page = (pcs & ~0xFFF) + (imm << 12)
nxt = np.roll(words, -1)
is_add = (nxt & 0xFFC00000) == 0x91000000          # add xd, xn, #imm (64-bit, no shift)
add_imm = ((nxt >> 10) & 0xFFF).astype(np.int64)
pair_target = adrp_page + add_imm
csa = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
def mac_find(name):
    s = mraw.find(name.encode() + b"\0")
    while s > 0 and mraw[s - 1] != 0:
        s = mraw.find(name.encode() + b"\0", s + 1)
    # file offset -> vaddr
    for sg in mb.segments:
        if sg.file_offset <= s < sg.file_offset + sg.file_size:
            va = s - sg.file_offset + sg.virtual_address
    idx = np.nonzero(is_adrp & is_add & (pair_target == va))[0]
    out = []
    for i in idx:
        pc = int(pcs[i])
        # next adrp/add into x1 within 8 instructions
        for ins in csa.disasm(mraw[m_va2off(pc):m_va2off(pc) + 48], pc):
            pass
        j = i + 2
        for k in range(j, j + 8):
            if is_adrp[k] and is_add[k] and (int(words[k]) & 0x1F) == 1:
                out.append(int(pair_target[k]))
                break
    return out

# ---------- Win ----------
pe = pefile.PE(WIN, fast_load=True)
wraw = pe.__data__
base = pe.OPTIONAL_HEADER.ImageBase
csx = Cs(CS_ARCH_X86, CS_MODE_64)
wtext = [s for s in pe.sections if s.Name.startswith(b".text")][0]
tbytes = bytes(wraw[wtext.PointerToRawData: wtext.PointerToRawData + wtext.SizeOfRawData])
tva = wtext.VirtualAddress
def win_find(name):
    s = bytes(wraw).find(b"\0" + name.encode() + b"\0") + 1
    rva = pe.get_rva_from_offset(s)
    out = []
    # lea r??, [rip+disp32]: 48/4C 8D modrm(00 reg 101) disp32
    for m in re.finditer(rb"[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", tbytes):
        p = m.start()
        disp = struct.unpack_from("<i", tbytes, p + 3)[0]
        if tva + p + 7 + disp == rva:
            pc = tva + p
            for ins in csx.disasm(tbytes[p:p + 64], pc):
                if ins.mnemonic == "lea" and ins.op_str.startswith("rdx, [rip"):
                    out.append(int(ins.op_str.split("+ ")[1].rstrip("]"), 16) + ins.address + ins.size)
                    break
    return out

def dis_mac(a, n=N):
    o = m_va2off(a)
    return [f"{i.address:#x}  {i.mnemonic} {i.op_str}" for i in list(csa.disasm(mraw[o:o + 4 * n], a))[:n]]
def dis_win(a, n=N):
    o = pe.get_offset_from_rva(a)
    return [f"{i.address:#x}  {i.mnemonic} {i.op_str}" for i in list(csx.disasm(bytes(wraw[o:o + 8 * n]), a))[:n]]

def table_find(raw, name, ptr2str, find_ptr):
    """Unity keeps icalls as names[N] followed by funcs[N]. Returns the func pointer for name."""
    import struct
    p = find_ptr(name)
    if p is None:
        return None
    st = p
    while ptr2str(struct.unpack_from("<Q", raw, st - 8)[0]).find("::") > 0: st -= 8
    en = p
    while ptr2str(struct.unpack_from("<Q", raw, en)[0]).find("::") > 0: en += 8
    return struct.unpack_from("<Q", raw, en + (p - st))[0]

def _mac_ptr2str(v):
    v &= 0xFFFFFFFFF
    o = m_va2off(v)
    if o is None or not (0 <= o < len(mraw)): return ""
    e = mraw.find(b"\0", o, o + 300)
    return mraw[o:e].decode("latin1") if e > 0 else ""
def _mac_find_ptr(name):
    s = mraw.find(b"\0" + name.encode() + b"\0") + 1
    va = next(s - sg.file_offset + sg.virtual_address for sg in mb.segments if sg.file_offset <= s < sg.file_offset + sg.file_size)
    q = np.frombuffer(mraw[:len(mraw) // 8 * 8], dtype="<u8")
    h = np.nonzero((q & 0xFFFFFFFFF) == va)[0]
    return int(h[0]) * 8 if len(h) else None
def _win_ptr2str(v):
    try:
        o = pe.get_offset_from_rva(v - base)
    except Exception:
        return ""
    if not (0 <= o < len(wraw)): return ""
    e = bytes(wraw[o:o + 300]).find(b"\0")
    return bytes(wraw[o:o + e]).decode("latin1") if e > 0 else ""
def _win_find_ptr(name):
    import struct
    rb = bytes(wraw)
    s = rb.find(b"\0" + name.encode() + b"\0") + 1
    p = rb.find(struct.pack("<Q", base + pe.get_rva_from_offset(s)))
    return p if p >= 0 else None
def mac_find(name):
    v = table_find(mraw, name, _mac_ptr2str, _mac_find_ptr)
    return [v & 0xFFFFFFFFF] if v else []
def win_find(name):
    v = table_find(bytes(wraw), name, _win_ptr2str, _win_find_ptr)
    return [v - base] if v else []

if __name__ == "__main__":
    for nm in names:
        m = mac_find(nm); w = win_find(nm)
        print(f"=== {nm}: mac {[hex(x) for x in m]}  win {[hex(x) for x in w]}")
        for a in m[:2]:
            print("-- mac", hex(a)); print("\n".join(dis_mac(a)))
        for a in w[:2]:
            print("-- win", hex(a)); print("\n".join(dis_win(a)))
