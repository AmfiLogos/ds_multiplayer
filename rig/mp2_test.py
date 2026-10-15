#!/usr/bin/env python3
"""First real 2-player lockstep test (joined dylib: rig + det + mp + dsnet + ord + ui).

  mp2_test.py [--save PATH] [--ticks 6000] [--fps-a 10] [--fps-b 30] [--keep]

A hosts (instances/A/mp.json, TCP 127.0.0.1:7777) and loads the save (det on, auto-lock).
B joins from its title screen; the host's save reaches B through the join point.
A claims 2 player units, B claims the other 2. Each side gives orders ONLY to its own units, through
the capture path (ord_inject of the input events: select unit, right-click a cell -> ord capture ->
dsnet -> replay on both). Then: pause vote from B, speed votes, an order from B on A's unit, a forced
desync on B (det_corrupt) -> auto pause -> resync from the host's save -> equal again.
Every phase is checked on both copies; per-tick fingerprints are compared with desync_diff.py."""
import json, os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

# MP2_PAIR=GH (env) runs the test on another pair: 'A' / 'B' in this file are mapped to those instances.
_PAIR = os.environ.get('MP2_PAIR', 'AB')[:2]
if _PAIR != 'AB':
    _T = {'A': _PAIR[0], 'B': _PAIR[1]}
    def _wrap(f):
        return lambda x, *a, **k: f(_T.get(x, x), *a, **k)
    for _n in ('send', 'start', 'stop', 'wait_for', 'idir', 'read_pid', 'pair_port'):
        setattr(rig, _n, _wrap(getattr(rig, _n)))

PORT = rig.pair_port('A')
results = {}


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def c(inst, **k):
    return rig.send(inst, k, timeout=120)


def st(x):
    for _ in range(10):
        s = c(x, cmd='status')
        if 'tick' in s:
            return s
        time.sleep(3)
    raise RuntimeError(f'{x}: status not answered: {s}')


def mp(x):
    return st(x).get('mp', {})


PORTAL = {'reached': 0, 'busy': False}


def handle_portal():
    """A unit reached the exit portal: the return screen (popup 3) is open on both and pauses the game. The chooser
    (the host when the unit is unclaimed) presses Return and confirms, as a player would (mpui's ui_result command);
    both land on the base map and the test goes on there. Returns 1 if it handled one."""
    if PORTAL['busy']:
        return 0
    try:
        ra = rig.send('A', {'cmd': 'ui_result'}, timeout=30)
    except Exception:
        return 0
    if not ra.get('open'):
        return 0
    PORTAL['busy'] = True
    try:
        chooser = 'A' if ra.get('chooser', 0) in (0, -1, -2) else 'B'
        PORTAL['reached'] += 1
        log(f'portal reached (return screen open, chooser {chooser}): pressing Return and confirming')
        rig.send(chooser, {'cmd': 'ui_result', 'action': 'return'}, timeout=60); time.sleep(3)
        for _ in range(3):
            rig.send(chooser, {'cmd': 'ui_result', 'action': 'confirm'}, timeout=60); time.sleep(4)
            if not rig.send(chooser, {'cmd': 'ui_result'}, timeout=30).get('open'):
                break
        t0 = time.time()
        while time.time() - t0 < 900:
            a, b = rig.send('A', {'cmd': 'status'}, timeout=60), rig.send('B', {'cmd': 'status'}, timeout=60)
            if a.get('map') == 0 and b.get('map') == 0 and not a.get('busy') and not b.get('busy') and a.get('det', {}).get('locked') and b.get('det', {}).get('locked'):
                log('portal: both on the base map, tick', a.get('tick'), b.get('tick'))
                break
            time.sleep(3)
        return 1
    finally:
        PORTAL['busy'] = False


def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        if handle_portal():
            t0 = time.time()                                       # the return flow took its own time
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, info)


def settle_both():
    """both copies stopped at the same tick (the slower copy catches up to the paused step first)"""
    prev = None
    for _ in range(60):
        cur = (st('A')['tick'], st('B')['tick'])
        if cur == prev and cur[0] == cur[1]:
            return cur
        prev = cur; time.sleep(3)
    return None


def saves_snapshot(x):
    d = os.path.join(rig.idir(x), 'data', 'Saves')
    return {f: os.path.getmtime(os.path.join(d, f)) for f in os.listdir(d) if f.endswith('.json') and not f.startswith('mp_sync')} if os.path.isdir(d) else {}


def inject(x, ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}
    d.update({k.replace('_', '@'): v for k, v in f.items()})
    return c(x, **d)


def order_move(x, guid, cell):
    """The real input path: select the unit, right-click a cell (captured by ord, sent through dsnet).
    ord_hover first: batchmode has no cursor UI to register an interaction option (and a reload drops it)."""
    c(x, cmd='ord_hover', n=0)
    r1 = inject(x, 'SelectPlayerUnitRequested', i32_16=0, guid_20=guid)
    r2 = inject(x, 'InteractGridRequested', v2_16=f'{cell[0]},{cell[1]}', u8_24=0)
    return r1.get('ok') and r2.get('ok')


def units(x):
    return c(x, cmd='list_units')['units']


def summary(us):
    by = {}
    for u in us:
        if 'cell' not in u:
            continue
        s = by.setdefault(u['faction'], [0, 0, 0.0]); s[0] += 1; s[1] += u['dead']; s[2] += max(u['hp'], 0)
    return {f'f{f}': (v[0], v[1], round(v[2], 1)) for f, v in sorted(by.items())}


def fp_equal_from(t_from):
    """Compare A and B fp.log lines for ticks >= t_from. Returns (common, first_diff)."""
    def rd(x):
        d = {}
        for line in open(os.path.join(rig.idir(x), 'fp.log')):
            p = line.split()
            if len(p) >= 5 and int(p[0]) >= t_from:
                d[int(p[0])] = p[1]
        return d
    a, b = rd('A'), rd('B')
    com = sorted(set(a) & set(b))
    first = next((t for t in com if a[t] != b[t]), None)
    return len(com), first


def main():
    save = arg('--save', ''); ticks = arg('--ticks', 6000)
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    json.dump({'role': 'host', 'port': PORT, 'name': 'A', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(rig.idir('A'), 'mp.json'), 'w'))
    # partial_resync off: this test keeps proving the full reload path (the partial one: desync_partial_test.py)
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'B', 'partial_resync': False},
              open(os.path.join(rig.idir('B'), 'mp.json'), 'w'))
    try:
        rig.start('A', fps=arg('--fps-a', 10))
        rig.wait_for('A', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title A', timeout=1500)
        work = os.path.join(rig.idir('A'), 'save_in'); os.makedirs(work, exist_ok=True)
        if save:
            src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        else:                                                  # default: the demo's x10 save (shorter fights, less chasing)
            from demo import make_demo_save
            src = os.path.join(work, 'demo_buffed.json'); log('x10 demo save:', make_demo_save(src), 'player units buffed')
        log('A load', c('A', cmd='load_save', file=src, det=1))
        wait('A locked', lambda: st('A').get('det', {}).get('locked'))
        log('A locked at tick', st('A')['tick'], 'mp', mp('A'))
        rig.start('B', fps=arg('--fps-b', 30))
        rig.wait_for('B', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title B', timeout=1500)
        log('B at title, mp', mp('B'))
        sb = wait('B loaded the host save and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st('B')), timeout=900)
        log('B locked at tick', sb['tick'], 'mp', sb.get('mp'))
        time.sleep(5)
        ma, mb = mp('A'), mp('B')
        check('join: B loaded the host save via the join point', mb.get('loads', 0) >= 1 and not mb.get('desynced'), f'A {ma} B {mb}')

        us = units('A')
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        # exit portal room of this save's floor 0 (x 452-466, y 187-199) + margin: a player unit on it opens the
        # return screen (popup 3, the host chooses) and stops the test; enemies there are never targets
        near_portal = lambda c_: 446 <= c_[0] <= 472 and 181 <= c_[1] <= 205
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u and not near_portal(u['cell'])]
        mine = {'A': players[:2], 'B': players[2:4]}
        for x in 'AB':
            for u in mine[x]:
                log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
            c(x, cmd='ord_hover', n=0)                 # batchmode: the cursor UI never registers an option
        time.sleep(3)
        t_start = st('A')['tick']

        # ---- fight: each side orders only its own units, by the capture path
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        plan = []
        for x in 'AB':
            for u in mine[x]:
                e = min(enemies, key=lambda e: d2(u, e))
                plan.append((x, u, e['cell'], u['cell']))
        end = t_start + ticks
        phase = 0
        last = 0
        stuck = [0, time.time()]
        popup_closed = [0]
        while True:
            sa = st('A')
            done = sa['tick'] - t_start
            if phase == 0 or (phase == 1 and done > ticks * 0.35) or (phase == 2 and done > ticks * 0.55) or (phase == 3 and done > ticks * 0.75):
                for x, u, far, home in plan:
                    tgt = far if phase in (0, 2) else home
                    ok = order_move(x, u['guid'], tgt)
                    log(f'phase {phase}: {x} orders {u["name"][:10]} -> {tgt}: {"sent" if ok else "INJECT FAILED"}')
                phase += 1
            if time.time() - last > 20:
                log(f'A tick {sa["tick"]} mp {sa.get("mp", {}).get("timer")} | B tick {st("B")["tick"]}'); last = time.time()
            if sa['tick'] >= end:
                break
            if handle_portal():
                stuck[:] = [st('A')['tick'], time.time()]; continue
            if sa['tick'] != stuck[0]:
                stuck[:] = [sa['tick'], time.time()]
            elif time.time() - stuck[1] > 15 and popup_closed[0] < 5:
                # a game popup with a time lock (level-up after the fight) voted pause on both: in batchmode
                # nobody clicks it, so the test plays the player closing it (reason 4 = PopupTimeLock)
                for x in 'AB':
                    log(x, 'popup pause release', c(x, cmd='mp_popup', reason=4, add=0))
                popup_closed[0] += 1
                stuck[1] = time.time()
            elif time.time() - stuck[1] > 40:
                log('NO TICKS FOR 40 s: A', mp('A'), 'B', mp('B'))
                raise RuntimeError('world stopped during the fight')
            time.sleep(2)
        time.sleep(3)
        ma, mb = mp('A'), mp('B')
        oa, ob = c('A', cmd='ord_stats'), c('B', cmd='ord_stats')
        log('ord A', oa); log('ord B', ob)
        ua, ub = summary(units('A')), summary(units('B'))
        log('units A', ua, 'B', ub)
        n, first = fp_equal_from(t_start)
        check('fight: fingerprints equal every tick', n > ticks * 0.9 and first is None, f'common ticks {n}, first diff {first}')
        check('fight: orders went through capture and dsnet', oa.get('captured', 0) >= 4 and ob.get('captured', 0) >= 4 and ma.get('orders', 0) >= 8 and ma.get('orders') == mb.get('orders'),
              f'captured A {oa.get("captured")} B {ob.get("captured")}, applied A {ma.get("orders")} B {mb.get("orders")}')
        hp0 = summary(us)
        # same tick on both: pause, let the slower copy reach the paused step, then read every unit
        c('B', cmd='mp_pause', on=1)
        same = settle_both()
        la, lb = units('A'), units('B')
        oa_, ob_ = mp('A'), mp('B')                            # owner tables (dsnet_owner_hashes), same paused step
        check('ownership tables identical on both at the same tick', oa_.get('own_hash') is not None and
              (oa_.get('own_hash'), oa_.get('item_hash'), oa_.get('n_own')) == (ob_.get('own_hash'), ob_.get('item_hash'), ob_.get('n_own')),
              f"A own {oa_.get('own_hash')} items {oa_.get('item_hash')} n {oa_.get('n_own')}; B own {ob_.get('own_hash')} items {ob_.get('item_hash')} n {ob_.get('n_own')}")
        key = lambda us_: sorted((u['guid'], u.get('cell'), u['hp'], u['dead'], u['task']) for u in us_)
        check('fight: units identical on both at the same tick (paused)', same is not None and key(la) == key(lb),
              f'tick {same}; A {summary(la)} B {summary(lb)}')
        c('B', cmd='mp_pause', on=0)
        wait('running again after the HP read', lambda: not mp('A').get('paused') and not mp('B').get('paused'), timeout=120)
        sla, slb = summary(la), summary(lb)                    # read at the same paused tick above
        check('fight: combat happened (enemy HP lost, equal on both at the same tick)', sla.get('f3', (0, 0, 0))[2] < hp0['f3'][2] and sla == slb,
              f'start {hp0} at tick {same}: A {sla} B {slb}')
        check('fight: no desync flagged', not ma.get('desyncs') and not mb.get('desyncs'), f'{ma.get("desyncs")} {mb.get("desyncs")}')

        def settle():
            """both copies stopped at the same tick (the slower copy catches up to the paused step first)"""
            prev = None
            for _ in range(40):
                cur = (st('A')['tick'], st('B')['tick'])
                if cur == prev and cur[0] == cur[1]:
                    return cur
                prev = cur; time.sleep(3)
            return None
        # ---- pause vote from B stops both
        log('B pause vote', c('B', cmd='mp_pause', on=1))
        stopped = settle_both()
        check('pause: B vote stops both at the same tick', stopped is not None and mp('A').get('paused') and mp('B').get('paused'), f'stopped at {stopped}')
        log('B run vote', c('B', cmd='mp_pause', on=0))
        time.sleep(4)
        t3a = st('A')['tick']; time.sleep(3)
        ok = wait('both running', lambda: not mp('A').get('paused') and not mp('B').get('paused'), timeout=60, every=1)
        t3a = st('A')['tick']; time.sleep(6)
        check('pause: B run vote resumes both', ok and st('A')['tick'] > t3a, f"A {mp('A').get('votes')}")

        # ---- speed vote (two speeds only: 1 normal, 2 fast 3.5x): the lowest vote wins
        c('A', cmd='mp_speed', speed=2); c('B', cmd='mp_speed', speed=1)
        ok1 = wait('normal on both (A fast, B normal)', lambda: mp('A').get('speed') == mp('B').get('speed') == 'normal', timeout=90, every=1)
        c('B', cmd='mp_speed', speed=2)
        ok2 = wait('fast 3.5x on both (both fast)', lambda: mp('A').get('speed') == mp('B').get('speed') == 'fast 3.5x', timeout=90, every=1)
        check('speed: A fast + B normal -> normal on both; both fast -> fast 3.5x on both', ok1 and ok2, '')
        def rate(secs=10):
            a0, b0, t0 = st('A')['tick'], st('B')['tick'], time.time(); time.sleep(secs)
            a1, b1, t1 = st('A')['tick'], st('B')['tick'], time.time()
            return round((a1 - a0) / (t1 - t0), 1), round((b1 - b0) / (t1 - t0), 1)
        time.sleep(3); rf = rate()
        c('A', cmd='mp_speed', speed=1); c('B', cmd='mp_speed', speed=1)
        wait('normal again', lambda: mp('A').get('speed') == mp('B').get('speed') == 'normal', timeout=90, every=1)
        time.sleep(3); rn = rate()
        check('speed rate: normal ~60 and fast ~210 game ticks per real second on both copies',
              all(48 <= r <= 72 for r in rn) and all(170 <= r <= 250 for r in rf), f'normal A,B {rn}; fast A,B {rf}')

        # ---- B orders A's unit: refused (capture path: blocked on B; direct mp order: dropped on both)
        ua0 = mine['A'][0]
        cap0 = c('B', cmd='ord_stats').get('captured', 0)
        da0, db0 = mp('A').get('orders_dropped_owner', 0), mp('B').get('orders_dropped_owner', 0)
        oa0, ob0 = mp('A').get('orders', 0), mp('B').get('orders', 0)
        order_move('B', ua0['guid'], enemies[0]['cell'])
        r = c('B', cmd='mp_move', guid=ua0['guid'], x=enemies[0]['cell'][0], y=enemies[0]['cell'][1])
        wait('the refused order reached both', lambda: mp('A').get('orders_dropped_owner', 0) > da0 and mp('B').get('orders_dropped_owner', 0) > db0, timeout=120, every=2)
        ma, mb = mp('A'), mp('B')
        check('ownership: B order on A unit refused on both',
              ma.get('orders_dropped_owner', 0) == da0 + 1 and mb.get('orders_dropped_owner', 0) == db0 + 1 and ma.get('orders') == oa0 and mb.get('orders') == ob0,
              f'mp_move rc {r}; dropped A {da0}->{ma.get("orders_dropped_owner")} B {db0}->{mb.get("orders_dropped_owner")}; '
              f'applied A {oa0}->{ma.get("orders")} B {ob0}->{mb.get("orders")}; B captures {cap0}->{c("B", cmd="ord_stats").get("captured")}')

        # ---- map changes by vote: A proposes (UI event -> ord -> ui_map_propose), B accepts, both change map
        def map_change(name, ev, fields, want):
            c('A', cmd='mp_speed', speed=2); c('B', cmd='mp_speed', speed=2)          # fast before: must be normal after
            wait('fast before ' + name, lambda: mp('A').get('speed') == mp('B').get('speed') == 'fast 3.5x', timeout=90, every=1)
            s0a, s0b = saves_snapshot('A'), saves_snapshot('B')
            f0 = (st('A').get('map'), st('A').get('floor'))
            log(name, 'A proposes', inject('A', ev, **fields))
            try:
                wait('B voted', lambda: c('B', cmd='ui_map_vote', accept=1).get('rc') == 1 or (st('B').get('map'), st('B').get('floor')) != f0, timeout=60, every=1)
            except RuntimeError:
                log('B had no open prompt')
            ok = True; info = ''
            try:
                sa2 = wait('both changed map and running', lambda: (lambda a, b: (a, b) if want(a) and want(b) and a.get('det', {}).get('locked')
                           and b.get('det', {}).get('locked') and not a.get('busy') and not b.get('busy') else None)(st('A'), st('B')), timeout=900, every=3)
                wait('unpaused after ' + name, lambda: not mp('A').get('paused') and not mp('B').get('paused'), timeout=180)
                ta, tb = c('A', cmd='trn_status'), c('B', cmd='trn_status')
                sp = (mp('A').get('speed'), mp('B').get('speed'))
                t0 = max(st('A')['tick'], st('B')['tick'])
                wait('1000 ticks after ' + name, lambda: min(st('A')['tick'], st('B')['tick']) >= t0 + 1000, timeout=1800, every=5)
                n, first = fp_equal_from(t0 + 1)
                s1a, s1b = saves_snapshot('A'), saves_snapshot('B')
                auto_a = [f for f in s1a if s1a[f] != s0a.get(f)]; auto_b = [f for f in s1b if s1b[f] != s0b.get(f)]
                info = (f'map/floor {f0} -> {(sa2[0].get("map"), sa2[0].get("floor"))} on A, {(sa2[1].get("map"), sa2[1].get("floor"))} on B; '
                        f'gen hash A {ta.get("gen", {}).get("hash")} B {tb.get("gen", {}).get("hash")}; equal ticks after {n}, first diff {first}; '
                        f'speed after {sp}; saves written A {auto_a} B {auto_b}')
                ok = (sa2[0].get('map'), sa2[0].get('floor')) == (sa2[1].get('map'), sa2[1].get('floor')) and n >= 1000 and first is None
                check(name + ': both changed, fingerprints equal 1000 ticks after', ok, info)
                check(name + ': speed back to normal on both', sp == ('normal', 'normal'), f'{sp}')
                check(name + ': autosave only on A (host)', not auto_b, f'A {auto_a} B {auto_b}')
            except RuntimeError as e:
                check(name + ': both changed, fingerprints equal 1000 ticks after', False, f'{e}; A {st("A")} B {st("B")}')
        guids = ','.join(u['guid'] for u in players)
        region = st('A').get('region', 1)
        if st('A').get('map') != 1:
            check('floor vote: skipped (the party left through the portal, now on base)', True, '')
        else:
          map_change('floor vote', 'GoDownDungeonRequested', dict(lguid_16=guids), lambda s_: s_.get('map') == 1 and s_.get('floor', 0) >= 1)
        if st('A').get('map') != 1:
            check('return-to-base vote: skipped (already on base)', True, '')
        else:
          map_change('return-to-base vote', 'ChangeMapRequested',
                   dict(i32_16=1, i32_20=0, i32_24=region, lguid_32=guids, i32_40=0), lambda s_: s_.get('map') == 0)

        # ---- forced desync on B -> auto pause -> resync from the host's save -> equal again
        loads0 = mp('B').get('loads', 0)
        log('corrupt B', c('B', cmd='det_corrupt'))
        sb = wait('B flagged desync', lambda: (lambda m: m if m.get('desyncs', 0) >= 1 else None)(mp('B')), timeout=120, every=1)
        log('B desync flagged', sb)
        sb = wait('B reloaded from host', lambda: (lambda s: s if s.get('mp', {}).get('loads', 0) > loads0 and s.get('det', {}).get('locked')
                                                     and not s['mp'].get('desynced') and not s['mp'].get('loading') else None)(st('B')), timeout=900)
        log('B resynced', sb.get('mp'))
        try:
            wait('running again', lambda: not mp('A').get('paused') and not mp('B').get('paused'), timeout=120)
        except RuntimeError:
            log('still paused after resync: A', mp('A'), 'B', mp('B')); raise
        tr = max(st('A')['tick'], st('B')['tick'])
        time.sleep(30)
        n, first = fp_equal_from(tr + 1)
        ma, mb = mp('A'), mp('B')
        check('resync: equal again after the reload', n > 200 and first is None and not ma.get('desynced') and not mb.get('desynced'),
              f'common ticks after resync {n}, first diff {first}, desyncs A {ma.get("desyncs")} B {mb.get("desyncs")}')
    finally:
        log('RESULTS', json.dumps(results))
        if '--keep' not in sys.argv:
            rig.stop('A'); rig.stop('B')
        for x in 'AB':
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p) and '--keep' not in sys.argv:
                os.replace(p, p + '.last')
        print('prefs identical after run:', rig.check_prefs())


if __name__ == '__main__':
    main()
