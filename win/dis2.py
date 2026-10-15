#!/usr/bin/env python3
"""Side-by-side disassembly of one method on Mac arm64 and Windows x64 (by win_map-style name match).
    python3 dis2.py 'Refactor.Combat.CombatUtility$$SnapTo8Direction' [maxins]"""
import sys, json, re, bisect, os
import pefile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM, CS_ARCH_X86, CS_MODE_64
H = os.path.expanduser("~")
name = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 150
def load(d):
    s = json.load(open(f"{d}/script.json"))
    hits = [m["Address"] for m in s["ScriptMethod"] if m["Name"] == name]
    names = {}
    for m in s["ScriptMethod"]: names.setdefault(m["Address"], m["Name"])
    st = sorted(set(names) | set(s["Addresses"]))
    return hits, names, st
mh, mn, ms = load(f"{H}/ds_skill_truth/dump_25846389")
wh, wn, ws = load(f"{H}/ds_multiplayer/win/25846389/dump")
mraw = open(f"{H}/ds_skill_truth/GA_arm64_25846389.dylib", "rb").read()
pe = pefile.PE(f"{H}/ds_multiplayer/win/25846389/GameAssembly.dll", fast_load=True)
def end(st, a):
    i = bisect.bisect_right(st, a); return st[i] if i < len(st) else a + 0x800
for a in mh[:1]:
    print(f"== mac {a:#x}")
    for i in list(Cs(CS_ARCH_ARM64, CS_MODE_ARM).disasm(mraw[a:end(ms, a)], a))[:N]:
        t = ""
        if i.mnemonic in ("bl", "b") and i.op_str.startswith("#"): t = "  ; " + mn.get(int(i.op_str[1:], 16), "")
        print(f"{i.address:#x}  {i.mnemonic} {i.op_str}{t}")
for a in wh[:1]:
    print(f"== win {a:#x}")
    for i in list(Cs(CS_ARCH_X86, CS_MODE_64).disasm(pe.get_data(a, end(ws, a) - a), a))[:N]:
        t = ""
        if i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x"): t = "  ; " + wn.get(int(i.op_str, 16), "")
        print(f"{i.address:#x}  {i.mnemonic} {i.op_str}{t}")
