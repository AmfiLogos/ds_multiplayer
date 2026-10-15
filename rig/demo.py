#!/usr/bin/env python3
"""Owner demo: two copies play the same dungeon in lockstep, A = host (left half), B = guest (right half).

  demo.py            hidden run (windows exist but are invisible): use this to test
  demo.py --show     VISIBLE windows, left A / right B, on desktop space id 10 (--space N) (only when the owner asks)
  demo.py --manual   same setup (host, join, claims), then no script: the owner plays in each window himself.
                     Runs until Ctrl-C (or --manual-seconds N, used for hidden tests). Popups are left to him.

Script (~3-4 min): A hosts and loads the dungeon save, B joins and gets the host's save, A claims 2 units and
B claims 2; each side sends its own units into the fight through the real input path (select + right-click,
captured and sent through the lockstep); B votes pause, then run; both vote fast, then normal; A proposes going
down a floor, B agrees, both play 1 minute on the new floor. One log line per event; fingerprints compared at the end.
Sound is off; frame rate is not capped low (60)."""
import os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig
import mp2_test as t

SHOW = '--show' in sys.argv
if SHOW:   # windows go to another desktop (managed space id; owner's last desktop = 10), never his current one
    os.environ['DSRIG_SPACE'] = sys.argv[sys.argv.index('--space') + 1] if '--space' in sys.argv else '10'
MANUAL = '--manual' in sys.argv
log = t.log
steps = {}
USER_PAUSE = [False]                     # True while the demo's own pause vote is on


def step(name, fn):
    """run one demo step; a failing step is logged and skipped (the demo goes on)"""
    try:
        r = fn()
        steps[name] = 'ok'
        log(f'[demo] {name}: ok', r if r is not None else '')
    except Exception as e:
        steps[name] = f'FAILED: {e}'
        log(f'[demo] {name}: FAILED ({e}); skipped')


POPUP_NAMES = {3: 'DungeonResultView (no player unit left in the dungeon: game over / return screen)', 11: 'TradeUI', 21: 'LevelUp'}


def popup_watch(stop):
    """Logs every popup the ui lane reports ("ui: popup N requested") with the party's state, and every pause vote.
    It releases a pause ONLY when a real popup other than 3 opened since the last release (nobody may be at the
    guest window). A pause that comes from a player's vote (the owner's own pause button) is never touched,
    and popup 3 (return screen / game over: the host chooses) is never released."""
    seen = {x: os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) for x in 'AB'}
    pending = set()
    while not stop.is_set():
        try:
            for x in 'AB':
                with open(os.path.join(rig.idir(x), 'rig.log'), errors='replace') as f:
                    f.seek(seen[x]); txt = f.read(); seen[x] += len(txt.encode('utf-8', 'replace'))
                for line in txt.splitlines():
                    if 'ui: popup ' in line and ' requested' in line:
                        pid_ = int(line.split('ui: popup ')[1].split()[0])
                        us_ = t.units('A')
                        party = [(u['name'][:8], round(u['hp']), 'DOWNED' if u.get('downed') else ('dead' if u['dead'] else 'ok'), u.get('cell'))
                                 for u in us_ if u['faction'] == 1]
                        log(f'[demo] popup {pid_} on {x}: {POPUP_NAMES.get(pid_, "?")}; party {party}')
                        if pid_ != 3:
                            pending.add(pid_)
                    elif 'mp: local ' in line:
                        log(f'[demo] {x}: {line.split("mp: ")[1].strip()} (player input, never released by the demo)')
            if pending and t.mp('A').get('paused'):
                for x in 'AB':
                    for r in (1, 4, 5):
                        t.c(x, cmd='mp_popup', reason=r, add=0)
                log(f'[demo] popup pause released (popups {sorted(pending)})')
                pending.clear()
        except Exception:
            pass
        stop.wait(3)


def make_demo_save(dst):
    """Demo-only save in the copy's own folder: the 4 player units get x10 base stats (Strength, Constitution,
    WillPower, Intelligence, Agility, Perception) so the party can clear floors. The owner's saves are only read."""
    import json as _j
    d = _j.load(open(rig.SAVE))
    pl = d['EntityContainerSaveData']['UnitContainerSaveData']['Dungeon']['PlayerGuidList']
    comps = d['EntityComponentSaveData']['EntityComponents']
    n = 0
    for g in pl:
        for c in comps.get(g, []):
            gs = c.get('Data', {}).get('GeneratedStats')
            if gs:
                for k in gs:
                    gs[k] = gs[k] * 10
                n += 1
    tmp = dst + '.tmp'; _j.dump(d, open(tmp, 'w')); os.replace(tmp, dst)
    return n


def main():
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    import json
    json.dump({'role': 'host', 'port': t.PORT, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(rig.idir('A'), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': t.PORT, 'name': 'Guest'},
              open(os.path.join(rig.idir('B'), 'mp.json'), 'w'))
    t0 = time.time()
    try:
        rig.start('A', window=True, fps=60, show='left' if SHOW else None)
        rig.wait_for('A', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title A', timeout=1500)
        work = os.path.join(rig.idir('A'), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        log(f'[demo] demo save: {make_demo_save(src)} player units with x10 base stats (demo only, in the copy folder)')
        t.c('A', cmd='load_save', file=src, det=1)
        t.wait('A in the dungeon', lambda: t.st('A').get('det', {}).get('locked'))
        log('[demo] host A is in the dungeon, tick', t.st('A')['tick'])
        rig.start('B', window=True, fps=60, show='right' if SHOW else None)
        rig.wait_for('B', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title B', timeout=1500)
        t.wait('B joined and loaded the host save', lambda: t.st('B').get('det', {}).get('locked') and t.mp('B').get('loads', 0) >= 1, timeout=900)
        log('[demo] guest B joined: same dungeon, tick', t.st('B')['tick'])
        t_start = t.st('A')['tick']
        import threading
        stop = threading.Event()
        if not MANUAL:
            threading.Thread(target=popup_watch, args=(stop,), daemon=True).start()

        us = t.units('A')
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        mine = {'A': players[:2], 'B': players[2:4]}

        def claims():
            out = []
            for x in 'AB':
                for u in mine[x]:
                    out.append((x, u['name'], t.c(x, cmd='mp_claim', guid=u['guid']).get('rc')))
            time.sleep(2)
            return out
        step('claims (A: 2 units, B: 2 units)', claims)

        if MANUAL:
            limit = float(sys.argv[sys.argv.index('--manual-seconds') + 1]) if '--manual-seconds' in sys.argv else None
            log('[demo] manual mode: A (host, left) controls ' + ', '.join(u['name'] for u in mine['A']) +
                '; B (guest, right) controls ' + ', '.join(u['name'] for u in mine['B']) + '. Ctrl-C here to stop.')
            t_m = time.time()
            try:
                while (limit is None or time.time() - t_m < limit) and rig.alive(rig.read_pid('A')) and rig.alive(rig.read_pid('B')):
                    time.sleep(10)
                    ma, mb = t.mp('A'), t.mp('B')
                    log(f'[demo] tick A {t.st("A")["tick"]} B {t.st("B")["tick"]}, speed {ma.get("speed")}, paused {ma.get("paused")}, desyncs A {ma.get("desyncs")} B {mb.get("desyncs")}')
            except KeyboardInterrupt:
                log('[demo] stopped by Ctrl-C')
            n, first = t.fp_equal_from(t_start)
            log(f'[demo] fingerprints: {n} common ticks, first difference {first}')
            return
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        # Exit portal: a player unit on it opens the return screen (popup 3) for everyone. Floor 1's portal room
        # (demo save: x 452-466, y 187-199) and, on a new floor, the arrival area (the party arrives at the portal)
        # are kept out: no enemy there is ever a target.
        avoid = [((452, 466), (187, 199))]
        def near_portal(cell, pad=6):
            return any(x0 - pad <= cell[0] <= x1 + pad and y0 - pad <= cell[1] <= y1 + pad for (x0, x1), (y0, y1) in avoid)
        def clear(label, seconds):
            """attack-move: every 12 s each side sends its own living units at the nearest living enemy away from the portal"""
            t_c = time.time(); rounds = 0
            while time.time() - t_c < seconds:
                us_ = t.units('A')
                foes = [u for u in us_ if u['faction'] == 3 and not u['dead'] and 'cell' in u and not near_portal(u['cell'])]
                if not foes:
                    return (f'{label}: no enemy left after {rounds} rounds; party '
                            f'{[(u["name"][:8], round(u["hp"]), "DOWNED" if u.get("downed") else ("dead" if u["dead"] else "ok")) for u in us_ if u["faction"] == 1]}')
                alive = {u['guid']: u for u in us_ if u['faction'] == 1 and not u['dead'] and 'cell' in u}
                for x in 'AB':
                    for u in mine[x]:
                        if u['guid'] in alive:
                            me = alive[u['guid']]
                            e = min(foes, key=lambda e: d2(me, e))
                            t.order_move(x, u['guid'], e['cell'])
                rounds += 1
                time.sleep(12)
            us_ = t.units('A')
            return (f'{label}: {sum(1 for u in us_ if u["faction"] == 3 and not u["dead"] and "cell" in u)} enemies left, '
                    f'party {[(u["name"][:8], round(u["hp"]), "DOWNED" if u.get("downed") else ("dead" if u["dead"] else "ok")) for u in us_ if u["faction"] == 1]}')
        step('clear floor 1 (attack-move, both sides)', lambda: clear('floor 1', 150))

        def pause_vote():
            USER_PAUSE[0] = True
            t.c('B', cmd='mp_pause', on=1)
            stopped = t.settle_both()
            if not stopped:
                raise RuntimeError('did not stop at the same tick')
            sb = t.st('B'); sa = t.st('A')
            p1 = {u['guid']: u.get('cell') for u in t.units('B')}
            shot = lambda n: t.c('B', cmd='screenshot', path=os.path.join(rig.idir('B'), f'pause_{n}.png'), w=480, h=270)
            shot(1); time.sleep(5); shot(2)
            p2 = {u['guid']: u.get('cell') for u in t.units('B')}
            same_png = open(os.path.join(rig.idir('B'), 'pause_1.png'), 'rb').read() == open(os.path.join(rig.idir('B'), 'pause_2.png'), 'rb').read()
            log(f'[demo] while paused: local game speed A {sa.get("speed")} B {sb.get("speed")}; B sim cells unchanged {p1 == p2}; '
                f'B camera images 5 s apart identical {same_png}')
            t.c('B', cmd='mp_pause', on=0)
            USER_PAUSE[0] = False
            t.wait('running', lambda: not t.mp('A').get('paused') and not t.mp('B').get('paused'), timeout=60)
            return f'both stopped at {stopped}, then ran again'
        step('pause vote from B', pause_vote)

        def speed_vote():
            t.c('A', cmd='mp_speed', speed=2); t.c('B', cmd='mp_speed', speed=2)
            t.wait('fast', lambda: t.mp('A').get('speed') == t.mp('B').get('speed') == 'fast 3.5x', timeout=60, every=1)
            a0 = t.st('A')['tick']; time.sleep(15); a1 = t.st('A')['tick']
            t.c('A', cmd='mp_speed', speed=1); t.c('B', cmd='mp_speed', speed=1)
            t.wait('normal', lambda: t.mp('A').get('speed') == t.mp('B').get('speed') == 'normal', timeout=60, every=1)
            return f'fast 3.5x for 15 s ({(a1 - a0) / 15:.0f} ticks/s), back to normal'
        step('speed vote: fast, then normal', speed_vote)

        def floor_vote():
            f0 = t.st('A').get('floor')
            alive = [u['guid'] for u in t.units('A') if u['faction'] == 1 and not u['dead']]
            t.inject('A', 'GoDownDungeonRequested', lguid_16=','.join(alive))
            try:
                t.wait('B votes yes', lambda: t.c('B', cmd='ui_map_vote', accept=1).get('rc') == 1 or t.st('B').get('floor') != f0, timeout=60, every=1)
            except RuntimeError:
                pass
            sa, sb = t.wait('both on the next floor', lambda: (lambda a, b: (a, b) if a.get('floor') != f0 and b.get('floor') != f0
                            and not a.get('busy') and not b.get('busy') and a.get('det', {}).get('locked') and b.get('det', {}).get('locked') else None)(t.st('A'), t.st('B')), timeout=600, every=3)
            t.wait('running on the new floor', lambda: not t.mp('A').get('paused') and not t.mp('B').get('paused'), timeout=120)
            return f'floor {f0} -> A {sa.get("floor")} / B {sb.get("floor")}, map hash A {t.c("A", cmd="trn_status").get("gen", {}).get("hash")} B {t.c("B", cmd="trn_status").get("gen", {}).get("hash")}'
        step('floor-down vote (A proposes, B agrees)', floor_vote)
        def arrival():
            cells = [u['cell'] for u in t.units('A') if u['faction'] == 1 and not u['dead'] and 'cell' in u]
            xs, ys = [c_[0] for c_ in cells], [c_[1] for c_ in cells]
            avoid[:] = [((min(xs) - 4, max(xs) + 4), (min(ys) - 4, max(ys) + 4))]
            return f'portal / arrival area on floor 2 kept out of the targets: x {avoid[0][0]}, y {avoid[0][1]}'
        step('note the arrival (portal) area of floor 2', arrival)
        step('clear part of floor 2', lambda: clear('floor 2', 90))

        n, first = t.fp_equal_from(t_start)
        ma, mb = t.mp('A'), t.mp('B')
        log(f'[demo] fingerprints: {n} common ticks, first difference {first}; desyncs A {ma.get("desyncs")} B {mb.get("desyncs")}')
        log(f'[demo] steps: {steps}; wall time {time.time() - t0:.0f} s')
    finally:
        try:
            stop.set()
        except NameError:
            pass
        if SHOW:
            log('[demo] done: windows kept open (quit them, or: python3 rig.py stop both)')
        else:
            rig.stop('A'); rig.stop('B')
        for x in 'AB':
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p):
                os.replace(p, p + '.last')
        print('prefs identical after the demo:', rig.check_prefs())


if __name__ == '__main__':
    main()
