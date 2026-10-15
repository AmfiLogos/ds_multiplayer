#!/usr/bin/env python3
"""Cost of the shared math on the Mac: wall time of N locked sim ticks, math on vs off, same save / seed / orders.
  math_cost.py <copy> [--ticks 2000] [--runs 3] [--per_frame 50]
Runs alternate off / on. Off = <copy>/dsmp_math.off. Each run: start, load the ElvenExpedition fight save (det),
det_test's 16 move orders, `run` to lock + N at per_frame ticks per frame; time from the run command to the tick."""
import os, sys, time, json, statistics
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig"))
import rig, det_test
X = sys.argv[1]
def arg(n, d): return type(d)(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
N, RUNS, PF = arg("--ticks", 2000), arg("--runs", 3), arg("--per_frame", 50)
SAVE = os.path.expanduser("~/ds_multiplayer/win/instances/W/elven_save.json")
c = lambda **k: rig.send(X, k, timeout=120)
def one(math_on):
    d = rig.idir(X); off = os.path.join(d, "dsmp_math.off"); on = os.path.join(d, "dsmp_math.on")
    for q in ("mp.json",):
        p = os.path.join(d, q)
        if os.path.exists(p): os.replace(p, p + ".last")
    if math_on:
        if os.path.exists(off): os.replace(off, off + ".unused")
    else: open(off, "w").close()
    mk = os.path.getsize(os.path.join(d, "rig.log")) if os.path.exists(os.path.join(d, "rig.log")) else 0
    rig.start(X, fps=30)
    try:
        rig.wait_for(X, lambda s: s.get("scene") == 1 and not s.get("busy"), "title", timeout=900)
        c(cmd="det_seed", seed=12345); c(cmd="load_save", file=SAVE, det=1)
        rig.wait_for(X, lambda s: s.get("det", {}).get("locked"), "locked", timeout=900)
        lock = c(cmd="status")["tick"]
        for o in det_test.plan_orders(c(cmd="list_units")["units"], lock): c(cmd="schedule", kind="move", **o)
        t0 = time.perf_counter(); c(cmd="run", to=lock + N, per_frame=PF, jitter=0)
        while c(cmd="status")["tick"] < lock + N: time.sleep(0.05)
        dt = time.perf_counter() - t0
        lg = open(os.path.join(d, "rig.log"), errors="replace"); lg.seek(mk); txt = lg.read()
        state = "on" if "libm pointers of GameAssembly redirected" in txt else "off"
        fp = [l.split()[1] for l in open(os.path.join(d, "fp.log")) if l.split() and l.split()[0] == str(lock + N)]
        return dt, state, fp[0] if fp else None
    finally:
        rig.stop(X)
        if os.path.exists(off): os.replace(off, off + ".unused")
res = {"on": [], "off": []}
for r in range(RUNS):
    for m in (False, True):
        dt, state, fp = one(m)
        print(f"run {r} math {'on' if m else 'off'} (log: {state}): {N} ticks in {dt:.2f} s = {dt / N * 1000:.3f} ms/tick, end fp {fp}", flush=True)
        res["on" if m else "off"].append(dt / N * 1000)
for k in ("off", "on"):
    print(f"math {k}: mean {statistics.mean(res[k]):.3f} ms/tick, median {statistics.median(res[k]):.3f}, runs {['%.3f' % v for v in res[k]]}")
print(f"cost: {(statistics.median(res['on']) / statistics.median(res['off']) - 1) * 100:+.1f}% (medians)")
