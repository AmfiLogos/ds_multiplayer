"""unitsel lane: shared helpers for the pair tests (lease, start host + guest on the ElvenExpedition x10 demo save,
claims, orders through the capture path, checks). One leased pair; copies hidden, muted, windowed (rig.py)."""
import json, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(WT, 'rig'))
import rig  # noqa: E402

rig.DYLIB = os.environ.get('US_DYLIB') or os.path.join(WT, 'build', 'dsrig.dylib')
LANE = 'unitsel'
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, str(info)[:900])
    return ok


def owner_playing():
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/|Dungeon Settlers Modded.app'], capture_output=True, text=True).stdout
    # only the game binary itself (another tool's shell line can contain the same words)
    return [l for l in out.splitlines() if '/Contents/MacOS/' in l and 'pgrep' not in l and ' -c ' not in l]


def wait_owner():
    while True:
        p = owner_playing()
        if not p:
            return
        log('owner plays (', p[:2], '): waiting 60 s before starting test copies')
        time.sleep(60)


def room(n=2):
    """wait until the machine has room for n more test copies (cap 6 machine-wide)"""
    while rig.running_test_copies() > 6 - n:
        log('test copy cap: waiting 60 s'); time.sleep(60)


def start_copy(x, window):
    while True:
        wait_owner(); room(1)
        try:
            return rig.start(x, window=window, fps=30)
        except SystemExit:
            log(x, 'start refused (cap): retry in 60 s'); time.sleep(60)


def lease():
    while True:
        wait_owner(); room(2)
        pr = rig.lease(LANE)
        if pr:
            return pr
        log('no free pair: waiting 60 s'); time.sleep(60)


def c(_inst, _timeout=120, **k):
    try:
        return rig.send(_inst, k, timeout=_timeout)
    except Exception as e:
        return {'ok': False, 'error': repr(e)}


def st(x):
    for _ in range(20):
        s = c(x, cmd='status')
        if 'tick' in s or 'scene' in s:
            return s
        time.sleep(2)
    raise RuntimeError(f'{x}: status not answered: {s}')


def mp(x):
    return st(x).get('mp', {})


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
        except Exception:
            r = None
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def mark(x):
    p = os.path.join(rig.idir(x), 'rig.log')
    return os.path.getsize(p) if os.path.exists(p) else 0


def log_since(x, m):
    with open(os.path.join(rig.idir(x), 'rig.log'), errors='replace') as f:
        f.seek(m); return f.read()


def inject(x, ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}
    d.update({k.replace('_', '@'): v for k, v in f.items()})
    return c(x, **d)


def select(x, guid):
    return inject(x, 'SelectPlayerUnitRequested', i32_16=0, guid_20=guid)


def order_move(x, guid, cell):
    """the real input path: select the unit, right-click a cell (captured by ord, sent through dsnet)"""
    c(x, cmd='ord_hover', n=0)
    r1 = select(x, guid)
    r2 = inject(x, 'InteractGridRequested', v2_16=f'{cell[0]},{cell[1]}', u8_24=0)
    return bool(r1.get('ok') and r2.get('ok'))


def units(x, m=None):
    k = {'cmd': 'list_units'}
    if m is not None:
        k['map'] = m
    return c(x, **k).get('units', [])


def players(x):
    return [u for u in units(x) if u['faction'] == 1 and not u['dead'] and 'cell' in u]


def settle_both(X, Y, n=60):
    prev = None
    for _ in range(n):
        cur = (st(X)['tick'], st(Y)['tick'])
        if cur == prev and cur[0] == cur[1]:
            return cur
        prev = cur; time.sleep(2)
    return None


def fp_equal_from(X, Y, t_from):
    def rd(x):
        d = {}
        p = os.path.join(rig.idir(x), 'fp.log')
        if not os.path.exists(p):
            return d
        for line in open(p):
            q = line.split()
            if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from:
                d[int(q[0])] = q[1]
        return d
    a, b = rd(X), rd(Y)
    com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)


def setup(X, Y, window=False, host_name='The owner', guest_name='Ben', claim=True, save=None, allow_pause=True, guest_claims=None):
    """host X loads the x10 demo dungeon save, guest Y joins from its title. claim: X takes players[0:2], Y [2:4]."""
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    port = rig.pair_port(X)
    json.dump({'role': 'host', 'port': port, 'name': host_name, 'allow_speedup': True, 'allow_pause': bool(allow_pause)}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': guest_name}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
    for i in (X, Y):
        d = os.path.join(rig.idir(i), 'data', 'mp_owners')
        if os.path.isdir(d):
            os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
    start_copy(X, window)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    if save:
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
    else:
        from demo import make_demo_save
        src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
    log('host load', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
    start_copy(Y, window)
    rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900, every=3)
    time.sleep(5)
    us = players(X)
    mine = {X: us[:2], Y: us[2:4]}
    if guest_claims:                                          # these guids go to the guest, the others to the host
        mine = {Y: [u for u in us if u['guid'] in guest_claims], X: [u for u in us if u['guid'] not in guest_claims][:2]}
    if claim:
        for x in (X, Y):
            for u in mine[x]:
                log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
            c(x, cmd='ord_hover', n=0)
        time.sleep(4)
    return mine


def run_main(name, body, window=False):
    pair = lease(); X, Y = pair[0], pair[1]
    log(name, 'pair', pair, 'dylib', rig.DYLIB)
    err = None
    try:
        body(X, Y)
    except Exception:
        import traceback; err = traceback.format_exc(); log(err[-1500:])
        results['ran to the end'] = False
    finally:
        for x in (X, Y):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        rig.release(pair, LANE)
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER (restored)')
        ok = bool(results) and all(results.values()) and err is None
        log('RESULT', 'PASS' if ok else 'FAIL', json.dumps(results))
    return 0 if ok else 1
