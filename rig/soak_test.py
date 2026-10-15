#!/usr/bin/env python3
"""lan_v10 core: base soak (review finding 2). One leased pair, the base save, normal play driven by the test, the
fingerprint + deep hash + FULL-state check (the game's save, per section) on, for --minutes (default 60).

  soak_test.py [--minutes 60] [--save PATH] [--full-every 1800] [--proxy "150,30,2"] [--keep]

X hosts (mp.json), Y joins (mp.json, or through a lag proxy with --proxy "delay_ms,jitter_ms,loss_pct": then Y has no
mp.json and joins by address through rig/lagproxy.py). Y claims 2 units. Every ~15 s each player gives a move order to
one of its units through the real input path (select + right-click a nearby cell); X also orders unclaimed units.
Every 4 min the speed goes Fast then Normal; every 7 min Y pauses for 5 s. The base AI works on its own (hauling,
crafting, building). Every 5 min: a status line. At the end: desyncs, partial / full resyncs, deep and full checks,
and every FULL-STATE difference / report folder. Pass = no desync at all and full checks ran and were equal.
Results: build/core_test/soak_<time>/results.json"""
import json, os, random, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

results = []


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
        time.sleep(1)
    raise RuntimeError(f'{x}: status not answered: {s}')


def mp(x):
    return st(x).get('mp', {})


def wait(what, pred, timeout=600, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def check(name, ok, info=''):
    results.append({'name': name, 'ok': bool(ok), 'info': info})
    log(('PASS ' if ok else 'FAIL ') + name, info)
    return ok


def order_move(x, guid, cell):
    c(x, cmd='ord_hover', n=0)
    r1 = c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
    r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
    return r1.get('ok') and r2.get('ok')


def grep_log(x, pat, pos):
    with open(os.path.join(rig.idir(x), 'rig.log'), 'rb') as f:
        f.seek(pos)
        return [l for l in f.read().decode('utf8', 'replace').splitlines() if pat in l]


def main():
    minutes = arg('--minutes', 60.0)
    out = os.path.join(rig.ROOT, 'build', 'core_test', 'soak_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    pair = rig.lease('core', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    lease0 = rig._with_leases(lambda d: dict(d.get(pair) or {}))   # under xplay_run the outer process holds it
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS[pair]
    proxy = arg('--proxy', '')
    fc = arg('--full-every', 1800)
    log('pair', pair, 'port', port, 'minutes', minutes, 'full check every', fc, 'steps', 'proxy', proxy or 'none', 'dylib', rig.DYLIB)
    pp = None
    stats = {}
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': fc},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if proxy:
            if os.path.exists(yj):
                os.replace(yj, yj + '.off')
        else:
            json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben', 'full_check_steps': fc}, open(yj, 'w'))
        rig.start(X, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        save = arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
        src = os.path.join(work, 'soak_base.json'); shutil.copy2(save, src)
        log('X load', c(X, cmd='load_save', file=src, det=1))
        wait('X locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=2)
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        if proxy:
            d, j, l = (proxy.split(',') + ['0', '0'])[:3]
            lp = 7900 + (port % 100)
            pp = subprocess.Popen([sys.executable, os.path.join(rig.ROOT, 'rig', 'lagproxy.py'), str(lp), str(port), d, j, l],
                                  stdout=open(os.path.join(out, 'proxy.log'), 'w'), stderr=subprocess.STDOUT)
            time.sleep(1)
            log('Y joins through the lag proxy', lp, c(Y, cmd='mpm_join_tcp', host='127.0.0.1', port=lp, name='Ben'))
        wait('Y loaded the host save', lambda: st(Y).get('det', {}).get('locked') and mp(Y).get('loads', 0) >= 1 and not mp(Y).get('needs_state'), timeout=900, every=2)
        # Y claims 2 player units
        us = [u for u in c(X, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
        mine_y = [u['guid'] for u in us[:2]]
        for g in mine_y:
            c(Y, cmd='mp_claim', guid=g)
        time.sleep(3)
        log('Y claimed', mine_y)
        logpos = {x: os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) for x in (X, Y)}
        m0 = {x: mp(x) for x in (X, Y)}
        t0 = time.time(); next_status = t0 + 300; next_speed = t0 + 240; next_pause = t0 + 420; rnd = random.Random(7)
        orders = {X: 0, Y: 0}; timer0 = m0[Y].get('timer', 0)
        while time.time() - t0 < minutes * 60:
            # lease lost (freed / taken by another lane, e.g. the release gate): stop driving the pair. 2026-10-14 a soak
            # kept ordering units on AB after its copies were stopped and failed the gate's mp2 ownership check there.
            l_now = rig._with_leases(lambda d: dict(d.get(pair) or {}))
            if lease0.get('since') and (l_now.get('pid'), l_now.get('since')) != (lease0.get('pid'), lease0.get('since')):
                raise RuntimeError(f'lease on {pair} lost: soak stopped')
            for x in (X, Y):
                try:
                    us = [u for u in c(x, cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
                except Exception as e:                        # busy (a save / load / map change): skip this round
                    log('list_units', x, repr(e)[:120]); us = []
                cand = [u for u in us if (u['guid'] in mine_y) == (x == Y)]
                if cand:
                    u = rnd.choice(cand)
                    cell = (u['cell'][0] + rnd.randint(-6, 6), u['cell'][1] + rnd.randint(-6, 6))
                    if order_move(x, u['guid'], cell):
                        orders[x] += 1
            now = time.time()
            if now > next_speed:
                c(X, cmd='mp_speed', speed=2); c(Y, cmd='mp_speed', speed=2); time.sleep(20)
                c(X, cmd='mp_speed', speed=1); c(Y, cmd='mp_speed', speed=1); next_speed = now + 240
            if now > next_pause:
                c(Y, cmd='mp_pause', on=1); time.sleep(5); c(Y, cmd='mp_pause', on=0); next_pause = now + 420
            if now > next_status:
                my, mx = mp(Y), mp(X)
                log(f'{(now - t0) / 60:.0f} min: steps {my.get("timer", 0) - timer0}, ticks {my.get("ticks")}, desyncs {my.get("desyncs")}, partials {my.get("partials")}, '
                    f'loads {my.get("loads")}, full ok {my.get("full_ok")} fails {my.get("full_fails")} (slowest guest {my.get("full_ms_max")} ms, host {mx.get("full_ms_max")} ms), '
                    f'deep ok {my.get("deep_ok")} other {my.get("deep_other")}, orders X {orders[X]} Y {orders[Y]}')
                next_status = now + 300
            time.sleep(15)
        my, mx = mp(Y), mp(X)
        diffs = grep_log(Y, 'FULL-STATE CHECK DIFFERS', logpos[Y])
        deep = grep_log(Y, 'deep hash differs', logpos[Y])
        des = grep_log(Y, 'DESYNC:', logpos[Y])
        reports = grep_log(X, 'Desync report saved', logpos[X])
        stats = {'minutes': minutes, 'steps': my.get('timer', 0) - timer0, 'ticks_guest': my.get('ticks'), 'orders': orders,
                 'desyncs': my.get('desyncs', 0) - m0[Y].get('desyncs', 0), 'partials': my.get('partials', 0) - m0[Y].get('partials', 0),
                 'loads': my.get('loads', 0) - m0[Y].get('loads', 0), 'full_ok': my.get('full_ok', 0), 'full_fails': my.get('full_fails', 0),
                 'full_ms_max_guest': my.get('full_ms_max'), 'full_ms_max_host': mx.get('full_ms_max'), 'deep_ok': my.get('deep_ok'),
                 'deep_other': my.get('deep_other'), 'full_diffs': diffs[:20], 'deep_diffs': deep[:20], 'desync_lines': des[:20], 'reports': reports[:20]}
        for k in ('full_diffs', 'deep_diffs', 'desync_lines', 'reports'):
            for l in stats[k]:
                log(k, l[:300])
        check('soak: no desync of any kind', stats['desyncs'] == 0 and not diffs and not deep, json.dumps({k: stats[k] for k in ('desyncs', 'partials', 'loads', 'full_fails', 'deep_other')}))
        check('soak: full-state checks ran and were all equal', stats['full_ok'] >= max(1, int(minutes * 60 * 60 / fc / 2)) and stats['full_fails'] == 0,
              f'{stats["full_ok"]} equal, {stats["full_fails"]} differing, slowest {stats["full_ms_max_guest"]} / {stats["full_ms_max_host"]} ms')
        check('soak: both played (orders applied, steps advanced)', orders[X] > 10 and orders[Y] > 10 and stats['steps'] > minutes * 60 * 20,
              f'steps {stats["steps"]} in {minutes} min, orders {orders}')
    except Exception as e:
        check('run', False, repr(e))
    finally:
        npass = sum(r['ok'] for r in results)
        json.dump({'pair': pair, 'pass': npass, 'fail': len(results) - npass, 'stats': stats, 'results': results}, open(os.path.join(out, 'results.json'), 'w'), indent=1)
        for x in (X, Y):
            p = os.path.join(rig.idir(x), 'rig.log')
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(out, f'{x}_rig.log'))
        if pp:
            pp.terminate()
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj + '.off'):
            os.replace(yj + '.off', yj)
        if '--keep' not in sys.argv:
            rig.stop(X); rig.stop(Y); rig.release(pair)
        log(f'SUMMARY {npass} PASS, {len(results) - npass} FAIL; {out}')
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')


if __name__ == '__main__':
    main()
