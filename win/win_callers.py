#!/usr/bin/env python3
"""Callers of a Windows GameAssembly.dll address (direct call/jmp rel32), named by the method that contains them.
    python3 win_callers.py 0x4dbc90 [...]"""
import pefile, re, struct, json, bisect, sys, os
H = os.path.expanduser("~")
pe = pefile.PE(f"{H}/ds_multiplayer/win/25846389/GameAssembly.dll", fast_load=True)
s = json.load(open(f"{H}/ds_multiplayer/win/25846389/dump/script.json"))
names = {}
for m in s["ScriptMethod"]: names.setdefault(m["Address"], m["Name"])
st = sorted(set(names) | set(s["Addresses"]))
targets = {int(x, 16) for x in sys.argv[1:]}
for sec in pe.sections:
    if sec.Name.rstrip(b"\0") not in (b".text", b"il2cpp"): continue
    b = sec.get_data(); va = sec.VirtualAddress
    for m in re.finditer(b"[\xe8\xe9]", b):
        p = m.start()
        if p + 5 > len(b): break
        t = va + p + 5 + struct.unpack_from("<i", b, p + 1)[0]
        if t in targets:
            a = va + p; i = bisect.bisect_right(st, a) - 1
            print(f"{t:#x} <- {a:#x} {names.get(st[i], hex(st[i]))}")
