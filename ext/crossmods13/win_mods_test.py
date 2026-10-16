#!/usr/bin/env python3
"""v13 crossmods: our mods on Windows, through the loader kit in the CrossOver bottle DSMP (hidden, no graphics, no sound).
  win_mods_test.py <kit dir> [--no-install] [--game-s N]
The kit must hold the mods multiplayer, custom-modes, gcd-cdr, hell-difficulty and the test-only gcd-probe + hell-probe.
Checks: modules load (dsload.log); Hell code patches N/N + Hell tables built + loaded instead of the stock bundle; staff
strip on Hell only (hell-probe: AFFECTER_Mage kit on CombatDifficulty 5 and 1); Mods window lists our four mods; Custom
Modes' mp part on; in a world: "global cooldown #n" lines of gcd-cdr with gcd-probe's forced cooldowns on units with
cooldown reduction. Needs: no wine process, ~/ds_multiplayer/rig/wine.lock free, a lease (taken here)."""
import json, os, re, shutil, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win")
import wloader as w
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out", time.strftime("%Y%m%d-%H%M%S")); os.makedirs(OUT, exist_ok=True)

MINE = set()                                   # wine processes this run started (only these are ever stopped)
def note_started(before):
    MINE.update(p for p, _ in w.dsmp_pids() if p not in before)
def stop_mine():
    if not MINE: return
    try: w.cmd({"cmd": "quit"}, timeout=5)
    except Exception: pass
    time.sleep(2)
    for pid in list(MINE):
        try: os.kill(pid, 9)
        except Exception: pass
    MINE.clear()

WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
SAVE = f"{H}/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json"
args = sys.argv[1:]
KIT = args[0]; GAME_S = int(args[args.index("--game-s") + 1]) if "--game-s" in args else 120
results = []
def log(*a): print("[xm]", *a, flush=True)
def check(name, ok, info=""):
    results.append((name, bool(ok), info)); log("PASS" if ok else "FAIL", name, info)
def dsl(): return open(os.path.join(w.PROF, "dsload.log"), errors="replace").read() if os.path.exists(os.path.join(w.PROF, "dsload.log")) else ""

PAIR = None; t_l = time.time()
while not PAIR and time.time() - t_l < float(os.environ.get("XM_LEASE_WAIT", "1200")):   # max 4 pairs machine-wide: wait for one
    lease = subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "lease", "crossmods", "--pid", str(os.getpid())], capture_output=True, text=True)
    PAIR = lease.stdout.strip() if lease.returncode == 0 else None
    if not PAIR: time.sleep(20)
log("lease", PAIR or lease.stderr.strip(), f"after {time.time() - t_l:.0f} s")
# one wine copy machine-wide (taken after the lease, so a wait for a pair never holds the wine lock)
HAVE_LOCK = bool(PAIR) and subprocess.run([sys.executable, WL, "take", "crossmods", "--pid", str(os.getpid()), "--wait", os.environ.get("XM_LOCK_WAIT", "3600")]).returncode == 0
try:
    if not PAIR: raise RuntimeError("no lease")
    if not HAVE_LOCK: raise RuntimeError("wine busy / wine.lock held by another lane")
    if "--no-install" not in args:
        log("install rc", w.install(KIT))
    # profile (what the Mods window writes): our four mods + the two test probes
    md = os.path.join(w.PROF, "data", "Mods"); os.makedirs(md, exist_ok=True)
    en = os.environ.get("XM_ENABLED", "multiplayer,custom-modes,gcd-cdr,hell-difficulty,gcd-probe,hell-probe").split(",")
    json.dump({"enabled": en, "order": en}, open(os.path.join(md, "active.json"), "w"))
    for f in ("hell_probe_go.txt", "hell_probe_go.txt.done"):   # a go file left by a stopped run
        p = os.path.join(w.PROF, f)
        if os.path.exists(p): os.replace(p, os.path.join(OUT, "stale_" + f))
    # mods per world: this test's save is played with this mod set (the loader's world check reads worlds.json)
    wj = os.path.join(md, "worlds.json"); wd = json.load(open(wj)) if os.path.exists(wj) else {}
    key = "+".join(sorted(m for m in en if m in ("custom-modes", "gcd-cdr", "hell-difficulty")))
    wd.setdefault("worlds", {})["f816ebc6-195c-432c-9fff-754b75ee3738"] = key
    json.dump(wd, open(wj, "w"))
    for f in ("dsload.log", "rig.log"):                      # fresh logs for this run (old ones kept)
        p = os.path.join(w.PROF, f)
        if os.path.exists(p): os.replace(p, os.path.join(OUT, "prev_" + f))
    ui = os.path.join(OUT, "ui"); os.makedirs(ui, exist_ok=True)
    os.environ["DSL_UI_TEST"] = w.winpath(ui); os.environ["DSL_GCDPROBE_FORCE"] = "8"
    before = {p for p, _ in w.dsmp_pids()}
    w.start([]); time.sleep(5); note_started(before)
    t0 = time.time(); st = {}
    while time.time() - t0 < 400:
        try: st = w.cmd({"cmd": "status"}, timeout=5)
        except Exception: st = {}
        if st.get("boot_ready") and st.get("scene") == 1: break
        time.sleep(5)
    log("title:", {k: st.get(k) for k in ("scene", "boot_ready", "busy")}, f"{time.time() - t0:.0f} s")
    L = dsl()
    check("reached the title (control port answers)", st.get("boot_ready") and st.get("scene") == 1)
    if os.environ.get("XM_TITLE_ONLY"): raise SystemExit("title only")
    check("gcd-cdr loaded (Windows table, name lookups)", "global cooldown now uses cooldown reduction" in L)
    m = re.search(r"Hell difficulty ready: (\d+) code patches", L)
    npat = len([l for l in open(os.path.join(KIT, "files", "mods", "hell-difficulty", "rva", "25846389_win_patches.tsv")) if l.startswith("patch")])
    check(f"Hell code patches {m.group(1) if m else 0}/{npat}", m and int(m.group(1)) == npat)
    check("Hell tables built / checked and hooked", "Hell tables ready:" in L, next((l for l in L.splitlines() if "Hell tables" in l), ""))
    check("Hell table bundle loaded instead of the stock one", "Hell tables: the game loads" in L)
    check("Custom Modes on (its own module, mp part loaded)", "Foreign Expedition + PvP on" in L)
    # staff strip: Hell only
    for diff in (5, 1):
        open(os.path.join(w.PROF, "hell_probe_go.txt"), "w").write(str(diff))
        t1 = time.time()
        while time.time() - t1 < 30 and f"CombatDifficulty {diff}: AFFECTER_Mage" not in dsl(): time.sleep(1)
    L = dsl()
    k5 = (re.findall(r"CombatDifficulty 5: AFFECTER_Mage kit = \[([^\]]*)\]", L) or [None])[-1]; k1 = (re.findall(r"CombatDifficulty 1: AFFECTER_Mage kit = \[([^\]]*)\]", L) or [None])[-1]
    check("staff strip on Hell (5): no staff", k5 and "Staff" not in k5, k5 if k5 is not None else "no probe line")
    check("no strip on difficulty 1: staff kept", k1 and "Staff" in k1, k1 if k1 is not None else "no probe line")
    check("strip log line from hell-difficulty", "Hell recruit: 1 staff item(s) removed" in L)
    # Mods window
    def ui_cmd(op, **kw):
        kw.update(op=op, seq=str(time.time())); open(os.path.join(ui, "cmd.json.tmp"), "w").write(json.dumps(kw))
        os.replace(os.path.join(ui, "cmd.json.tmp"), os.path.join(ui, "cmd.json"))
        for _ in range(200):
            try:
                r = json.load(open(os.path.join(ui, "result.json")))
                if r.get("seq") == kw["seq"]: return r
            except Exception: pass
            time.sleep(0.1)
        return {}
    ui_cmd("open"); time.sleep(3); s = ui_cmd("state")
    json.dump(s, open(os.path.join(OUT, "mods_window_state.json"), "w"), indent=1)
    txt = json.dumps(s)
    names = {i: json.load(open(os.path.join(KIT, "files", "mods", i, "mod.json")))["description"][:30] for i in ("multiplayer", "custom-modes", "gcd-cdr", "hell-difficulty")}   # rows show each mod's description
    check("Mods window lists multiplayer, custom-modes, gcd-cdr, hell-difficulty", all(v in txt for v in names.values()),
          ", ".join(k for k, v in names.items() if v not in txt) or "all four")
    ui_cmd("close")
    # world: gcd-cdr effect lines
    work = os.path.join(OUT, "save_in"); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(SAVE)); shutil.copy2(SAVE, src)
    log("load", w.cmd({"cmd": "load_save", "file": w.winpath(src), "det": 0}))
    t1 = time.time()
    while time.time() - t1 < 400:
        try: st = w.cmd({"cmd": "status"}, timeout=10)
        except Exception: st = {}
        if st.get("scene") == 2 and not st.get("busy") and st.get("tick", -1) > 0: break
        time.sleep(4)
    log("world:", {k: st.get(k) for k in ("scene", "tick", "busy")})
    w.cmd({"cmd": "set_speed", "speed": 1})
    time.sleep(GAME_S)
    L = dsl()
    g = [l for l in L.splitlines() if "global cooldown #" in l]
    gm = [l for l in L.splitlines() if "GCD measured" in l]
    open(os.path.join(OUT, "gcd_lines.txt"), "w").write("\n".join(g + gm))
    withcdr = [l for l in g if not re.search(r"cooldown reduction 0\.0 ", l)]
    check("gcd-cdr effect lines in a world", g, f"{len(g)} lines, {len(withcdr)} with cooldown reduction; e.g. {(withcdr or g or [''])[0][-110:]}")
    check("a unit with cooldown reduction gets a shorter global cooldown", withcdr)
finally:
    if HAVE_LOCK: stop_mine()
    for f in ("dsload.log", "rig.log", "starter.log", "player.log"):
        p = os.path.join(w.PROF, f)
        if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f))
    if PAIR: subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "release", PAIR, "crossmods"], capture_output=True)
    if HAVE_LOCK: subprocess.run([sys.executable, WL, "release", "crossmods"])
    log("left:", w.dsmp_pids())
fails = [r for r in results if not r[1]]
print(f"\n{len(results) - len(fails)} PASS {len(fails)} FAIL; logs in {OUT}")
sys.exit(1 if fails else 0)
