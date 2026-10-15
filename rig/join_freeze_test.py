#!/usr/bin/env python3
"""lan_v10 core (review finding 4): how long the host (and everyone) stands still when a player joins late.

  join_freeze_test.py [--joins 3] [--save PATH]

X hosts a running world; Y joins late (by address), plays 40 s, leaves, joins again (--joins times). For each join, from
X's side: the host's own reload (rig.log "host reloads its join-point save: queued" -> "load done"), the time X's world
tick does not move (status polled every 0.25 s), and the time until the session runs again (not paused). A third player
joining costs the host the same: the join point, the save upload and the host's reload are the same steps (and every
guest already in the game now loads the same save, dsnet_server.c). The copies run at background priority (nice 19,
batchmode): a real Mac is faster. Results: build/core_test/joinfreeze_<time>/results.json"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
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


def main():
    out = os.path.join(rig.ROOT, 'build', 'core_test', 'joinfreeze_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    pair = rig.lease('core', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS[pair]
    res = []
    try:
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj):
            os.replace(yj, yj + '.off')
        rig.start(X, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        save = arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'))
        src = os.path.join(work, 'jf.json'); shutil.copy2(save, src)
        c(X, cmd='load_save', file=src, det=1)
        wait('X locked', lambda: st(X).get('det', {}).get('locked'), every=2)
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        for k in range(arg('--joins', 3)):
            time.sleep(20)                                        # the host plays alone
            lp = os.path.getsize(os.path.join(rig.idir(X), 'rig.log'))
            t_join = time.time()
            c(Y, cmd='mpm_join_tcp', host='127.0.0.1', port=port, name='Ben')
            ticks = []                                            # (time, host tick, host paused)
            while time.time() - t_join < 120:
                s = st(X); ticks.append((time.time() - t_join, s.get('tick'), s.get('mp', {}).get('paused')))
                sy = st(Y)
                if sy.get('det', {}).get('locked') and sy.get('mp', {}).get('loads', 0) >= 1 and not sy.get('mp', {}).get('needs_state') and not s.get('mp', {}).get('paused') and len(ticks) > 8:
                    if ticks[-1][1] != ticks[-5][1]:
                        break
                time.sleep(0.25)
            # longest stretch with no host tick change
            longest, run_start = 0.0, ticks[0][0]
            for i in range(1, len(ticks)):
                if ticks[i][1] != ticks[i - 1][1]:
                    longest = max(longest, ticks[i - 1][0] - run_start); run_start = ticks[i][0]
            paused = [t for t, _, p in ticks if p]
            with open(os.path.join(rig.idir(X), 'rig.log'), 'rb') as f:
                f.seek(lp); txt = f.read().decode('utf8', 'replace').splitlines()
            def at(pat):
                l = next((l for l in txt if pat in l), None)
                if not l: return None
                h, m, s = l[:12].split(':'); return int(h) * 3600 + int(m) * 60 + float(s)
            q, d = at('host reloads its join-point save: queued'), at('mp_sync.json deleted (load done)')
            r = {'join': k, 'host_reload_s': round(d - q, 2) if q and d else None, 'host_tick_frozen_s': round(longest, 2),
                 'session_paused_s': round(paused[-1] - paused[0], 2) if paused else 0, 'until_running_s': round(ticks[-1][0], 2),
                 'save_bytes': next((l.split('uploaded ')[1].split('-byte')[0] for l in txt if 'uploaded' in l), None)}
            res.append(r); log('join', r)
            c(Y, cmd='mp_quit', mode='title')                    # Quit to title = leave the session
            wait('Y on the title', lambda: st(Y).get('scene') == 1 and not st(Y).get('busy'), timeout=300, every=2)
            c(X, cmd='mp_left_choice', keep=1)                   # host: Keep playing (the leave holds everyone)
    except Exception as e:
        log('ERROR', repr(e))
    finally:
        json.dump({'pair': pair, 'joins': res}, open(os.path.join(out, 'results.json'), 'w'), indent=1)
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj + '.off'):
            os.replace(yj + '.off', yj)
        rig.stop(X); rig.stop(Y); rig.release(pair)
        log('SUMMARY', json.dumps(res), out)


if __name__ == '__main__':
    main()
