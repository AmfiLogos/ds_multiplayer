#!/usr/bin/env python3
"""dsmpfix lane, fix A: a unit click (the game's own selection event) never makes the full-state check differ.

Real break it catches: the game keeps a per-player "new skill" red-dot state (PlayerUnitsSaveData/SkillLearnNotices =
UnitSkillContainer._skillLearnNotices) that the unit panel (EntitySummaryUIPresenter) writes for the units THIS player
selects. Up to de9b909 the full-state check compared it, so a guest's normal unit click made the next check differ ->
forced desync -> full resync of the guest (lane dslock probe 2026-10-25). The soak / mp2 saves have every notice already
snapshotted (a click changes nothing there), so those tests never saw it.

ONE leased pair (rig.py lease), started only while the 1-minute load is under 15. Hidden WINDOWED copies (the unit panel
runs as in a player's game), multiplayer only (DSRIG_CUSTOM=0), the host listens on an OS-chosen port (never a shared
fixed port: other lanes' copies joined runs there). World: the wiki's public base save (4 player units, notices not yet
snapshotted). The guest claims 2 units. For --minutes (default 3): every ~10 s each player clicks units through the game's
own selection event (ord_inject SelectPlayerUnitRequested: the guest its own units and a host unit, the host its own
units) and moves one of its units (select + right-click a near cell). Full-state check every --full-every steps (default
300); DSRIG_FULL_KEEP=1 keeps each check's save so the test can see the clicks reached the unit panel.

Checks:
 setup   the guest joined and loaded THIS host world; both played (steps advanced, orders from both)
 clicks  in a kept check save of the same step, the guest's SkillLearnNotices differ from the host's (the local red-dot
         state changed on one side): without this the run proves nothing
 full    no full-state difference, no desync, no reload on the guest; full checks ran (>= 4) and were all equal
 fp      step fingerprints equal over the common ticks (>= 1000)
Usage: selnotice_pair.py [--dylib PATH] [--minutes 3] [--full-every 300]
  --dylib  default: this worktree's build/dsrig.dylib
Output: PASS <name> / FAIL <name> [info] per check, then RESULT PASS|FAIL <n> PASS, <m> FAIL.
Logs: ext/dsmpfix/out/selnotice_<time>/ (run.log, each copy's rig.log part, the kept check saves' notices)."""
import json, os, random, shutil, socket, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(W, 'rig'))
import rig

ARG = lambda k, d: type(d)(sys.argv[sys.argv.index(k) + 1]) if k in sys.argv else d
rig.DYLIB = os.path.abspath(os.path.expanduser(ARG('--dylib', os.path.join(W, 'build', 'dsrig.dylib'))))
MINUTES, FULL = ARG('--minutes', 3.0), ARG('--full-every', 300)
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
LOAD_MAX, LOAD_WAIT_S, LEASE_WAIT_S = 15.0, 20 * 60, 20 * 60
LANE = 'dsmpfix-selnotice'
OUT = os.path.join(HERE, 'out', 'selnotice_' + time.strftime('%Y%m%d-%H%M%S'))
checks = []


def log(*a):
    line = ' '.join(str(x) for x in a)
    print(time.strftime('%H:%M:%S'), line, flush=True)
    with open(os.path.join(OUT, 'run.log'), 'a') as f: f.write(time.strftime('%H:%M:%S ') + line + '\n')
def check(name, ok, info=''):
    checks.append((name, bool(ok))); log('PASS' if ok else 'FAIL', name, '' if ok else str(info)[:900])
def c(x, **k): return rig.send(x, k, timeout=120)
def st(x):
    try: return c(x, cmd='status')
    except Exception as e: return {'error': repr(e)}
def mp(x): return st(x).get('mp', {})
def wait(what, f, timeout=600, every=2.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = f()
            if v: return v
        except Exception: pass
        time.sleep(every)
    log('timeout:', what); return None
def notice_text(x):                                   # the stock Lock Consecration box's text while it shows
    t = c(x, cmd='ui_texts', min=10).get('texts', [])
    return next((e['text'] for e in t if 'CommonNotice' in e.get('path', '') and 'Lock Consecration' in e.get('text', '')), None)
def click(x, guid):                                  # the game's own unit selection (a left click on the unit / its bar card)
    return c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid}).get('ok')
def order_move(x, guid, cell):
    c(x, cmd='ord_hover', n=0)
    r1 = click(x, guid)
    r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0}).get('ok')
    return r1 and r2
def players(x):
    return [u for u in c(x, cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
def aside(p):                                          # never delete: earlier runs' files go to this run's aside folder
    if os.path.exists(p):
        d = os.path.join(OUT, 'aside'); os.makedirs(d, exist_ok=True)
        shutil.move(p, os.path.join(d, os.path.relpath(p, rig.INST).replace(os.sep, '_')))
def free_port():                                       # OS-chosen, not listened on by anyone right now
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p
def log_from(x, pos):
    p = os.path.join(rig.idir(x), 'rig.log')
    with open(p, errors='replace') as f: f.seek(pos); return f.read()
def notices(path):
    try: return json.load(open(path)).get('PlayerUnitsSaveData', {}).get('SkillLearnNotices')
    except Exception: return None
def fp(x, t_from):
    d = {}
    for line in open(os.path.join(rig.idir(x), 'fp.log')):
        p = line.split()
        if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from: d[int(p[0])] = p[1]
    return d


def run(X, Y, port):
    for x in (X, Y):
        for p in ('mp.json', 'fp.log', os.path.join('data', 'fullcheck')): aside(os.path.join(rig.idir(x), p))
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': FULL},
              open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'full_check_steps': FULL},
              open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
    pos = {x: (os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) if os.path.exists(os.path.join(rig.idir(x), 'rig.log')) else 0) for x in (X, Y)}
    rig.start(X, fps=30, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
    if 'cannot listen' in log_from(X, pos[X]):
        raise RuntimeError('the host cannot listen on port %d: no guest started' % port)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'selnotice_base.json'); shutil.copy2(TPL, src)
    log('host load', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900)
    # the Template save is older than the game's Lock Consecration option: a stock Yes/No box opens on load and holds
    # the join save (popup_join_test.py); the host answers No, as a player would before inviting
    if wait('the Lock Consecration notice on the host', lambda: notice_text(X), timeout=120):
        log('host answers the Lock Consecration notice: No', c(X, cmd='ui_notice', button='cancel'))
        wait('notice closed', lambda: not notice_text(X), timeout=30, every=1)
    rig.start(Y, fps=30, window=True)
    rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
    ok = wait('guest loaded the host world', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                       and not s.get('mp', {}).get('needs_state'))(st(Y)), timeout=900)
    hp, gp = players(X), players(Y)
    same = sorted(u['guid'] for u in hp) == sorted(u['guid'] for u in gp) and len(hp) >= 4
    check('setup: the guest joined and loaded THIS host world (same 4 player units)', ok and same, (mp(Y), len(hp), len(gp)))
    if not (ok and same): return
    mine = {Y: [u['guid'] for u in hp[:2]], X: [u['guid'] for u in hp[2:4]]}
    for g in mine[Y]: log('guest claims', g, c(Y, cmd='mp_claim', guid=g))
    time.sleep(3)
    m0 = {x: mp(x) for x in (X, Y)}; t_fp = st(X).get('tick', 0)
    rnd = random.Random(11); orders = {X: 0, Y: 0}; clicks = {X: 0, Y: 0}; t0 = time.time(); last = 0
    while time.time() - t0 < MINUTES * 60:
        # the first 45 s only the guest clicks (its notices change, the host's not: the check saves of those steps differ);
        # then both. Each player's panel soon notes every unit, so the two states meet again later (run 11:06, step 900+)
        for x in ((Y,) if time.time() - t0 < 45 else (Y, X)):   # clicks first (the panel opens on the clicked unit), then a move order
            for g in mine[x] + ([mine[X][0]] if x == Y else []):
                if click(x, g): clicks[x] += 1
                time.sleep(1.0)
            try: us = {u['guid']: u for u in players(x)}
            except Exception as e: log('list_units', x, repr(e)[:120]); us = {}
            g = rnd.choice(mine[x])
            if g in us:
                cell = (us[g]['cell'][0] + rnd.randint(-5, 5), us[g]['cell'][1] + rnd.randint(-5, 5))
                if order_move(x, g, cell): orders[x] += 1
        if time.time() - last > 60:
            my = mp(Y); log(f'{(time.time() - t0) / 60:.1f} min: guest timer {my.get("timer")} desyncs {my.get("desyncs")} loads {my.get("loads")} '
                            f'full ok {my.get("full_ok")} fails {my.get("full_fails")}; clicks {clicks} orders {orders}'); last = time.time()
        time.sleep(4)
    time.sleep(3)
    my, mx = mp(Y), mp(X)
    gl, hl = log_from(Y, pos[Y]), log_from(X, pos[X])
    for x, t in ((X, hl), (Y, gl)): open(os.path.join(OUT, f'{x}_rig.log'), 'w').write(t)
    diffs = [l for l in gl.splitlines() if 'FULL-STATE CHECK DIFFERS' in l]
    des = [l for l in gl.splitlines() if 'DESYNC' in l or 'full resync' in l]
    for l in (diffs + des)[:10]: log('guest log:', l[:300])
    steps = my.get('timer', 0) - m0[Y].get('timer', 0)
    check('setup: both played (steps advanced, clicks and orders from both)',
          steps > MINUTES * 60 * 10 and min(orders.values()) >= 3 and min(clicks.values()) >= 6, f'steps {steps}, clicks {clicks}, orders {orders}')
    # clicks reached the unit panel: the kept check saves of one step differ in SkillLearnNotices between the players
    kd = {x: os.path.join(rig.idir(x), 'data', 'fullcheck') for x in (X, Y)}
    com = sorted(set(os.listdir(kd[X])) & set(os.listdir(kd[Y])), key=lambda n: int(n.split('.')[0])) if all(os.path.isdir(d) for d in kd.values()) else []
    seen = None
    for n in com:
        a, b = notices(os.path.join(kd[X], n)), notices(os.path.join(kd[Y], n))
        if a is not None and b is not None and a != b: seen = (n, a, b); break
    if seen:
        json.dump({'step_file': seen[0], 'host': seen[1], 'guest': seen[2]}, open(os.path.join(OUT, 'notices_differ.json'), 'w'), indent=1)
    check('clicks: the unit panel changed the clicking player\'s SkillLearnNotices (kept check saves of one step differ)',
          seen, f'{len(com)} common kept check saves, none differ in SkillLearnNotices')
    nfull = my.get('full_ok', 0) - m0[Y].get('full_ok', 0)
    check('full: no full-state difference, no desync, no reload on the guest; full checks ran and were equal',
          not diffs and my.get('desyncs', 0) == m0[Y].get('desyncs', 0) and my.get('loads', 0) == m0[Y].get('loads', 0)
          and my.get('full_fails', 0) == m0[Y].get('full_fails', 0) and nfull >= 4,
          f'differs {len(diffs)} (first: {diffs[0][-220:] if diffs else "-"}), desyncs {m0[Y].get("desyncs")}->{my.get("desyncs")}, '
          f'loads {m0[Y].get("loads")}->{my.get("loads")}, full ok +{nfull} fails {my.get("full_fails")}')
    a, b = fp(X, t_fp), fp(Y, t_fp)
    cm = sorted(set(a) & set(b)); first = next((t for t in cm if a[t] != b[t]), None)
    check('fp: step fingerprints equal over the common ticks', len(cm) >= 1000 and first is None, f'{len(cm)} common, first difference at tick {first}')


def main():
    os.makedirs(OUT, exist_ok=True)
    log('dylib', rig.DYLIB, 'minutes', MINUTES, 'full check every', FULL, 'steps')
    if not os.path.exists(rig.DYLIB): print('RESULT FAIL 0 PASS, 1 FAIL (no dylib)'); sys.exit(2)
    t0 = time.time()
    while os.getloadavg()[0] >= LOAD_MAX:
        if time.time() - t0 > LOAD_WAIT_S: print('RESULT FAIL 0 PASS, 1 FAIL (load stayed high)'); sys.exit(2)
        log(f'1-minute load {os.getloadavg()[0]:.1f} (limit {LOAD_MAX:.0f}): waiting 60 s'); time.sleep(60)
    t0 = time.time(); pair = rig.lease(LANE, os.getpid())
    while not pair and time.time() - t0 < LEASE_WAIT_S:
        log('no free pair: retry in 60 s'); time.sleep(60); pair = rig.lease(LANE, os.getpid())
    if not pair: print('RESULT FAIL 0 PASS, 1 FAIL (no rig lease)'); sys.exit(2)
    X, Y = pair[0], pair[1]; port = free_port()
    rig.pair_port = lambda x, _p=port: _p              # rig.start's fix_mp_port writes this port, not the pair's fixed one
    for k, v in (('DSRIG_CUSTOM', '0'), ('DSRIG_FULL_KEEP', '1'), ('DSRIG_BG', '1')): os.environ[k] = v
    log('pair', pair, 'port', port, '(OS-chosen)', 'host', X, 'guest', Y)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        run(X, Y, port)
    except Exception as e:
        check('run', False, repr(e)); traceback.print_exc()
    finally:
        for x in (Y, X):
            try: rig.stop(x)
            except Exception as e: log('stop', x, repr(e))
        rig.release(pair, LANE)
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        n = sum(ok for _, ok in checks)
        log(f'RESULT {"PASS" if checks and n == len(checks) else "FAIL"} {n} PASS, {len(checks) - n} FAIL; {OUT}')


if __name__ == '__main__':
    main()
