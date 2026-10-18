#!/usr/bin/env python3
"""fogfix (hotfix v0.1.13, 2026-10-18): per-player fog must not call a view of an earlier dungeon or an ended session.

  fogfix_pair.py --dylib PATH [--cycles 3]

v0.1.13 crashed (SIGSEGV / 0xc0000005 in EntityObjectView.SetVisible from mp_fog_frame -> redraw_now ->
redraw_entities) after a 2nd or 3rd dungeon entry in the same process, and at quit to title right after
"fog: shared fog of war on (vanilla)". This pair repeats that path on one leased hidden pair (lane fogfix):
X hosts MoonlightBrigade with Shared fog Off, takes f1; Y joins with no units (a watcher). Each cycle: X proposes,
Y approves, both land in the dungeon, Y follows X's camera, the host toggles Shared fog On then Off (a full redraw
on both peers), and (not on the last cycle) f1 uses the dungeon portal and both return to the base, where the fog
is toggled again. After the last entry: Y quits to title (its session ends: the shared-fog redraw), then X.
Checks: both copies alive after every step, the guest watches with the host's vision in every dungeon, the new
epoch log line on both, fingerprints equal every tick, no desync, both reach the title."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle, T.fp_equal_from
results = T.results
CYCLES = T.arg('--cycles', 3)


def rig_log_since(x, pos):
    f = open(os.path.join(rig.idir(x), 'rig.log'), errors='replace'); f.seek(pos); return f.read()


def alive(x):
    pid = rig.read_pid(x)
    return bool(pid) and rig.alive(pid)


def both_alive(X, Y, what):
    a, b = alive(X), alive(Y)
    check(f'both copies alive: {what}', a and b, f'{X} {a} {Y} {b}')
    if not (a and b):
        raise SystemExit(f'a copy died: {what}')


def toggle_fog(X, Y, where):
    for v in (1, 0):
        r = c(X, cmd='mpm_rules', fog=v); log(X, 'shared fog', v, r)
        wait(f'both see shared fog {v}', lambda: c(X, cmd='mpm_fog').get('shared') == v and c(Y, cmd='mpm_fog').get('shared') == v, timeout=60)
        time.sleep(6)                                    # the redraw runs on the next frames; the v0.1.13 crash came 4-6 s later
        both_alive(X, Y, f'shared fog {v} {where}')


def return_to_base(X, Y, f1):
    bl = c(X, cmd='ui_buildings', map=1, subtype=2)
    portal = bl['buildings'][0]['guid']
    log(X, 'f1 uses the dungeon portal', c(X, cmd='ui_interact', unit=f1, building=portal, map=1))
    rs = wait('return screen open on both', lambda: (lambda a, b: (a, b) if a.get('open') and b.get('open') else None)(c(X, cmd='ui_result'), c(Y, cmd='ui_result')), timeout=900, every=3)
    P = X if rs[0]['chooser'] == 0 else Y
    log('picker', P, c(P, cmd='ui_result', action='return')); time.sleep(3)
    for k in range(3):                                   # the screen may ask twice (units left behind)
        log(P, 'confirms', k + 1, c(P, cmd='ui_result', action='confirm')); time.sleep(4)
        if not c(P, cmd='ui_result')['open']:
            break
    wait('both on the base map', lambda: all((lambda s: s.get('map') == 0 and not s.get('busy'))(st(x)) for x in (X, Y)), timeout=900, every=3)


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', '')
    os.environ['DSL_NOTCH'] = '0'
    t_lease = time.time(); pair = rig.lease('fogfix', os.getpid())
    while not pair and time.time() - t_lease < 3600:     # pairs are shared by every lane: wait for one (1 h at most)
        time.sleep(30); pair = rig.lease('fogfix', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'Mac', 'allow_speedup': True, 'allow_pause': True, 'shared_fog': False},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        mk = {X: 0, Y: 0}
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        t0 = st(X)['tick']
        for cy in range(1, CYCLES + 1):
            # a fresh unit each dive: the unit back from dive 1 was not picked for dive 2 ("Choose at least one unit")
            f1 = next(col for col in tm(X)['columns'] if col['player'] == -1)['model'][1]
            tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
            tm(X, action='click', guid=f1, button=0); time.sleep(1.5); tm(X, action='click', guid=f1, button=1); time.sleep(1.5)
            settle(X, Y, lambda a, b: col_of(b, f1) == 0, f'X takes ({cy})', timeout=60)
            T.propose(X, [f1])
            settle(X, Y, lambda a, b: b['prop']['shown'] and b['prop']['ok'] == 'Approve', f'guest summary {cy}', timeout=90)
            tm(Y, action='approve')
            land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
            wait(f'both in the dungeon ({cy})', lambda: land(X) and land(Y), timeout=900, every=3)
            log(f'entry {cy}: both in the dungeon')
            wait('guest watches', lambda: c(Y, cmd='ui_viewer').get('viewer') == 1, timeout=120, every=2)
            c(Y, cmd='ui_viewer', action='follow')
            time.sleep(12)
            both_alive(X, Y, f'12 s after entry {cy}')
            fy, fx = c(Y, cmd='mpm_fog'), c(X, cmd='mpm_fog')
            check(f'entry {cy}: guest watches with the host\'s vision', fy.get('watcher') == 1 and fy.get('visible', 0) > 0 and fy.get('visible') == fx.get('visible'),
                  f"guest {fy} host {fx}")
            toggle_fog(X, Y, f'in dungeon {cy}')
            if cy < CYCLES:
                return_to_base(X, Y, f1)
                time.sleep(8)
                both_alive(X, Y, f'back on the base after entry {cy}')
                toggle_fog(X, Y, f'on the base after entry {cy}')
        for x in (X, Y):
            n = rig_log_since(x, mk[x]).count('fog: view cache cleared')
            check(f'{x}: new view epochs logged (map loads / changes)', n >= 2 * CYCLES, n)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick (no desync)', n >= 300 and first is None, f'common ticks {n}, first diff {first}')
        for x in (X, Y):
            m = st(x).get('mp', {})
            check(f'{x}: no desync', not m.get('desynced') and not m.get('desyncs'), {k: m.get(k) for k in ('desync', 'desynced', 'desyncs')})
        # guest quits to title (its session ends: shared-fog redraw), then the host
        pos = os.path.getsize(os.path.join(rig.idir(Y), 'rig.log'))
        log(Y, 'quit to title', c(Y, cmd='mp_quit'))
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=600)
        time.sleep(10)
        both_alive(X, Y, 'guest at the title')
        log(Y, 'fog lines', [l[-120:] for l in rig_log_since(Y, pos).splitlines() if 'fog:' in l][-6:])
        log(X, 'quit to title', c(X, cmd='mp_quit'))
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=600)
        time.sleep(10)
        both_alive(X, Y, 'host at the title')
        for x in (X, Y):
            log(x, 'blank view lines (info)', rig_log_since(x, 0).count('ERROR [view_blank]'))
    finally:
        for i in pair:
            try:
                rig.stop(i)
            except Exception as e:
                log('stop', i, e)
        rig.release(pair, 'fogfix')
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER')
        log('RESULT', 'PASS' if results and all(results.values()) and sys.exc_info()[0] is None else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
