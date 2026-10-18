#!/usr/bin/env python3
"""watchfog (owner 2026-10-18): a guest with no units watches the host's expedition with per-player fog (Shared fog Off).

  watchfog_pair.py --dylib PATH --tag before|after [--shots DIR]

One leased hidden pair (lane watchfog). X hosts MoonlightBrigade, takes f1 and proposes; Y (no units) approves with 0
units; both land in the dungeon; Y follows X's camera. Shared fog Off (per player). Frames of both views (<tag>_guest.png, <tag>_host.png).
--tag after also: mpm_fog counts on Y (visible tiles > 0), then the old rule forced (mpm_fog {watch:0}): the
ERROR [view_blank] line in Y's rig.log within 15 s, frame forced_old_guest.png; the rule back on: "visible again".
Both: fingerprints equal from before the proposal to 300 ticks after landing (fog is display only)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle, T.fp_equal_from
results = T.results
TAG = T.arg('--tag', 'after')
SHOTS = T.arg('--shots', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots'))
os.makedirs(SHOTS, exist_ok=True)


def shot(x, name):
    p = os.path.join(SHOTS, f'{name}.png')
    r = c(x, cmd='mpm_shot', how='backbuffer', path=p)
    if not os.path.exists(p): r = tm(x, action='shot', path=p)
    lit = -1
    try:
        from PIL import Image
        im = Image.open(p).convert('L'); w, h = im.size
        box = im.crop((int(w * 0.2), int(h * 0.25), int(w * 0.8), int(h * 0.75)))   # the map middle, away from the HUD
        px = list(box.getdata()); lit = sum(1 for v in px if v > 40) / len(px)
    except Exception as e:
        log('shot read', p, e)
    log('frame', p, 'lit share of the map middle %.3f' % lit)
    return lit


def rig_log_since(x, pos):
    f = open(os.path.join(rig.idir(x), 'rig.log'), errors='replace'); f.seek(pos); return f.read()


def rig_log_size(x): return os.path.getsize(os.path.join(rig.idir(x), 'rig.log'))


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', '')
    pair = rig.lease('watchfog', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB, 'tag', TAG)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'Mac', 'allow_speedup': True, 'allow_pause': True, 'shared_fog': False},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        log('fog rule on Y', c(Y, cmd='mpm_status').get('settings', {}).get('fog'), 'mpm_fog', c(Y, cmd='mpm_fog'))
        free = next(col for col in tm(X)['columns'] if col['player'] == -1)['model']
        f1 = free[1]
        t0 = st(X)['tick']
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        tm(X, action='click', guid=f1, button=0); time.sleep(1.5); tm(X, action='click', guid=f1, button=1); time.sleep(1.5)
        settle(X, Y, lambda a, b: col_of(b, f1) == 0, 'X takes', timeout=60)
        T.propose(X, [f1])
        settle(X, Y, lambda a, b: b['prop']['shown'] and b['prop']['ok'] == 'Approve', 'guest summary', timeout=60)
        tm(Y, action='approve')
        settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        t_land = max(st(X)['tick'], st(Y)['tick'])
        wait('guest watches', lambda: c(Y, cmd='ui_viewer').get('viewer') == 1, timeout=120, every=2)
        log('guest follows', c(Y, cmd='ui_viewer', action='follow'))
        time.sleep(12)
        v = c(Y, cmd='ui_viewer'); log('guest viewer', {k: v.get(k) for k in ('viewer', 'leader', 'follow', 'cam', 'title')})
        a_cam = c(X, cmd='ui_viewer').get('cam'); log('host cam', a_cam)
        lit_g = shot(Y, f'{TAG}_guest'); lit_h = shot(X, f'{TAG}_host')
        fy, fx = c(Y, cmd='mpm_fog'), c(X, cmd='mpm_fog')
        log('mpm_fog guest', fy, 'host', fx)
        if TAG == 'after':
            check('guest is a watcher on the dungeon map (own 0)', fy.get('watcher') == 1 and fy.get('own') == 0 and fy.get('map') == 1, fy)
            check('guest sees the host\'s vision (visible tiles > 0, = host\'s own)', fy.get('visible', 0) > 0 and fy.get('visible') == fx.get('visible'), f"guest {fy.get('visible')} host {fx.get('visible')}")
            check('host keeps his own rule (not a watcher, own > 0)', fx.get('watcher') == 0 and fx.get('own', 0) > 0, fx)
            check('guest frame shows the map (lit share > 5%)', lit_g > 0.05, lit_g)
            pos = rig_log_size(Y)
            log('force old rule', c(Y, cmd='mpm_fog', watch=0))
            t1 = time.time(); line = ''
            while time.time() - t1 < 20 and not line:
                time.sleep(1); line = next((l for l in rig_log_since(Y, pos).splitlines() if 'ERROR [view_blank]' in l), '')
            log('detector line:', line)
            check('detector fires within 20 s on the old behaviour', bool(line) and 'nothing visible for' in line, line)
            lit_o = shot(Y, 'forced_old_guest')
            # the tiles the guest saw while watching stay explored (dim), so the frame is darker, not black; the int013
            # frame (--tag before, fresh dungeon) is the black one
            check('old behaviour frame darker than the watching frame', 0 <= lit_o < lit_g, f'{lit_o:.3f} < {lit_g:.3f}')
            time.sleep(6)
            n_lines = sum('ERROR [view_blank]' in l for l in rig_log_since(Y, pos).splitlines())
            check('one line per occurrence', n_lines == 1, n_lines)
            c(Y, cmd='mpm_fog', watch=1); time.sleep(4)
            back = rig_log_since(Y, pos)
            check('"visible again" after the rule is back', 'view: tiles visible again' in back, [l for l in back.splitlines() if 'view:' in l or 'fog:' in l][-4:])
            shot(Y, 'restored_guest')
            hx = rig_log_since(X, 0)
            check('host never logged a blank view', 'ERROR [view_blank]' not in hx)
        wait('300 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 300, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick (no desync)', n >= 300 and first is None, f'common ticks {n}, first diff {first}')
        for x in (X, Y):
            s = st(x); log(x, 'desync', s.get('mp', {}).get('desync'), 'det', {k: s.get('det', {}).get(k) for k in ('locked', 'diverged')})
    finally:
        for i in pair:
            try:
                rig.stop(i)
            except Exception as e:
                log('stop', i, e)
        rig.release(pair, 'watchfog')
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER')
        log('RESULT', 'PASS' if results and all(results.values()) and sys.exc_info()[0] is None else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
