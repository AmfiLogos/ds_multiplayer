#!/usr/bin/env python3
"""Mac <-> Windows cross-play: runs a rig test script with the SECOND copy of the pair played by the Windows copy W
(CrossOver bottle DSMP, baselib.dll proxy). The Mac host is the first copy of the leased pair.

  xplay_run.py <pair> <script.py> [--math] [script options ...]   e.g. xplay_run.py GH mp2_test.py --math
  (mp2_xplay.py <pair> ... = xplay_run.py <pair> mp2_test.py ...)
rig.lease() inside the script returns <pair>; rig.release() is a no-op (lease/release outside).
--math: shared musl libm on both (Mac: <host>/dsmp_math.on, Windows: DSMP_MATH=1).
Lease the pair first (rig.py lease windows) and release it after.
"""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig")); sys.path.insert(0, HERE)
import rig, wrig

PAIR = sys.argv[1]
SCRIPT = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2].endswith(".py") else "mp2_test.py"
sys.argv = [SCRIPT] + sys.argv[3 if len(sys.argv) > 2 and sys.argv[2].endswith(".py") else 2:]
HOST, WIN = PAIR[0], PAIR[1]           # WIN: the pair's second letter, played by the Windows copy
MATH = "--math" in sys.argv
if MATH: sys.argv.remove("--math")

o_send, o_start, o_stop, o_wait_for, o_idir, o_read_pid, o_alive = rig.send, rig.start, rig.stop, rig.wait_for, rig.idir, rig.read_pid, rig.alive
def send(x, obj, timeout=90): return wrig.cmd(obj, timeout=timeout) if x == WIN else o_send(x, obj, timeout)
def start(x, window=False, fps=30):
    if x != WIN:
        flag, off = os.path.join(o_idir(x), "dsmp_math.on"), os.path.join(o_idir(x), "dsmp_math.off")
        if MATH:                                   # (math is on by default since v9; .off is the switch)
            open(flag, "w").close()
            if os.path.exists(off): os.replace(off, off + ".unused")
        else: open(off, "w").close()
        return o_start(x, window=window, fps=fps)
    os.environ["DSRIG_FPS"] = str(fps)
    os.environ["DSMP_MATH"] = "1" if MATH else "0"
    wrig.start()
    return 1
def stop(x): return wrig.stop() if x == WIN else o_stop(x)
def idir(x): return wrig.INST if x == WIN else o_idir(x)
def read_pid(x): return (wrig.dsmp_pids() or [(0,)])[0][0] if x == WIN else o_read_pid(x)
def alive(pid): return True if pid and pid in [p for p, _ in wrig.dsmp_pids()] else o_alive(pid)
def wait_for(x, pred, what, timeout=240, every=1.0):
    if x != WIN: return o_wait_for(x, pred, what, timeout, every)
    t0 = time.time(); last = None
    while time.time() - t0 < timeout:
        if not wrig.dsmp_pids(): raise RuntimeError(f"W: Windows copy died while waiting for {what}")
        try:
            last = wrig.cmd({"cmd": "status"}, timeout=30)
            if pred(last): return last
        except Exception as e: last = str(e)
        time.sleep(every)
    raise RuntimeError(f"W: timeout waiting for {what}: {last}")
def lease(lane, pid=None): return PAIR
def release(pair): return True
for n in ("send", "start", "stop", "idir", "read_pid", "alive", "wait_for", "lease", "release"):
    setattr(rig, n, globals()[n])

os.environ["MP2_PAIR"] = PAIR
import importlib
if os.path.dirname(SCRIPT): sys.path.insert(0, os.path.abspath(os.path.dirname(SCRIPT)))
mod = importlib.import_module(os.path.splitext(os.path.basename(SCRIPT))[0])   # mp2_test maps A/B -> PAIR letters
print(f"cross-play {SCRIPT}: host {HOST} (Mac), client {WIN} = Windows copy W, math {'shared musl' if MATH else 'native'}", flush=True)
mod.main()
