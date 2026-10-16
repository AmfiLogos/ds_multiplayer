#!/usr/bin/env python3
"""Spectator HUD A + mini overview, 3 copies (ext/spectator lane): host H + player P (pair 1) + spectator S (first copy of
pair 2). Two leased pairs (lanes "spectator" + "spectator2"), 3 copies run; waits for both (other lanes first).

  spect_mini.py [--tag NAME] [--minutes 10]

 1. H hosts the demo save, P joins as a player and claims 2 units, S watches (cached save, no join point).
 2. S follows H: S's camera = H's view, "Following <H>'s view" chip, mini overview shown with H's and P's view
    rectangles; H moves its camera: S follows and H's rectangle moves in S's mini. Next player: S follows P, same checks.
 3. Mini overview: bigger / smaller, move (saved), a click on the map = look there (follow stops, the mini stays, own
    grey view shows), Free camera hides it, O shows it again, x hides it.
 4. N minutes of play: H and P give move orders and move their cameras, S switches whom it follows; at the end the
    fingerprints of H, P and S are equal over the whole run and nobody desynced.
 5. S leaves (quit to title): no popup on H, nobody paused; H and P stay equal.
Frames (backbuffer, with the HUD): out/<tag>/S_*.png. Writes out/<tag>/test.log and result.json."""
import json, os, random, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(ROOT, 'build', 'dsrig.dylib')   # merged into src 2026-10-15
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('mini_%H%M%S')
MINUTES = float(A[A.index('--minutes') + 1]) if '--minutes' in A else 10.0
OUT = os.path.join(ROOT, 'ext', 'spectator', 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, WATCH=70)
results = {}


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def lease_two():
    for _ in range(240):                                     # up to 2 h: other lanes come first
        p1 = rig.lease('spectator')
        p2 = rig.lease('spectator2') if p1 else None
        if p1 and p2:
            return p1, p2
        if p1: rig.release(p1, 'spectator')
        time.sleep(30)
    return None, None


def main():
    p1, p2 = lease_two()
    if not p1:
        log('FAIL not run: no free pair (two needed)'); sys.exit(1)
    H, P, S = p1[0], p1[1], p2[0]
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
        log('frame', p, c(x, cmd='mpm_shot', path=p, how='backbuffer').get('ok'))

    def join(x, port, button):
        press(x, 'MULTIPLAYER'); press(x, 'JOIN_ADDR')
        c(x, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5)
        c(x, cmd='mpm_press', button=B[button])
        wait(f'{x} loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 and not s.get('mp', {}).get('needs_state'))(st(x)), timeout=900, every=1)

    def rect_of(m, pl):
        return next((r['r'] for r in m.get('rects', []) if r['player'] == pl), None)

    for x in (H, P, S):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
        u = os.path.join(rig.idir(x), 'data', 'mp_ui.json')       # a fresh mini overview (no saved place / size)
        if os.path.exists(u):
            os.replace(u, u + '.last')
        m = os.path.join(rig.idir(x), 'mp_menu.json')            # a saved "Spectators: Not allowed" from an older run
        if os.path.exists(m):
            d = json.load(open(m)); d['spectators'] = 1; os.replace(m, m + '.last'); json.dump(d, open(m, 'w'))
    busy = [x for x in (H, P, S) if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
    if busy:                                                 # a copy started outside the lease: never drive it
        log('copies already running outside this lease:', busy); rig.release(p1, 'spectator'); rig.release(p2, 'spectator2'); return
    log('copies', H, P, S, 'dylib', rig.DYLIB)
    try:
        for x in (H, P, S): rig.start(x, window=True, fps=30)   # hidden windows: frames with the HUD
        for x in (H, P, S): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        join(P, port, 'CONNECT')
        press(H, 'START')
        wait('H + P running', lambda: not mp(H).get('paused') and not mp(P).get('paused'), timeout=180)
        us = c(H, cmd='list_units')['units']
        mine = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        pu = mine[:2]; hu = mine[2:4]
        for u in pu: log('P claims', u['name'], c(P, cmd='mp_claim', guid=u['guid']))
        time.sleep(3)
        join(S, port, 'WATCH')
        hm, pm, sm = mp(H), mp(P), mp(S)
        hp, pp = hm.get('player'), pm.get('player')
        check('1. S watches: spectator, not present; H + P present', sm.get('spectator') and hm.get('present') == pm.get('present') == sm.get('present')
              and bin(hm.get('present', 0)).count('1') == 2, f"present {hm.get('present')}/{pm.get('present')}/{sm.get('present')}, H {hp} P {pp}")
        t_run = max(st(H)['tick'], st(P)['tick'], st(S)['tick'])
        cell = hu[0]['cell'] if hu else mine[0]['cell']

        # 2. follow H, then P
        c(H, cmd='ui_coop', action='camera', x=cell[0] + 4, y=cell[1] + 2); c(P, cmd='ui_coop', action='camera', x=cell[0] - 6, y=cell[1] - 3)
        time.sleep(2)
        sp = c(S, cmd='ui_spect')
        if sp.get('leader') != hp:
            for _ in range(4):
                sp = c(S, cmd='ui_spect', press=2)
                if sp.get('leader') == hp: break
        sp = c(S, cmd='ui_spect', press=0); time.sleep(3)
        m1 = c(S, cmd='ui_mini'); ch, cs_ = c(H, cmd='ui_coop'), c(S, cmd='ui_coop')
        log('S mini (following H)', json.dumps(m1))
        check('2a. S follows H: same camera, chip "Following", mini shown with a camera texture',
              sp.get('follow') == hp and abs(ch['cam'][0] - cs_['cam'][0]) < 0.6 and abs(ch['cam'][1] - cs_['cam'][1]) < 0.6
              and m1.get('shown') and m1.get('chip_on') and 'Following' in m1.get('chip', '') and m1.get('camera') and m1.get('renders', 0) > 0
              and m1.get('texture', [0])[0] > 0, f"host cam {ch['cam']} S cam {cs_['cam']} chip {m1.get('chip')!r}")
        r_h0, r_p0 = rect_of(m1, hp), rect_of(m1, pp)
        check('2b. mini: H\'s and P\'s view rectangles, both names in the legend', r_h0 and r_p0 and r_h0 != r_p0, f'H {r_h0} P {r_p0} legend {m1.get("legend")!r}')
        shot(S, 'follow_H')
        c(H, cmd='ui_coop', action='camera', x=cell[0] + 14, y=cell[1] + 8); time.sleep(3)
        m2 = c(S, cmd='ui_mini'); ch, cs_ = c(H, cmd='ui_coop'), c(S, cmd='ui_coop')
        r_h1 = rect_of(m2, hp)
        check('2c. H moves: S\'s camera follows, H\'s rectangle moves right/up in the mini',
              abs(ch['cam'][0] - cs_['cam'][0]) < 0.6 and r_h1 and r_h0 and r_h1[0] > r_h0[0] and r_h1[1] < r_h0[1], f'H rect {r_h0} -> {r_h1}, S cam {cs_["cam"]}')
        sp = c(S, cmd='ui_spect', press=2); time.sleep(3)   # next player (the follow moves with it)
        m3 = c(S, cmd='ui_mini'); cp, cs_ = c(P, cmd='ui_coop'), c(S, cmd='ui_coop')
        check('2d. Next player: S follows P (camera, chip), mini still shown',
              sp.get('follow') == pp and abs(cp['cam'][0] - cs_['cam'][0]) < 0.6 and m3.get('shown') and 'Following' in m3.get('chip', ''),
              f"follow {sp.get('follow')} P cam {cp['cam']} S cam {cs_['cam']} chip {m3.get('chip')!r}")
        r_p1 = rect_of(m3, pp)
        c(P, cmd='ui_coop', action='camera', x=cell[0] - 16, y=cell[1] - 9); time.sleep(3)
        m4 = c(S, cmd='ui_mini'); r_p2 = rect_of(m4, pp)
        check('2e. P moves: P\'s rectangle moves left/down in the mini', r_p1 and r_p2 and r_p2[0] < r_p1[0] and r_p2[1] > r_p1[1], f'P rect {r_p1} -> {r_p2}')
        shot(S, 'follow_P')

        # 3. the window
        w0 = m4.get('w'); mb = c(S, cmd='ui_mini', action='bigger'); ms = c(S, cmd='ui_mini', action='smaller')
        check('3a. bigger / smaller', mb.get('w', 0) > w0 and ms.get('w') == w0 and mb.get('texture', [0])[0] > m4.get('texture', [0])[0], f'{w0} -> {mb.get("w")} -> {ms.get("w")}, texture {mb.get("texture")}')
        mv = c(S, cmd='ui_mini', action='move', x=-300, y=-200)
        check('3b. move: placed and saved', mv.get('pos_set') and abs(mv.get('x', 0) + 300) < 1 and abs(mv.get('y', 0) + 200) < 1, f"x {mv.get('x')} y {mv.get('y')}")
        shot(S, 'mini_moved')
        mc = c(S, cmd='ui_mini', action='click', fx=0.5, fy=0.5); time.sleep(1); mc = c(S, cmd='ui_mini')
        sc_ = c(S, cmd='ui_coop')
        check('3c. click on the map: look there, follow stops, the mini stays with the own grey view',
              mc.get('follow') == -1 and mc.get('shown') and mc.get('kept') and mc.get('own') is not None and not mc.get('chip_on')
              and abs(sc_['cam'][0] - mc['view'][0]) < 3, f"follow {mc.get('follow')} own {mc.get('own')} cam {sc_['cam']} map centre {mc.get('view')}")
        shot(S, 'mini_free')
        c(S, cmd='ui_spect', press=1); mf = c(S, cmd='ui_mini')
        check('3d. Free camera: the mini hides', not mf.get('shown'), str(mf.get('shown')))
        mo = c(S, cmd='ui_mini', action='o')
        check('3e. O shows it again (free camera)', mo.get('shown') and mo.get('setting') == 1, str(mo.get('shown')))
        mx = c(S, cmd='ui_mini', action='hide'); c(S, cmd='ui_spect', press=0); time.sleep(1); mx2 = c(S, cmd='ui_mini')
        check('3f. x hides it, also while following; the setting stays off', not mx.get('shown') and not mx2.get('shown') and mx2.get('setting') == 0 and mx2.get('follow') >= 0,
              f"{mx.get('shown')} {mx2.get('shown')} setting {mx2.get('setting')}")
        c(S, cmd='ui_mini', action='o'); c(S, cmd='ui_mini', action='reset')
        mback = c(S, cmd='ui_mini')
        check('3g. O again: shown while following, default place', mback.get('shown') and not mback.get('pos_set'), f"shown {mback.get('shown')}")
        hc = c(H, cmd='ui_mini'); pc = c(P, cmd='ui_mini')
        check('3h. players never get the mini or the chip', not hc.get('shown') and not pc.get('shown') and not hc.get('chip_on') and not pc.get('chip_on'))

        # 4. N minutes of play
        log(f'4. {MINUTES} minutes of play')
        rnd = random.Random(7); t_end = time.time() + MINUTES * 60; k = 0; rcs = []
        while time.time() < t_end:
            k += 1
            for x, us_ in ((P, pu), (H, hu)):
                if not us_: continue
                u = rnd.choice(us_); cl = u['cell']
                rcs.append(c(x, cmd='mp_move', guid=u['guid'], x=cl[0] + rnd.randint(-4, 4), y=cl[1] + rnd.randint(-4, 4)).get('rc'))
                c(x, cmd='ui_coop', action='camera', x=cl[0] + rnd.randint(-12, 12), y=cl[1] + rnd.randint(-8, 8))
            if k % 3 == 0: c(S, cmd='ui_spect', press=2)
            if k % 7 == 0: c(S, cmd='ui_mini', action='click', fx=rnd.random(), fy=rnd.random()); c(S, cmd='ui_spect', press=0)
            if k % 10 == 0:
                d = {x: mp(x).get('desyncs') for x in (H, P, S)}; log('progress', k, 'desyncs', d, 'ticks', {x: st(x)['tick'] for x in (H, P, S)})
            time.sleep(15)
        shot(S, 'after_run')
        time.sleep(5)
        a, b, s = fp(H, t_run), fp(P, t_run), fp(S, t_run)
        com = sorted(set(a) & set(b) & set(s)); first = next((t for t in com if not (a[t] == b[t] == s[t])), None)
        ds = {x: (mp(x).get('desyncs'), mp(x).get('desynced')) for x in (H, P, S)}
        check(f'4. {MINUTES:g} min run: fingerprints of H, P and S equal, no desync', len(com) >= 15 * MINUTES and first is None and all(not v[0] and not v[1] for v in ds.values()),
              f'{len(com)} common fingerprints, first diff {first}, desyncs {ds}, orders rc {sorted(set(map(str, rcs)))} ({len(rcs)})')

        # 5. S leaves
        c(S, cmd='mp_quit', mode='title'); time.sleep(4)
        l = c(H, cmd='mp_left'); hm = mp(H)
        check('5a. S left: no popup, no hold, nobody paused', not l.get('players') and not hm.get('paused') and not mp(P).get('paused') and hm.get('spectators') == 0,
              f'left {l}, host paused {hm.get("paused")}, spectators {hm.get("spectators")}')
        t1 = st(H)['tick']
        wait('300 ticks', lambda: min(st(H)['tick'], st(P)['tick']) > t1 + 300, timeout=300, every=5)
        a, b = fp(H, t1), fp(P, t1); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        check('5b. H and P equal after S left', len(com) >= 8 and first is None, f'{len(com)} common, first diff {first}')
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        for x in (H, P, S): rig.stop(x)
        rig.release(p1, 'spectator'); rig.release(p2, 'spectator2')
        log('prefs identical:', rig.check_prefs())
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        log('RESULTS', json.dumps(results))


if __name__ == '__main__':
    main()
