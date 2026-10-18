#!/usr/bin/env python3
"""Take back a lent unit that is not on the current map: the message names the unit (4-player test, v0.1.15).

  takeback_pair.py --dylib PATH [--keep]

X (host) takes f0, Y takes f1. X lends f0 to Y in the base. Y sends f1 alone to the dungeon, X approves; f0 stays
in the base (map 0) while both peers look at the dungeon (map 1). X takes f0 back: Y must read
"The owner took back <name>" and X "You took back <name>", never "this unit" (the bug: the name was looked up on
the current map only)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle
results = T.results


def log_size(x):
    return os.path.getsize(os.path.join(rig.idir(x), 'rig.log'))


def log_since(x, mark):
    with open(os.path.join(rig.idir(x), 'rig.log'), errors='replace') as f:
        f.seek(mark)
        return f.read()


def ctl(x, g):
    return c(x, cmd='ui_lend', guid=g, action='state').get('controller')


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib'))
    pair = rig.lease(f'takeback-{os.getpid()}', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        free = next(col for col in tm(X)['columns'] if col['player'] == -1)['model']
        f0, f1 = free[0], free[1]
        name = next(u['name'] for u in c(X, cmd='list_units', map=0)['units'] if u['guid'] == f0)
        log('f0', f0, repr(name))
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        tm(X, action='click', guid=f0, button=0); time.sleep(1.5)
        tm(Y, action='click', guid=f1, button=0); time.sleep(1.5); tm(Y, action='click', guid=f1, button=1); time.sleep(1.5)
        settle(X, Y, lambda a, b: col_of(a, f0) == 0 and col_of(b, f1) == 1, 'takes', timeout=60)
        my = log_size(Y)
        c(X, cmd='ui_lend', guid=f0, action='lend', to=1)
        wait('lent on both', lambda: ctl(X, f0) == 1 and ctl(Y, f0) == 1, timeout=60)
        time.sleep(2)
        check('lend in the base: "The owner lent you <name>"', f'The owner lent you {name}' in log_since(Y, my), repr(name))
        T.propose(Y, [f1])                                             # f0 (lent) stays in the base
        settle(X, Y, lambda a, b: a['prop']['active'] and (a['stock']['open'] or a['prop']['shown']), 'X receives', timeout=60)
        if tm(X)['stock']['open']:
            tm(X, action='stock_next'); settle(X, Y, lambda a, b: a['prop']['shown'], 'X summary', timeout=60)
        tm(X, action='approve')
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=300, every=3)
        time.sleep(5)
        on1 = [u['guid'] for u in c(Y, cmd='list_units', map=1).get('units', [])]
        check('setup: f0 is in the base, not on the map the peers look at', f0 not in on1 and ctl(Y, f0) == 1, f'f0 on map 1: {f0 in on1}, ctl {ctl(Y, f0)}')
        mx, my = log_size(X), log_size(Y)
        c(X, cmd='ui_lend', guid=f0, action='take')
        wait('taken back on both', lambda: ctl(X, f0) == -1 and ctl(Y, f0) == -1, timeout=60)
        time.sleep(3)
        lx, ly = log_since(X, mx), log_since(Y, my)
        check('take back off the map: Friend reads "The owner took back <name>"', f'The owner took back {name}' in ly and 'took back this unit' not in ly,
              [l for l in ly.splitlines() if 'took back' in l or 'lend event' in l][-3:])
        check('take back off the map: the owner reads "You took back <name>"', f'You took back {name}' in lx and 'took back this unit' not in lx,
              [l for l in lx.splitlines() if 'took back' in l or 'lend event' in l][-3:])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) and sys.exc_info()[0] is None else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
