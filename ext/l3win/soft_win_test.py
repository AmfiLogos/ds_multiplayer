#!/usr/bin/env python3
"""Soft restart on Windows (loader kit in the DSMP bottle, -batchmode, no window): flow J through the loader's own
test switch (DSL_SOFT_TEST=1: apply by <profile>/softrestart/apply_request.json).
  title round: A at the title -> apply -> B at the title, A gone
The old world round (apply inside a loaded world, B loads A's autosave) is gone: since v13 the loader refuses to apply
inside a world on purpose (the player applies at the title), so that path no longer exists to test.
Output: ext/l3win/soft_out/<round>/ (dsload logs of A and B, handoff) + printed PASS / FAIL lines."""
import glob, json, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l3w
P = l3w.PROF; WP = l3w.WROOT + "\\profiles\\modded"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "soft_out")
SAVE = "ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0"
res = {}
def log(*a): print(time.strftime("%H:%M:%S"), *a, flush=True)
def check(name, ok, info=""): res[name] = bool(ok); log("PASS" if ok else "FAIL", name, info)
def read(p): return open(p, errors="replace").read() if os.path.exists(p) else ""
def wait(pred, what, t=240):
    t0 = time.time()
    while time.time() - t0 < t:
        v = pred()
        if v: return v
        time.sleep(1)
    log("timeout:", what); return None
def status():
    try: return l3w.cmd({"cmd": "status"}, timeout=10)
    except Exception: return {}
def game_pids(): return [p for p, c in l3w.dsmp_pids() if "DungeonSettlers.exe" in c and "Dungeon Settlers Modded\\game" in c]

def round_(name, world):
    os.makedirs(f"{OUT}/{name}", exist_ok=True)
    os.makedirs(f"{P}/data/Mods", exist_ok=True)
    open(f"{P}/data/Mods/soft_p1.json", "w").write('{"enabled":["multiplayer"],"order":["multiplayer"]}\n')
    for f in glob.glob(f"{P}/dsload.[0-9]*.log") + glob.glob(f"{P}/softrestart/*"):
        if os.path.isfile(f): os.replace(f, f"{OUT}/{name}/old_" + os.path.basename(f))
    os.environ.update(DSL_SOFT_TEST="1", DSL_SOFT_NOACTIVATE="1")
    a_log = f"{P}/dsload.log"
    ts = time.time()
    if l3w.start([]): check(f"{name}: A started", False); return
    ok = wait(lambda: os.path.exists(a_log) and os.path.getmtime(a_log) > ts and "soft restart ready (A)" in read(a_log), "A soft ready", 180)
    check(f"{name}: A up, soft restart ready", ok)
    if not ok: l3w.stop(); return
    st = wait(lambda: (lambda s: s if s.get("ok") and s.get("scene") == 1 and not s.get("busy") else None)(status()), "title", 180)
    if world:
        r = l3w.cmd({"cmd": "load_save", "name": SAVE})
        log("load", r)
        wait(lambda: (lambda s: s if s.get("scene") == 2 and not s.get("busy") else None)(status()), "world", 240)
        time.sleep(5)
    a_pid = game_pids()
    t0 = time.time()
    req = {"profile": WP + "\\data\\Mods\\soft_p1.json"}
    if world: req["camera_move"] = [3.0, -2.0]
    os.makedirs(f"{P}/softrestart", exist_ok=True)
    open(f"{P}/softrestart/apply_request.json", "w").write(json.dumps(req))
    shown = wait(lambda: "A: B shown" in read(a_log) or "FAILED" in read(a_log), "swap", 150)
    a_txt = read(a_log)
    m = re.search(r"B started, pid (\d+)", a_txt)
    b_logs = [f"{P}/dsload.{m.group(1)}.log"] if m else []
    b_txt = "".join(read(f) for f in b_logs)
    wait(lambda: len(game_pids()) <= 1, "A quit", 30)
    time.sleep(3)
    left = game_pids()
    for f in [a_log] + b_logs + glob.glob(f"{P}/softrestart/*.json") + glob.glob(f"{P}/softrestart/*.log"):
        shutil.copy2(f, f"{OUT}/{name}/" + os.path.basename(f))
    check(f"{name}: A started B", m, m.group(0) if m else "")
    check(f"{name}: swap done (A: B shown, A quits)", shown and "A: B shown" in a_txt and "FAILED" not in a_txt, f"{time.time() - t0:.1f}s")
    check(f"{name}: B shown", "B: SHOWN" in b_txt, re.search(r"B: SHOWN \(([^)]*)\)", b_txt).group(1) if "B: SHOWN" in b_txt else "")
    check(f"{name}: only B runs after the swap", len(left) == 1 and left != a_pid, f"before {a_pid} after {left}")
    if world:
        sv = re.search(r"A: autosave done: (\S+)", a_txt)
        check(f"{name}: A autosaved, B loaded that save", sv and f"B: LoadCampaignRequested {sv.group(1)}" in b_txt and "B: world loaded" in b_txt, sv.group(1) if sv else "")
        ta = re.search(r"target ([-\d.]+),([-\d.]+) size ([-\d.]+)", a_txt.split("A: handoff written")[-1])
        tb = re.search(r"B: camera target ([-\d.]+),([-\d.]+) size ([-\d.]+)", b_txt)
        same = ta and tb and all(abs(float(x) - float(y)) <= 1.0 for x, y in zip(ta.groups(), tb.groups()))
        check(f"{name}: same camera target (within one tile)", same, f"A {ta.groups() if ta else None} B {tb.groups() if tb else None}")
    l3w.stop()

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    round_("title", False)
    json.dump(res, open(f"{OUT}/summary.json", "w"), indent=1)
    log("RESULTS", sum(res.values()), "/", len(res))
