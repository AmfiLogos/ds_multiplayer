#!/usr/bin/env python3
"""(--log <file>: take the hook list from W's rig.log "hooked 0x..." lines instead of the source tables.)
Hooks that MSVC may have inlined away on Windows: for every address the mod hooks (rig_hook / hook tables in
build/gen/*.c), count direct callers on the Mac (arm64 BL) and on Windows (x64 call/jmp rel32). Mac > 0 and
Windows == 0 means the Windows hook never fires for direct calls. Output: inline_audit.tsv"""
import re, os, subprocess, json, collections
H = os.path.expanduser("~"); W = f"{H}/ds_multiplayer/win"; G = f"{W}/build/gen"
mapw2m = {}
for l in open(f"{W}/win_map.tsv"):
    f = l.rstrip("\n").split("\t")
    if f[0] == "method" and f[3]: mapw2m[int(f[3], 16)] = (int(f[2], 16), f[1])
hooked = set()
for fn in os.listdir(G):
    if not fn.endswith(".c"): continue
    t = open(os.path.join(G, fn)).read()
    defs = {n: int(a, 16) for n, a in re.findall(r"#define (\w+)\s+0x([0-9A-F]+)", t)}
    for arg in re.findall(r"rig_hook\(\s*(\w+)", t) + re.findall(r"\{\s*(VA_\w+|0x[0-9A-Fa-f]+)\s*,\s*\(?\w+\s*,\s*\(void \*\*\)", t):
        a = int(arg, 16) if arg.startswith("0x") else defs.get(arg)
        if a in mapw2m: hooked.add(a)
import sys
if "--log" in sys.argv:   # the hooks the Windows DLL really installed: "<who>: hooked 0x..." lines from W's rig.log
    hooked = set()
    for l in open(sys.argv[sys.argv.index("--log") + 1]):
        m = re.search(r"hooked (0x[0-9a-f]+)", l)
        if m and int(m.group(1), 16) in mapw2m: hooked.add(int(m.group(1), 16))
        elif m: print("not in win_map (Windows-only hook):", l.strip()[:120])
ws = sorted(hooked); ms = [mapw2m[a][0] for a in ws]
def cnt(cmd, args):
    out = subprocess.run(["python3", cmd] + [hex(x) for x in args], capture_output=True, text=True, cwd=W).stdout
    return collections.Counter(int(l.split()[0], 16) for l in out.splitlines() if l.strip())
cw = cnt("win_callers.py", ws); cm = cnt("mac_callers.py", ms)
rows = [(mapw2m[a][1], a, mapw2m[a][0], cm[mapw2m[a][0]], cw[a]) for a in ws]
with open(f"{W}/inline_audit.tsv", "w") as f:
    for r in rows: f.write(f"{r[0]}\t{r[1]:#x}\t{r[2]:#x}\tmac_callers={r[3]}\twin_callers={r[4]}\n")
print(len(rows), "hooked methods")
for r in rows:
    if r[3] > 0 and r[4] == 0: print("INLINED ON WINDOWS:", r[0], hex(r[1]), "mac callers", r[3])
