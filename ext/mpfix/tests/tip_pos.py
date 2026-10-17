#!/usr/bin/env python3
"""Hover card position (owner v12 item 3, lane mpfix-ui): a card for an element inside a panel is OUTSIDE that panel, at its
right edge, top aligned with the element; no room on the right = outside its left edge. Never over the panel.
One hidden, muted copy (one leased pair, its first copy). Elements: title column (Multiplayer page), host page (column +
a Rules row), lobby window, Esc menu, HUD cog (Players panel gear + one row of its settings box).

  tip_pos.py --dylib PATH [--lane mpfix-ui] [--keep-frames]

Each hover: the ui_tip status (rig `ui_tip` action probe): screen_rect = the drawn card, panel_rect = the element's panel,
elem_rect = the element, side = right / left. Check: card shown on screen, anchored, and
  right: card x0 >= panel x1;  left: card x1 <= panel x0;  in both cases no overlap with the panel; card top = element top
  (unless kept on screen at the bottom). Frames (own check only, never to the owner) in ext/mpfix/out/<tag>/."""
import json, os, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
arg = lambda k, d=None: A[A.index(k) + 1] if k in A else d
rig.DYLIB = os.path.abspath(arg('--dylib', os.path.join(ROOT, 'build', 'dsrig.dylib')))
LANE = f"{arg('--lane', 'mpfix-ui')}-{os.getpid()}"
TAG = time.strftime('tippos_%H%M%S')
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, BACK=3, LOAD_SAVE=7, START=10, HOST_SAVE=121, FE_CANCEL=110)
results, rows = {}, []


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = None
    for _ in range(60):
        pair = rig.lease(LANE, os.getpid())
        if pair: break
        time.sleep(30)
    if not pair: log('FAIL not run: no free pair'); sys.exit(1)
    X = pair[0]
    c = lambda **k: rig.send(X, k, timeout=120)
    st = lambda: c(cmd='status')
    press = lambda n: (c(cmd='mpm_press', button=B[n]), time.sleep(1.5))

    def clear():
        c(cmd='mpm_tip', title='-none-'); c(cmd='ui_tip_hover', title='-none-'); c(cmd='ui_tip', action='hide'); time.sleep(0.5)

    def probe(where, title, how='mpm_tip'):
        clear()
        c(cmd=how, title=title); time.sleep(1.5)
        t0 = time.time()
        while time.time() - t0 < 12:                                   # a window still building (lobby, HUD): wait for the card
            p = c(cmd='ui_tip', action='chain')                       # the probe + the element's ancestors (diagnosis)
            if p.get('shown') == 1 and p.get('on_screen') == 1 and not p.get('align_routine'): break
            time.sleep(0.5)
        time.sleep(0.5); p = c(cmd='ui_tip', action='chain')
        f = os.path.join(OUT, f'{len(rows):02d}_{where.replace(" ", "_")}.png')
        if '--keep-frames' in A: c(cmd='mpm_shot', how='backbuffer', path=f)
        q, pr, er, side = p.get('screen_rect') or [0] * 4, p.get('panel_rect') or [0] * 4, p.get('elem_rect') or [0] * 4, p.get('side')
        overlap = q[0] < pr[2] and q[2] > pr[0] and q[1] < pr[3] and q[3] > pr[1]
        top_ok = abs(q[3] - er[3]) <= 2 or q[1] <= 1                  # top at the element's top, or kept on screen at the bottom
        if side == 'right': place_ok = q[0] >= pr[2] - 0.5
        elif side == 'left': place_ok = q[2] <= pr[0] + 0.5
        else: place_ok = False
        ok = p.get('shown') == 1 and p.get('on_screen') == 1 and p.get('anchored') == 1 and place_ok and not overlap and top_ok
        rows.append(dict(where=where, title=title, ok=ok, side=side, card=q, panel=pr, elem=er, screen=p.get('screen')))
        check(f'{where}: "{title}" card outside its panel ({side})', ok,
              f'card {q} panel {pr} elem {er} side {side} overlap {overlap} top_ok {top_ok} shown {p.get("shown")} on_screen {p.get("on_screen")}')
        if not ok or '--chain' in A: log('  chain', json.dumps(p.get('chain')))
        return p

    def fe_later():                                                     # a copy with old expedition results: its window ("Later")
        for _ in range(3):
            w = c(cmd='mpm_fewin')
            if not w.get('mode'): return
            log('expedition window open (mode', w.get('mode'), '): Later'); press('FE_CANCEL'); time.sleep(1)

    busy = [x for x in pair if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
    if busy:
        log('copies already running outside this lease:', busy); rig.release(pair, LANE); return
    stamp = time.strftime('%Y%m%d-%H%M%S')
    for f in ('mp.json',):
        p = os.path.join(rig.idir(X), f)
        if os.path.exists(p): os.replace(p, f'{p}.before_tippos_{stamp}')
    ui = os.path.join(rig.idir(X), 'data', 'mp_ui.json')               # tooltips on, the Players panel open
    os.makedirs(os.path.dirname(ui), exist_ok=True)
    if os.path.exists(ui): os.replace(ui, f'{ui}.before_tippos_{stamp}')
    open(ui, 'w').write('{"see_through":0,"names":1,"list_open":1,"tooltips":1}\n')
    log('pair', pair, 'copy', X, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        rig.start(X, window=True, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{X} title', timeout=1500)
        time.sleep(3); fe_later()
        # 1. title column: the Multiplayer page
        press('MULTIPLAYER'); time.sleep(1)
        probe('title column', 'Your Save')
        probe('title column', 'Back')
        # 2. host page: column + a Rules row (the Rules panel is always open now)
        press('HOST_SAVE'); time.sleep(1)
        ms = c(cmd='mpm_status')
        log('host page state', ms.get('state'))
        probe('host page', 'Host from a Save')
        probe('host page rules', 'Units per Player')
        # 3. lobby: host alone
        clear()
        press('LOAD_SAVE'); time.sleep(3)
        save = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        c(cmd='load_save', file=src, det=0)
        t0 = time.time()
        while time.time() - t0 < 600:
            s = st()
            if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') and c(cmd='mpm_status').get('state') == 'lobby': break
            time.sleep(2)
        time.sleep(4); fe_later()
        log('lobby state', c(cmd='mpm_status').get('state'))
        probe('lobby', 'Start')
        # 4. Esc menu (in game)
        clear()
        c(cmd='mpm_press', button=B['START']); time.sleep(6)
        log('after Start', c(cmd='mpm_status').get('state'))
        c(cmd='mpm_esc'); time.sleep(2)
        probe('Esc menu', 'Leave Multiplayer')
        clear(); c(cmd='mpm_key', key='escape'); time.sleep(2)
        # 5. HUD cog: the Players panel gear, then a row of its settings box
        probe('HUD cog', 'Settings', how='ui_tip_hover')
        c(cmd='ui_pop', open=1); time.sleep(1)
        probe('HUD cog box', 'Show Tooltips on Hover', how='ui_tip_hover')
        c(cmd='ui_pop', open=0)
        clear()
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        try: rig.stop(X)
        except Exception as e: log('stop', X, e)
        rig.release(pair, LANE)
        log('prefs identical after:', rig.check_prefs())
        json.dump(dict(results=results, rows=rows), open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        n = sum(1 for k, v in results.items() if k != 'error' and v)
        log(f'RESULT {n}/{len([k for k in results if k != "error"])} pass', 'ERROR' if 'error' in results else '', OUT)


main()
