#!/usr/bin/env python3
"""Lobby window Quit / Turn off multiplayer (owner 2026-10-20) + the Steam lobby's hidden address block, on one pair.
  Round 1 (English, host looks like a Steam lobby: DSRIG_TEST_STEAMFRIENDS=1 on the host only):
    host lobby: address block hidden, "Show address for friends without Steam" shows it; guest lobby: only Quit;
    host Quit -> box -> confirm: host on the title, guest told "The host left the game".
  Round 2 (German, address lobby): guest lobby Quit -> guest on the title (no box); the guest joins again;
    host Turn off multiplayer -> box -> confirm: host stays in the world, session over, time runs, a stock move order
    works; guest told "The host left the game".
Shots (game frames, mpm_shot): <out>/*.png. Usage: lobbyquit_pair.py [--dylib build/dsrig.dylib] [--out DIR]"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

if '--dylib' in sys.argv:
    rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1])
OUT = os.path.abspath(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else os.path.join(os.getcwd(), 'lobbyquit_out')
os.makedirs(OUT, exist_ok=True)
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, LEAVE=12, JOIN_ADDR=14, CONNECT=15, CONFIRM=68, QUIT=130, SHOW_ADDR=131)
results = {}
LANG = {}                                    # copy -> game LanguageType (3 = German), read by the prepare_data wrapper
_prep = rig.prepare_data


def _prep_lang(x):
    _prep(x)
    us = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG.get(x, 0); json.dump(s, open(us, 'w'), indent=1)


rig.prepare_data = _prep_lang


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def owner_playing():
    """the owner's own game: his launcher, its game copy (DSModLauncher/games/, binary "Dungeon Settlers"), the modded app,
    or the Steam game. Test copies elsewhere (instances, other lanes' scratch copies) never count."""
    import subprocess
    r = subprocess.run(['pgrep', '-fl', 'Contents/MacOS/(DungeonSettlers|Dungeon Settlers|DSModLauncher)'], capture_output=True, text=True).stdout
    own = ('/Library/Application Support/DSModLauncher/games/', '/Applications/Dungeon Settlers Modded.app/', '/steamapps/common/Dungeon Settlers/', '/Applications/DSModLauncher.app/')
    return [l for l in r.splitlines() if any(o in l.split(' -')[0] for o in own)]


def owner_wait():
    n = 0
    while owner_playing():
        if n % 10 == 0:
            log('the owner plays: waiting')
        n += 1; time.sleep(60)


def main():
    pair = None
    for _ in range(60):                                       # other lanes hold the pairs: wait up to 30 min
        pair = rig.lease(f'lobbyquit-{os.getpid()}')
        if pair:
            break
        time.sleep(30)
    assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    ms = lambda x: c(x, cmd='mpm_status')
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.5))[0]

    def wait(what, pred, timeout=600, every=1):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                r = pred()
            except Exception:
                r = None
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def shot(x, name):
        for how in ('backbuffer', 'camera'):                                 # the game's own frame; camera = canvases re-rendered
            p = os.path.join(rig.idir(x), f'lq_{name}_{how}.png'); r = c(x, cmd='mpm_shot', path=p, how=how)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f'{name}_{how}.png'))
            log('shot', name, how, r.get('ok'))

    def lw_buttons(x):
        return {int(k) for k in (ms(x).get('lw', {}).get('buttons') or {})}

    def start_pair(lang, steam_host):
        for x in (H, G):
            rig.stop(x)
        time.sleep(2)
        LANG[H] = LANG[G] = lang
        owner_wait()
        if steam_host:
            os.environ['DSRIG_TEST_STEAMFRIENDS'] = '1'
        rig.start(H, fps=30, window=True)
        os.environ.pop('DSRIG_TEST_STEAMFRIENDS', None)
        rig.start(G, fps=30, window=True)
        for x in (H, G):
            rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        time.sleep(3)

    def host_and_join():
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        c(H, cmd='load_save', file=src, det=0)
        wait('host lobby', lambda: ms(H).get('state') in ('lobby', 'MPM_LOBBY', 6) or ms(H).get('lw', {}).get('active'), timeout=600)
        return join()

    def join():
        port = ms(H).get('port')
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest lobby window', lambda: ms(G).get('lw', {}).get('active'), timeout=900)
        time.sleep(4)

    def guest_told_host_left(log_from):
        """the guest's session ended with code 1 (MPM_END_HOST_LEFT: "The host left the game" in its language), then the title"""
        wait('guest on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=60)
        time.sleep(1)
        with open(os.path.join(rig.idir(G), 'rig.log'), 'rb') as f:                 # byte offset (names are UTF-8)
            f.seek(log_from); lines = f.read().decode('utf-8', 'replace').splitlines()
        ended = [l for l in lines if 'mp: session ended (' in l]
        return bool(ended) and 'session ended (1)' in ended[-1], ended[-1:] or ['(no session end in the guest log)']

    def glog_size():
        return os.path.getsize(os.path.join(rig.idir(G), 'rig.log'))

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    from demo import make_demo_save
    work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
    log('pair', pair, 'dylib', rig.DYLIB, 'out', OUT)
    try:
        # ---------------- round 1: English, Steam lobby
        if '--only2' not in sys.argv:
            start_pair(0, True)
            host_and_join()
            m = ms(H); b = lw_buttons(H)
            check('1. host lobby (Steam): Quit + Turn off multiplayer + Start shown', {B['QUIT'], B['LEAVE'], B['START']} <= b, f'buttons {sorted(b)}')
            check('1. host lobby (Steam): address hidden, Show address button shown', not m['lw'].get('addr') and B['SHOW_ADDR'] in b, f"addr {m['lw'].get('addr')!r}")
            shot(H, 'en_host_lobby_steam_hidden')
            bg = lw_buttons(G)
            check('1. guest lobby: only Quit (no Turn off multiplayer)', B['QUIT'] in bg and B['LEAVE'] not in bg, f'buttons {sorted(bg)}')
            shot(G, 'en_guest_lobby')
            press(H, 'SHOW_ADDR'); time.sleep(1); m = ms(H); b = lw_buttons(H)
            check('1. Show address: the address block shows, the button goes', m['lw'].get('addr') and B['SHOW_ADDR'] not in b and B['QUIT'] in b,
                  f"addr {m['lw'].get('addr')!r} note {m['lw'].get('inv_note')!r}")
            check('1. one Same home line', (m['lw'].get('inv_note') or '').count('Same home') == 1, repr(m['lw'].get('inv_note')))
            shot(H, 'en_host_lobby_steam_shown')
            press(H, 'QUIT'); time.sleep(1); m = ms(H)
            check('1. host Quit: confirm box first', m.get('box', {}).get('on') and m.get('confirm_leave') == 2, f"box {m.get('box')}")
            shot(H, 'en_host_quit_box')
            g0 = glog_size(); c(H, cmd='mpm_click', button=B['CONFIRM'])
            t0 = time.time()
            wait('host on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(H)), timeout=60)
            check('1. host Quit: host on the title', True, f'{round(time.time() - t0, 1)} s')
            ok, mg = guest_told_host_left(g0)
            check('1. host Quit: guest told the host left', ok, mg)
            shot(H, 'en_host_after_quit_title'); shot(G, 'en_guest_after_host_quit')

        # ---------------- round 2: German, address lobby
        start_pair(3, False)
        host_and_join()
        m = ms(H); b = lw_buttons(H)
        check('2. DE host lobby (address): address shown, no Show address button', m['lw'].get('addr') and B['SHOW_ADDR'] not in b, f"addr {m['lw'].get('addr')!r}")
        shot(H, 'de_host_lobby_address'); shot(G, 'de_guest_lobby')
        press(G, 'QUIT')
        wait('guest on the title after its Quit', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=60)
        check('2. guest Quit: on the title, no box', not ms(G).get('box', {}).get('on'), '')
        shot(G, 'de_guest_after_quit_title')
        time.sleep(3); join()
        press(H, 'LEAVE'); time.sleep(1); m = ms(H)
        check('2. host Turn off multiplayer: confirm box first', m.get('box', {}).get('on') and m.get('confirm_leave') == 1, f"box {m.get('box')}")
        shot(H, 'de_host_turnoff_box')
        g0 = glog_size(); c(H, cmd='mpm_click', button=B['CONFIRM']); time.sleep(4)
        ok, mg = guest_told_host_left(g0)
        check('2. Turn off: guest told the host left', ok, mg)
        s1 = st(H); m = ms(H)
        check('2. Turn off: host still in the world, session over', s1.get('scene') == 2 and not s1.get('mp', {}).get('started'), f"scene {s1.get('scene')} mp {s1.get('mp')}")
        log('host speed after Turn off', {k: s1.get(k) for k in ('speed', 'requested_speed', 'speed_lock')})
        # the lobby held the world paused; the player's play button (SetGameSpeedRequested, the stock speed event) runs it
        if not s1.get('speed'):
            log('paused as in the lobby: the play button', c(H, cmd='set_speed', speed=1)); time.sleep(1)
        t1 = st(H).get('tick'); time.sleep(5); s2 = st(H); t2 = s2.get('tick')
        check('2. Turn off: time runs (solo)', t2 > t1, f"tick {t1} -> {t2}, speed {s2.get('speed')}")
        us = c(H, cmd='list_units')['units']
        u = next(u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u)
        r = rig.send(H, {'cmd': 'order_move', 'guid': u['guid'], 'x': u['cell'][0] + 3, 'y': u['cell'][1]}); time.sleep(6)
        u2 = next(x for x in c(H, cmd='list_units')['units'] if x['guid'] == u['guid'])
        check('2. Turn off: a move order works (solo)', r.get('ok') and u2.get('cell') != u['cell'], f"{u['cell']} -> {u2.get('cell')} ({r})")
        shot(H, 'de_host_after_turnoff_world'); shot(G, 'de_guest_after_host_turnoff')
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())
        print('RESULT', 'PASS' if results and all(results.values()) else 'FAIL')


if __name__ == '__main__':
    main()
