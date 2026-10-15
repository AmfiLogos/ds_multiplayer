#!/usr/bin/env python3
"""lan_v8 quick cross-play checks (Mac host X, Windows guest W), run through xplay_run.py:
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
        for u in allu[2:3]: c(Y, cmd='mp_claim', guid=u); time.sleep(1)

        # chat both ways
        c(Y, cmd='mpm_chat', open=1); time.sleep(1); c(Y, cmd='mpm_chat', text='hello from windows'); time.sleep(0.5)
        typing = chat(Y)
        c(Y, cmd='mpm_chat', enter=1); time.sleep(3)
        c(X, cmd='mpm_chat', open=1); time.sleep(1); c(X, cmd='mpm_chat', text='hello from mac'); time.sleep(0.5); c(X, cmd='mpm_chat', enter=1); time.sleep(3)
        cx, cy = chat(X), chat(Y)
        check('chat: W typing blocks hotkeys (status while typing)', typing.get('typing') == 1, typing)
        check('chat: W -> host and host -> W lines on both', any('hello from windows' in l for l in cx.get('last', [])) and any('hello from mac' in l for l in cy.get('last', []))
              and any('hello from windows' in l for l in cy.get('last', [])), f"X {cx.get('last')} | W {cy.get('last')}")

        # recruitment popup on W: nobody paused (Windows OnActivate wrapper)
        mk = mark(Y)
        inject(Y, 'ShowPopUpRequested', i32_16=8); time.sleep(3)
        a1, b1 = st(X), st(Y); time.sleep(4); a2, b2 = st(X), st(Y)
        lg = log_since(Y, mk)
        check('recruitment open on W: both keep running, time lock skipped on W', a2['tick'] > a1['tick'] and b2['tick'] > b1['tick'] and not a2.get('mp', {}).get('paused')
              and not b2.get('mp', {}).get('paused') and 'RecruitView time lock skipped' in lg, f"X {a1['tick']}->{a2['tick']} W {b1['tick']}->{b2['tick']} log {[l for l in lg.splitlines() if 'time lock' in l][-2:]}")
        inject(Y, 'ClosePopUpRequested', i32_16=8); time.sleep(2)

        # population Split (the filtered ClanDataContainer getters on W)
        c(X, cmd='ui_story', action='gold', value=99999); time.sleep(2)
        c(X, cmd='ui_units', dir=1); time.sleep(1); c(X, cmd='ui_units', dir=1); time.sleep(2)
        c(X, cmd='ui_story', action='pop', value=2); time.sleep(2.5)
        pa, pb = pv(X), pv(Y)
        check('population Split: rule on both, HUD own/cap on both (W HUD through the class-filtered getters)', pa.get('mode') == 2 and pb.get('mode') == 2
              and pb.get('hud') == f"{pb.get('have')}/{pb.get('cap')}" and pa.get('hud') == f"{pa.get('have')}/{pa.get('cap')}", f"{pa} | {pb}")
        def hire(x):
            inject(x, 'ClosePopUpRequested', i32_16=8); time.sleep(1); inject(x, 'ShowPopUpRequested', i32_16=8); time.sleep(2.5)
            before = pv(X).get('clan'); m = mark(x)
            c(x, cmd='ui_story', action='recruit', value=0); time.sleep(2); c(x, cmd='ui_result', action='confirm'); time.sleep(4)
            return before, pv(X).get('clan'), log_since(x, m)
        n = 0
        while pv(Y).get('have', 0) < pv(Y).get('cap', 0) and n < 4:
            bf, af, lg = hire(Y); n += 1; log('W hire', bf, '->', af)
            if af == bf: break
        q = pv(Y)
        check('population Split: W hires up to its cap', q.get('have') == q.get('cap') and n >= 1, q)
        bf, af, lg = hire(Y)
        check('population Split: a W hire above the cap is refused with the rule message', af == bf and 'Population full for you' in lg, [l for l in lg.splitlines() if 'opulation' in l][-2:])
        inject(Y, 'ClosePopUpRequested', i32_16=8); time.sleep(1)
        c(X, cmd='ui_story', action='pop', value=0); time.sleep(2)

        # story event: W makes its choices through the stock button lambda
        def sv(x): return c(x, cmd='ui_story')
        c(X, cmd='ui_story', action='rule', value=2); time.sleep(2)            # rule First answer: only W answers
        c(X, cmd='ui_story', action='event', key='Event_Random4')
        wait('event popup on both', lambda: sv(X).get('active') == 1 and sv(Y).get('active') == 1 and sv(X).get('node') and sv(Y).get('node'), timeout=30, every=1)
        nodes, via = 0, []
        t1 = time.time()
        while time.time() - t1 < 150 and (sv(X).get('active') == 1 or sv(Y).get('active') == 1) and nodes < 6:
            sa = sv(X); ch = sa.get('chooser'); n0 = sa.get('node')
            if ch == 0: c(X, cmd='ui_story', action='click', idx=0); via.append('X click')
            else:
                r = c(Y, cmd='win_evlambda', idx=0); via.append(f'W lambda rc {r.get("rc")}')
                if ch < 0 and ch != -3: c(X, cmd='ui_story', action='click', idx=0)
            wait('choice applied on both', lambda: (lambda p, q: (p.get('node') != n0 or not p.get('active')) and (q.get('node') != n0 or not q.get('active')))(sv(X), sv(Y)), timeout=40, every=1)
            nodes += 1
        sa, sb = sv(X), sv(Y)
        check('story: choices (W through the stock lambda) applied on both, the event ended', not sa.get('active') and not sb.get('active') and any(v.startswith('W lambda rc 0') for v in via), f"{via} | {sa} | {sb}")
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
