#!/usr/bin/env python3
"""Mac <-> Windows cross-play: rig/mp2_test.py with its client copy 'B' replaced by the Windows copy W
(CrossOver bottle DSMP, baselib.dll proxy). The Mac host is the first copy of the leased pair.

  mp2_xplay.py <pair> [--math] [mp2_test.py options ...]      e.g. mp2_xplay.py CD --math
--math: shared musl libm on both (Mac: <host>/dsmp_math.on, Windows: DSMP_MATH=1).
Lease the pair first (rig.py lease windows) and release it after.
"""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig")); sys.path.insert(0, HERE)
import rig, wrig

PAIR = sys.argv[1]; sys.argv = [sys.argv[0]] + sys.argv[2:]
HOST, WIN = PAIR[0], PAIR[1]           # WIN: the pair's second letter, played by the Windows copy
MATH = "--math" in sys.argv
if MATH: sys.argv.remove("--math")

o_send, o_start, o_stop, o_wait_for, o_idir, o_read_pid, o_alive = rig.send, rig.start, rig.stop, rig.wait_for, rig.idir, rig.read_pid, rig.alive
def send(x, obj, timeout=90): return wrig.cmd(obj, timeout=timeout) if x == WIN else o_send(x, obj, timeout)
def start(x, window=False, fps=30):
    if x != WIN:
        flag = os.path.join(o_idir(x), "dsmp_math.on")
        if MATH: open(flag, "w").close()
        elif os.path.exists(flag): os.replace(flag, flag + ".off")
        return o_start(x, window=window, fps=fps)
    os.environ["DSRIG_FPS"] = str(fps)
    if MATH: os.environ["DSMP_MATH"] = "1"
    else: os.environ.pop("DSMP_MATH", None)
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
for n in ("send", "start", "stop", "idir", "read_pid", "alive", "wait_for"):
    setattr(rig, n, globals()[n])

os.environ["MP2_PAIR"] = PAIR
import mp2_test                         # maps A/B -> PAIR letters, then our wrappers send WIN to Windows
print(f"cross-play: host {HOST} (Mac), client {WIN} = Windows copy W, port {mp2_test.PORT}, math {'shared musl' if MATH else 'native'}", flush=True)
mp2_test.main()
