#!/usr/bin/env python3
"""Spectator pair test SP-1 + SP-3 + SP-4 (ext/spectator lane): one leased pair (lane "spectator"), the spectator build
ext/spectator/build/dsrig.dylib. Owner's menu flow (no mp.json), TCP.

  spect_pair.py [--tag NAME]

 1. H hosts the demo save from the menu; G joins as a PLAYER (join point 1, the server keeps that save), claims a unit,
    then leaves to the title; H chooses Keep playing.
 2. G presses Watch on the Join page (same address). Checks: no new join point on H (its save count stays), the server
    served the cached save (spect saves 1, join points 0), H never paused while G joined, H ran on; G is a spectator,
    not in H's present mask, in H's spectator list; fingerprints equal for 600+ ticks after G's load.
 3. G's orders and votes are refused (move, pause, speed, claim): nothing reaches H; G's old unit stays RESERVED for him.
 4. UI: G's viewer box "Watching <host>'s world", no clock buttons, "Watching (1)" in both panels, the host's cog row
    "On (1)"; Follow: G's camera follows H's view; G's chat line reaches H as "(watching)"; G's ping reaches H (grey).
    Frames: out/<tag>/{H,G}_*.png.
 5. SP-3: H turns spectators off (cog path: box, Remove): G back on the title with "turned spectators off"; H runs on.
    G presses Watch again: refused "does not allow spectators".
Writes out/<tag>/test.log and result.json."""
import json, os, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(ROOT, 'build', 'dsrig.dylib')   # merged into src 2026-10-15
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('run_%H%M%S')
OUT = os.path.join(ROOT, 'ext', 'spectator', 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, BACK=3, WATCH=70)
results = {}


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = None
    for _ in range(60):                                      # wait for a free pair (other lanes come first)
        pair = rig.lease('spectator')
        if pair: break
        time.sleep(30)
    assert pair, 'no free pair'
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

    def fp(x, after):
        out = {}
        for line in open(os.path.join(rig.idir(x), 'fp.log')):
            p = line.split()
            if len(p) >= 5 and int(p[0]) > after:
                out[int(p[0])] = p[1]
        return out

    def shot(x, name):
        p = os.path.join(OUT, f'{x}_{name}.png')
        r = c(x, cmd='mpm_shot', path=p)
        log('frame', p, r.get('ok'))

    def to_join_page(x, port):
        press(x, 'MULTIPLAYER'); press(x, 'JOIN_ADDR')
        c(x, cmd='mpm_type', text=f'127.0.0.1:{port}')

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        rig.start(H, fps=30); rig.start(G, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(3)
        # 1. host + a player join (join point 1), the player leaves
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        to_join_page(G, port); press(G, 'CONNECT')
        wait('player G loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        log('host presses Start (the lobby hold ends)', press(H, 'START'))
        wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
        us = c(H, cmd='list_units')['units']
        mine = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        u0 = mine[0]
        log('player G claims', u0['name'], c(G, cmd='mp_claim', guid=u0['guid']))
        time.sleep(3)
        check('1. G owns its unit as a player', c(H, cmd='mp_owner', guid=u0['guid']).get('owner') == mp(G).get('player'))
        log('G quits to the title', c(G, cmd='mp_quit', mode='title'))
        wait('G on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=120, every=1)
        wait('host popup', lambda: c(H, cmd='mp_left').get('players'), timeout=30, every=0.5)
        c(H, cmd='mp_left_choice', keep=1)
        wait('host running', lambda: not mp(H).get('paused'), timeout=30, every=0.5)
        time.sleep(3)

        # 2. G watches
        h0 = mp(H); saves0 = h0.get('saves'); tick0 = st(H)['tick']
        to_join_page(G, port); time.sleep(1)
        log('G presses Watch', c(G, cmd='mpm_press', button=B['WATCH']))
        paused_seen, samples, t0 = 0, 0, time.time()
        while time.time() - t0 < 900:
            m = mp(H); samples += 1
            if m.get('paused'): paused_seen += 1
            sg = st(G)
            if sg.get('det', {}).get('locked') and sg.get('mp', {}).get('loads', 0) >= 1 and not sg.get('mp', {}).get('needs_state'):
                break
            time.sleep(0.25)
        join_s = time.time() - t0
        h1 = mp(H); tick1 = st(H)['tick']; g1 = mp(G)
        log('host after the watch', json.dumps({k: h1.get(k) for k in ('saves', 'paused', 'present', 'spectators', 'srv_spectators', 'srv_spect_saves', 'srv_spect_join_points', 'loads')}))
        log('G after the watch', json.dumps({k: g1.get(k) for k in ('spectator', 'player', 'present', 'spectators', 'loads', 'needs_state', 'desynced')}))
        check('2a. no new join point: host save count unchanged, served from the cached save',
              h1.get('saves') == saves0 and h1.get('srv_spect_saves') == 1 and h1.get('srv_spect_join_points') == 0,
              f"saves {saves0}->{h1.get('saves')}, spect saves {h1.get('srv_spect_saves')}, join points {h1.get('srv_spect_join_points')}")
        check('2b. host never paused while G joined, and ran on', paused_seen == 0 and tick1 > tick0 + 60,
              f'{paused_seen} of {samples} samples paused, host ticks {tick0}->{tick1}, join took {join_s:.0f} s')
        gslot = g1.get('player')
        check('2c. G is a spectator: not in the present mask, in the spectator list on both',
              g1.get('spectator') and gslot is not None and not (h1.get('present', 0) >> gslot & 1) and (h1.get('spectators', 0) >> gslot & 1)
              and (g1.get('spectators', 0) >> gslot & 1), f"slot {gslot}, host present {h1.get('present')}, host spectators {h1.get('spectators')}")
        tl = st(G)['tick']
        wait('600 ticks on G', lambda: st(G)['tick'] > tl + 600, timeout=600, every=5)
        time.sleep(3)
        a, b = fp(H, tl), fp(G, tl); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        check('2d. fingerprints equal after the spectator joined (replay of the cached save)', len(com) >= 15 and first is None and not mp(G).get('desyncs'),
              f'{len(com)} common fingerprints, first diff {first}, G desyncs {mp(G).get("desyncs")}')

        # 3. G changes nothing
        hb = mp(H); cell = u0.get('cell', [0, 0])
        r_move = c(G, cmd='mp_move', guid=u0['guid'], x=cell[0] + 2, y=cell[1])
        r_pause = c(G, cmd='mp_pause', on=1)
        r_speed = c(G, cmd='mp_speed', speed=2)
        r_claim = c(G, cmd='mp_claim', guid=mine[1]['guid'])
        time.sleep(4)
        ha = mp(H)
        check('3a. spectator orders / votes refused at the source', not r_move.get('ok') and not r_pause.get('ok') and not r_speed.get('ok') and not r_claim.get('ok'),
              f'move {r_move} pause {r_pause} speed {r_speed} claim {r_claim}')
        check('3b. nothing reached the host: no order, no pause, speed normal, owners unchanged',
              ha.get('orders') == hb.get('orders') and ha.get('orders_dropped_owner') == hb.get('orders_dropped_owner') and not ha.get('paused')
              and ha.get('speed') == 'normal' and ha.get('own_hash') == hb.get('own_hash'),
              f"orders {hb.get('orders')}->{ha.get('orders')}, dropped {hb.get('orders_dropped_owner')}->{ha.get('orders_dropped_owner')}, paused {ha.get('paused')}, speed {ha.get('speed')}")
        o = c(H, cmd='mp_owner', guid=u0['guid'])
        check('3c. G\'s old unit stays RESERVED for him (a spectator never gets it)', o.get('owner') == -2, str(o))

        # 4. UI
        time.sleep(2)
        sg, sh = c(G, cmd='ui_spect'), c(H, cmd='ui_spect')
        log('G ui_spect', json.dumps(sg)); log('H ui_spect', json.dumps(sh))
        hn = 'Host'
        check('4a. G: viewer box "Watching <host>\'s world" + "You are a spectator", clock buttons hidden',
              sg.get('viewer_on') and "s world" in sg.get('title', '') and 'Watching' in sg.get('title', '') and 'spectator' in sg.get('text', '')
              and all(v in (0, -1) for v in sg.get('clock_buttons', [1])), f"title {sg.get('title')!r} text {sg.get('text')!r} clock {sg.get('clock_buttons')}")
        check('4b. "Watching (1)" in both panels; host cog row On (1)', 'Watching (1)' in sg.get('panel_body', '') and 'Watching (1)' in sh.get('panel_body', '')
              and 'On (1)' in (sh.get('panel_spect') or ['', ''])[1], f"host cog {sh.get('panel_spect')}")
        shot(G, 'watching'); shot(H, 'host_panel')
        c(H, cmd='ui_coop', action='camera', x=cell[0] + 6, y=cell[1] + 4)
        r = c(G, cmd='ui_spect', press=0); log('G Follow', r.get('follow'))
        time.sleep(3)
        ch = c(H, cmd='ui_coop'); cg = c(G, cmd='ui_coop')
        dx, dy = abs(ch['cam'][0] - cg['cam'][0]), abs(ch['cam'][1] - cg['cam'][1])
        check('4c. Follow: G\'s camera follows H\'s view (outside the sim)', r.get('follow') == 0 and dx < 0.6 and dy < 0.6, f"host cam {ch['cam']} G cam {cg['cam']}")
        shot(G, 'follow')
        m = c(G, cmd='ui_mini'); log('G mini', json.dumps(m))
        check('4f. mini overview while following: shown, map camera drawing, H\'s view rectangle, "Following" chip',
              m.get('shown') and m.get('camera') and m.get('renders', 0) > 0 and m.get('chip_on') and any(r['player'] == 0 for r in m.get('rects', [])),
              f"rects {m.get('rects')} texture {m.get('texture')} chip {m.get('chip')!r}")
        c(G, cmd='mpm_shot', path=os.path.join(OUT, 'G_mini.png'), how='backbuffer')
        c(G, cmd='mpm_chat', open=1); time.sleep(0.8); c(G, cmd='mpm_chat', text='hello from the stands'); time.sleep(0.8); c(G, cmd='mpm_chat', enter=1)
        time.sleep(2)
        lines = c(H, cmd='mpm_status').get('chat', {}).get('last', [])
        check('4d. G\'s chat line reaches H marked "(watching)"', any('(watching)' in l and 'hello from the stands' in l for l in lines), str(lines))
        rp = c(G, cmd='ui_coop', action='ping', x=cell[0] + 3, y=cell[1] + 1)
        time.sleep(2)
        hp = c(H, cmd='ui_coop').get('pings', [])
        check('4e. G\'s ping reaches H', rp.get('rc') == 0 and any(p.get('owner') == gslot for p in hp), f'rc {rp.get("rc")} host pings {hp}')
        shot(H, 'ping')

        # 5. SP-3: spectators off
        r = c(H, cmd='mpm_spectators', allow=0); log('host: spectators off ->', r)
        time.sleep(1)
        rb = c(H, cmd='mpm_box', action='confirm') if r.get('box') else {'ok': 'no box'}
        log('host confirms Remove', rb)
        sg = wait('G back on the title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') and not s.get('mp', {}).get('started') else None)(st(G)), timeout=180, every=1)
        time.sleep(4)
        ms = c(G, cmd='mpm_status')
        hm = mp(H)
        check('5a. off: confirm box, G back on the title with "turned spectators off", host runs on',
              r.get('box') == 1 and 'turned spectators off' in (ms.get('notice', '') + ms.get('msg', '') + json.dumps(ms)) and not hm.get('paused') and hm.get('spectators') == 0,
              f"box {r.get('box')}, G notice {ms.get('notice')!r} msg {ms.get('msg')!r}, host paused {hm.get('paused')} spectators {hm.get('spectators')}")
        shot(G, 'kicked')
        to_join_page(G, port); time.sleep(1)
        c(G, cmd='mpm_press', button=B['WATCH'])
        time.sleep(8)
        ms = c(G, cmd='mpm_status')
        check('5b. Watch while off: refused "does not allow spectators"', 'does not allow spectators' in json.dumps(ms), f"msg {ms.get('msg')!r} notice {ms.get('notice')!r}")
        shot(G, 'refused')
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair, 'spectator')
        ok = rig.check_prefs()
        log('prefs identical:', ok)
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        log('RESULTS', json.dumps(results))


if __name__ == '__main__':
    main()
