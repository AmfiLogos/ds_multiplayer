#!/usr/bin/env python3
"""Long base-map lockstep run (weather timers, long-lived System.Random): host + client, no orders.

  base_test.py [--minutes 15] [--save PATH]      (leases a free pair)

Host loads the base save (MoonlightBrigade autosave, map 0), the client joins through the join point, both run
at normal speed. Every minute: weather timers on both (det_weather) and mp status. At the end: per-tick fingerprints
compared (fp.log), desync counters. Leases the pair as lane "determinism-base" and releases it after."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')


def arg(n, d):
    a = sys.argv
    return type(d)(a[a.index(n) + 1]) if n in a else d


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def main():
    minutes = arg('--minutes', 15.0); save = arg('--save', SAVE)
    pair = rig.lease('determinism-base')
    assert pair, 'no free pair'
    H, C = pair[0], pair[1]
    log('pair', pair)
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    assert rig.check_prefs()
    for x, role in ((H, 'host'), (C, 'client')):
        d = {'role': role, 'name': x, 'port': rig.pair_port(x), 'allow_speedup': True, 'allow_pause': True}
        if role == 'client':
            d['host'] = '127.0.0.1'
        json.dump(d, open(os.path.join(rig.idir(x), 'mp.json'), 'w'))
    ok = False
    try:
        rig.start(H, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        c(H, cmd='load_save', file=src, det=1)
        rig.wait_for(H, lambda s: s.get('det', {}).get('locked'), 'host locked', timeout=900, every=2)
        rig.start(C, fps=30)
        rig.wait_for(C, lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1, 'client joined', timeout=1500, every=2)
        t0 = max(st(H)['tick'], st(C)['tick'])
        log('both in, map', st(H).get('map'), 'tick', t0)
        end = time.time() + minutes * 60
        while time.time() < end:
            time.sleep(60)
            wh, wc = c(H, cmd='det_weather'), c(C, cmd='det_weather')
            mh, mc = st(H).get('mp', {}), st(C).get('mp', {})
            same = {k: v for k, v in wh.items() if k != 'tick'} == {k: v for k, v in wc.items() if k != 'tick'}
            log(f'tick {wh["tick"]}/{wc["tick"]} weather host {wh.get("map0")} client {wc.get("map0")} same {same}; '
                f'desyncs {mh.get("desyncs")}/{mc.get("desyncs")} paused {mh.get("paused")}')
        # same tick: pause, settle, compare weather
        c(C, cmd='mp_pause', on=1)
        prev = None
        for _ in range(40):
            cur = (st(H)['tick'], st(C)['tick'])
            if cur == prev and cur[0] == cur[1]:
                break
            prev = cur; time.sleep(3)
        wh, wc = c(H, cmd='det_weather'), c(C, cmd='det_weather')

        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and int(p[0]) > t0:
                    out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(C)
        com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        mh, mc = st(H).get('mp', {}), st(C).get('mp', {})
        ok = first is None and len(com) > minutes * 60 * 50 and wh == wc and not mh.get('desyncs') and not mc.get('desyncs')
        log(f'RESULT {"PASS" if ok else "FAIL"}: {len(com)} common ticks, first diff {first}; weather at the same tick host {wh} client {wc}; '
            f'desyncs {mh.get("desyncs")}/{mc.get("desyncs")}')
    finally:
        rig.stop(H); rig.stop(C)
        for x in (H, C):
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p):
                os.replace(p, p + '.last')
        rig.release(pair)
        print('prefs identical:', rig.check_prefs())
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
