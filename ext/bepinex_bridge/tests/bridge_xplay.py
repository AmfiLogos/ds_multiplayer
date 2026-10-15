#!/usr/bin/env python3
"""BepInEx bridge pair test: Mac host X (lane dsrig_bridge.dylib, no BepInEx) + Windows guest W_BRG (lane baselib.dll +
BepInEx be.788 + DsmpBridge + DsmpBridgeDemo plugins). Run through the lane runner (lease the pair first):
  python3 tools/brg_wrig.py xplay <pair> tests/bridge_xplay.py [--dylib PATH]
Checks: the plugin loads next to baselib.dll and sees the session (role Guest, player id); a C# mod action
(Multiplayer.AddGold(250) from the demo plugin on W) runs on BOTH peers at the same step through the lockstep order path;
gold equal on both; full-state checks equal; fingerprints equal; 0 desyncs. Then INFO: a managed-only channel
(demo.note) on a pair where the Mac peer has no plugin is counted as unhandled there (expected limit)."""
import os, sys, json, time, shutil
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
LANE = os.path.expanduser('~/ds_multiplayer/ext/bepinex_bridge')
SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
results = {}


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:700])
def c(x, **k):
    try: return rig.send(x, k, timeout=120)
    except Exception as e: return {'ok': False, 'error': repr(e)}
def st(x): return c(x, cmd='status')
def br(x): return c(x, cmd='bridge_status')
def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: r = pred()
        except Exception: r = None
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def fp_equal_from(x, y, t_from):
    def rd(i):
        d = {}
        for line in open(os.path.join(rig.idir(i), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from: d[int(p[0])] = p[1]
        return d
    a, b = rd(x), rd(y); com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)
def demo_status(y):
    try: return json.loads(open(os.path.join(rig.idir(y), 'bridge_demo.status')).read())
    except Exception: return {}
def trigger(y, text):
    p = os.path.join(rig.idir(y), 'bridge_demo.trigger')
    open(p + '.tmp', 'w').write(text); os.replace(p + '.tmp', p)


def main():
    a = sys.argv
    rig.DYLIB = os.path.abspath(a[a.index('--dylib') + 1]) if '--dylib' in a else os.path.join(LANE, 'build', 'dsrig_bridge.dylib')
    pair = rig.lease('bepinex'); X, Y = pair[0], pair[1]; PORT = rig.PAIR_PORTS[pair]
    log('pair', pair, 'host', X, '(Mac) dylib', rig.DYLIB, '| guest', Y, '= W_BRG', rig.idir(Y))
    try:
        for f in ('bridge_demo.status', 'bridge_demo.notes'):
            p = os.path.join(rig.idir(Y), f)
            if os.path.exists(p): os.replace(p, os.path.join(LANE, 'old', f + time.strftime('.%H%M%S')))
        json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 600},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben', 'full_check_steps': 600}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
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

        # (a) plugin loaded next to baselib.dll and sees the session
        ds = wait('demo status active', lambda: (lambda d: d if d.get('active') == 1 else None)(demo_status(Y)), timeout=60)
        bw = br(Y)
        check('plugin: DsmpBridge found the native bridge on W and sees the session (Guest, own player id)',
              ds.get('installed') == 1 and ds.get('role') == 'Guest' and ds.get('player') == bw.get('player') and ds.get('player', -1) >= 1
              and bw.get('dispatch') == 1, f'{ds} | native {bw}')
        bx = br(X)
        check('native: Mac host has the bridge (role Host), no managed dispatcher (no BepInEx on Mac)',
              bx.get('active') == 1 and bx.get('role') == 1 and bx.get('dispatch') == 0, bx)

        # (b) a C# mod action through the lockstep order path
        g0x, g0y = br(X).get('gold'), br(Y).get('gold')
        check('gold equal before', g0x == g0y and g0x is not None and g0x >= 0, f'{g0x} {g0y}')
        trigger(Y, 'gold 250')
        wait('gold applied on both', lambda: br(X).get('gold') == g0x + 250 and br(Y).get('gold') == g0y + 250, timeout=60, every=1)
        time.sleep(3)
        bx, bw, ds = br(X), br(Y), demo_status(Y)
        check('bridged action: AddGold(250) sent by the W plugin, applied once on both peers (native action)',
              bw.get('sent') == 1 and bx.get('applied_native') == 1 and bw.get('applied_native') == 1 and bx.get('gold') == bw.get('gold') == g0x + 250
              and 'Sent' in ds.get('last', ''), f'X {bx} | W {bw} | demo {ds}')
        def same_step(x):
            for line in open(os.path.join(rig.idir(x), 'rig.log'), errors='replace'):
                if 'bridge: dsmp.gold.add 250' in line: return line.strip()[-90:]
        lx, ly = same_step(X), same_step(Y)
        log('apply lines', lx, '|', ly)

        # full-state checks (gold is in the save sections) + fingerprints, a few checks after the action
        # the host only sends its sections; the guest compares and logs "full-state check at step N: equal|..." (mp_core.c)
        def fulls(): return [l for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') if 'full-state check at step' in l]
        f0 = len(fulls())
        wait('two more full checks on the guest', lambda: len(fulls()) >= f0 + 2, timeout=240, every=3)
        new = fulls()[f0:]; sx, sy = st(X).get('mp', {}), st(Y).get('mp', {})
        check('full-state checks equal after the action (guest compares), 0 desyncs on both',
              all(l.rstrip().endswith('equal') for l in new) and sx.get('desyncs', 1) == 0 and sy.get('desyncs', 1) == 0,
              f"{[l.strip()[-45:] for l in new]} desyncs X {sx.get('desyncs')} W {sy.get('desyncs')}")
        n, d = fp_equal_from(X, Y, t0)
        check('fingerprints equal on both from the join', n > 300 and d is None, f'{n} common ticks, first diff {d}')

        # INFO: managed-only channel on a Mac+Windows pair
        trigger(Y, 'note hello from a C# mod')
        wait('note applied on W', lambda: br(Y).get('applied_managed', 0) >= 1, timeout=60, every=1)
        time.sleep(3)
        bx, bw = br(X), br(Y)
        notes = open(os.path.join(rig.idir(Y), 'bridge_demo.notes')).read().strip() if os.path.exists(os.path.join(rig.idir(Y), 'bridge_demo.notes')) else ''
        log('INFO managed channel demo.note: W applied_managed', bw.get('applied_managed'), 'notes', repr(notes), '| Mac unhandled', bx.get('unhandled'),
            '(expected: a managed channel needs the plugin on every peer)')
        results['info: managed channel runs on W, Mac counts it unhandled'] = bw.get('applied_managed') == 1 and bx.get('unhandled') == 1
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for i in (Y, X):
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
