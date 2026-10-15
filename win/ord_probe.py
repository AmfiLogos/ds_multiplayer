#!/usr/bin/env python3
"""Order-capture probe on the Windows copy alone (no mp): ord_loop on, select a player unit, right-click a cell,
read ord_stats. Usage: ord_probe.py   (DSMP_GRAPHICS=1 in the env keeps a D3D device)"""
import json, time, sys, os
import wrig
SAVE = os.path.expanduser("~/ds_multiplayer/win/instances/W/elven_save.json")
if not wrig.dsmp_pids(): wrig.start()
def c(o): return wrig.cmd(o, timeout=120)
def wait(p, t=400):
    e = time.time() + t
    while time.time() < e:
        try:
            s = c({"cmd": "status"})
            if p(s): return s
        except Exception: pass
        time.sleep(2)
    raise SystemExit("timeout")
wait(lambda s: s.get("scene") == 1 and not s.get("busy"))
print(c({"cmd": "load_save", "file": "Z:" + SAVE, "det": 1}))
wait(lambda s: s.get("det", {}).get("locked"))
c({"cmd": "run", "to": c({"cmd": "status"})["tick"] + 100000, "per_frame": 2})
print("loop", c({"cmd": "ord_loop", "on": 1}))
us = [u for u in c({"cmd": "list_units"})["units"] if u["faction"] == 1 and not u["dead"]]
u = us[0]; cell = [u["cell"][0] + 3, u["cell"][1]]
print("hover", c({"cmd": "ord_hover", "n": 0}))
print("sel", c({"cmd": "ord_inject", "ev": "SelectPlayerUnitRequested", "i32@16": 0, "guid@20": u["guid"]}))
print("rc", c({"cmd": "ord_inject", "ev": "InteractGridRequested", "v2@16": f"{cell[0]},{cell[1]}", "u8@24": 0}))
time.sleep(3)
print("stats", json.dumps(c({"cmd": "ord_stats"}))[:900])
wrig.stop()
