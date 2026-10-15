#!/usr/bin/env python3
"""Cross-play determinism check: Windows copy W (CrossOver bottle DSMP) vs Mac copy C (rig.py), same save,
same seed, locked sim, N ticks, no orders. Compares the fingerprint at lock and after N ticks, and the
per-tick fp.log of both. Lease a pair first (rig.py lease windows).

  xplay_test.py [--ticks 600] [--seed 12345] [--mac C] [--keep]
"""
import json, os, sys, time
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig"))
import rig
import wrig

def arg(n, d): return type(d)(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
SAVE = os.path.expanduser(arg("--save", "~/ds_multiplayer/win/instances/W/elven_save.json"))
MATH = "--math" in sys.argv          # DSMP_MATH=1 on both: shared musl libm instead of Apple libm / UCRT
if MATH:
    os.environ["DSMP_MATH"] = "1"
    # the real Mac build now links math_mac.c; it turns on with <instance>/dsmp_math.on
    open(os.path.join(rig.idir(arg("--mac", "C")), "dsmp_math.on"), "w").close()
def summary(units):
    """per faction: units, dead, total hp (as det_test.py's combat summary)"""
    out = {}
    for u in units:
        f = out.setdefault(u["faction"], [0, 0, 0.0])
        f[0] += 1; f[1] += 1 if u.get("dead") else 0; f[2] += max(u.get("hp", 0), 0)
    return {k: f"{v[0]} units, {v[1]} dead, hp {v[2]:.1f}" for k, v in sorted(out.items())}
N = arg("--ticks", 600); SEED = arg("--seed", 12345); MAC = arg("--mac", "C")

def w(o): return wrig.cmd(o)
def m(o): return rig.send(MAC, o)
def wait(fn, what, t=300):
    end = time.time() + t
    while time.time() < end:
        try:
            r = fn()
            if r: return r
        except Exception: pass
        time.sleep(2)
    raise SystemExit(f"timeout: {what}")

if not wrig.dsmp_pids(): wrig.start()
if not rig.alive(rig.read_pid(MAC)): rig.start(MAC)
wait(lambda: w({"cmd": "status"}).get("scene") == 1, "W title")
wait(lambda: m({"cmd": "status"}).get("scene") == 1, "C title")
for f, nm in ((w, "W"), (m, MAC)):
    print(nm, f({"cmd": "det_seed", "seed": SEED}))
    print(nm, f({"cmd": "load_save", "file": ("Z:" + SAVE) if nm == "W" else SAVE, "det": 1}))
def locked(f): s = f({"cmd": "status"}); return s if s.get("det", {}).get("locked") else None
sw = wait(lambda: locked(w), "W locked"); sm = wait(lambda: locked(m), "C locked")
fw0, fm0 = w({"cmd": "fingerprint"}), m({"cmd": "fingerprint"})
print("at lock  W", fw0["tick"], fw0["hash"], fw0["units"], fw0["positions"], " C", fm0["tick"], fm0["hash"], fm0["units"], fm0["positions"])
L = fw0["tick"]
if fm0["tick"] != L: print("WARNING: lock ticks differ")
sum0 = (summary(w({"cmd": "list_units"})["units"]), summary(m({"cmd": "list_units"})["units"]))
print("start W", sum0[0]); print("start C", sum0[1])
if "--orders" in sys.argv:                       # det_test's plan: player units walk into the enemies (combat)
    import det_test
    orders = det_test.plan_orders(w({"cmd": "list_units"})["units"], L)
    for o in orders:
        for f in (w, m): f(dict(cmd="schedule", kind="move", **o))
    print("scheduled", len(orders), "orders on both")
for f in (w, m): f({"cmd": "run", "to": L + N, "per_frame": 6, "jitter": 0})
wait(lambda: w({"cmd": "status"})["tick"] >= L + N, "W run", 1800)
wait(lambda: m({"cmd": "status"})["tick"] >= L + N, "C run", 1800)
fw1, fm1 = w({"cmd": "fingerprint"}), m({"cmd": "fingerprint"})
print("after %d W" % N, fw1["tick"], fw1["hash"], fw1["units_hash"], fw1["pos_hash"], " C", fm1["tick"], fm1["hash"], fm1["units_hash"], fm1["pos_hash"])
sum1 = (summary(w({"cmd": "list_units"})["units"]), summary(m({"cmd": "list_units"})["units"]))
print("end   W", sum1[0]); print("end   C", sum1[1])
# per-tick comparison
def fp(path):
    d = {}
    for ln in open(path):
        p = ln.split()
        if len(p) >= 5: d[int(p[0])] = p[1:5]
    return d
a = fp(os.path.expanduser("~/ds_multiplayer/win/instances/W/fp.log")); b = fp(os.path.join(rig.idir(MAC), "fp.log"))
common = sorted(set(a) & set(b)); first = next((t for t in common if a[t] != b[t]), None)
print(f"fp.log ticks compared {len(common)}; first differing tick: {first}" + ("" if first is None else f" W {a[first]} C {b[first]}"))
res = {"lock_tick": L, "ticks": N, "lock_equal": fw0["hash"] == fm0["hash"], "end_equal": fw1["hash"] == fm1["hash"],
       "first_diff_tick": first, "compared": len(common), "W": fw1, "C": fm1, "W_lock": fw0, "C_lock": fm0, "save": SAVE, "math": MATH,
       "units_start": {"W": sum0[0], "C": sum0[1]}, "units_end": {"W": sum1[0], "C": sum1[1]}}
tag = arg("--tag", "run")
json.dump(res, open(os.path.expanduser(f"~/ds_multiplayer/win/instances/xplay_{tag}.json"), "w"), indent=1)
print("RESULT lock_equal=%s end_equal=%s" % (res["lock_equal"], res["end_equal"]))
if "--keep" not in sys.argv:
    wrig.stop(); rig.stop(MAC)
