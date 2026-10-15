#!/usr/bin/env python3
"""latejoin lane: late-join pair test of the per-player "Fast join" option (cog row, mp_ui.json fast_join).

  fastjoin_pair.py --mode off|on [--joins 3] [--dylib PATH] [--play 40] [--full-every 600]

X hosts a running world; Y joins late by address, both play --play s (full-state checks every --full-every steps), Y
leaves, joins again. The option is set on BOTH copies through the panel's own setter (ui_coop set fast_join) before the
first join, and put back to Off at the end (the copies' data folders are shared with other lanes).
Per join: host reload (queued -> load done), host tick frozen, until running (join_freeze_test's measures), the guest's
load (join sent -> Y locked and loaded). Pass: Off = no skip line on either copy, On = a skip on both each join;
0 desync, no full-state or deep difference, at least one full check equal. Results: <rig>/build/core_test/fastjoin_*/"""
import json, os, shutil, sys, time
RIG = os.path.expanduser('~/ds_multiplayer/rig')           # shared leases / instances
sys.path.insert(0, RIG)
import rig


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    for _ in range(20):
        try:
            s = c(x, cmd='status')
        except Exception as e:
            s = {'error': str(e)}
        if 'tick' in s or 'scene' in s:
            return s
        time.sleep(0.5)
    raise RuntimeError(f'{x}: no status')


def wait(what, pred, timeout=900, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def logsize(x):
    return os.path.getsize(os.path.join(rig.idir(x), 'rig.log'))


def grep_log(x, pat, pos):
    with open(os.path.join(rig.idir(x), 'rig.log'), 'rb') as f:
        f.seek(pos)
        return [l for l in f.read().decode('utf8', 'replace').splitlines() if pat in l]


def set_fast(x, v):
    c(x, cmd='ui_coop', action='set', key='fast_join', value=v)
    try:                                                      # read back what the game saved (mp_ui.json)
        return json.load(open(os.path.join(st(x)['persistentDataPath'], 'mp_ui.json'))).get('fast_join')
    except Exception as e:
        return repr(e)[:80]


def main():
    mode = arg('--mode', 'off'); want = 1 if mode == 'on' else 0
    if '--dylib' in sys.argv:
        rig.DYLIB = os.path.abspath(os.path.expanduser(arg('--dylib', '')))
    play, fc = arg('--play', 40), arg('--full-every', 600)
    out = os.path.join(rig.ROOT, 'build', 'core_test', f'fastjoin_{mode}_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    pair = rig.lease('latejoin', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS[pair]
    res, checks, setv = [], [], {}
    log('mode', mode, 'pair', pair, 'dylib', rig.DYLIB)
    try:
        xj = os.path.join(rig.idir(X), 'mp.json')
        if os.path.exists(xj):
            shutil.copy2(xj, xj + '.fjbak')
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': fc},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj):
            os.replace(yj, yj + '.off')
        rig.start(X, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        setv[X] = set_fast(X, want)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        save = arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'))
        src = os.path.join(work, 'fj.json'); shutil.copy2(save, src)
        c(X, cmd='load_save', file=src, det=1)
        wait('X locked', lambda: st(X).get('det', {}).get('locked'), every=2)
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        setv[Y] = set_fast(Y, want)
        log('option set', setv)
        p0 = {X: logsize(X), Y: logsize(Y)}
        for k in range(arg('--joins', 3)):
            time.sleep(20)                                        # the host plays alone
            lp = {X: logsize(X), Y: logsize(Y)}
            t_join = time.time(); t_yload = None
            c(Y, cmd='mpm_join_tcp', host='127.0.0.1', port=port, name='Ben')
            ticks = []
            while time.time() - t_join < 120:
                s = st(X); ticks.append((time.time() - t_join, s.get('tick'), s.get('mp', {}).get('paused')))
                sy = st(Y)
                ym = sy.get('mp', {})
                if t_yload is None and sy.get('det', {}).get('locked') and ym.get('loads', 0) >= 1 and not ym.get('needs_state') and not sy.get('busy'):
                    t_yload = time.time() - t_join
                if sy.get('det', {}).get('locked') and ym.get('loads', 0) >= 1 and not ym.get('needs_state') and not s.get('mp', {}).get('paused') and len(ticks) > 8:
                    if ticks[-1][1] != ticks[-5][1]:
                        break
                time.sleep(0.25)
            longest, run_start = 0.0, ticks[0][0]
            for i in range(1, len(ticks)):
                if ticks[i][1] != ticks[i - 1][1]:
                    longest = max(longest, ticks[i - 1][0] - run_start); run_start = ticks[i][0]
            with open(os.path.join(rig.idir(X), 'rig.log'), 'rb') as f:
                f.seek(lp[X]); txt = f.read().decode('utf8', 'replace').splitlines()
            def at(pat):
                l = next((l for l in txt if pat in l), None)
                if not l: return None
                h, m, s = l[:12].split(':'); return int(h) * 3600 + int(m) * 60 + float(s)
            q, d = at('host reloads its join-point save: queued'), at('mp_sync.json deleted (load done)')
            time.sleep(play)                                      # both play: full-state checks run
            r = {'join': k, 'host_reload_s': round(d - q, 2) if q and d else None, 'host_tick_frozen_s': round(longest, 2),
                 'until_running_s': round(ticks[-1][0], 2), 'guest_loaded_s': round(t_yload, 2) if t_yload else None,
                 'skips_X': len(grep_log(X, 'stabilize wait skipped', lp[X])), 'skips_Y': len(grep_log(Y, 'stabilize wait skipped', lp[Y]))}
            res.append(r); log('join', r)
            c(Y, cmd='mp_quit', mode='title')
            wait('Y on the title', lambda: st(Y).get('scene') == 1 and not st(Y).get('busy'), timeout=300, every=2)
            c(X, cmd='mp_left_choice', keep=1)
        diffs = grep_log(Y, 'FULL-STATE CHECK DIFFERS', p0[Y]) + grep_log(X, 'FULL-STATE CHECK DIFFERS', p0[X])
        deep = grep_log(Y, 'deep hash differs', p0[Y])
        des = grep_log(Y, 'DESYNC:', p0[Y]) + grep_log(X, 'DESYNC:', p0[X])
        fl = grep_log(Y, 'full-state check at step', p0[Y])        # the guest's stats reset with each session: count log lines
        stats = {'full_ok': sum(l.endswith(': equal') for l in fl), 'full_fails': sum(' differs' in l for l in fl),
                 'full_diffs': diffs[:10], 'deep_diffs': deep[:10], 'desync_lines': des[:10]}
        log('stats', json.dumps(stats))
        skip_ok = all((r['skips_X'] > 0 and r['skips_Y'] > 0) if want else (r['skips_X'] == 0 and r['skips_Y'] == 0) for r in res)
        checks.append(('option read back on both copies', setv.get(X) == want and setv.get(Y) == want, str(setv)))
        checks.append((f'skip lines match mode {mode}', skip_ok and len(res) == arg('--joins', 3), json.dumps([(r['skips_X'], r['skips_Y']) for r in res])))
        checks.append(('no desync, no full / deep difference', not des and not diffs and not deep, ''))
        checks.append(('full-state checks ran, none failed', (stats['full_ok'] or 0) >= 1 and not stats['full_fails'], f"ok {stats['full_ok']} fails {stats['full_fails']}"))
    except Exception as e:
        log('ERROR', repr(e)); checks.append(('ran', False, repr(e)))
    finally:
        for x in (X, Y):                                          # leave the copies' option Off for other lanes
            try:
                c(x, cmd='ui_coop', action='set', key='fast_join', value=0)
            except Exception as e:
                log('reset', x, repr(e)[:120])
        json.dump({'pair': pair, 'mode': mode, 'joins': res, 'checks': checks}, open(os.path.join(out, 'results.json'), 'w'), indent=1)
        xj = os.path.join(rig.idir(X), 'mp.json')
        if os.path.exists(xj + '.fjbak'):
            os.replace(xj + '.fjbak', xj)
        elif os.path.exists(xj):
            os.replace(xj, xj + '.fastjoin_off')               # had none: no host mp.json left behind (rule 8: no rm)
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj + '.off'):
            os.replace(yj + '.off', yj)
        rig.stop(X); rig.stop(Y); rig.release(pair, 'latejoin')
        for name, ok, info in checks:
            print(('PASS ' if ok else 'FAIL ') + name, info, flush=True)
        log('SUMMARY', mode, json.dumps(res), out)


if __name__ == '__main__':
    main()
