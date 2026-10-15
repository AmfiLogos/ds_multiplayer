#!/usr/bin/env python3
"""ABI audit for the Windows build: every game method the mod calls or hooks whose signature passes or returns a
value type (struct) by value. arm64 passes small structs in registers (Vector2 = s0,s1; Guid = x0,x1), Windows x64
passes 8-byte structs packed in ONE integer register and bigger ones by pointer, so C prototypes that split a
struct into floats / two u64 are wrong on Windows. Output: abi_audit.tsv (file, method, signature)."""
import json, re, os, collections
H = os.path.expanduser("~")
gen = f"{H}/ds_multiplayer/win/build/gen"
sj = json.load(open(f"{H}/ds_multiplayer/win/25846389/dump/script.json"))
sig = {}
for m in sj["ScriptMethod"]: sig.setdefault(m["Address"], (m["Name"], m["Signature"]))
VAL = re.compile(r"(\w+_o) (\w+)[,)]")          # by-value struct parameter
rows = []
for fn in sorted(os.listdir(gen)):
    if not fn.endswith(".c"): continue
    t = open(os.path.join(gen, fn)).read()
    for a in sorted({int(x, 16) for x in re.findall(r"\b0x([0-9A-F]{5,8})\b", t)}):
        if a not in sig: continue
        nm, s = sig[a]
        params = s[s.index("(") + 1:]
        vals = [p for p in VAL.findall(params) if not p[0].endswith("Enum_o")]
        ret = s.split(" ")[0]
        if vals or (ret.endswith("_o") and "*" not in ret):
            rows.append((fn, nm, s))
with open(f"{H}/ds_multiplayer/win/abi_audit.tsv", "w") as f:
    for r in rows: f.write("\t".join(r) + "\n")
print(len(rows), "struct-by-value call sites")
for r in rows: print(r[0], r[1], "|", r[2][:150])
