#!/usr/bin/env python3
"""mpoverlay lane (owner 2026-10-17): the guest's "Watching X's expedition" box and the "X sees this too" line next to
the Teams window; the Players panel when Teams opens; the "Free" chip on a released unit's portrait (coordinator).
One leased pair (lane mpoverlay), hidden windows, muted, nice 19 + background priority, notch fix off.
Host X = the owner (takes one free unit and proposes), guest Y = Ben (no units: approves with 0 and watches).
  overlay_pair.py --dylib PATH --out DIR --host-lang ja --guest-lang en --host-res 1600x900 --guest-res 1512x982 [--tag T]
Frames + one JSON per frame (texts, rects) in DIR. Lang codes: en ko fr de ru zh-Hans zh-Hant ja es pt-BR."""
import json, os, shutil, sys, time, traceback
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import uxgame_pair as U
import teams_pair as T
rig = U.rig
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
results = {}


def arg(name, default): return U.arg(name, default)


OUT = arg('--out', '/tmp/mpoverlay'); os.makedirs(OUT, exist_ok=True)
TAG = arg('--tag', 'run')
LANGS, RES = {}, {}
LOGF = open(os.path.join(OUT, f'{TAG}.log'), 'a')


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:700])


c, st, wait = U.c, U.st, U.wait
def tm(inst, **k): return c(inst, cmd="ui_teams", **k)
def coop(inst, **k): return rig.send(inst, dict(cmd="ui_coop", **k), timeout=120)
def viewer(inst, **k): return c(inst, cmd="ui_viewer", **k)


prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = CODES.index(LANGS[x]); s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang
os.environ['DSL_NOTCH'] = '0'


def shot(x, name):
    p = os.path.join(OUT, f'{TAG}_{name}.png')
    r = c(x, cmd='mpm_shot', how='backbuffer', path=p)
    info = {'view': c(x, cmd='ui_view'), 'viewer': viewer(x), 'teams': {k: v for k, v in tm(x).items() if k in ('open', 'tfold', 'modal', 'win_rect', 'hud_shown', 'hide_rect')}}
    json.dump(info, open(p[:-4] + '.json', 'w'), ensure_ascii=False, indent=1)
    log('frame', p, r.get('ok'))
    return info


def step(name, fn):
    log('--- step', name)
    try: fn()
    except Exception: check('step ' + name + ' ran', False, traceback.format_exc()[-700:])


def overlap(a, b):
    """screen rects [x, y, w, h] (y up) overlap"""
    return a and b and a[2] > 0 and b[2] > 0 and a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]


def main():
    import hashlib
    global prefs, h0
    prefs, h0 = '', ''
    rig.DYLIB = arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib'))
    pair = None
    for _ in range(240):                                     # the machine-wide limit is 4 pairs: wait up to 40 min
        pair = rig.lease('mpoverlay', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]; port = rig.PAIR_PORTS.get(pair, 7795)
    LANGS[X], LANGS[Y] = arg('--host-lang', 'en'), arg('--guest-lang', 'en')
    RES[X] = [int(v) for v in arg('--host-res', '1512x982').split('x')]; RES[Y] = [int(v) for v in arg('--guest-res', '1512x982').split('x')]
    log('pair', pair, 'dylib', rig.DYLIB, 'langs', LANGS, 'res', RES)
    try:
        # the owner may be playing (his prefs then differ from the backup by his own hand): only prove this run leaves them as they are
        import hashlib
        prefs = os.path.expanduser('~/Library/Preferences/com.CanOpener.Dungeon-Settlers.plist')
        h0 = hashlib.sha256(open(prefs, 'rb').read()).hexdigest() if os.path.exists(prefs) else ''
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):
            d = os.path.join(rig.idir(i), 'data'); os.makedirs(d, exist_ok=True)
            open(os.path.join(d, 'mp_ui.json'), 'w').write('{"list_open":1}\n')
            for fn in ('mp_teams_button.json', 'mp_ui_viewer.json'):
                f = os.path.join(d, fn)
                if os.path.exists(f): os.rename(f, f + time.strftime('.prev_%Y%m%d_%H%M%S'))
        U.start(X, *RES[X])
        wait('title host', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(X)), timeout=1500, every=5)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
        log('host load', c(X, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
        U.start(Y, *RES[Y])
        wait('title guest', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(Y)), timeout=1500, every=5)
        wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900, every=3)
        time.sleep(8)
        free = next(col for col in tm(X)['columns'] if col['player'] == -1)['model']
        f1 = free[0]
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        tm(X, action='click', guid=f1, button=0); time.sleep(1.5); tm(X, action='click', guid=f1, button=1); time.sleep(1.5)
        T.settle(X, Y, lambda a, b: T.col_of(b, f1) == 0, 'host takes', timeout=60)
        log('propose', T.propose(X, [f1]))
        T.settle(X, Y, lambda a, b: b.get('prop', {}).get('shown') and b['prop'].get('ok'), 'guest summary', timeout=300)
        log('approve', tm(Y, action='approve'))
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(8)
        for x in (X, Y):
            if tm(x).get('open'): tm(x, action='close')
        us = [u for u in c(X, cmd='list_units', map=1).get('units', []) if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        cx, cy = us[0]['cell']
        for x in (X, Y): coop(x, action='zoom', x=10); coop(x, action='camera', x=cx + 0.5, y=cy + 0.5)
        time.sleep(4)
        g = shot(Y, 'guest_watching')
        check('guest watches (viewer box on)', g['viewer'].get('viewer') == 1, g['viewer'])
        h = shot(X, 'host_game')

        def panel_teams():   # task 2: the Players panel before / after the Teams window opens (both peers)
            for x, who in ((X, 'host'), (Y, 'guest')):
                b = c(x, cmd='ui_view')
                tm(x, action='open'); time.sleep(2.5)
                info = shot(x, f'{who}_teams_open')
                a = info['view']
                log(who, 'panel before', b.get('plate'), 'hide_why', b.get('hide_why'), '| after', a.get('plate'), 'hide_why', a.get('hide_why'), 'teams', info['teams'])
                if x == Y:
                    vb = info['viewer']
                    log('guest box', vb.get('box'), 'hidden', vb.get('hidden'), 'aside', vb.get('aside'), 'box_px', vb.get('box_px'), 'sees_px', vb.get('sees_px'), 'win_px', vb.get('win_px'))
                    if 'hidden' in vb:      # new build: shown box / line never on the open window
                        check(f'{TAG}: Teams open: the box is off the window', not vb.get('box_px') or not overlap(vb.get('box_px'), vb.get('win_px')), vb)
                        check(f'{TAG}: Teams open: the "sees this too" line is off the window', not vb.get('sees_px') or not overlap(vb.get('sees_px'), vb.get('win_px')), vb)
                # the same window 8 s later (the top layer goes above the window only when plist_frame runs: every 18 frames)
                f0 = st(x).get('frames', 0); time.sleep(2); f1 = st(x).get('frames', 0)
                log(who, 'frames per second now', (f1 - f0) / 2.0)
                time.sleep(4); shot(x, f'{who}_teams_open_8s')
                tm(x, action='close'); time.sleep(1.5)
        step('panel_teams', panel_teams)

        def hide_show():     # task 1: the Hide / Show control (new build only)
            v = viewer(Y)
            if 'hidden' not in v: log('old build: no hide control'); return
            # the box comes back from "aside" on viewer_frame (every 12 frames: ~2.5 s at the copies' 5 fps)
            wait('box back after Teams closed', lambda: (lambda q: q.get('aside') == 0 and q.get('link_px'))(viewer(Y)), timeout=60, every=1)
            r = viewer(Y, action='click_hide'); time.sleep(4)
            info = shot(Y, 'guest_hidden')
            vb = info['viewer']
            check(f'{TAG}: one click on Hide: the box folds to the "Watching / Show" bar, no "sees this too"', vb.get('hidden') == 1 and not vb.get('sees_px'), vb)
            tm(Y, action='open'); time.sleep(2.5); shot(Y, 'guest_hidden_teams_open'); tm(Y, action='close'); time.sleep(1.5)
            jf = os.path.join(rig.idir(Y), 'data', 'mp_ui_viewer.json')
            check(f'{TAG}: hidden is remembered (mp_ui_viewer.json)', os.path.exists(jf) and '"hide":1' in open(jf).read(), open(jf).read() if os.path.exists(jf) else '')
            wait('hidden bar back after Teams closed', lambda: (lambda q: q.get('aside') == 0 and q.get('link_px'))(viewer(Y)), timeout=60, every=1)
            viewer(Y, action='click_show'); time.sleep(4)
            vb = shot(Y, 'guest_shown_again')['viewer']
            check(f'{TAG}: one click on Show: the whole box again', vb.get('hidden') == 0 and vb.get('box', [0, 0, 0])[2] >= 60, vb)
        step('hide_show', hide_show)

        def free_chip():     # coordinator: release one unit in the dungeon -> "Free" chip on its portrait card
            r = c(X, cmd='mp_release', guid=f1); log('mp_release', r); time.sleep(3)
            log('owner now', c(X, cmd='ui_owner', guid=f1), c(Y, cmd='ui_owner', guid=f1))
            lang_dir = os.path.join(os.path.dirname(rig.DYLIB), 'lang')
            for x, who in ((X, 'host'), (Y, 'guest')):
                word = json.load(open(os.path.join(lang_dir, LANGS[x] + '.json'))).get('Free', 'Free')
                v8 = c(x, cmd='ui_v8'); shot(x, f'{who}_free_chip')
                check(f'{TAG}: "Free" chip ({word}) on the released unit\'s card ({who})', any(word in t for t in v8.get('chips', [])), v8.get('chips'))
        step('free_chip', free_chip)
    except Exception:
        check('run reached the end', False, traceback.format_exc()[-700:])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try: rig.stop(i)
                except Exception as e: log('stop', i, e)
            rig.release(pair, 'mpoverlay')
            h1 = hashlib.sha256(open(prefs, 'rb').read()).hexdigest() if os.path.exists(prefs) else ''
            check('owner prefs file unchanged by this run', h0 == h1, f'{h0[:12]} -> {h1[:12]}')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
