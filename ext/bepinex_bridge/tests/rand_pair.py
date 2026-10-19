#!/usr/bin/env python3
"""Shared-random proof on a hidden Mac pair where BOTH peers run BepInEx (lane mpapi_bridge).

Each copy starts like the launcher starts a world with BepInEx: dsload.dylib (loader build) + its multiplayer module
(built from the mpapi branch: MP_REPO=$HOME/ds_multiplayer MP_PIN=mpapi loader/build.sh) + Doorstop + bepinex-mac 0.1.1
(an APFS clone per copy) + DsmpBridge + DsmpRandProbe (test plugin). Host X loads the test save, guest Y joins.
The probe (plugin/DsmpRandProbe) draws inside the lockstep step on every peer:
  "send N" -> a bridge action: its handler draws N x UnityEngine.Random.Range(int) / .value / .Range(float) and N x
              Multiplayer.Rand("probe", 1) (sent from both peers, several times);
  "tick N" -> a bridge action that arms a Harmony postfix on Refactor.Tick.TickFlow.Tick (not hooked by us) for N ticks.
PASS = both peers logged the same draw sets (same step, same UnityEngine.Random values, same Multiplayer.Rand values,
shared = 1), 0 desyncs, full-state checks equal, fingerprints equal. UnityEngine.Random differing is a RESULT (reported
with the det trace flags), the Multiplayer.Rand part is the API's promise.

usage: rand_pair.py [--loader LOADER_BUILD_DIR] [--n 6] [--rounds 3]
Lease: rig.lease('mpapi_bridge'), released at the end. Copies hidden (batchmode), small, never frontmost."""
import json, os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_BIE = os.path.expanduser('~/ds_multiplayer/ext/bepinex_bridge/l1/bie/0.1.1')   # bepinex-mac 0.1.1 with this game's interop
SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
OUT = os.path.join(LANE, 'build', 'rand_pair')
results, info = {}, []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(detail)[:900])
def c(x, **k):
    try: return rig.send(x, k, timeout=120)
    except Exception as e: return {'ok': False, 'error': repr(e)}
def st(x): return c(x, cmd='status')
def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try: r = pred()
        except Exception: r = None
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def park(p):
    if os.path.exists(p):
        d = os.path.join(OUT, 'old'); os.makedirs(d, exist_ok=True)
        os.replace(p, os.path.join(d, os.path.basename(p) + time.strftime('.%Y%m%d-%H%M%S')))
def pstatus(x):
    try: return json.loads(open(os.path.join(rig.idir(x), 'rand_probe.status')).read())
    except Exception: return {}
def plog(x):
    p = os.path.join(rig.idir(x), 'rand_probe.log')
    return [json.loads(l) for l in open(p)] if os.path.exists(p) else []
def trigger(x, text):
    p = os.path.join(rig.idir(x), 'rand_probe.trigger')
    wait('trigger taken ' + x, lambda: not os.path.exists(p), timeout=30, every=0.5)
    open(p + '.tmp', 'w').write(text); os.replace(p + '.tmp', p)
def fp_equal_from(x, y, t_from):
    def rd(i):
        d = {}
        for line in open(os.path.join(rig.idir(i), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from: d[int(p[0])] = p[1]
        return d
    a, b = rd(x), rd(y); com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)


def build_plugins():
    d = os.path.join(OUT, 'plugins'); os.makedirs(d, exist_ok=True)
    for proj in ('DsmpBridge', 'DsmpRandProbe'):
        r = subprocess.run(['nice', '-n', '19', os.path.expanduser('~/.dotnet/dotnet'), 'build', '-c', 'Release', '-o', os.path.join(OUT, 'dotnet_out')],
                           cwd=os.path.join(LANE, 'plugin', proj), capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'{proj} build failed:\n' + r.stdout[-1500:])
        shutil.copy2(os.path.join(OUT, 'dotnet_out', proj + '.dll'), d)
    return d


def start(x, loader, plugins):
    """the l1 launcher's BepInEx start (tools/mac_bie_host.py) with the current loader; own BepInEx clone per copy"""
    if rig.alive(rig.read_pid(x)): raise SystemExit(f'{x} already running')
    bie = os.path.join(OUT, 'bie_' + x)
    if not os.path.isdir(bie):
        subprocess.run(['cp', '-c', '-R', SRC_BIE, bie], check=True)   # APFS clone: BepInEx writes logs / config / cache here
    park(os.path.join(bie, 'BepInEx', 'LogOutput.log'))
    rig.prepare_data(x); rig.fix_mp_port(x)
    if os.path.exists(rig.sockpath(x)): os.unlink(rig.sockpath(x))
    home = os.path.join(rig.idir(x), 'home'); dsload = os.path.join(loader, 'dsload.dylib')
    prof = os.path.join(OUT, 'profile.json')
    json.dump({'enabled': ['multiplayer'], 'order': ['multiplayer'], 'provided': []}, open(prof, 'w'))
    env = {'HOME': home, 'CFFIXED_USER_HOME': home, 'USER': os.environ.get('USER', ''), 'LOGNAME': os.environ.get('LOGNAME', ''),
           'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': rig.idir(x), 'DSRIG_FPS': '30',
           'DSRIG_BATCH': '1', 'DYLD_INSERT_LIBRARIES': dsload + ':' + os.path.join(bie, 'libdoorstop.dylib'),
           'DSL_MODS_DIR': os.path.join(OUT, 'mods'), 'DSL_PROFILE': prof, 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSL_SOFT_NOACTIVATE': '1',
           'DSML_DSLOAD': dsload, 'DSML_BEPINEX_DIR': bie,
           'DOORSTOP_ENABLED': '1', 'DOORSTOP_TARGET_ASSEMBLY': os.path.join(bie, 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'),
           'DOORSTOP_CLR_RUNTIME_CORECLR_PATH': os.path.join(bie, 'dotnet/libcoreclr.dylib'), 'DOORSTOP_CLR_CORLIB_DIR': os.path.join(bie, 'dotnet'),
           'DOORSTOP_IGNORE_DISABLED_ENV': '0', 'BEPINEX_PLUGIN_DIRS': plugins}
    env.update({k: v for k, v in os.environ.items() if k.startswith('DSRIG_') and k not in env})
    args = [os.path.join(rig.idir(x), rig.EXE_REL), '-logFile', os.path.join(rig.idir(x), 'player.log'),
            '-screen-fullscreen', '0', '-screen-width', '640', '-screen-height', '360', '-batchmode']
    spec = {'args': args, 'env': env, 'front_before': rig.front_pid() or 0, 'bg': True, 'show': False}
    open(os.path.join(rig.idir(x), 'launch.json'), 'w').write(json.dumps(spec))
    open(rig.pidfile(x), 'w').write('')
    subprocess.Popen([sys.executable, os.path.abspath(rig.__file__), '_guard', x], stdin=subprocess.DEVNULL,
                     stdout=open(os.path.join(rig.idir(x), 'guard.log'), 'ab'), stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(100):
        if rig.alive(rig.read_pid(x)): break
        time.sleep(0.1)
    log(x, 'started (dsload + multiplayer module + BepInEx) pid', rig.read_pid(x))


def compare(lx, ly):
    """draw sets by (src, seq): same step, same values on both peers"""
    kx = {(r['src'], r['seq']): r for r in lx}; ky = {(r['src'], r['seq']): r for r in ly}
    com = sorted(set(kx) & set(ky)); diff = {'step': [], 'unity': [], 'mp': [], 'shared': []}
    for k in com:
        a, b = kx[k], ky[k]
        if a['step'] != b['step']: diff['step'].append(k)
        if (a['ri'], a['rv'], a['rf']) != (b['ri'], b['rv'], b['rf']): diff['unity'].append(k)
        if a['mp'] != b['mp']: diff['mp'].append(k)
        if not (a['shared'] == b['shared'] == 1): diff['shared'].append(k)
    return com, diff


def main():
    a = sys.argv
    loader = os.path.abspath(a[a.index('--loader') + 1]) if '--loader' in a else os.path.expanduser('~/ds_mpapi/ds_modloader_bridge/loader/build')
    N = int(a[a.index('--n') + 1]) if '--n' in a else 6
    rounds = int(a[a.index('--rounds') + 1]) if '--rounds' in a else 3
    os.makedirs(OUT, exist_ok=True)
    md = os.path.join(OUT, 'mods'); park(md); shutil.copytree(os.path.join(loader, 'mods', 'multiplayer'), os.path.join(md, 'multiplayer'))
    plugins = build_plugins()
    pair = rig.lease('mpapi_bridge')
    if not pair: raise SystemExit('no free pair for mpapi_bridge')
    X, Y = pair[0], pair[1]; PORT = rig.PAIR_PORTS[pair]
    log('pair', pair, 'host', X, 'guest', Y, 'loader', loader)
    try:
        for i in (X, Y):
            for f in ('rand_probe.status', 'rand_probe.log', 'rand_probe.trigger'):
                p = os.path.join(rig.idir(i), f)
                if os.path.exists(p): os.replace(p, os.path.join(OUT, 'old_' + i + '_' + f + time.strftime('.%H%M%S')))
            d = os.path.join(rig.idir(i), 'data', 'mp_owners')
            if os.path.isdir(d): os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
        json.dump({'role': 'host', 'port': PORT, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 600},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben', 'full_check_steps': 600}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.cap_wait('rand_pair host'); start(X, loader, plugins)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(SAVE)); shutil.copy2(SAVE, src)
        log('host load', c(X, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(X).get('det', {}).get('locked'))
        rig.cap_wait('rand_pair guest'); start(Y, loader, plugins)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)))
        time.sleep(8); t0 = st(X)['tick']

        sx = wait('probe status X', lambda: (lambda d: d if d.get('active') == 1 else None)(pstatus(X)), timeout=90)
        sy = wait('probe status Y', lambda: (lambda d: d if d.get('active') == 1 else None)(pstatus(Y)), timeout=90)
        check('both peers: DsmpBridge found the multiplayer module, native api >= 2 with random, session on, Tick patch in',
              all(s.get('installed') == 1 and s.get('api', 0) >= 2 and s.get('has_rand') == 1 and s.get('patched') == 1 for s in (sx, sy))
              and sx.get('role') == 'Host' and sy.get('role') == 'Guest', f'X {sx} | Y {sy}')
        check('outside the step (MonoBehaviour.Update): RandShared is 0 on both', sx.get('shared_in_update') == 0 and sy.get('shared_in_update') == 0)

        # draws inside bridge handlers, sent from both peers; then the Harmony Tick postfix
        for r in range(rounds):
            trigger(Y, f'send {N}'); time.sleep(1.5); trigger(X, f'send {N}'); time.sleep(1.5)
        trigger(Y, f'tick {N}')
        want_h, want_t = 2 * rounds, N
        wait('all draw sets on both', lambda: all(sum(1 for r in plog(i) if r['src'] == 'h') >= want_h and sum(1 for r in plog(i) if r['src'] == 't') >= want_t
                                                   for i in (X, Y)), timeout=180, every=2)
        time.sleep(2)
        lx, ly = plog(X), plog(Y)
        com, diff = compare(lx, ly)
        shutil.copy2(os.path.join(rig.idir(X), 'rand_probe.log'), os.path.join(OUT, 'rand_probe_X.log'))
        shutil.copy2(os.path.join(rig.idir(Y), 'rand_probe.log'), os.path.join(OUT, 'rand_probe_Y.log'))
        hs = [k for k in com if k[0] == 'h']; ts = [k for k in com if k[0] == 't']
        check(f'draw sets logged on both peers at the same steps ({len(hs)} handler, {len(ts)} Tick postfix)',
              len(hs) == want_h and len(ts) == want_t and not diff['step'], f"step diffs {diff['step'][:5]}")
        check('Multiplayer.Rand: same values on both peers in every set, RandShared 1 (handler and Tick postfix)',
              not diff['mp'] and not diff['shared'], f"mp diffs {diff['mp'][:5]} shared diffs {diff['shared'][:5]}")
        uh = [k for k in diff['unity'] if k[0] == 'h']; ut = [k for k in diff['unity'] if k[0] == 't']
        msg = f'UnityEngine.Random from C#: handler sets differing {len(uh)}/{len(hs)}, Tick-postfix sets differing {len(ut)}/{len(ts)}'
        info.append(msg); log('RESULT (not a pass/fail of the API):', msg)
        results['info: UnityEngine.Random from C# inside the step is the same on both peers'] = not diff['unity']
        if diff['unity']:
            k = diff['unity'][0]; a_, b_ = {(r['src'], r['seq']): r for r in lx}[k], {(r['src'], r['seq']): r for r in ly}[k]
            log('first differing set', k, 'X', a_['ri'][:3], a_['rv'][:2], '| Y', b_['ri'][:3], b_['rv'][:2])

        # chat from a C# mod: broadcast from both
        trigger(Y, 'chat hello from the guest <b>'); time.sleep(2); trigger(X, 'chat hello from the host')
        time.sleep(2)
        cx, cy = pstatus(X), pstatus(Y)
        check('Multiplayer.Chat broadcast from both peers -> Shown', 'Shown' in cx.get('last', '') and 'Shown' in cy.get('last', ''), f"X {cx.get('last')} | Y {cy.get('last')}")

        def fulls(): return [l for l in open(os.path.join(rig.idir(Y), 'rig.log'), errors='replace') if 'full-state check at step' in l]
        f0 = len(fulls())
        wait('two more full checks on the guest', lambda: len(fulls()) >= f0 + 2, timeout=300, every=3)
        new = fulls()[f0:]; mx, my = st(X).get('mp', {}), st(Y).get('mp', {})
        check('full-state checks equal after the draws (guest compares), 0 desyncs on both',
              all(l.rstrip().endswith('equal') for l in new) and mx.get('desyncs', 1) == 0 and my.get('desyncs', 1) == 0,
              f"{[l.strip()[-45:] for l in new]} desyncs X {mx.get('desyncs')} Y {my.get('desyncs')}")
        n, d = fp_equal_from(X, Y, t0)
        check('fingerprints equal on both from the join', n > 300 and d is None, f'{n} common ticks, first diff {d}')
        det = st(X).get('det', {}); log('host det counters', {k: v for k, v in det.items() if 'draw' in k or 'view' in k})
        for i in (X, Y):
            for f in ('rig.log', 'dsload.log'):
                p = os.path.join(rig.idir(i), f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{i}_{f}'))
            p = os.path.join(OUT, 'bie_' + i, 'BepInEx', 'LogOutput.log')
            if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{i}_LogOutput.log'))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for i in (Y, X):
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        log('release', pair, rig.release(pair, 'mpapi_bridge'))
        api = {k: v for k, v in results.items() if not k.startswith('info:')}
        log('RESULT', 'PASS' if api and all(api.values()) else 'FAIL', json.dumps(results))
        for m in info: log('INFO', m)


if __name__ == '__main__':
    main()
