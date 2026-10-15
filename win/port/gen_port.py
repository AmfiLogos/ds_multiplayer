#!/usr/bin/env python3
"""Generate Windows sources from the Mac mod sources (run by build_win.sh before every build).

- Copies src/rig_cmd.c, src/rig_det.c, src/mp_*.c to win/build/gen/ and rewrites every hex literal that
  win_map.tsv knows (method / metadata / export / internal) to its Windows value. Field offsets are not
  touched (they are the same for every field the mod uses; win_map.tsv checks that).
- rig_det.c: the arm64 frame-pointer walk (CALLERS) becomes RtlCaptureStackBackTrace (x64 .pdata unwind);
  det_ranges.tsv -> det_ranges_win.tsv.
- Writes build/det_ranges_win.tsv (same classes as rig/tools/gen_ranges.py, Windows addresses).
- Fails if a source pattern it must patch is gone, or if an address that looks like an RVA has no Windows twin.
- Warns when a translated method address is shared by several methods on Windows (MSVC identical-code folding):
  hooking such an address hooks all of them.
"""
import collections, json, os, re, sys

H = os.path.expanduser("~")
ROOT = os.environ.get("DSMP_ROOT") or f"{H}/ds_multiplayer"   # DSMP_ROOT: a worktree
SRC = os.environ.get("DSMP_SRC", f"{ROOT}/src")      # DSMP_SRC: frozen release snapshot
OUT = f"{ROOT}/win/build/gen"
MAP = f"{ROOT}/win/win_map.tsv"
WIN_SJ = f"{ROOT}/win/25846389/dump/script.json"
os.makedirs(OUT, exist_ok=True)

tr = {}
kinds = {}
for line in open(MAP):
    f = line.rstrip("\n").split("\t")
    if f[0] in ("method", "metadata", "export", "internal") and f[3]:
        tr[int(f[2], 16)] = int(f[3], 16); kinds[int(f[2], 16)] = (f[0], f[1])

sj = json.load(open(WIN_SJ))
win_names = collections.defaultdict(list)
for m in sj["ScriptMethod"]:
    win_names[m["Address"]].append(m["Name"])

# literals that are not RVAs (masks, hash constants, cave bounds ...)
NOT_RVA = {0x10000, 0x7FFFF, 0xFFFFFF, 0x3FFFFFF, 0x100000C, 0x7000000, 0x604B000, 0x604B780, 0x6B9B800, 0x6B9C000}
HEX = re.compile(r"\b0x([0-9A-Fa-f]{5,8})\b")
problems = []; shared = []

import pefile as _pefile, struct as _struct
_ga = None
def win_first_word(rva):
    global _ga
    if _ga is None: _ga = _pefile.PE(f"{ROOT}/win/25846389/GameAssembly.dll", fast_load=True)
    return _struct.unpack("<I", _ga.get_data(rva, 4))[0]

_pdata = None
def win_func_end(rva):
    """x64 .pdata: end of the function that starts at rva, chained parts included"""
    global _pdata
    if _pdata is None:
        win_first_word(0x1000)
        sec = next(x for x in _ga.sections if x.Name.rstrip(b"\0") == b".pdata")
        d = sec.get_data(); _pdata = {}
        for i in range(0, len(d) - 11, 12):
            b, e, u = _struct.unpack_from("<III", d, i)
            if b: _pdata[b] = (e, _ga.get_data(u, 1)[0] >> 3 & 4)          # UNW_FLAG_CHAININFO: a part of the previous
    if rva not in _pdata: problems.append(f"no .pdata entry at {rva:#x}"); return rva
    e = _pdata[rva][0]
    while e in _pdata and _pdata[e][1]: e = _pdata[e][0]
    return e

def translate(name, text):
    def rep(m):
        v = int(m.group(1), 16)
        if v in tr:
            w = tr[v]
            if kinds[v][0] == "method" and len(win_names.get(w, [])) > 1:
                shared.append(f"{name}: {kinds[v][1]} -> {w:#x} shared by {len(win_names[w])} methods")
            return f"0x{w:X}"
        if 0x100000 <= v < 0x7000000 and v not in NOT_RVA:
            problems.append(f"{name}: {m.group(0)} looks like an RVA but has no Windows twin")
        return m.group(0)
    # an RVA inside a comment may be a mid-function offset (no twin): comments keep their text and are never flagged
    TOK = re.compile(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|/\*|//[^\n]*")
    out, pos = [], 0
    while True:
        m = TOK.search(text, pos)
        if not m: out.append(HEX.sub(rep, text[pos:])); break
        out.append(HEX.sub(rep, text[pos:m.start()]))
        if m.group(0) == "/*":
            e = text.find("*/", m.end()); e = len(text) if e < 0 else e + 2
            out.append(text[m.start():e]); pos = e
        elif m.group(0).startswith("//"): out.append(m.group(0)); pos = m.end()
        else: out.append(HEX.sub(rep, m.group(0))); pos = m.end()                  # string / char literal: code
    return "".join(out)

def split_call(t, i):
    """args of the call whose '(' is at t[i]: (list of top-level args, index after ')')"""
    depth, args, cur, j = 0, [], "", i + 1
    while j < len(t):
        ch = t[j]
        if ch in "([{": depth += 1
        elif ch in ")]}":
            if depth == 0: args.append(cur.strip()); return args, j + 1
            depth -= 1
        if ch == "," and depth == 0: args.append(cur.strip()); cur = ""
        else: cur += ch
        j += 1
    raise ValueError("unbalanced call")
def abi_rewrite(t, head_re, build):
    """rewrite every call `<head_re>args)` (head ends with the call's '(') with build(addr, args)"""
    out, pos, n = "", 0, 0
    for m in re.finditer(head_re, t):
        if m.start() < pos: continue
        args, end = split_call(t, m.end() - 1)
        out += t[pos:m.start()] + build(m.group(1), args); pos = end; n += 1
    return out + t[pos:], n

def must_sub(text, old, new, name):
    if old not in text:
        problems.append(f"{name}: pattern to patch not found: {old[:60]!r}")
        return text
    return text.replace(old, new)

def must_resub(text, pat, new, name):
    t, n = re.subn(pat, new, text, flags=re.S)
    if not n:
        problems.append(f"{name}: regex to patch not found: {pat[:60]!r}")
    return t

# every module of the Mac build (src/build.sh) except the Mac-only bootstrap (rig_main/rig_hook/rig_win;
# rig_sock is handled below); dsnet_steam.c is Mac-only for now (dyld/dlopen) -> port/steam_stub_win.c
SKIP_SRC = {"rig_main.c", "rig_hook.c", "rig_win.c", "rig_sock.c"}
FILES = sorted(f for f in os.listdir(SRC) if f.endswith(".c") and f not in SKIP_SRC) + ["ord_test/ord_smoke.c"]
# Custom Modes (custom/: FE, PvP, units; its own dylib on the Mac since the M4 split, compiled into the same DLL here
# with -DCM_SAME_IMAGE: rig_main_win.c starts it through mp_api_get): generated flat into build/gen, "../x.h" -> "x.h".
# The list = custom/build_module.sh's custom-modes-mp sources. DSMP_CUSTOM: a frozen snapshot's custom/ (release).
CUSTOM = os.environ.get("DSMP_CUSTOM", os.path.join(os.path.dirname(SRC.rstrip("/")), "custom"))
CUSTOM_FILES = ["cm_mp.c", "fe/fe_json.c", "fe/fe_save.c", "fe/fe_game.c", "fe/fe_menu.c", "pvp/pvp_core.c", "pvp/pvp_modcheck.c",
                "pvp/pvp_mods.c", "pvp/pvp_game.c", "pvp/pvp_ui.c", "units/pvp_units.c", "units/pvp_wiki.c", "units/pvp_build.c"]
FILES += [f"custom/{f}" for f in CUSTOM_FILES if os.path.exists(os.path.join(CUSTOM, f))]
if len([f for f in FILES if f.startswith("custom/")]) != len(CUSTOM_FILES): problems.append(f"custom: not all of {CUSTOM_FILES} in {CUSTOM}")
for fn in FILES:
    p = os.path.join(CUSTOM, fn[len("custom/"):]) if fn.startswith("custom/") else os.path.join(SRC, fn)
    if not os.path.exists(p):
        continue
    t = open(p).read()
    if fn.startswith("custom/"):
        t = t.replace('#include "../', '#include "')
        # Darwin stat / time: st_mtimespec -> st_mtime (seconds; no nanoseconds on msvcrt), localtime_r -> localtime_s
        t = t.replace("st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec / 1e9", "(double)st.st_mtime")
        t = t.replace("localtime_r(&st.st_mtimespec.tv_sec, &tm)", "localtime_s(&tm, &st.st_mtime)")
        t = t.replace("st.st_mtimespec.tv_sec", "st.st_mtime")
        if "localtime_r(" in t:
            t = re.sub(r"localtime_r\(([^,()]+), ([^()]+)\)", r"localtime_s(\2, \1)", t)
        if "st_mtimespec" in t: problems.append(f"{fn}: st_mtimespec left (Darwin-only stat field)")
    if fn == "rig_det.c":
        # DSRIG_RANGES names the Mac det_ranges.tsv (release gate, kit); Windows reads DSRIG_RANGES_WIN only
        # (2026-10-15: a kit run with the Mac file on Windows differed from the first tick)
        t = must_sub(t, 'getenv("DSRIG_RANGES")', 'getenv("DSRIG_RANGES_WIN")', fn)
        t = must_resub(t, r"#define CALLERS\(c\) do \{.*?\} while \(0\)\n",
                       "#define CALLERS(c) do { \\\n"
                       "    void *bt_[3] = { 0, 0, 0 }; USHORT n_ = RtlCaptureStackBackTrace(1, 3, bt_, NULL); \\\n"
                       "    (c)[0] = n_ > 0 ? rva_of((uintptr_t)bt_[0]) : 0; (c)[1] = n_ > 1 ? rva_of((uintptr_t)bt_[1]) : 0; \\\n"
                       "    (c)[2] = n_ > 2 ? rva_of((uintptr_t)bt_[2]) : 0; \\\n"
                       "} while (0)\n", fn)
        t = must_sub(t, "det_ranges.tsv", "det_ranges_win.tsv", fn)
    if fn == "rig_cmd.c":                                         # Windows-only test commands (port/win_test_cmds.c)
        t = must_sub(t, "char *dr = rig_det_exec(cmd, line); if (dr) return dr;",
                     "{ extern char *win_exec(const char *, const char *); char *wr = win_exec(cmd, line); if (wr) return wr; }\n"
                     "    char *dr = rig_det_exec(cmd, line); if (dr) return dr;", fn)
    # a module's own static send()/recv() would clash with the Winsock declarations: rename it
    for nm in ("send", "recv", "accept", "connect"):
        if re.search(r"\bstatic\s+\w+\s+\*?%s\(" % nm, t):
            t = re.sub(r"(?<![.\w>])%s\(" % nm, "mod_%s(" % nm, t)
    # Windows x64 ABI: a Vector2 argument is ONE 8-byte register (packed x,y), not two float registers as on arm64.
    # The Mac source writes some RectTransform setters as void (*)(float, float, Obj, void *): rewrite to a struct.
    t, nv2 = abi_rewrite(t, r"F\((\w+), void \(\*\)\(float, float, Obj, void \*\)\)\(",
                         lambda a, args: f"F({a}, void (*)(Obj, dsw_v2, void *))({args[2]}, (dsw_v2){{ {args[0]}, {args[1]} }}, {args[3]})")
    if nv2: print(f"ABI {fn}: {nv2} Vector2 float-pair calls rewritten")
    # EntityDataContainer.MoveUnit(MapType, UnitEntity, Vector2 last, Vector2 current) written as four floats (arm64 s0-s3)
    t, nmu = abi_rewrite(t, r"F\((\w+), void \(\*\)\(Obj, int, Obj, float, float, float, float, void \*\)\)\(",
                         lambda a, args: f"F({a}, void (*)(Obj, int, Obj, dsw_v2, dsw_v2, void *))({args[0]}, {args[1]}, {args[2]}, "
                                         f"(dsw_v2){{ {args[3]}, {args[4]} }}, (dsw_v2){{ {args[5]}, {args[6]} }}, {args[7]})")
    if nmu: print(f"ABI {fn}: {nmu} (Obj, int, Obj, V2, V2) float-quad calls rewritten")
    for mm in re.finditer(r"\(\*\)\(([^)]*\bfloat, float\b[^)]*)\)", t):
        # "float, float" alone: one of our own functions (ui_teams ui_panel_hit by dlsym, viewui); a game call always ends in void * (MethodInfo)
        if mm.group(1) not in ("float, float, Obj, void *", "float, float") and "dsnet_client" not in mm.group(1):
            problems.append(f"{fn}: float pair in a game-call prototype (a Vector2 on arm64?) not rewritten: ({mm.group(1)})")
    # A Guid written as two u64 (arm64: x1, x2). Windows x64 passes a 16-byte struct by pointer: use the struct type.
    t, ng = abi_rewrite(t, r"F\((\w+), (\w+) \(\*\)\(Obj, uint64_t, uint64_t, void \*\)\)\(",
                        lambda a, args: f"F({a}, void (*)(Obj, Guid, void *))({args[0]}, (Guid){{ {args[1]}, {args[2]} }}, {args[3]})")
    if ng: print(f"ABI {fn}: {ng} Guid u64-pair calls rewritten")
    # PvP (2026-10-15): (Guid, Vector2Int) as (lo, hi, pos) and (Guid, Guid) as four u64. Only calls whose arguments are
    # visibly a Guid's .lo / .hi are rewritten; anything else stays and the check below stops the build.
    def g_pos(a, args):
        if len(args) == 5 and args[1].strip().endswith(".lo") and args[2].strip().endswith(".hi"):
            return f"F({a}, void (*)(Obj, Guid, uint64_t, void *))({args[0]}, (Guid){{ {args[1]}, {args[2]} }}, {args[3]}, {args[4]})"
        return f"F({a}, void (*)(Obj, uint64_t, uint64_t, uint64_t, void *))({', '.join(args)})"
    t, ngp = abi_rewrite(t, r"F\((\w+), void \(\*\)\(Obj, uint64_t, uint64_t, uint64_t, void \*\)\)\(", g_pos)
    def g_gg(a, args):
        if len(args) == 6 and all(args[i].strip().endswith(sfx) for i, sfx in ((1, ".lo"), (2, ".hi"), (3, ".lo"), (4, ".hi"))):
            return f"F({a}, bool (*)(Obj, Guid, Guid, void *))({args[0]}, (Guid){{ {args[1]}, {args[2]} }}, (Guid){{ {args[3]}, {args[4]} }}, {args[5]})"
        return f"F({a}, bool (*)(Obj, uint64_t, uint64_t, uint64_t, uint64_t, void *))({', '.join(args)})"
    t, ngg = abi_rewrite(t, r"F\((\w+), bool \(\*\)\(Obj, uint64_t, uint64_t, uint64_t, uint64_t, void \*\)\)\(", g_gg)
    if ngp or ngg: print(f"ABI {fn}: {ngp} (Guid, Vector2Int) and {ngg} (Guid, Guid) u64 calls rewritten")
    if re.search(r"\(\*\)\([^)]*uint64_t, uint64_t", t):
        problems.append(f"{fn}: a (.., uint64_t, uint64_t ..) prototype (split Guid?) the ABI rewrite did not catch")
    # arm64 trick: a float argument written BEFORE the object (s0 and x0 are separate register files). On x64
    # arguments are positional: put `this` first.
    t, nf1 = re.subn(r"F\((\w+), (\w+) \(\*\)\(float, Obj, void \*\)\)\(([^,()]+), ([^,()]+), NULL\)",
                     r"F(\1, \2 (*)(Obj, float, void *))(\4, \3, NULL)", t)
    if nf1: print(f"ABI {fn}: {nf1} (float, Obj) calls reordered")
    for mm in re.finditer(r"\(\*\)\(((?:float|double)[^)]*?Obj[^)]*)\)", t):
        problems.append(f"{fn}: arm64-ordered prototype (float before Obj) not rewritten: ({mm.group(1)})")
    if re.search(r"\(\*\)\(float, float, Obj", t):
        problems.append(f"{fn}: a (float, float, Obj ...) Vector2 call the ABI rewrite did not catch")
    # first-instruction build guards ({ <Mac rva>, <arm64 word> } in a want[] table, e.g. ui_tip.c build_ok): put the
    # first 4 bytes of the Windows function there (the rva itself is translated below like any other)
    def win_word(m):
        v = int(m.group(1), 16)
        if v not in tr: problems.append(f"{fn}: build guard rva {v:#x} has no Windows twin"); return m.group(0)
        return "{ %s, %#010x }" % (m.group(1), win_first_word(tr[v]))
    t = re.sub(r"\{ (0x[0-9A-Fa-f]{5,8}), 0x[0-9a-f]{8} \}", lambda m: win_word(m) if "want[]" in t else m.group(0), t)
    if fn == "ui_game.c":                                         # popup-3 caller log (lan_v9 item 5)
        # arm64 frame-pointer walk: GameAssembly on Windows has no rbp chain (would read garbage): x64 .pdata unwind
        t = must_sub(t, """uintptr_t *fp = (uintptr_t *)__builtin_frame_address(0);
        for (int d = 0; d < 8 && fp && k < sizeof bt - 16; d++) {
            uintptr_t lr = fp[1]; fp = (uintptr_t *)fp[0];
            k += (size_t)snprintf(bt + k, sizeof bt - k, " +%#lx", (unsigned long)(lr - ds_base));
            if (!fp || (uintptr_t)fp & 7) break;
        }""", """void *ra_[8] = { 0 }; USHORT n_ = RtlCaptureStackBackTrace(1, 8, ra_, NULL);
        for (int d = 0; d < n_ && k < sizeof bt - 16; d++)
            k += (size_t)snprintf(bt + k, sizeof bt - k, " +%#lx", (unsigned long)((uintptr_t)ra_[d] - ds_base));""", fn)
    t = translate(fn, t)
    if fn == "ui_game.c":   # CheckGameOver range: the Mac end is the next method (RecordDeadUnit), not adjacent on Windows
        t = must_sub(t, "lr0 < 0x%X)" % tr[0xD858F0], "lr0 < 0x%X)" % win_func_end(tr[0xD854EC]), fn)
    sf = fn if fn.startswith("custom/") else f"src/{fn}"
    open(os.path.join(OUT, os.path.basename(fn)), "w").write(f"/* GENERATED by win/port/gen_port.py from {sf}: do not edit */\n#line 1 \"{sf}\"\n" + t)

# ord_game.c: TryCraft is inlined into TaskFinishEventHandler.CraftingTaskFinished on Windows (no direct caller),
# so ord's h_try_craft (crafted items -> owner tag) never runs. Same wrapping one level up: CraftingTaskFinished
# passes its taskData as TryCraft's `crafting` argument (Mac 0xDC953C), and nothing else in it creates items.
CRAFT_SRC = """static bool h_try_craft(Obj self, int32_t map, Obj crafting, Obj recipe, void *mi) {
    int owner = (item_api.tag && crafting) ? craft_owner_get(AT(crafting, 0x28, Obj)) : -1;   /* SharedTaskData.Target */
    int nb = 0; Guid *before = owner >= 0 ? item_guids(map, &nb) : NULL;
    bool r = o_try_craft(self, map, crafting, recipe, mi);
    if (before) { tag_new_items(map, before, nb, owner, 0); free(before); }
    return r;
}"""
CRAFT_WIN = """
/* ---- Windows only (appended by win/port/gen_port.py): TryCraft is inlined into CraftingTaskFinished */
static void (*o_win_craft_fin)(Obj, int32_t, Obj, void *);
static void h_win_craft_fin(Obj self, int32_t map, Obj task, void *mi) {
    int owner = (item_api.tag && task) ? craft_owner_get(AT(task, 0x28, Obj)) : -1;      /* as h_try_craft */
    int nb = 0; Guid *before = owner >= 0 ? item_guids(map, &nb) : NULL;
    o_win_craft_fin(self, map, task, mi);
    if (before) { tag_new_items(map, before, nb, owner, 0); free(before); }
}
void ord_win_craft_install(void) {
    rig_hook(0xAD0970, (void *)h_win_craft_fin, (void **)&o_win_craft_fin, "ord CraftingTaskFinished (TryCraft inlined on Windows)");
}
"""
p_ord = os.path.join(OUT, "ord_game.c")
if os.path.exists(p_ord):
    t = open(p_ord).read()
    if CRAFT_SRC not in open(os.path.join(SRC, "ord_game.c")).read():
        problems.append("ord_game.c: h_try_craft changed: update CRAFT_WIN in gen_port.py to match")
    else:
        open(p_ord, "w").write(t + CRAFT_WIN)

# lan_v8 hooks that MSVC inlined into their only caller (inline_audit.py). Same fix as TryCraft: a Windows-only
# wrapper one level up, appended to the generated module so it reaches the module's statics. Each needs the
# Mac hook text below unchanged (the build stops otherwise).
WIN_WRAP = {
    "ui_game.c": (["static int no_timelock(Obj popup) {", "static void hk_set_timelock(Obj p, void *mi) {",
                   "static void hk_unset_timelock(Obj p, void *mi) { if (!no_timelock(p)) o_unset_timelock(p, mi); }",
                   "static void hk_ev_click(Obj v, int idx, void *mi) {",
                   "static void hk_sc_name(Obj parts, Obj unit, void *mi) {      /* stock sets the plain name; add the tag after */\n    o_sc_name(parts, unit, mi);\n    sc_tag(parts, unit);\n}",
                   "static void guild_button_apply(void) {",
                   "static void hk_faction_refresh(Obj v, int map, void *mi) { o_faction_refresh(v, map, mi); if (u_gc_get(h_faction_view) != v) { u_gc_free(h_faction_view); h_faction_view = u_gc_new(v); } guild_button_apply(); }"], """
/* ---- Windows only (appended by win/port/gen_port.py): hooks MSVC inlined into their only caller */
/* PopUpUI_Base.Set/UnsetTimeLock are inlined into the sealed OnActivate / OnDeactivate (both test _timeLock +0x39):
   for a popup that never pauses in multiplayer, run them with _timeLock cleared (= the Mac hooks skipping both) */
static void (*o_win_pop_act)(Obj, void *), (*o_win_pop_deact)(Obj, void *);
static void win_pop_run(void (*o)(Obj, void *), Obj p, void *mi, const char *what) {
    if (!p || !no_timelock(p)) { o(p, mi); return; }
    uint8_t tl = AT(p, 0x39, uint8_t);
    if (tl && what) rig_log("ui: %s time lock skipped (multiplayer: never pauses)", cls_name(p));
    AT(p, 0x39, uint8_t) = 0; o(p, mi); AT(p, 0x39, uint8_t) = tl;
}
static void h_win_pop_act(Obj p, void *mi) { win_pop_run(o_win_pop_act, p, mi, "act"); }
static void h_win_pop_deact(Obj p, void *mi) { win_pop_run(o_win_pop_deact, p, mi, NULL); }
/* EventNodeUIView.OnClickChoice is inlined into its button lambda <>c__DisplayClass15_0.<SetChoiceCallback>b__0
   (closure: view +0x10, choice index +0x18): route the click through hk_ev_click as on the Mac */
static void (*o_win_ev_lambda)(Obj, void *);
static void h_win_ev_lambda(Obj dc, void *mi) {
    Obj v = dc ? AT(dc, 0x10, Obj) : NULL;
    if (!v) { o_win_ev_lambda(dc, mi); return; }
    hk_ev_click(v, AT(dc, 0x18, int32_t), NULL);
}
/* SubUI_UnitShortCutParts.RefreshUnitName is inlined into PlayerUnitShortCutView.RefreshUnitShortCutPartsName:
   after it, tag the parts that show this unit (hk_sc_name's sc_tag) */
static void (*o_win_sc_names)(Obj, Obj, void *);
int win_sc_names_calls, win_sc_names_tagged;                 /* test counters (win_wrapstat) */
static void h_win_sc_names(Obj view, Obj unit, void *mi) {
    o_win_sc_names(view, unit, mi); win_sc_names_calls++;
    for (int i = 0; unit && i < MAX_SC; i++) {
        Obj p = u_gc_get(sc[i].parts);
        if (p && u_alive(p) && u_gc_get(sc[i].unit) == unit) { sc_tag(p, unit); win_sc_names_tagged++; }
    }
}
int ui_win_sc_names_test(void) {                 /* test: the stock name refresh of the first tracked unit, through the hook */
    Obj v = find_view(&h_bar_view, "Refactor.UI", "PlayerUnitShortCutView");
    for (int i = 0; v && i < MAX_SC; i++) {
        Obj p = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
        if (p && u_alive(p) && u) { F(0xC20090, void (*)(Obj, Obj, void *))(v, u, NULL); return 0; }
    }
    return v ? -2 : -1;
}
int ui_win_ev_lambda_test(int idx) {             /* test: call the hooked lambda with a closure-shaped block */
    Obj v = u_gc_get(h_ev_view); if (!u_alive(v)) return -1;
    struct { void *k, *m; Obj view; int32_t idx; } dc = { 0, 0, v, idx };
    F(0xBEE3A0, void (*)(void *, void *))(&dc, NULL); return 0;
}
/* FactionManagementView.OnWorldRefresh (Foreign Expedition hub: Guild Contact button hidden with Recruiting Off) is
   inlined into FactionManagementPresenter.WorldRefreshed(IViewUpdateData): apply the button rule after it (the view
   handle comes from hk_faction_init / ActivateButton, both still hooked on Windows) */
static void (*o_win_fac_world)(Obj, Obj, void *);
static void h_win_fac_world(Obj p, Obj data, void *mi) { o_win_fac_world(p, data, mi); guild_button_apply(); }
void ui_win_install(void) {
    rig_hook(0xEF58E8, (void *)h_win_fac_world, (void **)&o_win_fac_world, "ui FactionManagementPresenter.WorldRefreshed (OnWorldRefresh inlined on Windows)");
    rig_hook(0xC53060, (void *)h_win_pop_act, (void **)&o_win_pop_act, "ui PopUpUI_Base.OnActivate (SetTimeLock inlined on Windows)");
    rig_hook(0xC53390, (void *)h_win_pop_deact, (void **)&o_win_pop_deact, "ui PopUpUI_Base.OnDeactivate (UnsetTimeLock inlined on Windows)");
    rig_hook(0xBEE3A0, (void *)h_win_ev_lambda, (void **)&o_win_ev_lambda, "ui EventNodeUIView choice lambda (OnClickChoice inlined on Windows)");
    rig_hook(0xC20090, (void *)h_win_sc_names, (void **)&o_win_sc_names, "ui PlayerUnitShortCutView.RefreshUnitShortCutPartsName (RefreshUnitName inlined on Windows)");
}
"""),
    "mpmenu.c": (["static bool h_textfocus(void *mi) {", "o_textfocus(mi); }"], """
/* ---- Windows only (appended by win/port/gen_port.py): KeyInputDispatcher.IsTextInputFocused is inlined into
   UpdateKeyStream. When the Mac hook h_textfocus says "text focused" (chat typing, lobby settings, ... or the stock
   test), do UpdateKeyStream's own "text focused" branch (Mac 0xD3B6DC): reset the two key lists (+0x10, +0x18:
   List _size = 0, _version + 1) and +0x40's +0x28, nothing else */
static void (*o_win_keystream)(Obj, void *);
int win_keystream_calls, win_keystream_blocked;              /* test counters (win_wrapstat) */
static void h_win_keystream(Obj self, void *mi) {
    win_keystream_calls++;
    if (!self || !h_textfocus(NULL)) { o_win_keystream(self, mi); return; }
    win_keystream_blocked++;
    for (int off = 0x10; off <= 0x18; off += 8) { Obj l = AT(self, off, Obj); if (l) { AT(l, 0x18, int32_t) = 0; AT(l, 0x1C, int32_t)++; } }
    Obj b = AT(self, 0x40, Obj); if (b) AT(b, 0x28, int32_t) = 0;
}
void mpmenu_win_install(void) {
    rig_hook(0xA62B80, (void *)h_win_keystream, (void **)&o_win_keystream, "mpmenu KeyInputDispatcher.UpdateKeyStream (IsTextInputFocused inlined on Windows)");
}
"""),
}
for fn, (needs, code) in WIN_WRAP.items():
    p_out = os.path.join(OUT, fn); src_t = open(os.path.join(SRC, fn)).read()
    miss = [n for n in needs if n not in src_t]
    if miss: problems.append(f"{fn}: Mac hook text changed, update WIN_WRAP in gen_port.py: {miss[0][:60]!r}")
    else:
        gen_t = open(p_out).read()
        open(p_out, "w").write(gen_t + code)

# rig_sock.c: keep the queue, JSON helpers and connection thread; replace the AF_UNIX listener
t = open(os.path.join(SRC, "rig_sock.c")).read()
cut = "static char sock_path[1100];"
if cut not in t:
    problems.append("rig_sock.c: listener marker not found")
else:
    t = t[:t.index(cut)] + '#include "rig_sock_tcp.inc"\n'
    open(os.path.join(OUT, "rig_sock.c"), "w").write("/* GENERATED by win/port/gen_port.py from src/rig_sock.c: do not edit */\n#line 1 \"src/rig_sock.c\"\n" + t)

# dsnet (+ its test) and rig_sock: socket close/read/write -> dsw_* (not member calls like tr.close)
POSIXIO = re.compile(r"(?<![.\w>])(close|read|write|send|recv)\(")
os.makedirs(os.path.join(OUT, "net"), exist_ok=True)
for rel in ("net/dsnet_link.c", "net/dsnet_server.c", "net/dsnet_client.c", "net/test/test_dsnet.c"):
    t = open(os.path.join(SRC, rel)).read()
    t = POSIXIO.sub(lambda m: "dsw_" + m.group(1) + "(", t)
    t = t.replace('#include "../dsnet.h"', '#include "dsnet.h"')
    open(os.path.join(OUT, "net", os.path.basename(rel)), "w").write(
        f"/* GENERATED by win/port/gen_port.py from src/{rel}: do not edit */\n#line 1 \"src/{rel}\"\n" + t)
p2 = os.path.join(OUT, "rig_sock.c")
if os.path.exists(p2):
    t = open(p2).read(); open(p2, "w").write(POSIXIO.sub(lambda m: "dsw_" + m.group(1) + "(", t))

# det_ranges_win.tsv (classes as in rig/tools/gen_ranges.py)
HELPER = re.compile(r'^Refactor\.(RandomUtil|WeightedRandomUtil)\b')
VIEW_NS = re.compile(r'^(Refactor\.View\.|Refactor\.UI\.|Refactor\.FX\.|Febucci\.|AllIn1SpriteShader\.|DG\.Tweening|TMPro\.|Refactor\.Sound\.|UnityEngine\.|Unity\.)')
VIEW_CLS = re.compile(r'(FX|SFX|Sound|BGM|Ambience|Graphic|HUD|MapCloud|SpriteManager|Camera|PostProcessingManager|Presenter|Animator|Tween|Particle|Speech|UI$|^UI|View$|Tooltip)')
# core_next merge 2026-10-15 (rig/tools/gen_ranges.py): every class-name segment after the namespace is tested (nested
# classes, e.g. PostProcessingManager.<Co_Shake>d__4), generic argument lists are cut first, and single view methods in
# non-view classes (loading tips). Namespaces from the Windows dump.cs "// Namespace:" lines (longest prefix first).
VIEW_METHODS = {'Refactor.Util.TextSheet$$GetRandomTip', 'Refactor.Util.DataSheetManager$$GetRandomTip'}
NAMESPACES = sorted({l.split('// Namespace:', 1)[1].strip() for l in open(f"{ROOT}/win/25846389/dump/dump.cs", errors="replace")
                     if l.startswith('// Namespace:')} - {''}, key=len, reverse=True)
def strip_generic_args(s):
    out, depth = [], 0
    for ch in s:
        if depth:
            depth += (ch == '<') - (ch == '>'); continue
        if ch == '<' and out and (out[-1].isalnum() or out[-1] in '_`'):
            depth = 1; continue
        out.append(ch)
    return ''.join(out)
def class_parts(cls_name):
    cls_name = strip_generic_args(cls_name)
    for ns in NAMESPACES:
        if cls_name.startswith(ns + '.'):
            return cls_name[len(ns) + 1:].split('.')
    return cls_name.split('.')
def is_view(n):
    c = n.split("$$")[0]
    return bool(VIEW_NS.match(c) or VIEW_CLS.search(c.split(".")[-1]) or n in VIEW_METHODS or any(VIEW_CLS.search(x) for x in class_parts(c)))
ms = sorted({(m["Address"], m["Name"]) for m in sj["ScriptMethod"]})
starts = sorted({a for a, _ in ms} | set(sj["Addresses"]))
import bisect
os.makedirs(f"{ROOT}/build", exist_ok=True)
n1 = n2 = 0; done = set()
# written to win/build (what the Windows copy W, the gate and the kits read) and, as before, to ROOT/build.
# Until 2026-10-15 only ROOT/build got it: win/build kept the 2026-10-12 file (same rules until the core_next merge).
RANGES_WIN = f"{ROOT}/win/build/det_ranges_win.tsv"
with open(RANGES_WIN, "w") as f:
    for a, n in ms:
        if a in done:           # identical-code folding: first name wins (sorted order)
            continue
        i = bisect.bisect_right(starts, a)
        end = starts[i] if i < len(starts) else a + 4
        cls_name = n.split("$$")[0]; last = cls_name.split(".")[-1]
        k = 2 if HELPER.match(cls_name) else 1 if is_view(n) else 0
        if k:
            names = win_names[a]
            # a folded body that also serves a sim method is not view code
            if k == 1 and any(not is_view(x) for x in names):
                continue
            done.add(a)
            f.write("%x %x %d %s\n" % (a, end, k, n.replace(" ", "_")))
            n1 += k == 1; n2 += k == 2

# hooks on folded (shared) Windows bodies: each must be class-filtered in rig_main_win.c (FILT) or known-harmless
FILTERED = {"QuitToTitleSceneRequested..ctor", "CombatInfoComponent.SetCombatTarget", "CombatTargetApplyData..ctor",
            "ui UnitObject.SetHUD", "ui ClanDataContainer.get_CurrentPopulation", "ui ClanDataContainer.get_MaxPopulation",
            "WaitUntilGameStabilizes.MoveNext"}
HARMLESS = {"Random.Range(int)", "Random.RandomRangeInt",          # the two are the same function
            "System.HashCode.GenerateGlobalSeed",                    # shared with System.Random's seed (also det)
            "FogOfWarView.SetHalfVisible", "FogOfWarView.LoadVisible"}   # one body on Windows; mp_fog.c handles both in one rule
for fn in sorted(os.listdir(OUT)):
    if not fn.endswith(".c"): continue
    t = open(os.path.join(OUT, fn)).read()
    defs = {n: int(a, 16) for n, a in re.findall(r"#define (\w+)\s+0x([0-9A-F]+)", t)}
    pairs = re.findall(r"rig_hook\(\s*(\w+)\s*,[^;]*?\"([^\"]+)\"", t) + re.findall(r"\{\s*(0x[0-9A-F]+|VA_\w+)\s*,\s*\(?\w+[^}]*?\"([^\"]+)\"\s*\}", t)
    for arg, who in pairs:
        a = int(arg, 16) if arg.startswith("0x") else defs.get(arg)
        if a is not None and len(win_names.get(a, [])) > 1 and who not in FILTERED | HARMLESS:
            problems.append(f"{fn}: hook \"{who}\" at {a:#x} is a body shared by {len(win_names[a])} methods on Windows: add it to FILT in rig_main_win.c")
for s in sorted(set(shared)):
    print("WARN", s)
import shutil; shutil.copy(RANGES_WIN, f"{ROOT}/build/det_ranges_win.tsv")
print(f"generated {len(FILES)} files into {OUT}; det_ranges_win: view {n1}, helper {n2}")
if problems:
    for p in problems: print("ERROR", p)
    sys.exit(1)
