#!/usr/bin/env python3
"""Frame-rate check of one copy during the ElvenExpedition fight, without lockstep.

  perf_test.py [--mode plain|det] [--ticks 6000] [--fps 10] [--x A]

plain: load_save without det (the game's own clock, mod hooks pass through: closest to stock that the rig
       allows; a copy without the rig dylib would write into the owner's save folder).
det:   load_save det:1, then `run` 6 ticks per frame (det_test's pacing).
Same 16 move orders as det_test.py (rig order_move at the same game ticks). Prints frames/s and ticks/s
every 20 s. Output: det_out/perf_<mode>.json"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig
from det_test import plan_orders


def arg(n, d):
    a = sys.argv
    return type(d)(a[a.index(n) + 1]) if n in a else d


def main():
    mode = arg('--mode', 'plain'); ticks = arg('--ticks', 6000); fps = arg('--fps', 10); x = arg('--x', 'A')
    log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
    assert rig.check_prefs()
    p = os.path.join(rig.idir(x), 'mp.json')
    if os.path.exists(p):
        os.replace(p, p + '.last')
    samples = []
    try:
        rig.start(x, fps=fps)
        rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        work = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        rig.send(x, {'cmd': 'load_save', 'file': src, 'det': 1 if mode == 'det' else 0})
        s = rig.wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0
                         and (mode != 'det' or s.get('det', {}).get('locked')), 'loaded', timeout=600, every=2)
        L = s['tick']
        orders = plan_orders(rig.send(x, {'cmd': 'list_units'})['units'], L)
        if mode == 'det':
            for o in orders:
                rig.send(x, dict(cmd='schedule', kind='move', **o))
            rig.send(x, {'cmd': 'run', 'to': L + ticks, 'per_frame': 6})
        else:
            rig.send(x, {'cmd': 'set_speed', 'speed': 1})
        pending = sorted(orders, key=lambda o: o['tick'])
        t0 = time.time(); last = rig.send(x, {'cmd': 'status'}); tl = time.time()
        while True:
            time.sleep(20)
            s = rig.send(x, {'cmd': 'status'}); now = time.time()
            if mode != 'det':
                while pending and s['tick'] >= pending[0]['tick']:
                    o = pending.pop(0); rig.send(x, {'cmd': 'order_move', 'guid': o['guid'], 'x': o['x'], 'y': o['y']})
            fr = (s['frames'] - last['frames']) / (now - tl); tr = (s['tick'] - last['tick']) / (now - tl)
            samples.append(dict(t=round(now - t0), tick=s['tick'] - L, fps=round(fr, 2), tps=round(tr, 1)))
            log(f'{mode}: tick +{s["tick"] - L} fps {fr:.1f} ticks/s {tr:.1f}')
            last, tl = s, now
            if s['tick'] - L >= ticks or now - t0 > 1500:
                break
        us = rig.send(x, {'cmd': 'list_units'})['units']
        log('end units', [(u['name'][:8], round(u['hp'])) for u in us if u['faction'] == 1])
    finally:
        rig.stop(x)
        json.dump(samples, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'det_out', f'perf_{mode}{"_minimal" if os.environ.get("DSRIG_MINIMAL") else ""}.json'), 'w'), indent=1)
        print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
