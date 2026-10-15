#!/usr/bin/env python3
"""Compare float code of the same C# methods on arm64 (Mac) and x64 (Windows).

    python3 floatscan_xplat.py [--all] [--show NAME_SUBSTR]

Default scope: gameplay namespaces (Refactor., AFFECTER_, Pathfinding.) plus UnityEngine.Mathf / Vector2 / System.Math.
Per method pair it counts:
  arm64: fused (fmadd fmsub fnmadd fnmsub, fmla fmls), separate fmul / fadd / fsub, fcvt (float<->double),
         fmin/fmax(nm), calls to libm imports (via __stubs) and to System.Math / Mathf methods.
  x64:   fused (vfmadd* vfmsub* vfnmadd* vfnmsub*), mulss/addss/subss (+sd), cvtss2sd/cvtsd2ss, minss/maxss,
         calls to System.Math / Mathf methods, x87 use.
Writes floatscan_xplat.tsv (one row per method with any float work) and prints totals.
"""
import bisect, collections, json, os, re, struct, sys
import lief, pefile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM, CS_ARCH_X86, CS_MODE_64

HOME = os.path.expanduser("~")
MAC_DUMP = f"{HOME}/ds_skill_truth/dump_25846389"
MAC_BIN = f"{HOME}/ds_skill_truth/GA_arm64_25846389.dylib"
WIN_DUMP = f"{HOME}/ds_multiplayer/win/25846389/dump"
WIN_BIN = f"{HOME}/ds_multiplayer/win/25846389/GameAssembly.dll"
OUT = f"{HOME}/ds_multiplayer/win/floatscan_xplat.tsv"
ALL = "--all" in sys.argv
SHOW = sys.argv[sys.argv.index("--show") + 1] if "--show" in sys.argv else None

MI = re.compile(r"MethodInfo_[0-9A-F]+")
def load(d):
    s = json.load(open(f"{d}/script.json"))
    byname = collections.defaultdict(list); names = collections.defaultdict(list)
    for m in s["ScriptMethod"]:
        k = (m["Name"], MI.sub("MethodInfo", m["Signature"]))
        byname[k].append(m["Address"]); names[m["Address"]].append(m["Name"])
    starts = sorted(set(names) | set(s["Addresses"]))
    return byname, names, starts
mb_n, m_names, m_starts = load(MAC_DUMP)
wb_n, w_names, w_starts = load(WIN_DUMP)

def end_of(starts, a):
    i = bisect.bisect_right(starts, a)
    return min(starts[i] if i < len(starts) else a + 0x1000, a + 0x8000)

# ---- Mac: stubs -> import names
mac = lief.parse(MAC_BIN)
mraw = open(MAC_BIN, "rb").read()
ptr2sym = {}
for bnd in mac.bindings:
    if bnd.has_symbol:
        ptr2sym[bnd.address] = bnd.symbol.name
stub2sym = {}
st = mac.get_section("__stubs")
csa = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
for off in range(0, st.size, 12):
    a = st.virtual_address + off
    ins = list(csa.disasm(mraw[st.offset + off: st.offset + off + 12], a))
    if len(ins) >= 2 and ins[0].mnemonic == "adrp":
        page = int(ins[0].op_str.split("#")[1], 16)
        m = re.search(r"#(0x[0-9a-f]+|\d+)\]", ins[1].op_str)
        if m:
            stub2sym[a] = ptr2sym.get(page + int(m.group(1), 0), "?")

# ---- Win
pe = pefile.PE(WIN_BIN, fast_load=True)
wraw = pe.__data__
_pd = pe.sections[[x.Name.rstrip(b"\0") for x in pe.sections].index(b".pdata")].get_data()
PDATA = {}
_ents = [struct.unpack_from("<III", _pd, _i) for _i in range(0, len(_pd) - 11, 12)]
_ents = sorted(x for x in _ents if x[0])
def _chained(u):                       # UNWIND_INFO flags & UNW_FLAG_CHAININFO
    try: return (pe.get_data(u, 1)[0] >> 3) & 4
    except Exception: return 0
_k = 0
while _k < len(_ents):                 # merge chained parts (shrink-wrapped prologues) into one function range
    b, e_, u = _ents[_k]; j = _k + 1
    while j < len(_ents) and _ents[j][0] == e_ and _chained(_ents[j][2]):
        e_ = _ents[j][1]; j += 1
    PDATA[b] = e_; _k = j
def wbytes(rva, n):
    o = pe.get_offset_from_rva(rva); return bytes(wraw[o:o + n])
csx = Cs(CS_ARCH_X86, CS_MODE_64)

MATHNAME = re.compile(r"^(System\.Math|System\.MathF|UnityEngine\.Mathf)\$\$")
LIBM = re.compile(r"^_+(sin|cos|tan|asin|acos|atan|atan2|pow|exp|exp2|log|log10|log2|fmod|hypot|cosh|sinh|tanh|sincos\w*|ldexpf?|modf)f?(_stret)?$")

def scan_arm(a):
    c = collections.Counter(); ex = []
    e = end_of(m_starts, a)
    for ins in csa.disasm(mraw[a:e], a):
        mn = ins.mnemonic; op = ins.op_str
        if mn in ("fmadd", "fmsub", "fnmadd", "fnmsub"):
            c["fused"] += 1; c["fused_d" if op.startswith("d") else "fused_s"] += 1
            if len(ex) < 3: ex.append(f"{ins.address:#x} {mn} {op}")
        elif mn in ("fmla", "fmls"):
            c["fused"] += 1; c["fused_vec"] += 1
            if len(ex) < 3: ex.append(f"{ins.address:#x} {mn} {op}")
        elif mn in ("fmul", "fnmul"): c["mul"] += 1
        elif mn in ("fadd", "fsub"): c["add"] += 1
        elif mn == "fdiv": c["div"] += 1
        elif mn == "fsqrt": c["sqrt"] += 1
        elif mn == "fcvt": c["cvt"] += 1
        elif mn in ("fmin", "fmax", "fminnm", "fmaxnm"): c["minmax"] += 1
        elif mn in ("bl", "b") and op.startswith("#"):
            t = int(op[1:], 16)
            if t in stub2sym and LIBM.match(stub2sym[t]):
                c["libm"] += 1; c["libm:" + stub2sym[t].lstrip("_")] += 1
            elif t in m_names and any(MATHNAME.match(n) for n in m_names[t]):
                c["mathcall"] += 1; c["m:" + m_names[t][0].split("$$")[1] + "@" + m_names[t][0].split("$$")[0].split(".")[-1]] += 1
        if mn == "ret" and ins.address + 4 >= e: break
    return c, ex

def scan_x64(a):
    c = collections.Counter(); ex = []
    e = PDATA.get(a) or end_of(w_starts, a)   # .pdata gives the exact x64 function end
    for ins in csx.disasm(wbytes(a, e - a), a):
        mn = ins.mnemonic; op = ins.op_str
        if re.match(r"vf(n)?m(add|sub)", mn):
            c["fused"] += 1
            if len(ex) < 3: ex.append(f"{ins.address:#x} {mn} {op}")
        elif mn in ("mulss", "mulsd", "vmulss", "vmulsd", "mulps", "vmulps"): c["mul"] += 1
        elif mn in ("addss", "subss", "addsd", "subsd", "vaddss", "vsubss", "vaddsd", "vsubsd", "addps", "subps"): c["add"] += 1
        elif mn in ("divss", "divsd", "vdivss", "vdivsd"): c["div"] += 1
        elif mn in ("sqrtss", "sqrtsd", "vsqrtss"): c["sqrt"] += 1
        elif mn in ("cvtss2sd", "cvtsd2ss", "vcvtss2sd", "vcvtsd2ss"): c["cvt"] += 1
        elif mn in ("minss", "maxss", "minsd", "maxsd"): c["minmax"] += 1
        elif mn.startswith("f") and mn[1:4] in ("ld ", "ld", "stp", "mul", "add") : c["x87"] += 1
        elif mn in ("call", "jmp") and op.startswith("0x"):
            t = int(op, 16)
            if t in w_names and any(MATHNAME.match(n) for n in w_names[t]):
                c["mathcall"] += 1; c["m:" + w_names[t][0].split("$$")[1] + "@" + w_names[t][0].split("$$")[0].split(".")[-1]] += 1
    return c, ex

scope = re.compile(r"^(Refactor\.|AFFECTER_|Pathfinding\.|UnityEngine\.Mathf\$\$|UnityEngine\.Vector2\$\$|UnityEngine\.Vector2Int\$\$|System\.Math\$\$|System\.MathF\$\$)")
rows = []; tot_m = collections.Counter(); tot_w = collections.Counter(); seen = set(); nmeth = 0
both_fused_div = []
for (nm, sig), maddrs in mb_n.items():
    if not ALL and not scope.match(nm):
        continue
    if SHOW and SHOW not in nm:
        continue
    waddrs = wb_n.get((nm, sig))
    if not waddrs:
        continue
    for i, ma in enumerate(maddrs):
        wa = waddrs[min(i, len(waddrs) - 1)]
        if (ma, wa) in seen:
            continue
        seen.add((ma, wa)); nmeth += 1
        cm, exm = scan_arm(ma); cw, exw = scan_x64(wa)
        fl = sum(cm[k] for k in ("fused", "mul", "add", "div", "cvt", "libm", "mathcall"))
        if not fl and not sum(cw.values()):
            continue
        tot_m.update(cm); tot_w.update(cw)
        tot_m["methods_float"] += 1
        if cm["fused"]: tot_m["methods_fused"] += 1
        if cw["fused"]: tot_w["methods_fused"] += 1
        if cm["cvt"] != cw["cvt"]: tot_m["methods_cvt_mismatch"] += 1
        rows.append((nm, ma, wa, cm, cw, exm, exw))

with open(OUT if not SHOW else os.devnull, "w") as o:
    o.write("name\tmac\twin\tarm_fused\tarm_mul\tarm_add\tarm_cvt\tarm_libm\tarm_mathcalls\tx64_fused\tx64_mul\tx64_add\tx64_cvt\tx64_mathcalls\tarm_fused_examples\n")
    for nm, ma, wa, cm, cw, exm, exw in sorted(rows, key=lambda r: -r[3]["fused"]):
        lm = " ".join(f"{k[5:]}x{v}" for k, v in cm.items() if k.startswith("libm:"))
        mc = " ".join(f"{k[2:]}x{v}" for k, v in cm.items() if k.startswith("m:"))
        wc = " ".join(f"{k[2:]}x{v}" for k, v in cw.items() if k.startswith("m:"))
        o.write(f"{nm}\t{ma:#x}\t{wa:#x}\t{cm['fused']}\t{cm['mul']}\t{cm['add']}\t{cm['cvt']}\t{lm}\t{mc}\t{cw['fused']}\t{cw['mul']}\t{cw['add']}\t{cw['cvt']}\t{wc}\t{' | '.join(exm)}\n")

print(f"method pairs scanned {nmeth}, with float work {tot_m['methods_float']}")
for k in ("fused", "fused_s", "fused_d", "fused_vec", "mul", "add", "div", "sqrt", "cvt", "minmax", "libm", "mathcall", "methods_fused", "methods_cvt_mismatch"):
    print(f"  arm64 {k:22s} {tot_m[k]}")
for k in ("fused", "mul", "add", "div", "sqrt", "cvt", "minmax", "mathcall", "x87", "methods_fused"):
    print(f"  x64   {k:22s} {tot_w[k]}")
print("  arm64 libm calls:", sorted(((k[5:], v) for k, v in tot_m.items() if k.startswith("libm:")), key=lambda x: -x[1]))
print("  arm64 math-method calls:", sorted(((k[2:], v) for k, v in tot_m.items() if k.startswith("m:")), key=lambda x: -x[1])[:15])
print("  x64 math-method calls:", sorted(((k[2:], v) for k, v in tot_w.items() if k.startswith("m:")), key=lambda x: -x[1])[:15])
if SHOW:
    for nm, ma, wa, cm, cw, exm, exw in rows:
        print(nm, hex(ma), hex(wa), dict(cm), dict(cw), exm, exw)
