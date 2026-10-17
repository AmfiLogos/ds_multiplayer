#!/usr/bin/env python3
"""Spectator 3-copy test SP-2 (ext/spectator lane): host H + player P (pair 1) + spectator S (first copy of pair 2).
Two leased pairs (lane "spectator" and "spectator2"); run only when both are free.

  spect_three.py [--tag NAME]

 1. H hosts the demo save, P joins as a player (join point), S watches (cached save: no join point, no pause, P does not
    reload). Fingerprints of all three equal for 600+ ticks.
 2. Votes count the 2 players only: P votes pause -> paused everywhere (S too); P votes run -> running. S's pause vote
    is refused. Present mask = H + P on all three.
 3. "Watching (1)" in H's and P's panels.
 4. S leaves (quit to title): no "disconnected" popup on H, no hold, nobody paused; P keeps playing; H + P equal after.
Writes out/<tag>/test.log and result.json."""
import json, os, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig
LANE = f'spectator-{os.getpid()}'                 # unique per run: two runs of one test never share a pair
LANE2 = f'spectator2-{os.getpid()}'

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(ROOT, 'build', 'dsrig.dylib')   # merged into src 2026-10-15
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('three_%H%M%S')
OUT = os.path.join(ROOT, 'ext', 'spectator', 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, WATCH=70)
results = {}


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    p1 = rig.lease(LANE); p2 = rig.lease(LANE2)
    if not p1 or not p2:
        for p in (p1, p2):
            if p: rig.release(p, LANE if p == p1 else LANE2)
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

    def join(x, port, button):
        press(x, 'MULTIPLAYER'); press(x, 'JOIN_ADDR')
        c(x, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5)
        c(x, cmd='mpm_press', button=B[button])
        wait(f'{x} loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 and not s.get('mp', {}).get('needs_state'))(st(x)), timeout=900, every=1)

    for x in (H, P, S):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
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
        time.sleep(3)
        h0, p0 = mp(H), mp(P)
        join(S, port, 'WATCH')
        h1, p1_, s1 = mp(H), mp(P), mp(S)
        check('1a. spectator joined from the cached save: no join point, P did not reload',
              h1.get('saves') == h0.get('saves') and p1_.get('loads') == p0.get('loads') and h1.get('srv_spect_saves') == 1 and h1.get('srv_spect_join_points') == 0,
              f"host saves {h0.get('saves')}->{h1.get('saves')}, P loads {p0.get('loads')}->{p1_.get('loads')}, spect saves {h1.get('srv_spect_saves')}")
        pm = h1.get('present')
        check('1b. present = host + player on all three; S a spectator', pm == p1_.get('present') == s1.get('present') and bin(pm).count('1') == 2 and s1.get('spectator'),
              f"present {pm}/{p1_.get('present')}/{s1.get('present')}")
        # 2. votes: 2 players
        r = c(S, cmd='mp_pause', on=1); time.sleep(3)
        check('2a. S\'s pause vote refused, nobody paused', not r.get('ok') and not mp(H).get('paused'), str(r))
        c(P, cmd='mp_pause', on=1)
        wait('paused everywhere', lambda: mp(H).get('paused') and mp(P).get('paused') and mp(S).get('paused'), timeout=30, every=0.5)
        check('2b. P\'s pause vote pauses everyone (S too)', True)
        c(P, cmd='mp_pause', on=0)
        wait('running everywhere', lambda: not mp(H).get('paused') and not mp(P).get('paused') and not mp(S).get('paused'), timeout=30, every=0.5)
        check('2c. P\'s run vote resumes (the spectator is not waited for)', True)
        # 3. panels
        time.sleep(2)
        sh, sp = c(H, cmd='ui_spect'), c(P, cmd='ui_spect')
        check('3. "Watching (1)" in the host\'s and the player\'s panels', 'Watching (1)' in sh.get('panel_body', '') and 'Watching (1)' in sp.get('panel_body', ''),
              f"H {sh.get('panel_body')!r} P {sp.get('panel_body')!r}")
        for x, nm in ((H, 'host'), (P, 'player'), (S, 'spectator')):
            c(x, cmd='mpm_shot', path=os.path.join(OUT, f'{x}_{nm}.png'), how='backbuffer')
        t0 = max(st(H)['tick'], st(S)['tick'])
        wait('600 ticks on S', lambda: st(S)['tick'] > t0 + 600, timeout=600, every=5)
        time.sleep(3)
        a, b, s = fp(H, t0), fp(P, t0), fp(S, t0)
        com = sorted(set(a) & set(b) & set(s)); first = next((t for t in com if not (a[t] == b[t] == s[t])), None)
        check('1c. fingerprints of H, P and S equal', len(com) >= 15 and first is None, f'{len(com)} common, first diff {first}')
        # 4. S leaves
        c(S, cmd='mp_quit', mode='title')
        time.sleep(4)
        l = c(H, cmd='mp_left'); hm = mp(H)
        check('4a. S left: no disconnect popup, no hold, nobody paused', not l.get('players') and not hm.get('paused') and not mp(P).get('paused') and hm.get('spectators') == 0,
              f'left {l}, host paused {hm.get("paused")}, spectators {hm.get("spectators")}')
        t1 = st(H)['tick']
        wait('300 ticks', lambda: min(st(H)['tick'], st(P)['tick']) > t1 + 300, timeout=300, every=5)
        a, b = fp(H, t1), fp(P, t1); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        check('4b. H and P equal after S left', len(com) >= 8 and first is None, f'{len(com)} common, first diff {first}')
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        for x in (H, P, S): rig.stop(x)
        rig.release(p1, LANE); rig.release(p2, LANE2)
        log('prefs identical:', rig.check_prefs())
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        log('RESULTS', json.dumps(results))


if __name__ == '__main__':
    main()
