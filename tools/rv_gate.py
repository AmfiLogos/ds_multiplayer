#!/usr/bin/env python3
"""rv gate: every game address in the multiplayer / Custom Modes (mp part) sources goes through rv().

The sources keep the addresses of the reference game version (src/rig.h RIG_BUILD). On another game version they
work only through src/rig_rv.c: F(), rig_hook(), md() / obj_new() and rv() itself map each address with the
version's address table (rva/<build>_<mac|win>.tsv, made by ds_modloader tools/gameupd/report.py). An address used
any other way (ds_base + 0x..., a table read by hand) would point at the wrong code on the next game version.

    python3 tools/rv_gate.py [--cm DIR] [extra files ...]      exit 1 = an address skips rv() (file:line listed)

What counts as an address: a hex number 0x10000..0x7FFFFFF in code (comments and strings skipped), except the
plain numbers in CONSTANTS. Allowed places: an argument of a call in CALLS (these apply rv(), or call something
that does), a #define whose uses are all allowed, an array in TABLES (its consumer applies rv(); the reason is
written next to it). A new hook table or a new helper that takes an address: add it here after checking that
its consumer goes through rv().
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALLS = {
    "F", "rv", "rig_hook", "md", "obj_new",
    "hook_checked",   # mp_game.c: reads the bytes at ds_base + rv(va), then rig_hook
    "empty_list",     # pvp_game.c: obj_new(ti)
    "comp_of",        # rig_det.c: md(slot)
}
TABLES = {
    ("ui_game.c", "hk"): "ui_install: rig_hook(hk[i].va, ...)",
    ("ord_game.c", "hk"): "ord_install: rig_hook(hk[i].va, ...)",
    ("trn_game.c", "scope_sites"): "trn install: rig_hook(scope_sites[i].va, ...)",
    ("rig_det.c", "ti_hashhelpers"): "md(ti_hashhelpers[i])",
    ("ui_tip.c", "want"): "ds_base + rv(want[i].rva)",
}
DEFINES_OK = {   # src/rig.h: the standalone test rig (rig_hook.c) runs only on the reference version's GameAssembly
    "RIG_CAVE_LO", "RIG_CAVE_HI", "RIG_SLOT_LO", "RIG_SLOT_HI", "RIG_IMAGE_END",
    "WIN_GA_SIZE",    # win/port/rig_main_win.c check_ga: the reference version's image size (skipped with a table)
}
CONSTANTS = {0x10000, 0x100000, 0x7000000, 0x7F00000, 0xFFFFFF,   # UTF-16 plane, sizes, the image bound, masks
             0x3FFFFFF, 0x7FFFF}                                    # rig_hook.c: arm64 branch / adrp field masks
HEX = re.compile(r"\b0x([0-9A-Fa-f]{5,8})(?:[uU]?[lL]{0,2})\b")


def strip(t):
    """comments and string / char literals -> spaces (line numbers kept)"""
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith("//", i):
            j = t.find("\n", i); j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in t[i:j])); i = j
        elif t[i] in "\"'":
            q, j = t[i], i + 1
            while j < n and t[j] != q and t[j] != "\n":
                j += 2 if t[j] == "\\" else 1
            out.append(q + " " * (j - i - 1) + (q if j < n else "")); i = j + 1
        else:
            out.append(t[i]); i += 1
    return "".join(out)


def enclosing_call(t, pos):
    """name of the innermost call whose parentheses hold pos (None: not inside a call in this statement)"""
    depth, i = 0, pos - 1
    while i >= 0:
        c = t[i]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                m = re.search(r"([A-Za-z_]\w*)\s*$", t[max(0, i - 80):i])
                if m and m.group(1) not in ("if", "while", "for", "return", "sizeof", "switch"):
                    return m.group(1)
            else:
                depth -= 1
        elif c in ";{}" and depth == 0 and c != "{":
            return None
        i -= 1
    return None


def array_of(t, pos):
    """the array an initializer literal belongs to: identifier before '[...] =' of its statement"""
    s = max(t.rfind(";", 0, pos), t.rfind("\n#", 0, pos)) + 1
    m = re.search(r"([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{", t[s:pos])
    return m.group(1) if m else None


def line_of(t, pos):
    return t.count("\n", 0, pos) + 1


def files(cm):
    out = []
    for d, dirs, fs in os.walk(os.path.join(ROOT, "src")):
        dirs[:] = [x for x in dirs if not x.endswith("_test") or x == "ord_test"]
        for f in fs:
            if f.endswith((".c", ".h", ".inc")) and (os.path.basename(d) != "ord_test" or f == "ord_smoke.c"):
                out.append(os.path.join(d, f))
    port = os.path.join(ROOT, "win", "port")                # absent in the loader's pin export of an older tree
    out += [os.path.join(port, f) for f in sorted(os.listdir(port)) if f.endswith(".c")] if os.path.isdir(port) else []
    if cm and os.path.isdir(cm):                       # Custom Modes: cm_mp.c + every sources_mp.txt (the mp part)
        out.append(os.path.join(cm, "cm_mp.c"))
        for d in sorted(os.listdir(cm)):
            lst = os.path.join(cm, d, "sources_mp.txt")
            if os.path.isfile(lst):
                for ln in open(lst):
                    ln = ln.strip()
                    if ln and not ln.startswith("#"):
                        out.append(os.path.join(cm, ln))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cm", default=os.environ.get("CM_REPO") or os.path.expanduser("~/ds_custommodes"))
    ap.add_argument("extra", nargs="*", help="more files (the loader's modules/multiplayer/mp_module.c)")
    a = ap.parse_args()
    srcs = files(a.cm) + a.extra
    text = {p: strip(open(p, errors="replace").read()) for p in srcs if os.path.isfile(p)}
    defines = {}                                       # name -> (file, line) of a #define holding a bare address
    bad = []

    def check(p, t, pos, what):
        call = enclosing_call(t, pos)
        if call in CALLS:
            return
        arr = array_of(t, pos)
        if arr and (os.path.basename(p), arr) in TABLES:
            return
        bad.append(f"{os.path.relpath(p, ROOT) if p.startswith(ROOT) else p}:{line_of(t, pos)}: {what} "
                   f"({'in ' + call + '()' if call else 'bare'}{', array ' + arr if arr else ''})")

    for p, t in text.items():
        for m in HEX.finditer(t):
            v = int(m.group(1), 16)
            if not 0x10000 <= v < 0x8000000 or v in CONSTANTS:
                continue
            ls = t.rfind("\n", 0, m.start()) + 1
            dm = re.match(r"[ \t]*#\s*define\s+(\w+)(\([^)]*\))?", t[ls:m.start()])
            if dm and not dm.group(2):                 # a value alias: its uses are checked below
                if enclosing_call(t, m.start()) not in CALLS and dm.group(1) not in DEFINES_OK:
                    defines[dm.group(1)] = p
                continue
            check(p, t, m.start(), f"{m.group(0)}")
    for name, where in sorted(defines.items()):
        for p, t in text.items():
            for m in re.finditer(r"\b%s\b" % re.escape(name), t):
                ls = t.rfind("\n", 0, m.start()) + 1
                if re.match(r"[ \t]*#\s*(define|undef|ifdef|ifndef|if)\b", t[ls:m.start()]):
                    continue
                check(p, t, m.start(), name)
    print(f"rv gate: {len(text)} files, {len(defines)} address #defines checked")
    if bad:
        print(f"rv gate: {len(bad)} game address(es) skip rv() (they would point at the wrong code on another game version):")
        for b in bad:
            print("  " + b)
        return 1
    print("rv gate: ok, every game address goes through rv()")
    return 0


if __name__ == "__main__":
    sys.exit(main())
