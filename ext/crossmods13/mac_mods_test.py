#!/usr/bin/env python3
"""v13 crossmods, Mac side of the same checks: loader test copy Z (hidden, muted) with <mods_all>/mac + the test probes.
  mac_mods_test.py <loader dir> [--game-s N]     (<loader dir>/build/dsload.dylib + build/mods_all/mac)
Checks: gcd-cdr loads; Hell code patches N/N; Hell tables built from the Mac stock bundle + loaded instead of it; staff
strip on Hell only (hell-probe, built here for arm64); in a world: gcd-cdr "global cooldown #" lines (gcd-probe forces
cooldowns on units with cooldown reduction). One rig lease. Never the owner's game, saves or prefs."""
import json, os, re, shutil, subprocess, sys, time
H = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))
LD = os.path.abspath(sys.argv[1]); args = sys.argv[2:]
GAME_S = int(args[args.index("--game-s") + 1]) if "--game-s" in args else 120
sys.path.insert(0, f"{LD}/test")
import lrig
X = "Z"
OUT = os.path.join(HERE, "out", "mac-" + time.strftime("%Y%m%d-%H%M%S")); os.makedirs(OUT, exist_ok=True)
results = []
def log(*a): print("[xm-mac]", *a, flush=True)
def check(name, ok, info=""): results.append((name, bool(ok))); log("PASS" if ok else "FAIL", name, info)
def dsl():
    p = os.path.join(lrig.idir(X), "dsload.log")
    return open(p, errors="replace").read() if os.path.exists(p) else ""

# the mods folder of this run: mods_all/mac + the two test probes (built here, arm64)
MD = os.path.join(OUT, "mods"); shutil.copytree(f"{LD}/build/mods_all/mac", MD)
for pid_, src, tbl in (("gcd-probe", f"{LD}/test/probe/gcd-probe/gcd_probe.c", f"{LD}/test/probe/gcd-probe/rva"),
                       ("hell-probe", f"{HERE}/hell_probe/hell_probe.c", None)):
    d = os.path.join(MD, pid_); os.makedirs(d, exist_ok=True)
    subprocess.run(["clang", "-arch", "arm64", "-O2", "-I", f"{LD}/sdk", "-dynamiclib", "-install_name", f"@rpath/{pid_}.dylib",
                    "-o", os.path.join(d, pid_ + ".dylib"), src], check=True)
    subprocess.run(["codesign", "-f", "-s", "-", os.path.join(d, pid_ + ".dylib")], capture_output=True)
    if tbl: shutil.copytree(tbl, os.path.join(d, "rva")); shutil.copy2(f"{LD}/test/probe/gcd-probe/mod.json", d)
    else: shutil.copy2(f"{HERE}/hell_probe/mod.json", d)
EN = ["multiplayer", "gcd-cdr", "hell-difficulty", "gcd-probe", "hell-probe"]
prof = os.path.join(OUT, "profile.json"); json.dump({"enabled": EN, "order": EN}, open(prof, "w"))

LANE = f"crossmods-{os.getpid()}"                     # unique per run: a pair is never shared with another run
PAIR = None; t_l = time.time()
while not PAIR and time.time() - t_l < 1800:
    r = subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "lease", LANE, "--pid", str(os.getpid())], capture_output=True, text=True)
    PAIR = r.stdout.strip() if r.returncode == 0 else None
    if not PAIR: time.sleep(20)
log("lease", PAIR)
try:
    if not PAIR: raise SystemExit("no lease")
    if not os.path.exists(os.path.join(lrig.idir(X), lrig.EXE_REL)):
        subprocess.run(["zsh", f"{LD}/test/make_instance.sh", X], check=True, capture_output=True)
    mj = os.path.join(lrig.idir(X), "mp.json")
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, "mp.json.aside"))          # single player
    p = os.path.join(lrig.idir(X), "dsload.log")
    if os.path.exists(p): os.replace(p, os.path.join(OUT, "prev_dsload.log"))
    os.environ.update({"LRIG_DYLIB": f"{LD}/build/dsload.dylib", "DSL_MODS_DIR": MD, "DSL_PROFILE": prof, "DSL_GCDPROBE_FORCE": "8"})
    os.environ.pop("DSML_SESSION", None)
    lrig.start(X)
    lrig.wait_for(X, lambda s: s.get("scene") == 1 and not s.get("busy"), "title", timeout=900)
    L = dsl()
    check("gcd-cdr loaded", "global cooldown now uses cooldown reduction" in L)
    m = re.search(r"Hell difficulty ready: (\d+) code patches", L)
    npat = len([l for l in open(f"{MD}/hell-difficulty/rva/25846389_mac_patches.tsv") if l.startswith("patch")])
    check(f"Hell code patches {m.group(1) if m else 0}/{npat}", m and int(m.group(1)) == npat)
    check("Hell tables built / checked and hooked", "Hell tables ready:" in L, next((l for l in L.splitlines() if "Hell tables" in l), ""))
    check("Hell table bundle loaded instead of the stock one", "Hell tables: the game loads" in L)
    for diff in (5, 1):
        open(os.path.join(lrig.idir(X), "hell_probe_go.txt"), "w").write(str(diff))
        t1 = time.time()
        while time.time() - t1 < 30 and f"CombatDifficulty {diff}: AFFECTER_Mage" not in dsl(): time.sleep(1)
    L = dsl()
    k5 = re.search(r"CombatDifficulty 5: AFFECTER_Mage kit = \[([^\]]*)\]", L); k1 = re.search(r"CombatDifficulty 1: AFFECTER_Mage kit = \[([^\]]*)\]", L)
    check("staff strip on Hell (5): no staff", k5 and k5.group(1) and "Staff" not in k5.group(1), k5.group(1) if k5 else "no probe line")
    check("no strip on difficulty 1: staff kept", k1 and "Staff" in k1.group(1), k1.group(1) if k1 else "no probe line")
    work = os.path.join(OUT, "save_in"); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(lrig.SAVE)); shutil.copy2(lrig.SAVE, src)
    log("load", lrig.send(X, {"cmd": "load_save", "file": src}))
    st = lrig.wait_for(X, lambda s: s.get("scene") == 2 and not s.get("busy") and s.get("tick", -1) > 0, "world", timeout=400, every=2)
    lrig.send(X, {"cmd": "set_speed", "speed": 1})
    time.sleep(GAME_S)
    L = dsl(); g = [l for l in L.splitlines() if "global cooldown #" in l]
    withcdr = [l for l in g if not re.search(r"cooldown reduction 0\.0 ", l)]
    open(os.path.join(OUT, "gcd_lines.txt"), "w").write("\n".join(g))
    check("gcd-cdr effect lines in a world", g, f"{len(g)} lines, {len(withcdr)} with cooldown reduction")
    check("a unit with cooldown reduction gets a shorter global cooldown", withcdr, (withcdr or [""])[0][-110:])
finally:
    try: lrig.stop(X)
    except Exception as e: log("stop:", e)
    p = os.path.join(lrig.idir(X), "dsload.log")
    if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, "dsload.log"))
    if PAIR: subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "release", PAIR, LANE], capture_output=True)
fails = [r for r in results if not r[1]]
print(f"\n{len(results) - len(fails)} PASS {len(fails)} FAIL; logs in {OUT}")
sys.exit(1 if fails else 0)
