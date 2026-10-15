#!/usr/bin/env python3
"""The Windows copy through the MOD LOADER (wrig.py with DSMP_WIN_LOADER=1 uses this): the loader kit in the CrossOver bottle DSMP (never "DS Windows Test"), copy installed by the kit's
own install.bat into C:\\users\\crossover\\AppData\\Local\\Dungeon Settlers Modded (Steam's folder for the test =
C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers, a stock clone).
  wloader.py install <kit dir>      run install.bat (no pause)
  wloader.py start [args]           start through the starter exe: -batchmode -nographics, control port 47851, window guard
  wloader.py cmd '<json>'           one control command (multiplayer module)
  wloader.py stop                   quit, then SIGKILL every DSMP process by PID
  wloader.py logs                   tail of dsload.log / rig.log / starter.log
One wine copy machine-wide: refuses to start while another wine process runs (pgrep -fl wine)."""
import json, os, socket, subprocess, sys, time, signal
H = os.path.expanduser("~")
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin"
BOTTLE = "DSMP"
DRIVE = f"{H}/Library/Application Support/CrossOver/Bottles/{BOTTLE}/drive_c"
ROOT = f"{DRIVE}/users/crossover/AppData/Local/Dungeon Settlers Modded"
PROF = f"{ROOT}/profiles/modded"
WROOT = "C:\\users\\crossover\\AppData\\Local\\Dungeon Settlers Modded"
STEAMGAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"
PORT = int(os.environ.get("L3W_PORT", "47851"))
import signal as _s

def dsmp_pids():
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    res = []
    for l in out.splitlines():
        if "wine" in l.lower() or ".exe" in l.lower():
            pid = int(l.split()[0])
            lo = subprocess.run(["lsof", "-p", str(pid)], capture_output=True, text=True).stdout
            if f"/Bottles/{BOTTLE}/" in lo: res.append((pid, l.split(None, 1)[1][:90]))
    return res

def winpath(p): return "Z:" + p.replace("/", "\\")

def other_wine():
    """another test's wine copy: any process of the DSMP bottle, or a Dungeon Settlers exe in any bottle. The owner's own
    bottles (Steam, Wollay...) keep long-lived wine services; those are not a test copy and are ignored."""
    busy = [f"{p} {c}" for p, c in dsmp_pids()]
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    busy += [l.strip()[:120] for l in out.splitlines() if ("DungeonSettlers.exe" in l or "winewrapper" in l) and "/bin/zsh" not in l and "/bin/bash" not in l]
    return "\n".join(busy)

def wait_free(limit=900):
    t0 = time.time()
    while True:
        o = other_wine()
        if not o: return True
        if time.time() - t0 > limit: print("wine still busy after", limit, "s:\n" + o); return False
        time.sleep(10)

def install(kit):
    if not wait_free(): return 1                     # install.bat refuses while any DungeonSettlers.exe runs in the bottle
    env = dict(os.environ, DSMODDED_NOPAUSE="1")
    r = subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE, "--cx-app", "cmd.exe", "/c",
                        winpath(os.path.abspath(kit)) + "\\install.bat", STEAMGAME], env=env, capture_output=True, text=True)
    print(r.stdout[-1500:]); return r.returncode

def bottle_silent():
    """the DSMP bottle must have Graphics=null and Audio="" (no window, no sound can reach the owner)"""
    out = subprocess.run([f"{C}/wine", "--bottle", BOTTLE, "reg", "query", "HKCU\\Software\\Wine\\Drivers"],
                         capture_output=True, text=True).stdout
    g = [l.split()[-1] for l in out.splitlines() if l.strip().startswith("Graphics")]
    a = [l.split() for l in out.splitlines() if l.strip().startswith("Audio")]
    ok = g == ["null"] and a and len(a[0]) == 2          # "Audio REG_SZ" with an empty value
    if not ok: print("DSMP bottle is not silent (Graphics", g, "Audio", a, "): not starting")
    return ok

WATCH = r"""
import os, sys, time, signal
sys.path.insert(0, %r); import wloader as w
t0 = time.time(); title = False
while time.time() - t0 < %d:
    time.sleep(10)
    if not [p for p, c in w.dsmp_pids() if "Dungeon Settlers Modded" in c]: sys.exit(0)
    try: title = title or w.cmd({"cmd": "status"}, timeout=5).get("boot_ready", False)
    except Exception: pass
    if not title and time.time() - t0 > 180: break
w.stop(); print("wloader watchdog: stopped the copy (no title in 3 min or past the time limit)")
"""

def start(extra):
    if not wait_free(): return 1
    if not bottle_silent(): return 1
    subprocess.Popen(["python3", "-c", WATCH % (os.path.dirname(os.path.abspath(__file__)), int(os.environ.get("WLOADER_MAX_S", "1500")))],
                     start_new_session=True, stdout=open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "instances", "wloader_watch.out"), "a"),
                     stderr=subprocess.STDOUT)
    if not subprocess.run(["pgrep", "-f", "dsmp_guard.py"], capture_output=True).stdout:
        subprocess.Popen(["python3", f"{H}/ds_multiplayer/win/tools/dsmp_guard.py", "7200"], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DSRIG_PORT=str(PORT), DSMP_GUARD_TAG="1", DSL_NO_ALERT="1")
    env["WINEDLLOVERRIDES"] = "winhttp=n,b"          # wine only: Doorstop's winhttp.dll from the game folder (Windows loads it anyway)
    for k in ("DSRIG_DIR", "DSRIG_RANGES", "DSL_MODS_DIR", "DSL_PROFILE", "DSL_LOG_DIR", "DSML_SESSION"): env.pop(k, None)
    args = ["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE, "--no-wait", WROOT + "\\Dungeon Settlers Modded.exe",
            "-batchmode", "-nographics"] + extra
    subprocess.run(args, env=env, stdout=open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "instances", "wloader_wine.out"), "a"), stderr=subprocess.STDOUT)
    print("started; DSMP processes:", dsmp_pids())

def cmd(obj, timeout=90):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
    s.sendall((obj if isinstance(obj, str) else json.dumps(obj)).encode() + b"\n")
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1 << 20)
        if not d: break
        buf += d
    s.close()
    return json.loads(buf)

SYS = ("services.exe", "winedevice.exe", "plugplay.exe", "svchost.exe", "explorer.exe", "rpcss.exe", "tabtip.exe", "conhost.exe", "start.exe")
def stop():
    """quit ours; kill only our game processes, and the bottle's wine services only when no other lane's program runs"""
    try: cmd({"cmd": "quit"}, timeout=5)
    except Exception: pass
    time.sleep(2)
    ps = dsmp_pids()
    mine = [p for p, c in ps if "Dungeon Settlers Modded" in c]
    for pid in mine:
        try: os.kill(pid, signal.SIGKILL)
        except Exception: pass
    others = [c for p, c in ps if p not in mine and ".exe" in c.lower() and not any(x in c.lower() for x in SYS)]
    if others: print("other DSMP programs still run (not touched):", others); return
    for pid, c in dsmp_pids():
        try: os.kill(pid, signal.SIGKILL)
        except Exception: pass

def logs(n=25):
    for f in ("starter.log", "dsload.log", "rig.log"):
        p = f"{PROF}/{f}"
        print(f"==== {f}")
        if os.path.exists(p): print("".join(open(p, errors="replace").readlines()[-n:]))

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "install": sys.exit(install(a[1]))
    if a[0] == "start": sys.exit(start(a[1:]) or 0)
    if a[0] == "cmd": print(json.dumps(cmd(a[1])))
    if a[0] == "stop": stop(); print("stopped; left:", dsmp_pids())
    if a[0] == "logs": logs(int(a[1]) if len(a) > 1 else 25)
