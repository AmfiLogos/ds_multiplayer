#!/usr/bin/env python3
"""Cross-play: the STOCK "Go to dungeon" path on the Windows guest (ExpeditionPlanPresenter.OnClickNext ->
TryStartExpedition, MSVC-inlined start redirected to the hooked InputOnAccept) + the stock critical-risks
confirmation. Mac host (first letter of the pair) loads the MoonlightBrigade base save; Windows W joins.
  expo_xplay.py <pair>        (lease the pair first)"""
import json, os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.expanduser("~/ds_multiplayer/rig")); sys.path.insert(0, HERE)
import rig, rigports, wrig
PAIR = sys.argv[1]; MAC = PAIR[0]; PORT = rig.pair_port(MAC)
rigports.expect_peers(1)   # the Windows copy W is in the Mac copy's session: rig.stop's player check allows it
SAVE = os.path.join(rig.SEED_DATA, "Saves", "MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json")
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
def wlog_since(t):
    return [l.rstrip() for l in open(os.path.join(wrig.INST, "rig.log"), errors="replace") if l[:8] >= t and ("win_exp" in l or "teams:" in l or "expedition" in l)]

def main():
    os.environ["DSMP_MATH"] = "1"; open(os.path.join(rig.idir(MAC), "dsmp_math.on"), "w").close()
    json.dump({"role": "host", "port": PORT, "name": "M", "allow_speedup": True, "allow_pause": True}, open(os.path.join(rig.idir(MAC), "mp.json"), "w"))
    json.dump({"role": "client", "host": "127.0.0.1", "port": PORT, "name": "W"}, open(os.path.join(wrig.INST, "mp.json"), "w"))
    t_start = time.strftime("%H:%M:%S")
    try:
        rig.start(MAC, fps=30)
        rig.wait_for(MAC, lambda s: s.get("scene") == 1 and not s.get("busy"), "title", timeout=900)
        log("M load", m(cmd="load_save", file=SAVE, det=1))
        wait("M locked", lambda: m(cmd="status").get("det", {}).get("locked"))
        wrig.start()
        sw = wait("W joined + locked", lambda: (lambda s: s if s.get("det", {}).get("locked") and s.get("mp", {}).get("loads", 0) >= 1 else None)(w(cmd="status")))
        time.sleep(5)
        players = sorted(u["guid"] for u in w(cmd="list_units")["units"] if u["faction"] == 1 and not u["dead"])
        log("player units", len(players))
        for g in players[:2]: log("W claims", g, w(cmd="mp_claim", guid=g))
        time.sleep(3)
        if "--norisk" in sys.argv: log("no-risk switch", w(cmd="win_exp", norisk=1))
        log("W opens the stock expedition panel", w(cmd="ord_inject", ev="ShowPopUpRequested", **{"i32@16": 2}))
        st = wait("presenter active on W", lambda: (lambda s: s if s.get("presenter") else None)(w(cmd="win_exp")), timeout=60)
        log("panel", st)
        for i in range(6):                                  # region step -> inventory check -> Go
            st = w(cmd="win_exp", **{"do": "next"}); log("next", i, st); time.sleep(3)
            if st.get("risk_shown") or st.get("started") == 1 or not st.get("presenter"): break
        if st.get("risk_shown"):
            check("stock critical-risks confirmation shown on Windows (as on the Mac)", True, st)
            st = w(cmd="win_exp", **{"do": "confirm"}); log("confirm", st); time.sleep(4)
        lines = wlog_since(t_start)
        for l in lines[-12:]: log("  W log:", l[:200])
        go = any("Go to dungeon pressed" in l for l in lines)
        swallowed = any("swallowed" in l for l in lines)
        check("stock Go on Windows reached the hooked InputOnAccept (ui_teams: Ready, no direct start)", go,
              "inlined start redirected" if swallowed else "via StartExpedition (confirmation path)")
        ma, mw = m(cmd="status").get("mp", {}), w(cmd="status").get("mp", {})
        check("no map change started, no desync", m(cmd="status").get("map") == 0 and w(cmd="status").get("map") == 0
              and not ma.get("desynced") and not mw.get("desynced"), f"maps {m(cmd='status').get('map')}/{w(cmd='status').get('map')}")
    finally:
        log("RESULTS", json.dumps(results))
        if "--keep" not in sys.argv:
            wrig.stop(); rig.stop(MAC)
        for p in (os.path.join(wrig.INST, "mp.json"), os.path.join(rig.idir(MAC), "mp.json")):
            if os.path.exists(p): os.replace(p, p + ".last")

if __name__ == "__main__":
    main()
