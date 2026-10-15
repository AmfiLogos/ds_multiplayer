#!/usr/bin/env python3
"""See-through steps (owner 2026-10-15): Off, 30, 50, 70, 90 %, cycling both ways, on a leased pair.

  seethrough_pair.py --dylib PATH [--keep]

X hosts the save (det on), Y joins; each claims 2 player units. X walks the cog row both ways; each step logs the
real SpriteRenderer alpha of the units X does not control (ui_view other_alpha). Checks: alpha = 1 - step/100
(Off = 1.0) on X, Y untouched, fingerprints equal (view only)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'ui_test'))
import rig
import result_pair as rp
from result_pair import log, c, st, wait, check, arg, fp_equal_from

PORT = 7813


def main():
    rig.DYLIB = arg('--dylib', rig.DYLIB)
    pair = rig.lease('seethrough', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    for i in (X, Y):
        if rig.alive(rig.read_pid(i)):
            rig.release(pair)
            raise SystemExit(f'{i} is still running: not mine, pair released')
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': PORT, 'name': 'A', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'B'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):
            d = os.path.join(rig.idir(i), 'data', 'mp_owners')
            if os.path.isdir(d):
                os.rename(d, d + time.strftime('.prev_%Y%m%d_%H%M%S'))
            os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
            open(os.path.join(rig.idir(i), 'data', 'mp_ui.json'), 'w').write('{"see_through":30,"names":1,"list_open":1}\n')
        rig.start(X, fps=10)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        us = c(X, cmd='list_units')['units']
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        for x, mine in ((X, players[:2]), (Y, players[2:4])):
            for u in mine:
                log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
        time.sleep(8)
        t0 = st(X).get('tick', 0)

        def view(step):
            time.sleep(3)
            va, vb = c(X, cmd='ui_view'), c(Y, cmd='ui_view')
            log(f'step {step}: A other_units {va.get("other_units")} alpha {va.get("other_alpha")} solid {va.get("other_solid")} | B see_through 30 alpha {vb.get("other_alpha")} solid {vb.get("other_solid")}')
            want = 1.0 if step == 0 else 1.0 - step / 100
            lo, hi = va.get('other_alpha', [-1, -1])
            check(f'A step {step}: other units alpha {want:.2f}', va.get('other_units', 0) >= 2 and abs(lo - want) < 0.01 and abs(hi - want) < 0.01, f'{lo} {hi}')
            blo, bhi = vb.get('other_alpha', [-1, -1])
            check(f'B untouched at 30 % while A is at {step}', abs(blo - 0.7) < 0.01 and abs(bhi - 0.7) < 0.01, f'{blo} {bhi}')
            return va

        va = view(30)
        nxt = [50, 70, 90, 0, 30]
        for s in nxt:                                            # next: row 2 click
            r = c(X, cmd='ui_click', row=2)
            check(f'next -> {s}', r.get('see_through') == s, str(r))
            view(s)
        for s in [0, 90, 70, 50, 30]:                            # prev: the "<" arrow
            r = c(X, cmd='ui_click', row=2, dir=-1)
            check(f'prev -> {s}', r.get('see_through') == s, str(r))
            view(s)
        time.sleep(30)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal (view only, 0 desync)', n >= 100 and first is None, f'common ticks {n}, first diff {first}')
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        ok = rp.results and all(rp.results.values()) and sys.exc_info()[0] is None
        log('RESULT', 'PASS' if ok else 'FAIL', json.dumps(rp.results))


if __name__ == '__main__':
    main()
