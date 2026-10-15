#!/usr/bin/env python3
"""Callers (BL/B) of Mac arm64 GameAssembly addresses, named by containing method.
    python3 mac_callers.py 0x80c414 [...]"""
import json, bisect, sys, os
import numpy as np, lief
H = os.path.expanduser("~")
P = f"{H}/ds_skill_truth/GA_arm64_25846389.dylib"
raw = open(P, "rb").read(); mb = lief.parse(P)
s = json.load(open(f"{H}/ds_skill_truth/dump_25846389/script.json"))
names = {}
for m in s["ScriptMethod"]: names.setdefault(m["Address"], m["Name"])
st = sorted(set(names) | set(s["Addresses"]))
targets = [int(x, 16) for x in sys.argv[1:]]
for sec in (mb.get_section("__text"), mb.get_section("il2cpp")):
    w = np.frombuffer(raw, dtype="<u4", count=sec.size // 4, offset=sec.offset)
    idx = np.nonzero(((w >> 26) == 0x25) | ((w >> 26) == 0x05))[0]
    imm = (w[idx] & 0x3FFFFFF).astype(np.int64); imm = np.where(imm & 0x2000000, imm - 0x4000000, imm)
    pc = sec.virtual_address + idx.astype(np.int64) * 4; dst = pc + imm * 4
    for t in targets:
        for a in pc[dst == t].tolist():
            i = bisect.bisect_right(st, a) - 1
            print(f"{t:#x} <- {a:#x} {names.get(st[i], hex(st[i]))}")
