#!/usr/bin/env python3
"""Lockstep determinism test: two hidden copies, same save, same scripted orders on the same ticks,
different frame caps and different tick pacing; then compare the fingerprint of every tick.

  det_test.py [--save PATH] [--ticks 6000] [--fps-a 10] [--fps-b 30] [--seed 12345] [--keep] [--detail FROM TO]
  (--detail: per-entity dump to instances/X/detail.log for lock tick + FROM .. + TO)

Copy A runs a fixed 6 ticks per frame; copy B runs 0..12 ticks per frame (random), then 0..40
for the second half (a different "game speed" over wall time). Result: desync_diff.py output,
plus a short combat summary. Logs: instances/X/fp.log, trace.bin, rig.log."""
import json, os, shutil, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

DEF_SAVE = rig.SAVE


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def plan_orders(units, lock_tick):
    """Same plan for both copies: each player unit walks to the nearest enemy, then regroups, then attacks again."""
    alive = [u for u in units if not u.get('dead') and 'cell' in u]
    players = [u for u in alive if u['faction'] == 1][:4]
    enemies = [u for u in alive if u['faction'] == 3]
    out = []
    if not players:
        return out
    d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
    for i, p in enumerate(players):
        tgt = min(enemies, key=lambda e: d2(p, e)) if enemies else None
        home = p['cell']
        far = tgt['cell'] if tgt else [home[0] + 6, home[1]]
        out += [dict(tick=lock_tick + 30 + 7 * i, guid=p['guid'], x=far[0], y=far[1]),
                dict(tick=lock_tick + 2000 + 5 * i, guid=p['guid'], x=home[0], y=home[1] + (i % 2)),
                dict(tick=lock_tick + 3200 + 3 * i, guid=p['guid'], x=far[0], y=far[1]),
                dict(tick=lock_tick + 5000 + 11 * i, guid=p['guid'], x=home[0] + 1, y=home[1])]
    return out


def summary(units):
    alive = [u for u in units if 'cell' in u]
    by = {}
    for u in alive:
        f = u['faction']; s = by.setdefault(f, [0, 0, 0.0])
        s[0] += 1; s[1] += u['dead']; s[2] += max(u['hp'], 0)
    return {f'faction{f}': dict(units=v[0], dead=v[1], hp=round(v[2], 1)) for f, v in sorted(by.items())}


def main():
    save = arg('--save', DEF_SAVE); ticks = arg('--ticks', 6000); seed = arg('--seed', 12345)
    fps = {'A': arg('--fps-a', 10), 'B': arg('--fps-b', 30)}
    log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    try:
        for x in 'AB':
            rig.start(x, fps=fps[x])
        for x in 'AB':
            rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title screen', timeout=1500)
        log('both at title')
        for x in 'AB':
            work = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work, exist_ok=True)
            src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
            log(x, rig.send(x, {'cmd': 'det_seed', 'seed': seed}))
            log(x, rig.send(x, {'cmd': 'load_save', 'file': src, 'det': 1}))
        st = {}
        for x in 'AB':
            st[x] = rig.wait_for(x, lambda s: s.get('det', {}).get('locked'), 'auto-lock after load', timeout=1500, every=2)
            log(x, 'locked at tick', st[x]['tick'], 'map', st[x].get('map'), 'region', st[x].get('region'), 'floor', st[x].get('floor'))
        fa, fb = rig.send('A', {'cmd': 'fingerprint'}), rig.send('B', {'cmd': 'fingerprint'})
        log('fingerprint at lock A', fa['hash'], fa['tick'], 'B', fb['hash'], fb['tick'], 'SAME' if fa['hash'] == fb['hash'] else 'DIFFERENT')
        L = st['A']['tick']
        if st['B']['tick'] != L:
            log('lock ticks differ: A', L, 'B', st['B']['tick'])
        units = rig.send('A', {'cmd': 'list_units'})['units']
        before = summary(units)
        orders = plan_orders(units, L)
        log(f'{len(orders)} orders planned; units at lock: {before}')
        for x in 'AB':
            for o in orders:
                r = rig.send(x, dict(cmd='schedule', kind='move', **o))
                assert r.get('ok'), r
        if '--detail' in sys.argv:
            i = sys.argv.index('--detail'); a, b = int(sys.argv[i + 1]), int(sys.argv[i + 2])
            for x in 'AB':
                rig.send(x, {'cmd': 'detail', 'from': L + a, 'to': L + b})
        end = L + ticks
        rig.send('A', {'cmd': 'run', 'to': end, 'per_frame': 6, 'jitter': 0})
        rig.send('B', {'cmd': 'run', 'to': end, 'per_frame': 12, 'jitter': 1, 'jitter_seed': 7})
        t0 = time.time(); switched = False; last = 0
        while True:
            sa, sb = rig.send('A', {'cmd': 'status'}), rig.send('B', {'cmd': 'status'})
            if 'tick' not in sa or 'tick' not in sb:            # main thread busy for 60 s (machine overloaded): retry
                log('status not answered:', sa if 'tick' not in sa else sb); time.sleep(5); continue
            if not switched and sb['tick'] >= L + ticks // 2:
                rig.send('B', {'cmd': 'run', 'to': end, 'per_frame': 40, 'jitter': 1}); switched = True
                log('B switched to 0..40 ticks per frame at tick', sb['tick'])
            if time.time() - last > 15:
                log(f'A tick {sa["tick"]} frames {sa["frames"]} | B tick {sb["tick"]} frames {sb["frames"]}'); last = time.time()
            if sa['tick'] >= end and sb['tick'] >= end:
                break
            if time.time() - t0 > 7200:
                log('timeout'); break
            for x, s in (('A', sa), ('B', sb)):
                if not s['det']['locked']:
                    raise RuntimeError(f'{x} unlocked itself: {s}')
            time.sleep(2)
        log('wall seconds', round(time.time() - t0), 'A det', sa['det'], 'B det', sb['det'])
        ua, ub = rig.send('A', {'cmd': 'list_units'})['units'], rig.send('B', {'cmd': 'list_units'})['units']
        log('units at end A:', summary(ua)); log('units at end B:', summary(ub))
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'desync_diff.py')])
    finally:
        if '--keep' not in sys.argv:
            rig.stop('A'); rig.stop('B')
            print('prefs identical after run:', rig.check_prefs())


if __name__ == '__main__':
    main()
