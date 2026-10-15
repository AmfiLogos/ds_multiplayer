#!/usr/bin/env python3
"""Cross-play lending (Mac host lends a unit to the Windows guest): run through xplay_run.py.
  xplay_run.py <pair> lend_xplay.py --math [--dylib PATH]
Mac X hosts MoonlightBrigade (det on), Windows W joins. Units by mp_claim (host 2, guest 1). Same checks as
src/ui_test/uxgame_pair.py step "lend" (lend menu, lend, borrower order applied, owner order dropped, take back,
lend again, give back), plus fingerprints equal on both from the join to the end."""
import os, sys, json, time, shutil
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
results = {}


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])
def c(x, **k):
    try: return rig.send(x, k, timeout=120)
    except Exception as e: return {'ok': False, 'error': repr(e)}
def st(x): return c(x, cmd='status')
def wait(what, pred, timeout=900, every=3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: r = pred()
        except Exception: r = None
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def units(x, m=None):
    k = {'cmd': 'list_units'}
    if m is not None: k['map'] = m
    return [u for u in c(x, **k).get('units', []) if u['faction'] == 1 and not u['dead']]
def mark(x): return os.path.getsize(os.path.join(rig.idir(x), 'rig.log'))
def log_since(x, m):
    with open(os.path.join(rig.idir(x), 'rig.log'), errors='replace') as f: f.seek(m); return f.read()
def fp_equal_from(x, y, t_from):
    def rd(i):
        d = {}
        for line in open(os.path.join(rig.idir(i), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from: d[int(p[0])] = p[1]
        return d
    a, b = rd(x), rd(y); com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)


def main():
    a = sys.argv
    rig.DYLIB = a[a.index('--dylib') + 1] if '--dylib' in a else os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib')
    pair = rig.lease('windows2'); X, Y = pair[0], pair[1]; PORT = rig.PAIR_PORTS[pair]
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):
            d = os.path.join(rig.idir(i), 'data', 'mp_owners')
            if os.path.isdir(d): os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
        rig.start(X, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(SAVE)); shutil.copy2(SAVE, src)
        log('host load', c(X, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)))
        time.sleep(8); t0 = st(X)['tick']
        allu = [u['guid'] for u in units(X, 0)]
        h, g = allu[:2], allu[2:3]
        for u in h: log('host claim', u, c(X, cmd='mp_claim', guid=u)); time.sleep(1)
        for u in g: log('guest claim', u, c(Y, cmd='mp_claim', guid=u)); time.sleep(1)
        time.sleep(4)
        u = h[0]
        check('claims: host unit owner 0 on both', c(X, cmd='mp_owner', guid=u).get('owner') == 0 and c(Y, cmd='mp_owner', guid=u).get('owner') == 0,
              f"{c(X, cmd='mp_owner', guid=u)} {c(Y, cmd='mp_owner', guid=u)}")
        r = c(X, cmd='ui_lend', guid=u, action='menu'); time.sleep(1.5)
        check('lend menu on the host portrait: "Lend control to Ben" + "Release"', r.get('menu_rows', 0) >= 2 and any('Lend control to Ben' in m for m in r.get('menu', [])) and any('Release' in m for m in r.get('menu', [])), r)
        mx, my = mark(X), mark(Y)
        c(X, cmd='ui_lend', action='row', row=0); time.sleep(4)
        ra, rb = c(X, cmd='ui_lend', guid=u, action='state'), c(Y, cmd='ui_lend', guid=u, action='state')
        check('lent: controller Ben on both peers', ra.get('controller') == 1 and rb.get('controller') == 1, f'{ra} | {rb}')
        check('lent: "The owner lent you ..." on W / "... to Ben" on the host', 'The owner lent you' in log_since(Y, my) and 'to Ben' in log_since(X, mx))
        cell = [x for x in units(Y, 0) if x['guid'] == u][0]['cell']
        def ords(x): m = st(x).get('mp', {}); return m.get('orders', 0), m.get('orders_dropped_owner', 0)
        o1 = ords(X); rig.send(Y, {'cmd': 'mp_move', 'guid': u, 'x': cell[0] + 1, 'y': cell[1]}, timeout=60); time.sleep(4); o2 = ords(X)
        check('lent: the Windows borrower\'s order is applied on the host', o2[0] == o1[0] + 1 and o2[1] == o1[1], f'{o1}->{o2}')
        o1 = ords(Y); rig.send(X, {'cmd': 'mp_move', 'guid': u, 'x': cell[0], 'y': cell[1]}, timeout=60); time.sleep(4); o2 = ords(Y)
        check('lent: the owner\'s own order is dropped on W while lent', o2[1] == o1[1] + 1, f'{o1}->{o2}')
        my = mark(Y)
        c(X, cmd='ui_lend', guid=u, action='take'); time.sleep(4)
        check('take back: controller cleared on both, "The owner took back" on W', c(X, cmd='ui_lend', guid=u, action='state').get('controller') == -1
              and c(Y, cmd='ui_lend', guid=u, action='state').get('controller') == -1 and 'The owner took back' in log_since(Y, my))
        c(X, cmd='ui_lend', guid=u, action='lend', to=1); time.sleep(4)
        mx = mark(X)
        c(Y, cmd='ui_lend', guid=u, action='give'); time.sleep(4)
        check('give back from W: controller cleared, "Ben gave back" on the host', c(X, cmd='ui_lend', guid=u, action='state').get('controller') == -1 and 'Ben gave back' in log_since(X, mx))
        time.sleep(10)
        n, d = fp_equal_from(X, Y, t0)
        check('fingerprints equal on both from the join', n > 300 and d is None, f'{n} common ticks, first diff {d}')
    finally:
        for i in (Y, X):
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
