#!/usr/bin/env python3
"""Windows test copy W in the CrossOver bottle DSMP (never the owner's bottles).

  wrig.py start            start the copy: -batchmode -nographics, null graphics driver, no audio, nice 19,
                           window guard (tools/dsmp_guard.py) running beside it
  wrig.py cmd '<json>'     one control command over TCP 127.0.0.1:<port> (same commands as the Mac rig)
  wrig.py stop             quit over the socket, then SIGKILL every DSMP process by PID (lsof-identified)
  wrig.py procs            list DSMP processes
Lease a pair first: python3 ~/ds_multiplayer/rig/rig.py lease windows
"""
import json, os, socket, subprocess, sys, time, signal
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rig'))
import ctl_token                                   # noqa: E402  ../rig/ctl_token.py: token first line
H = os.path.expanduser("~")
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin"
BOTTLE = "DSMP"
PREFIX = f"{H}/Library/Application Support/CrossOver/Bottles/{BOTTLE}"
INST = f"{H}/ds_multiplayer/win/instances/W"
PORT = 47811

def winpath(p): return "Z:" + p.replace("/", "\\")

def ctl_dirs_env():
    """test commands may write / read files there besides the copy's own folder (multiplayer rig_path_refused; ';' list)"""
    w = [winpath(os.path.join(H, "ds_*"))] + [winpath(t.rstrip("/")) for t in (os.environ.get("TMPDIR", ""), "/tmp", "/private/tmp") if t]
    return {"DSRIG_WRITE_DIRS": ";".join(w), "DSRIG_READ_DIRS": ";".join(w + [winpath(f"{H}/DungeonSettlers_SaveBackups")])}

def dsmp_pids():
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    res = []
    for l in out.splitlines():
        if "wine" in l.lower() or ".exe" in l.lower():
            pid = int(l.split()[0])
            lo = subprocess.run(["lsof", "-p", str(pid)], capture_output=True, text=True).stdout
            if f"/Bottles/{BOTTLE}/" in lo: res.append((pid, l.split(None, 1)[1][:90]))
    return res

def start():
    if not subprocess.run(["pgrep", "-f", "dsmp_guard.py"], capture_output=True).stdout:
        subprocess.Popen(["python3", f"{H}/ds_multiplayer/win/tools/dsmp_guard.py", "7200"], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DSRIG_DIR=winpath(INST), DSRIG_PORT=str(PORT), DSMP_GUARD_TAG="1")
    env.update(ctl_dirs_env())
    env.pop("DSRIG_RANGES", None)          # a Mac path (release gate pins the Mac ranges); Windows uses det_ranges_win.tsv
    args = ["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE, "--no-wait", "C:\\DSW\\DungeonSettlers.exe",
            "-batchmode", "-logFile", winpath(f"{INST}/player.log")]
    if not os.environ.get("DSMP_GRAPHICS"): args.insert(-2, "-nographics")   # DSMP_GRAPHICS=1: keep a D3D device (needs the bottle's mac graphics driver)
    subprocess.run(args, env=env, stdout=open(f"{INST}/wine.out", "a"), stderr=subprocess.STDOUT)
    print("started; DSMP processes:", dsmp_pids())

def cmd(obj, timeout=90):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
    s.sendall(ctl_token.token_line(INST) + (obj if isinstance(obj, str) else json.dumps(obj)).encode() + b"\n")
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1 << 20)
        if not d: break
        buf += d
    s.close()
    return json.loads(buf)

def stop():
    try: cmd({"cmd": "quit"}, timeout=5)
    except Exception: pass
    time.sleep(2)
    for pid, c in dsmp_pids():
        try: os.kill(pid, signal.SIGKILL)
        except Exception: pass
    print("left:", dsmp_pids())

if os.environ.get("DSMP_WIN_LOADER") == "1":
    # the Windows copy through the MOD LOADER: the loader kit installed in DSMP by its own install.bat, started through
    # "Dungeon Settlers Modded.exe" (wloader.py). Same commands; INST = that copy's profile folder (DSRIG_DIR).
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import wloader as _wl
    INST, PORT = _wl.PROF, _wl.PORT
    def start():
        if _wl.start([]): raise RuntimeError("wloader: the DSMP bottle stayed busy (another wine copy)")
    cmd, stop = _wl.cmd, _wl.stop

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "start": start()
    elif a[0] == "cmd": print(json.dumps(cmd(a[1])))
    elif a[0] == "stop": stop()
    elif a[0] == "procs": print(dsmp_pids())
