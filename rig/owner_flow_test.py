#!/usr/bin/env python3
"""The owner's live flows, with NO rig load command in det mode and no mp.json:
host = title > Multiplayer > Host > Load save > pick a save in the stock list (LoadCampaignRequested, det off),
guest = title > Multiplayer > Join by address > type ip:port > Connect.
Checks: the host locks by itself (det armed by the session), the guest loads the host's save and locks, both run,
fingerprints equal for 1000+ ticks. Leases a free pair. Log: det_out/owner_flow_<pair>.log (stdout)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

B = dict(MULTIPLAYER=1, HOST=2, BACK=3, PAUSE_MODE=4, SPEEDUP=5, GEAR=6, LOAD_SAVE=7, NEW_CAMPAIGN=8, INVITE=9, START=10,
         ESC_INVITE=11, LEAVE=12, CANCEL_JOIN=13, JOIN_ADDR=14, CONNECT=15, COPY_ADDR=16)
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    if '--dylib' in sys.argv:
        rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1]); log('dylib', rig.DYLIB)
    pair = rig.lease(f'determinism-owner-{os.getpid()}'); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)
    assert rig.check_prefs()
    for x in (H, G):                                    # the owner's app has no mp.json
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    log('pair', pair)
    try:
        fps = int(sys.argv[sys.argv.index('--fps') + 1]) if '--fps' in sys.argv else 30
        rig.start(H, fps=fps); rig.start(G, fps=fps)
        for x in (H, G):
            rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        time.sleep(3)
        # host: Multiplayer > Host > Load save > pick the save in the stock list (no det, like a click in the list)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        if '--save' in sys.argv:                        # e.g. the base save (MoonlightBrigade autosave, map 0)
            sv = sys.argv[sys.argv.index('--save') + 1]; src = os.path.join(work, os.path.basename(sv)); shutil.copy2(sv, src)
        else:
            src = os.path.join(work, 'demo_buffed.json')
            from demo import make_demo_save
            make_demo_save(src)
        log(H, 'stock list pick (LoadCampaignRequested, det off):', c(H, cmd='load_save', file=src, det=0))
        sh = wait('host session + det lock', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None)(st(H)))
        check('(b) host from a save via the stock list: session started and locked by itself', True,
              f"tick {sh['tick']} map {sh.get('map')} mp started {sh['mp'].get('started')}")
        port = c(H, cmd='mpm_status').get('port')
        # guest: Multiplayer > Join by address > type > Connect
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        sg = wait('guest loaded and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st(G)), timeout=900)
        check('(c) guest join by address: loaded the host save and locked', True, f"tick {sg['tick']} map {sg.get('map')}")
        wait('both running', lambda: not st(H)['mp'].get('paused') and not st(G)['mp'].get('paused'), timeout=180)
        time.sleep(5)
        sh2, sg2 = st(H), st(G)
        check('local game speed (drives animations) running on both, same value', sh2.get('speed') == sg2.get('speed') and (sh2.get('speed') or 0) > 0,
              f"host speed {sh2.get('speed')} (requested {sh2.get('requested_speed')}), guest speed {sg2.get('speed')} (requested {sg2.get('requested_speed')}); mp {sh2['mp'].get('speed')}/{sg2['mp'].get('speed')}")
        # animation check by frames: 8 camera frames 100 ms apart on each copy while the party stands still;
        # count pixels that change between consecutive frames (idle animations change sprites; a frozen Animator does not)
        from PIL import Image, ImageChops
        anim = {}
        for x in (H, G):
            paths = []
            for i in range(8):
                pth = os.path.join(rig.idir(x), f'anim_{i}.png'); c(x, cmd='screenshot', path=pth, w=640, h=360); paths.append(pth); time.sleep(0.1)
            diffs = []
            for a_, b_ in zip(paths, paths[1:]):
                d_ = ImageChops.difference(Image.open(a_).convert('RGB'), Image.open(b_).convert('RGB')).convert('L')
                diffs.append(sum(1 for v in d_.getdata() if v > 8))
            anim[x] = diffs
        log(f'changed pixels between consecutive frames: host {anim[H]} guest {anim[G]}')
        check('animations run on the host like on the guest (frames change)', sum(anim[H]) > 0.3 * sum(anim[G]) and sum(anim[G]) > 0,
              f'host {sum(anim[H])} guest {sum(anim[G])}')
        for x in (H, G): c(x, cmd='det_frame_reset')
        t0 = max(st(H)['tick'], st(G)['tick'])
        wait('1000 ticks', lambda: min(st(H)['tick'], st(G)['tick']) > t0 + 1000, timeout=600, every=5)
        time.sleep(3)

        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and int(p[0]) > t0:
                    out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(G); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        fh, fg = st(H).get('frame_ticks'), st(G).get('frame_ticks')
        log(f'world ticks per frame (0,1,2..8,9+): host {fh} guest {fg}')
        mh, mg = st(H)['mp'], st(G)['mp']
        check('fingerprints equal 1000+ ticks, no desync', len(com) >= 1000 and first is None and not mh.get('desyncs') and not mg.get('desyncs'),
              f'{len(com)} common ticks, first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}')
        check('players present on both (story votes count them)', bin(mh.get('present', 0)).count('1') == 2 and mh.get('present') == mg.get('present'), f"present {mh.get('present')}/{mg.get('present')}")
    except Exception as e:
        check('flow completed', False, str(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
