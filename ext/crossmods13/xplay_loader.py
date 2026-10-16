#!/usr/bin/env python3
"""v13 crossmods: Mac host + Windows guest, BOTH through the mod loader with the same gameplay mods (gcd-cdr on both),
running rig/mp2_test.py (the 19 checks). Same idea as win/xplay_run.py, but:
  host  = loader test copy Z (~/ds_launcher/loader/test_env/instances/Z, test/lrig.py; dsload.dylib + DSL_MODS_DIR =
          <mods_all>/mac, profile: multiplayer, custom-modes, gcd-cdr)
  guest = the Windows loader kit in the CrossOver bottle DSMP (win/wloader.py), same profile.
  xplay_loader.py <loader dir> [--no-install] [mp2 options]   (<loader dir>/build/mods_all + build/win/kit + build/dsload.dylib)
Takes one rig lease (the Mac copy) and the wine lock. Never the owner's game, saves or prefs."""
import json, os, subprocess, sys, time
H = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))                       # this ds_multiplayer checkout
LD = os.path.abspath(sys.argv[1]); args = sys.argv[2:]
sys.path.insert(0, f"{ROOT}/rig"); sys.path.insert(0, f"{ROOT}/win"); sys.path.insert(0, f"{LD}/test")
os.environ["DSMP_WIN_LOADER"] = "1"
import rig, wrig, lrig
import wloader as w
X, WIN = "Z", "W"
MODS = ["multiplayer", "custom-modes", "gcd-cdr"]
OUT = os.path.join(HERE, "out", "xplay-" + time.strftime("%Y%m%d-%H%M%S")); os.makedirs(OUT, exist_ok=True)
LOCK = f"{H}/ds_multiplayer/rig/wine.lock"
def log(*a): print("[xm-xplay]", *a, flush=True)

PAIR = None; t_l = time.time()
while not PAIR and time.time() - t_l < 1800:                       # the Mac copy counts as a pair
    r = subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "lease", "crossmods", "--pid", str(os.getpid())], capture_output=True, text=True)
    PAIR = r.stdout.strip() if r.returncode == 0 else None
    if not PAIR: time.sleep(20)
log("lease", PAIR)
HAVE_LOCK = False; t_w = time.time()
while PAIR and not HAVE_LOCK and time.time() - t_w < 1800:
    if not subprocess.run(["pgrep", "-fl", "wine"], capture_output=True, text=True).stdout.strip():
        try: os.mkdir(LOCK); HAVE_LOCK = True; open(os.path.join(LOCK, "owner"), "w").write("crossmods\n")
        except FileExistsError: pass
    if not HAVE_LOCK: time.sleep(20)
try:
    if not PAIR or not HAVE_LOCK: raise SystemExit("no lease / wine lock")
    if not os.path.exists(os.path.join(lrig.idir(X), lrig.EXE_REL)):
        subprocess.run(["zsh", f"{LD}/test/make_instance.sh", X], check=True, capture_output=True)
    if "--no-install" in args: args.remove("--no-install")
    else: log("kit install rc", w.install(f"{LD}/build/win/kit"))
    prof = os.path.join(OUT, "profile_mac.json"); json.dump({"enabled": MODS, "order": MODS}, open(prof, "w"))
    md = os.path.join(w.PROF, "data", "Mods"); os.makedirs(md, exist_ok=True)
    json.dump({"enabled": MODS, "order": MODS}, open(os.path.join(md, "active.json"), "w"))
    for k in ("DSL_MODS_DIR", "DSL_PROFILE", "LRIG_DYLIB", "DSL_LOG_DIR"): os.environ.pop(k, None)
    host_env = {"LRIG_DYLIB": f"{LD}/build/dsload.dylib", "DSL_MODS_DIR": f"{LD}/build/mods_all/mac", "DSL_PROFILE": prof}

    def send(x, obj, timeout=90): return wrig.cmd(obj, timeout=timeout) if x == WIN else lrig.send(X, obj, timeout)
    def start(x, window=False, fps=30):
        if x == WIN: os.environ["DSRIG_FPS"] = str(fps); wrig.start(); return 1
        os.environ.update(host_env)
        try: return lrig.start(X, window=window, fps=fps)
        finally:
            for k in host_env: os.environ.pop(k, None)
    def stop(x): return wrig.stop() if x == WIN else lrig.stop(X)
    def idir(x): return wrig.INST if x == WIN else lrig.idir(X)
    def read_pid(x): return (wrig.dsmp_pids() or [(0,)])[0][0] if x == WIN else lrig.read_pid(X)
    def alive(pid): return True if pid and pid in [p for p, _ in wrig.dsmp_pids()] else lrig.alive(pid)
    def wait_for(x, pred, what, timeout=240, every=1.0):
        if x != WIN: return lrig.wait_for(X, pred, what, timeout, every)
        t0 = time.time(); last = None
        while time.time() - t0 < timeout:
            if not wrig.dsmp_pids(): raise RuntimeError(f"W: Windows copy died while waiting for {what}")
            try:
                last = wrig.cmd({"cmd": "status"}, timeout=30)
                if pred(last): return last
            except Exception as e: last = str(e)
            time.sleep(every)
        raise RuntimeError(f"W: timeout waiting for {what}: {last}")
    for n, f in (("send", send), ("start", start), ("stop", stop), ("idir", idir), ("read_pid", read_pid), ("alive", alive),
                 ("wait_for", wait_for), ("lease", lambda lane, pid=None: X + WIN), ("release", lambda p: True)):
        setattr(rig, n, f)
    os.environ["MP2_PAIR"] = X + WIN
    sys.argv = ["mp2_test.py"] + args
    import mp2_test
    log(f"cross-play mp2_test: host {X} (Mac, loader: {', '.join(MODS)}), client {WIN} (Windows loader kit, same mods)")
    try: mp2_test.main()
    finally:
        for f, src in (("host_dsload.log", os.path.join(lrig.idir(X), "dsload.log")), ("win_dsload.log", os.path.join(w.PROF, "dsload.log")),
                       ("win_rig.log", os.path.join(w.PROF, "rig.log")), ("host_rig.log", os.path.join(lrig.idir(X), "rig.log"))):
            if os.path.exists(src): open(os.path.join(OUT, f), "w").write(open(src, errors="replace").read())
        log("logs in", OUT)
finally:
    for f in (lambda: wrig.stop(), lambda: lrig.stop(X)):
        try: f()
        except Exception as e: log("stop:", e)
    if PAIR: subprocess.run([sys.executable, f"{H}/ds_multiplayer/rig/rig.py", "release", PAIR, "crossmods"], capture_output=True)
    if HAVE_LOCK:
        os.makedirs(f"{H}/ds_multiplayer/rig/old", exist_ok=True)
        if os.path.exists(os.path.join(LOCK, "owner")): os.replace(os.path.join(LOCK, "owner"), f"{H}/ds_multiplayer/rig/old/wine.lock.owner.crossmods.{int(time.time())}")
        os.rmdir(LOCK)
