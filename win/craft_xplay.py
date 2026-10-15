#!/usr/bin/env python3
"""Cross-play crafting in own-storage gear mode: the Windows copy W HOSTS (its simulation tags crafted items:
the TryCraft path that MSVC inlined), the Mac copy (first letter of the pair) joins and queues crafting at a
station. Checks: the order reaches both, the crafted item gets an owner on the host, the owner is equal on both
peers (ord_item), fingerprints equal every tick.

  craft_xplay.py <pair> [--recipe RECIPE_WoodPlank] [--station GUID] [--ticks 20000]
Lease the pair first (rig.py lease windows)."""
import json, os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig")); sys.path.insert(0, HERE)
import rig, wrig

PAIR = sys.argv[1]; MAC = PAIR[0]
def arg(n, d): return type(d)(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
SAVE = os.path.join(rig.SEED_DATA, "Saves", "MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json")
RECIPE = arg("--recipe", "RECIPE_WoodPlank")
STATION = arg("--station", "0086f7d9-4735-4821-85b9-49d2455d3b86")     # BUILDING_CarpenterWorkstation in this save
TICKS = arg("--ticks", 20000)
PORT = rig.pair_port(MAC)
results = {}
def log(*a): print(time.strftime("%H:%M:%S"), *a, flush=True)
def check(n, ok, info=""): results[n] = bool(ok); log(("PASS " if ok else "FAIL ") + n, info)
def w(**k): return wrig.cmd(k, timeout=120)
def m(**k): return rig.send(MAC, k, timeout=120)
def wait(what, pred, timeout=900):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
            if r: return r
        except Exception: pass
        time.sleep(3)
    raise RuntimeError("timeout: " + what)
def tagged(path, since):
    out = []
    for ln in open(path, errors="replace"):
        mm = re.search(r"ord: item ([0-9a-f-]{36}) -> player (\d+) \((\w+)\)", ln)
        if mm and ln[:12] >= since: out.append((mm.group(1), int(mm.group(2)), mm.group(3)))
    return out

def main():
    os.environ["DSMP_MATH"] = "1"; open(os.path.join(rig.idir(MAC), "dsmp_math.on"), "w").close()
    json.dump({"role": "host", "port": PORT, "name": "W", "allow_speedup": True, "allow_pause": True, "gear_mode": 2},
              open(os.path.join(wrig.INST, "mp.json"), "w"))
    json.dump({"role": "client", "host": "127.0.0.1", "port": PORT, "name": "M"}, open(os.path.join(rig.idir(MAC), "mp.json"), "w"))
    since = time.strftime("%H:%M:%S")
    try:
        wrig.start()
        wait("W title", lambda: w(cmd="status").get("scene") == 1)
        log("W load", w(cmd="load_save", file="Z:" + SAVE, det=1))
        wait("W locked", lambda: w(cmd="status").get("det", {}).get("locked"))
        rig.start(MAC, fps=30)
        sm = wait("Mac joined + locked", lambda: (lambda s: s if s.get("det", {}).get("locked") and s.get("mp", {}).get("loads", 0) >= 1 else None)(m(cmd="status")))
        time.sleep(5)
        check("join: Mac guest loaded the Windows host's save", sm.get("mp", {}).get("loads", 0) >= 1, sm.get("mp"))
        log("maps W", w(cmd="status").get("map"), "Mac", m(cmd="status").get("map"))
        for f in (w, m): f(cmd="mp_speed", speed=3)
        sel = m(cmd="ord_select", guids=STATION, group=1001)        # SelectGroup.Building
        r = m(cmd="ord_inject", ev="CraftingTaskGenerateRequested", **{"i32@16": 0, "str@24": RECIPE, "i32@32": 1})
        log("guest selects station", sel, "queues", RECIPE, r)
        time.sleep(4)
        mw, mm_ = w(cmd="status").get("mp", {}), m(cmd="status").get("mp", {})
        check("order: the guest's crafting order applied on both", mw.get("orders", 0) >= 1 and mm_.get("orders", 0) >= 1, f"W {mw.get('orders')} Mac {mm_.get('orders')}")
        t0 = w(cmd="status")["tick"]
        hit = wait("a crafted item tagged on the Windows host",
                   lambda: [x for x in tagged(os.path.join(wrig.INST, "rig.log"), since) if x[2] == "simulation"] or
                           (w(cmd="status")["tick"] - t0 > TICKS and "none"), timeout=1800)
        if hit == "none":
            check("crafted item tagged by the host simulation", False, f"no tag within {TICKS} ticks"); return
        g, owner, _ = hit[0]
        check("crafted item tagged by the host simulation (Windows TryCraft path)", owner == 1, f"item {g} -> player {owner}")
        time.sleep(3)
        ow, om = w(cmd="ord_item", guid=g), m(cmd="ord_item", guid=g)
        check("item owner equal on both peers", ow.get("owner") == om.get("owner") == owner, f"W {ow} Mac {om}")
        for f in (w, m): f(cmd="mp_pause", on=1)
        time.sleep(4)
        def fp(p):
            d = {}
            for ln in open(p):
                q = ln.split()
                if len(q) >= 5: d[int(q[0])] = q[1:5]
            return d
        a, b = fp(os.path.join(wrig.INST, "fp.log")), fp(os.path.join(rig.idir(MAC), "fp.log"))
        common = sorted(set(a) & set(b)); first = next((t for t in common if a[t] != b[t]), None)
        check("fingerprints equal every tick", len(common) > 1000 and first is None, f"{len(common)} common ticks, first diff {first}")
    finally:
        log("RESULTS", json.dumps(results))
        if "--keep" not in sys.argv:
            wrig.stop(); rig.stop(MAC)
        for p in (os.path.join(wrig.INST, "mp.json"), os.path.join(rig.idir(MAC), "mp.json")):
            if os.path.exists(p): os.replace(p, p + ".last")

if __name__ == "__main__":
    main()
