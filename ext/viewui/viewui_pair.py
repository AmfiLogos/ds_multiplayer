#!/usr/bin/env python3
"""viewui lane (owner 2026-10-15): friend's view while following, "sees this too", Players panel stays with Teams,
Teams fold bar, Teams button setting. One leased pair (lane viewui), hidden windows 1512x982, host the owner, guest Ben,
base save MoonlightBrigade (same setup as src/ui_test/coop_pair.py). Frames: ext/viewui/shots/*.png (not committed).
  viewui_pair.py --dylib PATH"""
import json, os, subprocess, sys, time, traceback
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import uxgame_pair as U
rig = U.rig
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')
os.makedirs(SHOTS, exist_ok=True)
results = {}
c, st, wait, mark, log_since = U.c, U.st, U.wait, U.mark, U.log_since


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])


def coop(x, **k):
    try: return rig.send(x, dict(cmd='ui_coop', **k), timeout=120)
    except Exception as e: return {'ok': False, 'error': repr(e)}


def tm(x, **k): return c(x, cmd='ui_teams', **k)


def shot(x, name):
    p = os.path.join(SHOTS, f'{name}.png'); r = c(x, cmd='mpm_shot', how='backbuffer', path=p); log('frame', p, r.get('ok')); return p


def vw(r, pl): return next((v for v in r.get('views', []) if v['player'] == pl), {})


def step(name, fn):
    log('--- step', name)
    try: fn()
    except Exception: check('step ' + name + ' ran', False, traceback.format_exc()[-600:])


def run(X, Y, PORT):
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
    for i in (X, Y):
        os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
        open(os.path.join(rig.idir(i), 'data', 'mp_ui.json'), 'w').write('{"list_open":1}\n')
        tb = os.path.join(rig.idir(i), 'data', 'mp_teams_button.json')
        if os.path.exists(tb): os.rename(tb, tb + time.strftime('.prev_%Y%m%d_%H%M%S'))
    U.start(X, 1512, 982)
    wait('title host', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(X)), timeout=1500, every=5)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    import shutil; src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
    log('host load', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
    U.start(Y, 1512, 982)
    wait('title guest', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(Y)), timeout=1500, every=5)
    wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900, every=3)
    time.sleep(8)
    t_fp = st(X).get('tick', 0)
    us = [u for u in c(X, cmd='list_units', map=0).get('units', []) if u['faction'] == 1 and not u['dead'] and 'cell' in u]
    cx, cy = us[0]['cell']
    a, b = coop(X), coop(Y)
    check('setup: markers on, dsnet views present', a.get('coop_ok') == 1 and b.get('coop_ok') == 1 and a.get('net') == 1, f"{a.get('coop_ok')} {b.get('coop_ok')} {a.get('net')}")

    def same():
        coop(X, action='zoom', x=10); coop(Y, action='zoom', x=10)
        coop(X, action='camera', x=cx + 0.5, y=cy + 0.5); coop(Y, action='camera', x=cx + 0.5, y=cy + 0.5); time.sleep(4)
        v = vw(coop(X), 1)
        check('item 2: same spot -> "sees this too" (same 1, no corners)', v.get('same') == 1 and v.get('brackets') == 0, v)
        shot(X, 'same_spot_host')
        coop(Y, action='zoom', x=16); coop(Y, action='camera', x=cx + 2.5, y=cy + 1.5); time.sleep(4)   # Ben sees more, my screen is inside his
        v = vw(coop(X), 1)
        check('item 2: my screen inside the friend\'s view -> "sees this too"', v.get('same') == 1 and v.get('brackets') == 0, v)
        shot(X, 'inside_host')
        coop(Y, action='zoom', x=10); coop(Y, action='camera', x=cx + 12.5, y=cy + 4.5); time.sleep(4)   # part overlap
        v = vw(coop(X), 1)
        check('part overlap: corners, no "sees this too"', v.get('same') == 0 and v.get('brackets') == 1, v)
        shot(X, 'overlap_host')
    step('same', same)

    def follow():
        coop(X, action='set', key='offscreen', value=1)
        coop(X, action='follow', player=1); time.sleep(2)
        bad = []
        for k, (dx, dy) in enumerate(((20, 0), (-15, 8), (40, 25), (0, 0))):
            coop(Y, action='camera', x=cx + dx + 0.5, y=cy + dy + 0.5)
            for _ in range(6):
                time.sleep(0.25); r = coop(X); v = vw(r, 1)
                if r.get('follow') != 1: bad.append(('follow stopped', r.get('follow')))
                if v.get('brackets') or v.get('edge') or v.get('same'): bad.append((k, v))
        check('item 1: following Ben: no corners, no edge mark, no "sees this too" for Ben (24 samples while he moves)', not bad, bad[:4])
        shot(X, 'follow_host')
        coop(X, action='unfollow'); time.sleep(1)
        coop(X, action='camera', x=cx - 40.5, y=cy - 30.5); time.sleep(3)
        v = vw(coop(X), 1)
        check('after Stop following: the off-screen mark comes back (option on)', v.get('edge') == 1, v)
        coop(X, action='set', key='offscreen', value=0)
    step('follow', follow)

    def teams():
        r = tm(X, action='open'); time.sleep(2)
        t = tm(X); p = c(X, cmd='ui_view')
        check('item 3: Teams open -> Players panel stays (plate active, hide_why 0)', t.get('open') == 1 and p.get('plate', {}).get('active') == 1 and p.get('hide_why') == 0, f"open {t.get('open')} plate {p.get('plate')} why {p.get('hide_why')}")
        check('item 3: the Teams button stays while Teams is open', t.get('hud_shown') == 1, t.get('hud_shown'))
        check('Hide link placed on the window', t.get('hide_rect', [-1])[0] >= 0, f"hide {t.get('hide_rect')} win {t.get('win_rect')}")
        shot(X, 'teams_open_host')
        tm(X, action='tfold', on=1); time.sleep(1)
        t = tm(X)
        check('item 4: Teams folds to the bar (not modal, window off, bar on)', t.get('tfold') == 1 and t.get('modal') == 0 and t.get('bar_rect', [-1])[0] >= 0 and t.get('win_rect', [0])[0] == -1, f"{t.get('tfold')} {t.get('modal')} bar {t.get('bar_rect')} win {t.get('win_rect')}")
        shot(X, 'teams_folded_host')
        tm(X, action='close'); time.sleep(1); tm(X, action='open'); time.sleep(1)
        t = tm(X)
        jf = open(os.path.join(rig.idir(X), 'data', 'mp_teams_button.json')).read() if os.path.exists(os.path.join(rig.idir(X), 'data', 'mp_teams_button.json')) else ''
        check('item 4: the fold is remembered (reopen folded, saved in mp_teams_button.json)', t.get('tfold') == 1 and '"folded":1' in jf, f"{t.get('tfold')} {jf.strip()}")
        tm(X, action='tfold', on=0); time.sleep(1); t = tm(X)
        check('Show: the whole window again (modal)', t.get('tfold') == 0 and t.get('modal') == 1 and t.get('win_rect', [-1])[0] >= 0, f"{t.get('tfold')} {t.get('modal')} {t.get('win_rect')}")
        tm(X, action='close'); time.sleep(1)
        r1 = c(X, cmd='ui_click', row=0); r2 = c(X, cmd='ui_click', row=0)
        check('item 4: Players panel collapses / opens (header toggle, saved list_open)', r1.get('list_open') != r2.get('list_open'), f"{r1.get('list_open')} -> {r2.get('list_open')}")
    step('teams', teams)

    def teams_button():
        coop(X, action='set', key='teams_button', value=0); time.sleep(2)
        t = tm(X); ui = open(os.path.join(rig.idir(X), 'data', 'mp_ui.json')).read()
        check('item 5: "Teams button: Hide" hides the Teams button (saved)', t.get('hud_shown') == 0 and '"teams_button":0' in ui, f"{t.get('hud_shown')}")
        c(X, cmd='ui_pop', open=1); time.sleep(1); shot(X, 'cog_teams_button_hidden')
        c(X, cmd='ui_pop', open=0)
        coop(X, action='set', key='teams_button', value=1); time.sleep(2)
        check('... "Show" brings it back', tm(X).get('hud_shown') == 1)
    step('teams_button', teams_button)

    time.sleep(5)
    n, first = U.fp_equal_from_xy(X, Y, t_fp)
    check('fingerprints equal over the run (all view-only)', first is None and n > 50, f'common ticks {n}, first diff {first}')


def main():
    rig.DYLIB = sys.argv[sys.argv.index('--dylib') + 1]
    pair = subprocess.run([sys.executable, os.path.join(rig.ROOT, 'rig', 'rig.py'), 'lease', 'viewui'], capture_output=True, text=True).stdout.strip().split()[-1]
    X, Y = pair[0], pair[1]; PORT = rig.pair_port(X)
    log('pair', pair, 'port', PORT, 'dylib', rig.DYLIB)
    try:
        run(X, Y, PORT)
    finally:
        for x in (X, Y):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        subprocess.run([sys.executable, os.path.join(rig.ROOT, 'rig', 'rig.py'), 'release', pair, 'viewui'])
        log('prefs', 'same' if rig.check_prefs() else 'DIFFER (restored)')
        ok = results and all(results.values())
        log('RESULT', 'PASS' if ok else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
