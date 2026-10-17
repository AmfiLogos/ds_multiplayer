#!/usr/bin/env python3
"""Owner rules (lead 2026-10-13, v7): a guest's exit is a leave at once; the host pauses and gets "<name> disconnected"
(Wait / Keep playing); Keep playing -> the host orders the guest's RESERVED units; the guest rejoins mid-game ->
his units back, fingerprints equal. TCP (LAN) path, owner's menu flow (no mp.json).
  A. guest stock Quit to title (system menu path)  -> host popup data within 1 s, everyone paused
  B. Keep playing -> host order on the guest's unit applied on the host
  C. guest rejoins by address (same install = same uid) -> units back to him, his order applied, host's dropped,
     fingerprints equal 600+ ticks
  D. guest quits the app (Application.Quit hook) -> host popup within 1 s; Wait -> stays paused; guest restarts +
     rejoins -> resumes by itself
Leases a free pair (lane "mpglue")."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15)
results = {}


class SkipToE(Exception):
    pass


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = rig.lease(f'mpglue-{os.getpid()}'); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda inst, **k: rig.send(inst, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def popup_within(limit):
        t0 = time.time()
        while time.time() - t0 < 10:
            l = c(H, cmd='mp_left')
            if l.get('players'):
                return time.time() - t0, l
            time.sleep(0.05)
        return None, c(H, cmd='mp_left')

    def join_guest(port):
        if not rig.alive(rig.read_pid(G)):
            rig.start(G, fps=30)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)

    def fp_equal_from(t0):
        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and int(p[0]) > t0:
                    out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(G); com = sorted(set(a) & set(b))
        return len(com), next((t for t in com if a[t] != b[t]), None)

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    log('pair', pair)
    try:
        rig.start(H, fps=30); rig.start(G, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        join_guest(port)
        wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
        log('host presses Start (lobby closes, the owner\'s normal path)', press(H, 'START'))
        us = c(H, cmd='list_units')['units']
        mine = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u][:2]
        for u in mine:
            log('guest claims', u['name'], c(G, cmd='mp_claim', guid=u['guid']))
        time.sleep(3)
        gu = mine[0]

        if '--only-e' in sys.argv:
            raise SkipToE()
        # A. guest stock Quit to title
        r = c(G, cmd='mp_quit', mode='stock'); log('guest stock quit to title', r)
        if not r.get('ok'):
            log('system menu not built: event path'); c(G, cmd='mp_quit', mode='title')
        dt, l = popup_within(1.0)
        check('A. guest quit to title -> host popup data within 1 s', dt is not None and dt <= 1.0, f'{dt if dt is None else round(dt, 2)} s; {l}')
        time.sleep(1)
        check('A. host paused (everyone left pauses)', mp(H).get('paused'), f"host mp paused {mp(H).get('paused')}")
        sg = wait('guest on the title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st(G)), timeout=60, every=0.5)
        check('A. guest on the title, session closed', not sg.get('mp', {}).get('started'), f"guest mp {sg.get('mp')}")

        o = c(H, cmd='mp_owner', guid=gu['guid'])
        m0 = mp(H); cell = gu.get('cell', [0, 0])
        c(H, cmd='mp_move', guid=gu['guid'], x=cell[0] + 3, y=cell[1]); time.sleep(3); m1 = mp(H)
        check('A. guest\'s units RESERVED, host order refused before Keep playing', o.get('owner') == -2 and m1.get('orders_dropped_owner', 0) > m0.get('orders_dropped_owner', 0),
              f"owner {o}; dropped {m0.get('orders_dropped_owner')}->{m1.get('orders_dropped_owner')}")

        # B. Keep playing -> host orders the guest's reserved unit
        c(H, cmd='mp_left_choice', keep=1)
        wait('host running', lambda: not mp(H).get('paused'), timeout=30, every=0.5)
        time.sleep(1); m0 = mp(H)
        r = c(H, cmd='mp_move', guid=gu['guid'], x=cell[0] + 2, y=cell[1])
        time.sleep(3); m1 = mp(H)
        check('B. Keep playing: running, host order on the guest\'s reserved unit applied',
              m1.get('orders', 0) > m0.get('orders', 0) and m1.get('orders_dropped_owner') == m0.get('orders_dropped_owner') and not m1.get('paused'),
              f"orders {m0.get('orders')}->{m1.get('orders')}, dropped {m0.get('orders_dropped_owner')}->{m1.get('orders_dropped_owner')}")

        # C. guest rejoins: units back, his order applied, host's dropped, fingerprints equal
        join_guest(port)
        wait('both running after rejoin', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
        og = c(H, cmd='mp_owner', guid=gu['guid']); gslot = mp(G).get('player')
        check('C. rejoined: the guest\'s unit is his again', og.get('owner') == gslot and gslot is not None, f'owner {og}, guest slot {gslot}')
        h0, g0 = mp(H), mp(G)
        r1 = c(G, cmd='mp_move', guid=gu['guid'], x=cell[0], y=cell[1])
        time.sleep(3)
        h1 = mp(H)
        r2 = c(H, cmd='mp_move', guid=gu['guid'], x=cell[0] + 1, y=cell[1])
        time.sleep(3)
        h2 = mp(H)
        check('C. rejoined: his order on his unit applied, the host\'s now dropped',
              h1.get('orders', 0) > h0.get('orders', 0) and h2.get('orders_dropped_owner', 0) > h1.get('orders_dropped_owner', 0),
              f"host orders {h0.get('orders')}->{h1.get('orders')}, dropped {h1.get('orders_dropped_owner')}->{h2.get('orders_dropped_owner')}; {r1} {r2}")
        t0 = max(st(H)['tick'], st(G)['tick'])
        wait('600 ticks', lambda: min(st(H)['tick'], st(G)['tick']) > t0 + 600, timeout=600, every=5)
        time.sleep(3)
        n, first = fp_equal_from(t0)
        check('C. fingerprints equal after the rejoin', n >= 600 and first is None and not mp(G).get('desyncs'), f'{n} common ticks, first diff {first}')

        # D. guest quits the app -> popup; Wait -> stays paused; restart + rejoin -> resumes
        log('guest app quit', c(G, cmd='mp_quit', mode='app'))
        dt, l = popup_within(1.0)
        check('D. guest app quit -> host popup data within 1 s', dt is not None and dt <= 1.0, f'{dt if dt is None else round(dt, 2)} s; {l}')
        c(H, cmd='mp_left_choice', keep=0)
        time.sleep(5)
        l = c(H, cmd='mp_left')
        check('D. Wait: host stays paused', mp(H).get('paused') and l.get('waiting') == 1, f"paused {mp(H).get('paused')} {l}")
        wait('guest process gone', lambda: not rig.alive(rig.read_pid(G)), timeout=60, every=1)
        join_guest(port)
        wait('running after the rejoin', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180, every=1)
        check('D. guest rejoined after Wait: game resumed by itself, nobody waited for', not c(H, cmd='mp_left').get('players'), str(c(H, cmd='mp_left')))

    except SkipToE:
        pass
    except Exception as e:
        check('flow completed', False, repr(e))
        rig.stop(H); rig.stop(G); rig.release(pair); log('RESULTS', json.dumps(results)); return
    try:
        # E. owners go with the world: host saves, leaves, hosts a new session from that save -> RESERVED for the guest
        log('host save', c(H, cmd='mp_save', name='mp_owner_test'))
        c(H, cmd='mp_steam_leave'); time.sleep(3)
        rig.stop(G); rig.stop(H); rig.start(H, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500); time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        c(H, cmd='load_save', file=os.path.join(rig.idir(H), 'data', 'Saves', 'mp_owner_test.json'), det=0)
        wait('new host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)), timeout=600)
        time.sleep(3)
        o = c(H, cmd='mp_owner', guid=gu['guid'])
        check('E. new session from the saved world: the guest\'s unit is RESERVED for him', o.get('owner') == -2 and o.get('name'), str(o))
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
