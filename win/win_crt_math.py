#!/usr/bin/env python3
"""List the Windows CRT math entry points inside GameAssembly.dll (statically linked UCRT), found through the
C# wrappers System.Math / System.MathF / UnityEngine.Mathf, and whether each one dispatches on the CRT FMA3 flag.
Output: crt_math.tsv (function, win rva, dispatch-on-flag, reached-from)."""
import json, os, re, collections
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
H = os.path.expanduser("~"); D = f"{H}/ds_multiplayer/win/25846389"
FLAG = 0x520E4B8
pe = pefile.PE(f"{D}/GameAssembly.dll", fast_load=True)
cs = Cs(CS_ARCH_X86, CS_MODE_64)
s = json.load(open(f"{D}/dump/script.json"))
TEXT_END = 0x50D000      # CRT/libil2cpp live in .text; game code in section il2cpp
def _dispatch_pre(a):
    return dispatch(a) == "yes"
def dispatch(a):
    for i in list(cs.disasm(pe.get_data(a, 0x40), a))[:4]:
        if "rip +" in i.op_str:
            t = i.address + i.size + int(i.op_str.split("rip + ")[1].split("]")[0], 16)
            if t == FLAG: return "yes"
    return "no"
res = collections.defaultdict(set)
for m in s["ScriptMethod"]:
    mm = re.match(r"^(System\.Math|System\.MathF|UnityEngine\.Mathf)\$\$(\w+)$", m["Name"])
    if not mm: continue
    a = m["Address"]; seen = 0
    for _ in range(2):        # wrapper -> (il2cpp icall) -> crt
        tgt = None
        for i in cs.disasm(pe.get_data(a, 0x200), a):
            if i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x"):
                t = int(i.op_str, 16)
                if t < TEXT_END and t not in (0x443AF0, 0x443E20, 0x443D40):
                    tgt = t; break
            if i.mnemonic == "ret": break
        if tgt is None: break
        a = tgt; seen += 1
        if _dispatch_pre(a): break       # stop at the CRT dispatcher (FMA3 / SSE2 split)
    if seen:
        res[a].add(f"{mm.group(1).split('.')[-1]}.{mm.group(2)}")
with open(f"{H}/ds_multiplayer/win/crt_math.tsv", "w") as o:
    o.write("win_rva\tfma3_dispatch\treached_from\n")
    for a in sorted(res):
        o.write(f"{a:#x}\t{dispatch(a)}\t{' '.join(sorted(res[a]))}\n")
print(open(f"{H}/ds_multiplayer/win/crt_math.tsv").read())
