#!/usr/bin/env python3
"""mp_api v2 on a hidden Mac pair (lane mpapi): mod chat lines + the local random, with this checkout's dsrig.dylib.

  chat_pair.py [--dylib PATH]      (default: <this checkout>/build/dsrig.dylib)

Host X loads the test save, guest Y joins. Checks (each one = a real break when it fails):
  - a broadcast mod line reaches the other player as "<name> <Mod> mod: text", tags escaped;
  - a local line stays on its own screen;
  - flood: 20 broadcasts at once -> at most 3 sent (rate limited), the other screen gets at most 3;
  - draws outside the lockstep step are local (shared 0);
  - after all of it: 0 desyncs on both, full-state checks equal.
Pair: rig.lease('mpapi'), released at the end. Copies hidden (rig default), never frontmost."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(os.path.dirname(HERE))
SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
results = {}


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])
def c(x, **k):
    try: return rig.send(x, k, timeout=120)
    except Exception as e: return {'ok': False, 'error': repr(e)}
def st(x): return c(x, cmd='status')
def chat(x): return c(x, cmd='mpm_status').get('chat', {})
def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: r = pred()
        except Exception: r = None
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def main():
    a = sys.argv
    rig.DYLIB = a[a.index('--dylib') + 1] if '--dylib' in a else os.path.join(ROOT, 'build', 'dsrig.dylib')
    pair = rig.lease('mpapi')
    if not pair or len(pair) != 2: raise SystemExit(f'no free pair: {pair}')
    X, Y = pair[0], pair[1]; PORT = rig.pair_port(X)
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        json.dump({'role': 'host', 'port': PORT, 'name': 'Hosta', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 600},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben', 'full_check_steps': 600}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(SAVE)); shutil.copy2(SAVE, src)
        log('host load', c(X, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)

        r = c(X, cmd='bridge_chat', mod='Delvers', text='Boss <b>awake</b> on floor 3', broadcast=1)
        ly = wait('broadcast line on the guest', lambda: [l for l in chat(Y).get('last', []) if 'Delvers' in l], timeout=30, every=1)
        lx = [l for l in chat(X).get('last', []) if 'Delvers' in l]
        check('broadcast: sent (0) and shown on both with the sender name, tags escaped',
              r.get('rc') == 0 and ly and lx and 'Hosta' in ly[-1] and 'Delvers mod:' in ly[-1] and '<b>awake' not in ly[-1] and 'awake' in ly[-1],
              f'rc {r} | Y {ly[-1:]} | X {lx[-1:]}')

        n0x = chat(X).get('lines', 0)
        r = c(Y, cmd='bridge_chat', mod='Delvers', text='only me', broadcast=0); time.sleep(3)
        check('local line: shown on its own screen only', r.get('rc') == 0 and any('only me' in l for l in chat(Y).get('last', []))
              and chat(X).get('lines', 0) == n0x and not any('only me' in l for l in chat(X).get('last', [])), f'rc {r}')

        time.sleep(7)                                               # the broadcast bucket refills (3 at once, then 1 every 2 s)
        n0y = chat(Y).get('lines', 0)
        rcs = [c(X, cmd='bridge_chat', mod='Spam', text=f'flood {i}', broadcast=1).get('rc') for i in range(20)]
        time.sleep(4); n1y = chat(Y).get('lines', 0)
        check('flood: 20 broadcasts at once -> 3 sent, the rest rate limited (-5); the guest got at most 3 lines',
              rcs.count(0) == 3 and rcs.count(-5) == 17 and n1y - n0y <= 3, f'rcs {rcs} | guest lines {n0y}->{n1y}')

        rx, ry = c(X, cmd='bridge_rand', mod='probe', stream=1, n=4), c(Y, cmd='bridge_rand', mod='probe', stream=1, n=4)
        check('draws outside the lockstep step are local (shared 0, values differ between peers)',
              rx.get('shared') == 0 and ry.get('shared') == 0 and rx.get('v') != ry.get('v'), f'{rx} | {ry}')

        def fulls(): return [l for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') if 'full-state check at step' in l]
        f0 = len(fulls())
        wait('two more full checks on the guest', lambda: len(fulls()) >= f0 + 2, timeout=300, every=3)
        new = fulls()[f0:]; sx, sy = st(X).get('mp', {}), st(Y).get('mp', {})
        check('0 desyncs on both, full-state checks equal', all('equal' in l for l in new) and not sx.get('desynced') and not sy.get('desynced')
              and not sx.get('desyncs') and not sy.get('desyncs'), f'{[l.strip()[-80:] for l in new]} | X {sx} | Y {sy}')
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for x in (Y, X):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        rig.release(pair, 'mpapi')
        n = sum(results.values()); log(f'{n}/{len(results)} PASS')
    sys.exit(0 if results and all(results.values()) else 1)


if __name__ == '__main__':
    main()
