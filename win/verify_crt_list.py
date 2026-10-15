#!/usr/bin/env python3
"""Check each CRT entry in math/crt_patch_list.tsv (+ known extras) by running it in the emulator (win_emu.py)
on sample inputs and comparing with Python's math (1e-5 relative). Writes math/crt_patch_verified.tsv."""
import math, struct
from win_emu import WinEmu
e = WinEmu()
REF = {"acos": (math.acos, 1, "d"), "acosf": (math.acos, 1, "f"), "asin": (math.asin, 1, "d"), "asinf": (math.asin, 1, "f"),
       "atan": (math.atan, 1, "d"), "atanf": (math.atan, 1, "f"), "atan2": (math.atan2, 2, "d"), "atan2f": (math.atan2, 2, "f"),
       "cos": (math.cos, 1, "d"), "cosf": (math.cos, 1, "f"), "sin": (math.sin, 1, "d"), "sinf": (math.sin, 1, "f"),
       "tan": (math.tan, 1, "d"), "tanf": (math.tan, 1, "f"), "exp": (math.exp, 1, "d"), "expf": (math.exp, 1, "f"),
       "exp2f": (lambda x: 2.0 ** x, 1, "f"), "log": (math.log, 1, "d"), "logf": (math.log, 1, "f"),
       "log10": (math.log10, 1, "d"), "log10f": (math.log10, 1, "f"), "pow": (math.pow, 2, "d"), "powf": (math.pow, 2, "f"),
       "sinh": (math.sinh, 1, "d"), "cosh": (math.cosh, 1, "d"), "tanh": (math.tanh, 1, "d")}
cand = {}
for ln in open("math/crt_patch_list.tsv").read().split("\n")[1:]:
    if ln: s, r, *_ = ln.split("\t"); cand.setdefault(s, set()).add(int(r, 16))
cand.setdefault("pow", set()).add(0x4E8890); cand.setdefault("exp", set()).add(0x4DFBE0)
for s in ("powf", "expf", "logf", "exp2f"): cand.setdefault(s, set())
cand["sincos_stret"] = set(); cand["sincosf_stret"] = set()
args1 = [0.3, 0.7, 1.5, 2.0]; args2 = [(0.3, 1.7), (2.0, 3.0), (5.0, 0.5)]
out = ["symbol\twin_rva\tverified"]
for s in sorted(REF):
    fn, n, k = REF[s]
    for r in sorted(cand.get(s, ())):
        ok = True
        try:
            for a in (args1 if n == 1 else args2):
                a = a if n == 2 else (a,)
                got = (e.call_f32 if k == "f" else e.call_f64)(r, *a)
                want = fn(*a)
                if not (abs(got - want) <= 1e-5 * max(1, abs(want))): ok = False; break
        except Exception as ex:
            ok = False
        out.append(f"{s}\t{r:#x}\t{'yes' if ok else 'NO'}")
open("math/crt_patch_verified.tsv", "w").write("\n".join(out) + "\n")
print("\n".join(out))
