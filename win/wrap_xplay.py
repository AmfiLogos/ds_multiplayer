#!/usr/bin/env python3
"""Windows-only wrapper checks (name tag, chat keys) on a Mac host + Windows guest W; copy of v8_xplay.py setup.
  xplay_run.py <pair> wrap_xplay.py --math [--dylib PATH]
(v8_xplay.py header follows) lan_v8 quick cross-play checks (Mac host X, Windows guest W), run through xplay_run.py:
  xplay_run.py <pair> v8_xplay.py --math [--dylib PATH]
chat both ways; the recruitment popup on W pauses nobody (SetTimeLock inlined on Windows -> OnActivate wrapper);
population Split (W HUD + hire up to its cap, then refused); a story event whose choice W makes through the stock
button lambda (OnClickChoice inlined on Windows -> lambda wrapper, test command win_evlambda); fingerprints equal."""
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

        # name tag wrapper: the stock name refresh (RefreshUnitShortCutPartsName, RefreshUnitName inlined) through our hook
        r = c(Y, cmd='win_scname'); time.sleep(1); r2 = c(Y, cmd='win_wrapstat')
        check('name tag: the stock name refresh on W runs our wrapper and re-tags the unit', r.get('rc') == 0 and r2.get('sc_calls', 0) >= 1 and r2.get('sc_tagged', 0) >= 1, f'{r} {r2}')
        # chat keys wrapper (IsTextInputFocused inlined into UpdateKeyStream)
        k0 = c(Y, cmd='win_wrapstat'); time.sleep(2); k1 = c(Y, cmd='win_wrapstat')
        check('chat keys: not typing -> UpdateKeyStream runs stock (wrapper called, nothing blocked)', k1['key_calls'] > k0['key_calls'] and k1['key_blocked'] == k0['key_blocked'], f'{k0} {k1}')
        c(Y, cmd='mpm_chat', open=1); time.sleep(1)
        kk = c(Y, cmd='mpm_key', chars='hi from windows'); time.sleep(1)
        k2 = c(Y, cmd='win_wrapstat'); time.sleep(2); k3 = c(Y, cmd='win_wrapstat'); ch = chat(Y)
        check('chat keys: typing on W -> the game key stream is blocked every frame', ch.get('typing') == 1 and k3['key_blocked'] - k2['key_blocked'] >= 10, f'{k2} {k3} typing {ch.get("typing")}')
        check('chat keys: Unity key events (Event.KeyboardEvent) reach the W chat field', 'hi from windows' in ch.get('buf', ''), f"mpm_key {kk} buf {ch.get('buf')!r}")
        if 'hi from windows' not in ch.get('buf', ''): c(Y, cmd='mpm_chat', text='hi from windows')
        c(Y, cmd='mpm_chat', enter=1); time.sleep(3)
        k4 = c(Y, cmd='win_wrapstat'); time.sleep(2); k5 = c(Y, cmd='win_wrapstat')
        check('chat keys: after sending, the key stream is no longer blocked', k5['key_calls'] > k4['key_calls'] and k5['key_blocked'] == k4['key_blocked'], f'{k4} {k5}')
        check('chat: the W line reaches the host', any('hi from windows' in l for l in chat(X).get('last', [])), chat(X).get('last'))
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
