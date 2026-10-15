#!/usr/bin/env python3
"""Math.Pow / Math.Exp / Math.Log: Windows il2cpp icall + CRT (emulated, SSE2 path) vs Apple libm. Writes pow_compare.txt."""
import ctypes, math, random, struct, os
from win_emu import WinEmu
random.seed(2)
lm = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
for n, k in (("pow", 2), ("exp", 1), ("log", 1)):
    f = getattr(lm, n); f.restype = ctypes.c_double; f.argtypes = [ctypes.c_double] * k
W_POW = 0x4659E0   # System.Math$$Pow -> jmp here (il2cpp Math::Pow icall)
e = WinEmu(); out = []
def b(x): return struct.pack("<d", x).hex()
def run(name, gen, n, wf, mf):
    mism = 0; ex = []
    for _ in range(n):
        args = gen(); w = wf(*args); m = mf(*args)
        if b(w) != b(m):
            mism += 1
            if len(ex) < 4: ex.append(f"{args} win {w!r} mac {m!r}")
    line = f"{name}: {mism} / {n} differ" + ("" if not ex else "  e.g. " + "; ".join(ex)); print(line); out.append(line)
wpow = lambda x, y: e.call_f64(W_POW, x, y)
run("pow(int, 2) Edgar distances", lambda: (float(random.randint(-500, 500)), 2.0), 50000, wpow, lm.pow)
run("pow(e, -d/T) annealing-like", lambda: (math.e, -random.uniform(0, 50)), 100000, wpow, lm.pow)
run("pow(random, random)", lambda: (random.uniform(0.01, 100), random.uniform(-10, 10)), 100000, wpow, lm.pow)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "pow_compare.txt"), "w").write("\n".join(out) + "\n")
