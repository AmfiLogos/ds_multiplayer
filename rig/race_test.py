#!/usr/bin/env python3
"""Bug 2026-10-13: host's join-point reload hung on the title after a return-to-base (the game deleted mp_sync.json
between the reload being queued and read). Reproduces the race on a pair (owner menu flow, iron demo save):
right after the host queues its reload, mp_test_race does one of
  autosave: the game's own SaveAutoSaveFile (+ CleanupAutoSaves) with the iron flag on, as the map-change autosave
  game:     the game's CampaignSaveLoader.Delete("mp_sync")
  raw:      the file is just removed (exercises the stuck-load retry)
then checks the host comes back into the campaign, both run, fingerprints equal. Leases a free pair (lane "mpglue")."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, JOIN_ADDR=14, CONNECT=15)
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = rig.lease('mpglue'); assert pair, 'no free pair'
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
        for x in (H, G):
            rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
        for mode in ('autosave', 'game', 'raw'):
            h0, g0 = mp(H), mp(G)
            c(H, cmd='mp_test_race', mode=mode)
            c(G, cmd='mp_resync')
            try:
                wait(mode + ': guest reloaded', lambda: (lambda s: s.get('mp', {}).get('loads', 0) > g0.get('loads', 0) and s.get('det', {}).get('locked'))(st(G)), timeout=300)
                sh = wait(mode + ': host back in the campaign, locked', lambda: (lambda s: s if s.get('scene') == 2 and s.get('det', {}).get('locked') and not s.get('busy') else None)(st(H)), timeout=180)
                wait(mode + ': both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
                h1 = mp(H)
                t0 = max(st(H)['tick'], st(G)['tick'])
                wait('300 ticks', lambda: min(st(H)['tick'], st(G)['tick']) > t0 + 300, timeout=300, every=3)
                time.sleep(3)
                n, first = fp_equal_from(t0)
                check(f'{mode}: host reload completed, both run, fingerprints equal', n >= 300 and first is None,
                      f"host scene {sh.get('scene')}, game deletes blocked {g0 and h0.get('game_deletes_blocked')}->{h1.get('game_deletes_blocked')}, "
                      f"load retries {h1.get('load_retries')}; {n} equal ticks, first diff {first}")
            except RuntimeError as e:
                s = st(H)
                check(f'{mode}: host reload completed, both run, fingerprints equal', False, f"{e}; host scene {s.get('scene')} det {s.get('det', {}).get('locked')} mp {s.get('mp')}")
                break
        os.system(f"grep -E 'test race|kept|stuck on the title|Save became' {rig.idir(H)}/rig.log {rig.idir(H)}/data/ErrorLog.txt | tail -8")
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
