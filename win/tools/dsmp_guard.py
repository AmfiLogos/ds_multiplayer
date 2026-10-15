#!/usr/bin/env python3
"""Window guard for the DSMP bottle. Every 0.1 s: any visible window owned by a process whose environment
carries DSMP_GUARD_TAG=1 -> SIGKILL that process tree (by PID) and stop the DSMP wineserver.
Other bottles (the owner's Steam / Wollay) are never touched. Log: win/instances/guard.log
Usage: dsmp_guard.py <seconds>   (runs in the background next to every DSMP wine command)"""
import os, re, subprocess, sys, time, signal
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.expanduser("~/ds_multiplayer/win/instances/guard.log")
WS = os.path.expanduser("~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wineserver")
PREFIX = os.path.expanduser("~/Library/Application Support/CrossOver/Bottles/DSMP")
os.makedirs(os.path.dirname(LOG), exist_ok=True)
def log(m):
    with open(LOG, "a") as f: f.write(time.strftime("%H:%M:%S ") + m + "\n")
tagged = {}
# Wine processes rewrite their argv/env area, so `ps -E` cannot see a tag. A process belongs to the DSMP
# bottle when it has files open under Bottles/DSMP (lsof); cached per pid.
def in_dsmp(pid):
    try: out = subprocess.run(["lsof", "-p", str(pid)], capture_output=True, text=True).stdout
    except Exception: out = ""
    return "/Bottles/DSMP/" in out
def is_tagged(pid):
    if pid not in tagged: tagged[pid] = in_dsmp(pid)
    return tagged[pid]
def dsmp_pids():
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    c = [int(l.split()[0]) for l in out.splitlines() if re.search(r"wine|\.exe", l, re.I)]
    return [p for p in c if in_dsmp(p)]
end = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 600)
# 2026-10-14: one guard at a time (two 2-hour guards polling every 0.1 s were alive during the 20:12 screen crash)
import fcntl
_lk = open(os.path.join(os.path.dirname(LOG), "guard.lock"), "a")
try:
    fcntl.flock(_lk, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.exit(0)                                   # another guard already watches the DSMP bottle
log("guard start")
idle_since = None
while time.time() < end:
    if not dsmp_pids():                           # nothing of DSMP running: stop after 60 s idle
        idle_since = idle_since or time.time()
        if time.time() - idle_since > 60:
            break
    else:
        idle_since = None
    for line in subprocess.run([os.path.join(HERE, "winall")], capture_output=True, text=True).stdout.splitlines():
        pid = int(line.split()[0])
        if is_tagged(pid):
            log("VISIBLE WINDOW from DSMP pid %d: %s -> killing DSMP processes" % (pid, line))
            for p in dsmp_pids():
                try: os.kill(p, signal.SIGKILL)
                except Exception: pass
            subprocess.run([WS, "-k"], env=dict(os.environ, WINEPREFIX=PREFIX))
    time.sleep(2.0)                               # was 0.1 s: 600 full window lists per minute stressed WindowServer
log("guard end")
