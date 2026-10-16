#!/usr/bin/env python3
"""Map every game address the Mac mod uses to the Windows build.

Re-run after each game update:
    python3 win_map.py [--mac-dump DIR] [--mac-bin DYLIB] [--win-dump DIR] [--win-bin DLL] [--out win_map.tsv]

What it does
1. Harvests every hex constant 0x10000..0x7FFFFFF from the mod sources and research docs
   (src/, custom/, research/, rig/, PLAN.md; not win/, builds, logs, rwmt).
2. Classifies each against the Mac dump (script.json) and the Mac dylib:
   method start | metadata slot (TypeInfo / MethodInfo / string) | il2cpp export | il2cpp internal | other.
3. Resolves the Windows twin: method by name+signature (overload order as tie break), metadata by name,
   exports by name, internals by a call-site vote (Mac callers -> their Windows twins -> most common callee).
4. Field offsets: for every class whose name appears in the sources, compares every field offset
   (Mac dump.cs vs Windows dump.cs). Also a global count of differing fields over the whole dump.
Output: win_map.tsv (kind, name, mac, win, status, note, used_in).
"""
import argparse, bisect, collections, json, os, re, struct, sys

HOME = os.path.expanduser("~")
ROOT = os.environ.get("DSMP_ROOT") or os.path.join(HOME, "ds_multiplayer")
ap = argparse.ArgumentParser()
ap.add_argument("--mac-dump", default=os.path.join(HOME, "ds_skill_truth/dump_25846389"))
ap.add_argument("--mac-bin", default=os.path.join(HOME, "ds_skill_truth/GA_arm64_25846389.dylib"))
ap.add_argument("--win-dump", default=os.path.join(ROOT, "win/25846389/dump"))
ap.add_argument("--win-bin", default=os.path.join(ROOT, "win/25846389/GameAssembly.dll"))
ap.add_argument("--out", default=os.path.join(ROOT, "win/win_map.tsv"))
A = ap.parse_args()

# ---------- 1. harvest ----------
SKIP_DIRS = {"win", "build", "instances", "backup_prefs_2026-10-12", "rwmt_Multiplayer", "__pycache__",
             "det_out", "smoke_out", ".git"}
EXTS = (".c", ".h", ".cpp", ".md", ".py", ".sh")
HEX = re.compile(r"\b0x([0-9A-Fa-f]{5,8})\b")
used = collections.defaultdict(list)          # value -> ["file:line"]
idents = set()
CM = os.environ.get("CM_REPO") or os.path.join(HOME, "ds_custommodes")   # Custom Modes' own repo since post-v13
for top in ("src", "custom", "research", "rig", "PLAN.md"):
    p = os.path.join(ROOT, top)
    if top == "custom" and not os.path.exists(os.path.join(p, "build_module.sh")):
        p = CM                                    # its files keep the names custom/... in win_map.tsv
    walk = [(os.path.dirname(p), [], [os.path.basename(p)])] if os.path.isfile(p) else os.walk(p)
    for d, dirs, files in walk:
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
        for f in files:
            if not f.endswith(EXTS):
                continue
            fp = os.path.join(d, f)
            rel = os.path.join("custom", os.path.relpath(fp, p)) if p == CM else os.path.relpath(fp, ROOT)
            for n, line in enumerate(open(fp, errors="replace"), 1):
                idents.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]{3,}", line))
                for m in HEX.finditer(line):
                    v = int(m.group(1), 16)
                    if 0x10000 <= v < 0x8000000:
                        used[v].append(f"{rel}:{n}")

# ---------- 2. dumps ----------
MI = re.compile(r"MethodInfo_[0-9A-F]+")
def load_script(d):
    s = json.load(open(os.path.join(d, "script.json")))
    meth = {}                                  # addr -> (name, sig)
    byname = collections.defaultdict(list)     # (name, sig) -> [addr ...] in dump order
    for m in s["ScriptMethod"]:
        sig = MI.sub("MethodInfo", m["Signature"])
        meth.setdefault(m["Address"], (m["Name"], sig))
        byname[(m["Name"], sig)].append(m["Address"])
    md = {}; mdn = {}
    for m in s["ScriptMetadata"]:
        md[m["Address"]] = m["Name"]; mdn.setdefault(m["Name"], m["Address"])
    for m in s["ScriptMetadataMethod"]:
        md[m["Address"]] = m["Name"]; mdn.setdefault(m["Name"], m["Address"])
    for m in s["ScriptString"]:
        md.setdefault(m["Address"], "string:" + m["Value"][:60])
        mdn.setdefault("string:" + m["Value"][:60], m["Address"])
    starts = sorted(set(meth) | set(s["Addresses"]))
    return meth, byname, md, mdn, starts

mac_meth, mac_byname, mac_md, mac_mdn, mac_starts = load_script(A.mac_dump)
win_meth, win_byname, win_md, win_mdn, win_starts = load_script(A.win_dump)
# all names an address carries (shared generic code has several)
mac_names_at = collections.defaultdict(list)
for (nm, sig), addrs in mac_byname.items():
    for i, a in enumerate(addrs):
        mac_names_at[a].append((nm, sig, i))

def win_twin(mac_addr):
    """Windows address for a Mac method start, or None."""
    for nm, sig, i in mac_names_at.get(mac_addr, []):
        w = win_byname.get((nm, sig))
        if w:
            return w[min(i, len(w) - 1)], nm, (len(w) == len(mac_byname[(nm, sig)]))
    return None, (mac_names_at[mac_addr][0][0] if mac_names_at.get(mac_addr) else None), False

# ---------- binaries ----------
import lief, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
mac_raw = open(A.mac_bin, "rb").read()
mb = lief.parse(A.mac_bin)
mac_exp = {s.value: s.name.lstrip("_") for s in mb.exported_symbols}
mac_text = [(sec.virtual_address, sec.virtual_address + sec.size, sec.offset)
            for sg in mb.segments for sec in sg.sections if sec.name in ("__text", "il2cpp")]
pe = pefile.PE(A.win_bin, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
win_exp = {e.name.decode(): e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
win_raw = pe.__data__
def win_bytes(rva, n):
    off = pe.get_offset_from_rva(rva)
    return bytes(win_raw[off:off + n])
def in_mac_text(a):
    return any(lo <= a < hi for lo, hi, _ in mac_text)

# Mac BL index (callee -> callers), built lazily with numpy
_bl = None
def mac_callers(target):
    global _bl
    import numpy as np
    if _bl is None:
        _bl = []
        for lo, hi, off in mac_text:
            w = np.frombuffer(mac_raw, dtype="<u4", count=(hi - lo) // 4, offset=off)
            idx = np.nonzero((w >> 26) == 0x25)[0]
            imm = (w[idx] & 0x3FFFFFF).astype(np.int64)
            imm = np.where(imm & 0x2000000, imm - 0x4000000, imm)
            _bl.append((lo + idx.astype(np.int64) * 4, lo + idx.astype(np.int64) * 4 + imm * 4))
    out = []
    for src, dst in _bl:
        out.extend(src[dst == target].tolist())
    return out
def mac_func_of(a):
    i = bisect.bisect_right(mac_starts, a) - 1
    return mac_starts[i] if i >= 0 else None
cs = Cs(CS_ARCH_X86, CS_MODE_64)
def win_calls(start):
    i = bisect.bisect_right(win_starts, start)
    end = win_starts[i] if i < len(win_starts) else start + 0x2000
    end = min(end, start + 0x4000)
    out = []
    for ins in cs.disasm(win_bytes(start, end - start), start):
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            if not (start <= t < end):
                out.append(t)
    return out

def vote_internal(mac_target, limit=150):
    """Windows twin of a Mac non-method code address, by voting over callers."""
    votes = collections.Counter(); n = 0
    seen = set()
    for c in mac_callers(mac_target):
        f = mac_func_of(c)
        if f is None or f in seen:
            continue
        seen.add(f)
        w, _, _ = win_twin(f)
        if w is None:
            continue
        cl = set(win_calls(w))
        votes.update(t for t in cl if t not in win_meth)   # callee must not itself be a named method
        n += 1
        if n >= limit:
            break
    if not votes:
        return None, 0, n
    t, c = votes.most_common(1)[0]
    return t, c, n

# ---------- 3. resolve harvested addresses ----------
rows = []
for v in sorted(used):
    where = ",".join(sorted(set(used[v]))[:6]) + ("" if len(set(used[v])) <= 6 else f",+{len(set(used[v]))-6}")
    if v in mac_names_at:
        w, nm, uniq = win_twin(v)
        sig = mac_names_at[v][0][1]
        note = "" if len(mac_names_at[v]) == 1 else f"shared body ({len(mac_names_at[v])} names)"
        if w is None:
            rows.append(("method", nm, v, None, "MISSING", note, where))
        else:
            rows.append(("method", nm, v, w, "ok" if uniq else "ok-overload-order", note, where))
    elif v in mac_md:
        nm = mac_md[v]; w = win_mdn.get(nm)
        rows.append(("metadata", nm, v, w, "ok" if w else "MISSING", "", where))
    elif v in mac_exp:
        nm = mac_exp[v]; w = win_exp.get(nm)
        rows.append(("export", nm, v, w, "ok" if w else "MISSING", "", where))
    elif in_mac_text(v):
        f = mac_func_of(v)
        if f is not None and f != v and f in mac_names_at and v - f < 0x4000:
            nm = mac_names_at[f][0][0]
            rows.append(("code-inside", f"{nm}+{v-f:#x}", v, None, "CHECK",
                         "inside a method body: a mask/constant, an old-build address, or a mid-function site (needs a byte pattern)", where))
            continue
        t, c, n = vote_internal(v)
        if t is None:
            rows.append(("other", "", v, None, "not-an-address" if n == 0 else "UNRESOLVED",
                         "no caller in the Mac binary (a constant)" if n == 0 else f"{n} caller twins, no vote", where))
        else:
            rows.append(("internal", "il2cpp internal", v, t, "ok-vote" if c >= max(3, n // 2) else "WEAK-vote",
                         f"{c}/{n} caller twins agree", where))
    else:
        rows.append(("other", "", v, None, "not-an-address", "constant / bounds / data", where))

# ---------- 4. field offsets ----------
CLS = re.compile(r"^(?:\[.*\]\s*)?(?:public|internal|private|protected)?[\w\s]*?\b(class|struct|enum|interface)\s+([^\s:]+).*// TypeDefIndex")
FLD = re.compile(r"^\t(?!//)(.*?)\s+(\S+); // 0x([0-9A-F]+)$")
def fields(path):
    out = {}; ns = ""; cls = None; depth = 0
    for line in open(path, errors="replace"):
        if line.startswith("// Namespace: "):
            ns = line[14:].strip()
        m = CLS.match(line)
        if m and not line.startswith("\t"):
            cls = (ns + "." if ns else "") + m.group(2)
            continue
        if cls:
            m = FLD.match(line.rstrip("\n"))
            if m and "static" not in m.group(1) and "const" not in m.group(1):
                out[(cls, m.group(2))] = int(m.group(3), 16)
            elif m and "static" in m.group(1):
                out[(cls, "static:" + m.group(2))] = int(m.group(3), 16)
    return out
mf = fields(os.path.join(A.mac_dump, "dump.cs"))
wf = fields(os.path.join(A.win_dump, "dump.cs"))
glob_diff = [(k, mf[k], wf[k]) for k in mf if k in wf and mf[k] != wf[k]]
glob_missing = [k for k in mf if k not in wf]
used_cls = {k[0] for k in mf if k[0].rsplit(".", 1)[-1].split("<")[0] in idents}
for k in sorted(mf):
    if k[0] in used_cls:
        w = wf.get(k)
        st = "ok" if w == mf[k] else ("MISSING" if w is None else "DIFF")
        rows.append(("field", f"{k[0]}.{k[1]}", mf[k], w, st, "", ""))

with open(A.out, "w") as o:
    o.write("kind\tname\tmac\twin\tstatus\tnote\tused_in\n")
    for kind, nm, m, w, st, note, where in rows:
        fm = (lambda x: "" if x is None else f"{x:#x}")
        o.write(f"{kind}\t{nm}\t{fm(m)}\t{fm(w)}\t{st}\t{note}\t{where}\n")

c = collections.Counter((r[0], r[4]) for r in rows)
print("rows", len(rows))
for k in sorted(c):
    print(f"  {k[0]:12s} {k[1]:20s} {c[k]}")
print(f"global fields: mac {len(mf)}, win {len(wf)}, same-name pairs differing {len(glob_diff)}, mac-only {len(glob_missing)}")
for k, a, b in glob_diff[:15]:
    print("   DIFF", k, hex(a), hex(b))
print(f"classes compared in detail (named in sources): {len(used_cls)}")
