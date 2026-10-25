#!/usr/bin/env python3
"""Owner rule (2026-10-20): a guest whose own link to the host drops (receive silence, no BYE) does NOT go to the title:
"Connection lost, rejoining..." + Cancel, and it rejoins by itself (same uid: its units back) for up to 60 s.
TCP (LAN) path, owner's menu flow (no mp.json). One hidden pair. Fails on the old code (guest went to the title at 5 s).
  1. host frozen 15 s (SIGSTOP / SIGCONT) mid-game -> guest stays in the world, shows the line, rejoins by itself;
     its claimed unit is its own again; 0 desyncs and equal fingerprints for 2 min after
  1b. host frozen again, guest presses Cancel on the line -> title at once, no "connection lost" notice
  2. host Esc > Leave (End for everyone) -> guest on the title with "The host left the game", no auto rejoin
  3. host killed for good -> guest keeps trying ~60 s in the world, then the title + "Connection to the host lost"
Usage: autorejoin_pair.py [--dylib build/dsrig.dylib] [--only 1|2|3]"""
import json, os, signal, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, LEAVE=12, JOIN_ADDR=14, CONNECT=15)
results = {}
if '--dylib' in sys.argv:
    rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1])
ONLY = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = rig.lease(f'autorejoin-{os.getpid()}'); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    log('pair', pair, 'dylib', rig.DYLIB)
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')

    def sst(x):
        try:
            return st(x)
        except Exception:
            return {}
    mp = lambda x: st(x).get('mp', {})
    ar = lambda s: (s.get('ar') or s.get('mp') or {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    from demo import make_demo_save
    work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
    port = [0]

    def host_session():
        if not rig.alive(rig.read_pid(H)):
            rig.start(H, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port[0] = c(H, cmd='mpm_status').get('port')

    def join_guest():
        if not rig.alive(rig.read_pid(G)):
            rig.start(G, fps=30)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port[0]}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        hs = c(H, cmd='mpm_status').get('state', '')
        if 'LOBBY' in hs.upper():
            log('host Start (lobby, state %s)' % hs, press(H, 'START'))
        wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)

    def fp_equal_from(t0):
        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and p[0].isdigit() and int(p[0]) > t0:
                    out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(G); com = sorted(set(a) & set(b))
        return len(com), next((t for t in com if a[t] != b[t]), None)

    def watch_guest(secs, stop=None):
        """samples the guest for secs: titles seen, rejoin states, box texts"""
        seen = dict(title=False, states=set(), texts=set(), first_ar=None)
        t0 = time.time()
        while time.time() - t0 < secs:
            s = sst(G)
            # the title with no session (a join-point load passes scene 1 under its loading screen: not a title)
            if s.get('scene') == 1 and not ar(s).get('autorejoin') and not (s.get('mp') or {}).get('started'):
                seen['title'] = True
            a = ar(s).get('autorejoin')
            if a is not None:
                seen['states'].add(a)
                if a and seen['first_ar'] is None:
                    seen['first_ar'] = round(time.time() - t0, 1)
            if a:
                try:
                    bx = c(G, cmd='mpm_status').get('box', {})
                    if bx.get('on'):
                        seen['texts'].add(bx.get('text', '')[:120])
                except Exception:
                    pass
            if stop and stop(s):
                break
            time.sleep(0.5)
        return seen

    hpid = lambda: rig.read_pid(H)
    try:
        host_session()
        join_guest()
        us = c(H, cmd='list_units')['units']
        mine = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u][:1]
        for u in mine:
            log('guest claims', u['name'], c(G, cmd='mp_claim', guid=u['guid']))
        time.sleep(3)
        gu = mine[0]
        gslot = mp(G).get('player')
        check('0. guest owns the claimed unit', c(H, cmd='mp_owner', guid=gu['guid']).get('owner') == gslot, f'slot {gslot}')

        if ONLY in (None, '1'):
            # 1. host frozen 15 s
            log('SIGSTOP host', hpid()); os.kill(hpid(), signal.SIGSTOP)
            w = watch_guest(15)
            os.kill(hpid(), signal.SIGCONT); log('SIGCONT host')
            check('1. guest never on the title during the host stall', not w['title'], str(w))
            check('1. guest shows "Connection lost, rejoining..." (auto rejoin on)', 1 in w['states'] and any('rejoining' in t for t in w['texts']), str(w))
            w2 = watch_guest(90, stop=lambda s: ar(s).get('done', ar(s).get('ar_done', 0)) >= 1 and ar(s).get('autorejoin') == 0)
            s = sst(G)
            check('1. guest rejoined by itself after the stall, still not on the title', not w2['title'] and ar(s).get('done', 0) >= 1 and s.get('mp', {}).get('started'), f"{w2} ar {s.get('ar')}")
            wait('both running after the rejoin', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=120)
            gslot = mp(G).get('player')
            og = c(H, cmd='mp_owner', guid=gu['guid'])
            check('1. units back: the claimed unit is the guest\'s again', og.get('owner') == gslot and gslot is not None, f'owner {og}, slot {gslot}')
            ld = c(H, cmd='mp_left')
            check('1. host hold ended (nobody waited for)', not ld.get('players'), str(ld))
            t0 = max(st(H)['tick'], st(G)['tick']); tw = time.time()
            r1 = rig.send(G, dict(cmd='mp_move', guid=gu['guid'], x=gu['cell'][0] + 1, y=gu['cell'][1]), timeout=60); log('guest order after the rejoin', r1)
            while time.time() - tw < 120:
                time.sleep(10)
            n, first = fp_equal_from(t0)
            mh, mg = mp(H), mp(G)
            check('1. 2 min after the rejoin: 0 desyncs, fingerprints equal',
                  n >= 300 and first is None and not mh.get('desyncs') and not mg.get('desyncs') and not mg.get('desynced'),
                  f"{n} common ticks, first diff {first}, desyncs host {mh.get('desyncs')} guest {mg.get('desyncs')}")

            # 1b. Cancel on the line
            log('SIGSTOP host again', hpid()); os.kill(hpid(), signal.SIGSTOP)
            try:
                wait('guest rejoin line', lambda: (lambda s: ar(s).get('autorejoin') == 1 and c(G, cmd='mpm_status').get('box', {}).get('on'))(sst(G)), timeout=20, every=0.5)
                time.sleep(1)
                tc = time.time(); log('guest Cancel', c(G, cmd='mpm_box', action='confirm'))
                wait('guest title after Cancel', lambda: sst(G).get('scene') == 1, timeout=20, every=0.2)
                dt = time.time() - tc
                time.sleep(2)
                ms = c(G, cmd='mpm_status')
                check('1b. Cancel: guest on the title at once, no "lost" notice', dt < 8 and 'lost' not in ((ms.get('msg') or '') + (ms.get('notice') or '')).lower(), f"{dt:.1f} s, msg {ms.get('msg')!r} notice {ms.get('notice')!r}")
            finally:
                os.kill(hpid(), signal.SIGCONT); log('SIGCONT host')
            time.sleep(8)

        if ONLY in (None, '2'):
            # 2. host Leave
            if not (sst(G).get('mp') or {}).get('started'):
                if sst(H).get('scene') != 2 or not (sst(H).get('mp') or {}).get('started'):
                    rig.stop(H); time.sleep(2); host_session()
                join_guest()
            tries0 = ar(sst(G)).get('tries', 0)
            press(H, 'LEAVE'); time.sleep(1)
            hb = c(H, cmd='mpm_status').get('box', {})
            if hb.get('on'):
                log('host Leave box', hb.get('text', '')[:80], c(H, cmd='mpm_box', action='confirm'))
            w = watch_guest(15, stop=lambda s: s.get('scene') == 1 and not s.get('busy'))
            time.sleep(1)
            ms = c(G, cmd='mpm_status'); s = sst(G)
            check('2. host Leave: guest on the title, told "The host left the game"', w['title'] and 'left the game' in ((ms.get('msg') or '') + (ms.get('notice') or '')).lower(), f"{w} msg {ms.get('msg')!r} notice {ms.get('notice')!r}")
            check('2. host Leave: no auto rejoin', not any(w['states'] - {0}) and ar(s).get('tries', ar(s).get('ar_tries', 0)) == tries0, f"{w} {s.get('mp')} {s.get('ar')}")

        if ONLY in (None, '3'):
            # 3. host killed for good
            rig.stop(H); time.sleep(2); host_session(); join_guest()
            time.sleep(3)
            log('SIGKILL host', hpid()); tk = time.time(); os.kill(hpid(), signal.SIGKILL)
            w = watch_guest(100, stop=lambda s: s.get('scene') == 1 and not s.get('busy'))
            dt = time.time() - tk
            time.sleep(1)
            ms = c(G, cmd='mpm_status'); s = sst(G)
            check('3. host killed: guest keeps trying in the world, then the title after ~60 s',
                  1 in w['states'] and w['title'] and 55 <= dt <= 80 and w['first_ar'] is not None and w['first_ar'] < 10, f"{dt:.1f} s, {w}")
            check('3. host killed: "Connection to the host lost" notice', 'lost' in ((ms.get('msg') or '') + (ms.get('notice') or '')).lower(), f"msg {ms.get('msg')!r} notice {ms.get('notice')!r} mp {s.get('mp')}")
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        try:
            if rig.alive(hpid()):
                os.kill(hpid(), signal.SIGCONT)
        except Exception:
            pass
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())
        print('RESULT', 'PASS' if results and all(results.values()) else 'FAIL')


if __name__ == '__main__':
    main()
