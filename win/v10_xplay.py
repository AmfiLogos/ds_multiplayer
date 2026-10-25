#!/usr/bin/env python3
"""lan_v10 co-op markers cross-play (pings + camera views over dsnet) on a Mac host + Windows guest W; copy of v8_xplay.py setup.
  xplay_run.py <pair> wrap_xplay.py --math [--dylib PATH]
(v8_xplay.py header follows) lan_v8 quick cross-play checks (Mac host X, Windows guest W), run through xplay_run.py:
  xplay_run.py <pair> v8_xplay.py --math [--dylib PATH]
chat both ways; the recruitment popup on W pauses nobody (SetTimeLock inlined on Windows -> OnActivate wrapper);
population Split (W HUD + hire up to its cap, then refused); a story event whose choice W makes through the stock
button lambda (OnClickChoice inlined on Windows -> lambda wrapper, test command win_evlambda); fingerprints equal."""
import os, sys, json, time, shutil
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
import rigports
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



def inject(x, ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}; d.update({k.replace('_', '@'): v for k, v in f.items()}); return c(x, **d)
def chat(x): return c(x, cmd='mpm_status').get('chat', {})
def pv(x):
    d = c(x, cmd='ui_story').get('pop', {})
    for t in c(x, cmd='ui_texts', min=6).get('texts', []):
        if 'Txt_UnitNum' in t.get('path', ''): d['hud'] = t['text']
    return d


def main():
    a = sys.argv
    rig.DYLIB = a[a.index('--dylib') + 1] if '--dylib' in a else os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib')
    pair = rig.lease(f'windows2-{os.getpid()}'); X, Y = pair[0], pair[1]; PORT = rig.PAIR_PORTS[pair]
    rigports.expect_peers(1)   # the Windows copy W is in the Mac copy's session: rig.stop's player check allows it
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
        for u in allu[2:3]: c(Y, cmd='mp_claim', guid=u); time.sleep(1)

        # lan_v10 co-op markers across Mac / Windows (dsnet P_MARK / P_VIEW, outside the sim). W has no graphics
        # (batchmode -nographics): only the data path is checked (pings and views known on both), not the drawing.
        def coop(inst, **k): return rig.send(inst, dict(cmd='ui_coop', **k), timeout=120)
        a, b = coop(X), coop(Y)
        check('coop: marker API on both, same map key', a.get('net') == 1 and b.get('net') == 1 and a.get('coop_ok') == 1 and b.get('coop_ok') == 1 and a.get('key') == b.get('key'), f"{a} | {b}")
        us = [u for u in c(X, cmd='list_units', map=0).get('units', []) if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        cx, cy = us[0]['cell']; occ = {tuple(u['cell']) for u in us}
        def free(p):
            while any(abs(p[0] - q[0]) <= 1 and abs(p[1] - q[1]) <= 1 for q in occ): p = (p[0] + 1, p[1])
            return p
        hp, wp = free((cx + 5, cy + 3)), free((cx + 5, cy + 7))
        r = coop(X, action='ping', x=hp[0], y=hp[1]); time.sleep(3)
        pw = [p for p in coop(Y).get('pings', []) if tuple(p['cell']) == hp]
        check('ping: the host ping reaches W (owner 0)', r.get('rc') == 0 and pw and pw[0]['owner'] == 0, coop(Y).get('pings'))
        r = coop(Y, action='ping', x=wp[0], y=wp[1]); time.sleep(3)
        ph = [p for p in coop(X).get('pings', []) if tuple(p['cell']) == wp]
        check('ping: the W ping reaches the host (owner 1)', r.get('rc') == 0 and ph and ph[0]['owner'] == 1, coop(X).get('pings'))
        coop(X, action='toggle', x=hp[0], y=hp[1]); time.sleep(3)
        check('ping: deleted on the host -> gone on W too', not [p for p in coop(Y).get('pings', []) if tuple(p['cell']) == hp], coop(Y).get('pings'))
        coop(X, action='camera', x=cx + 6.5, y=cy + 2.5); time.sleep(4)
        vw = [v for v in coop(Y).get('views', []) if v['player'] == 0]
        check("views: W knows the host's camera (P_VIEW)", vw and abs(vw[0]['view'][0] - (cx + 6.5)) < 0.3 and abs(vw[0]['view'][1] - (cy + 2.5)) < 0.3, coop(Y).get('views'))
        wc = coop(Y, action='camera', x=cx - 4.5, y=cy + 1.5); time.sleep(4)
        vh = [v for v in coop(X).get('views', []) if v['player'] == 1]
        check("views: the host knows W's camera (P_VIEW)", vh and abs(vh[0]['view'][0] - wc.get('cam', [0])[0]) < 0.3, f"{coop(X).get('views')} W cam {wc.get('cam')}")
        time.sleep(8)
        n, d = fp_equal_from(X, Y, t0)
        check('fingerprints equal on both from the join', n > 300 and d is None, f'{n} common ticks, first diff {d}')
    finally:
        for i in (Y, X):
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
