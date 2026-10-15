#!/usr/bin/env python3
"""Bit-compare Windows CRT math (emulated from GameAssembly.dll, SSE2 path) with Apple libm on this Mac,
on the inputs the game feeds them. Prints mismatch counts and examples; writes libm_compare.txt.

    nice -n 19 python3 libm_compare.py [N_RANDOM]
"""
import ctypes, math, random, struct, sys, os
from win_emu import WinEmu

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300000
random.seed(1)
lm = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
def cf(name, nargs, t):
    f = getattr(lm, name); f.restype = t; f.argtypes = [t] * nargs; return f
m_atan2f = cf("atan2f", 2, ctypes.c_float); m_acos = cf("acos", 1, ctypes.c_double)
m_sinf = cf("sinf", 1, ctypes.c_float); m_cosf = cf("cosf", 1, ctypes.c_float)
m_fmodf = cf("fmodf", 2, ctypes.c_float); m_acosf = cf("acosf", 1, ctypes.c_float)
class SC(ctypes.Structure): _fields_ = [("s", ctypes.c_float), ("c", ctypes.c_float)]
m_sincosf = lm.__sincosf_stret; m_sincosf.restype = SC; m_sincosf.argtypes = [ctypes.c_float]

# Windows twins (rva in GameAssembly.dll 25846389), found from the call sites in the same C# methods
W_ATAN2F, W_ACOS, W_FMODF = 0x4DB470, 0x4DBC90, 0x4D9A70
W_ROT_A, W_ROT_B = 0x4D9850, 0x4DA140      # the two calls in VectorExtensions.Rotate (cosf / sinf, order checked below)
e = WinEmu()

def f32(x): return struct.unpack("<f", struct.pack("<f", x))[0]
def bits(x, d=False): return struct.pack("<d" if d else "<f", x).hex()
out = []
def report(name, mism, total, ex):
    line = f"{name}: {mism} / {total} differ" + ("" if not ex else "  e.g. " + "; ".join(ex[:4]))
    print(line); out.append(line)

# which of W_ROT_A/B is sin
ra = e.call_f32(W_ROT_A, 0.5); rb = e.call_f32(W_ROT_B, 0.5)
W_COSF, W_SINF = (W_ROT_A, W_ROT_B) if abs(ra - math.cos(0.5)) < 1e-6 else (W_ROT_B, W_ROT_A)
out.append(f"Rotate calls: cosf={W_COSF:#x} sinf={W_SINF:#x}")

# atan2f on integer grid deltas (SnapTo8Direction, NormalizeToEightDirection, FindPositionWithDirection)
mism = 0; ex = []; tot = 0; dir_m = 0
R = 256
for y in range(-R, R + 1):
    for x in range(-R, R + 1):
        if x == 0 and y == 0: continue
        w = e.call_f32(W_ATAN2F, float(y), float(x)); m = m_atan2f(float(y), float(x)); tot += 1
        if bits(w) != bits(m):
            mism += 1
            if len(ex) < 6: ex.append(f"({y},{x}) win {w!r} mac {m!r}")
            # does the 8-direction pick change?  angle*57.29578f, +360 if <0, /45, round half even
            def d8(a):
                a = f32(a * f32(57.29578)); a = f32(a + 360.0) if a < 0 else a; return round(f32(a / 45.0)) & 7
            if d8(w) != d8(m): dir_m += 1
report(f"atan2f integer grid |d|<={R}", mism, tot, ex); out.append(f"  8-direction result changed: {dir_m}")
mism = 0; ex = []
for _ in range(N):
    y = f32(random.uniform(-300, 300)); x = f32(random.uniform(-300, 300))
    w = e.call_f32(W_ATAN2F, y, x); m = m_atan2f(y, x)
    if bits(w) != bits(m):
        mism += 1
        if len(ex) < 6: ex.append(f"({y!r},{x!r}) win {w!r} mac {m!r}")
report("atan2f random floats", mism, N, ex)

# acos(double) on cosines of integer offset pairs (FindPositionsInCone) + random [-1,1]
mism = 0; ex = []; tot = 0
dirs = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
for dx, dy in dirs:
    for x in range(-40, 41):
        for y in range(-40, 41):
            if x == 0 and y == 0: continue
            dot = f32(dx * x + dy * y); den = f32(math.sqrt(f32(f32(dx * dx + dy * dy) * f32(x * x + y * y))))
            c = max(-1.0, min(1.0, f32(dot / den)))
            w = e.call_f64(W_ACOS, c); m = m_acos(c); tot += 1
            if bits(w, 1) != bits(m, 1):
                mism += 1
                if len(ex) < 4: ex.append(f"acos({c!r}) win {w!r} mac {m!r}")
for _ in range(N):
    c = f32(random.uniform(-1, 1))
    w = e.call_f64(W_ACOS, c); m = m_acos(c); tot += 1
    if bits(w, 1) != bits(m, 1):
        mism += 1
        if len(ex) < 6: ex.append(f"acos({c!r}) win {w!r} mac {m!r}")
report("acos(double) cone + random float inputs", mism, tot, ex)

# sinf / cosf (Win, separate) vs Apple __sincosf_stret (what the Mac game calls) and Apple sinf/cosf
mism_s = mism_c = 0; ex = []; tot = 0
angles = [f32(f32(d) * f32(math.pi / 180)) for d in range(-720, 721)] + [f32(f32(d / 10) * f32(math.pi / 180)) for d in range(-3600, 3601)]
angles += [f32(random.uniform(-7, 7)) for _ in range(N)]
for a in angles:
    sc = m_sincosf(a); ws = e.call_f32(W_SINF, a); wc = e.call_f32(W_COSF, a); tot += 1
    if bits(ws) != bits(sc.s):
        mism_s += 1
        if len(ex) < 4: ex.append(f"sin({a!r}) win {ws!r} mac {sc.s!r}")
    if bits(wc) != bits(sc.c):
        mism_c += 1
        if len(ex) < 8: ex.append(f"cos({a!r}) win {wc!r} mac {sc.c!r}")
report("sinf vs Apple sincosf", mism_s, tot, ex); report("cosf vs Apple sincosf", mism_c, tot, [])

# fmodf (exact by IEEE; sanity)
mism = 0; ex = []
for _ in range(N // 3):
    a = f32(random.uniform(-1000, 1000)); b = 360.0
    w = e.call_f32(W_FMODF, a, b); m = m_fmodf(a, b)
    if bits(w) != bits(m):
        mism += 1; ex.append(f"fmodf({a!r},360) win {w!r} mac {m!r}")
report("fmodf", mism, N // 3, ex)

open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "libm_compare.txt"), "w").write("\n".join(out) + "\n")

# acos as the cone code uses it: float(acos(double(c))) -> *57.29578f -> *0.0174533f, then compared to the half angle
mism = 0; tot = 0; ex = []
for dx, dy in dirs:
    for x in range(-60, 61):
        for y in range(-60, 61):
            if x == 0 and y == 0: continue
            dot = f32(dx * x + dy * y); den = f32(math.sqrt(f32(f32(dx * dx + dy * dy) * f32(x * x + y * y))))
            c = max(-1.0, min(1.0, f32(dot / den)))
            def chain(v): return f32(f32(f32(v) * f32(57.29578)) * f32(0.017453292))
            w = chain(e.call_f64(W_ACOS, c)); m = chain(m_acos(c)); tot += 1
            if bits(w) != bits(m):
                mism += 1
                if len(ex) < 4: ex.append(f"c={c!r} win {w!r} mac {m!r}")
report("cone angle after float rounding (integer offsets |d|<=60, 8 dirs)", mism, tot, ex)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "libm_compare.txt"), "w").write("\n".join(out) + "\n")
