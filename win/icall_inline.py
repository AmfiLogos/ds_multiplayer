#!/usr/bin/env python3
"""Hooked Unity icall wrappers inlined on Windows. An il2cpp icall wrapper loads its resolved function pointer from a
static slot (mov rax, [rip+slot] ... jmp rax). If another method also reads that slot, MSVC inlined the wrapper there
and our hook on the wrapper misses that call. For every hooked address (inline_union.tsv) whose Windows body starts
with that pattern: count rip-relative references to its slot in .text/il2cpp. Output: the wrappers referenced
outside their own body, with the methods that do it.  python3 icall_inline.py [tsv]"""
import pefile, struct, json, bisect, sys, os
import numpy as np
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
H = os.path.expanduser("~"); W = f"{H}/ds_multiplayer/win"
pe = pefile.PE(f"{W}/25846389/GameAssembly.dll", fast_load=True)
s = json.load(open(f"{W}/25846389/dump/script.json"))
names = {}
for m in s["ScriptMethod"]: names.setdefault(m["Address"], m["Name"])
st = sorted(set(names) | set(s["Addresses"]))
secs = [(sec.VirtualAddress, sec.get_data()) for sec in pe.sections if sec.Name.rstrip(b"\0") in (b".text", b"il2cpp")]
def code(rva, n=48):
    for va, b in secs:
        if va <= rva < va + len(b): return b[rva - va: rva - va + n]
md = Cs(CS_ARCH_X86, CS_MODE_64)
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1] if len(sys.argv) > 1 else f"{W}/instances/inline_union.tsv")]
slots = {}
for r in rows:
    a = int(r[1], 16); c = code(a)
    if not c: continue
    for ins in md.disasm(c, a):
        if ins.mnemonic == "mov" and "rip +" in ins.op_str and ins.op_str.startswith("rax, qword ptr"):
            slots[ins.address + ins.size + int(ins.op_str.split("rip + ")[1].rstrip("]"), 16)] = (r[0], a); break
        if ins.mnemonic in ("call", "jmp", "ret"): break
print(len(slots), "hooked methods are icall-style wrappers (slot load)")
hits = {k: [] for k in slots}
for va, b in secs:
    arr = np.frombuffer(b[: len(b) // 4 * 4 + 0], dtype=np.uint8)
    n = len(b) - 4
    for off in range(4):
        d = np.frombuffer(b[off: off + (n - off) // 4 * 4], dtype="<i4").astype(np.int64)
        pos = va + off + np.arange(len(d), dtype=np.int64) * 4
        tgt = pos + 4 + d
        for k in slots:
            for p in pos[tgt == k]: hits[k].append(int(p))
for k, (nm, a) in sorted(slots.items(), key=lambda x: x[1][0]):
    outside = []
    for p in hits[k]:
        i = bisect.bisect_right(st, p) - 1
        if st[i] != a: outside.append(names.get(st[i], hex(st[i])))
    print(f"{nm}\t{a:#x}\tslot {k:#x}\trefs {len(hits[k])}\toutside {len(outside)}\t{sorted(set(outside))[:8]}")
