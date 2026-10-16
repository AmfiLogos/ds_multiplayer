#!/usr/bin/env python3
"""register_host_choice2 (MPF API for lane fepvp) in the real menu. One leased pair, its first copy (hidden, muted).

  choice2_pair.py --dylib PATH [--lane mpfix-ui] [--keep-frames]

The rig test command mpm_testchoice registers two dummy choices beside the Foreign Expedition (old API, src fallback):
"Test Arena" (own panel: draw_page) and "Test Hub" (draw_page NULL: the Expedition Rules panel). Its page_start queues
the stock load of a save already in data/Saves (what a mod does); lobby_start answers "started" (both can be told to fail).
Checks: 1. the three choices under "Host a game" with the stock gap; 2. Arena page = Start + Back, no Load save / New
campaign, draw_page gets a RectTransform every frame, then draw_page(NULL) once on Back; 3. Hub page = Start + Back, the
Expedition Rules open without Done, draw_page not called; 4. FE (old API) page unchanged (Load save, no Start);
5. page_start error shown on the page; 6. Start -> page_start -> loading -> lobby, draw_page(NULL) on close, host_choice_now
= test-arena; 7. lobby_start error: lobby stays, notice; 8. lobby Start -> lobby_start -> playing. Frames: ext/mpfix/out/<tag>/."""
import json, os, re, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
arg = lambda k, d=None: A[A.index(k) + 1] if k in A else d
rig.DYLIB = os.path.abspath(arg('--dylib', os.path.join(ROOT, 'build', 'dsrig.dylib')))
LANE = arg('--lane', 'mpfix-ui')
TAG = time.strftime('choice2_%H%M%S')
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, BACK=3, LOAD_SAVE=7, NEW_CAMPAIGN=8, START=10, JOIN_ADDR=14, INFO=19, HOST_SAVE=121, HOST_FE=123,
         C1=124, C2=125, HOST_START=127, FE_CANCEL=110)
results = {}


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
        time.sleep(10)
    if not pair: log('no free pair'); return
    X = pair[0]
    c = lambda **k: rig.send(X, k, timeout=120)
    ms = lambda: c(cmd='mpm_status')
    tc = lambda **k: c(cmd='mpm_testchoice', **k)
    press = lambda n: (c(cmd='mpm_press', button=B[n]), time.sleep(1.5))
    act = lambda s, n: bool(s.get('title', {}).get(str(B[n]), {}).get('active'))
    shot = lambda name: c(cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'{name}.png')) if '--keep-frames' in A else None

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    busy = [x for x in pair if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
    if busy:
        log('copies already running outside this lease:', busy); rig.release(pair, LANE); return
    stamp = time.strftime('%Y%m%d-%H%M%S')
    p = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(p): os.replace(p, f'{p}.before_choice2_{stamp}')
    log('pair', pair, 'copy', X, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        # the save page_start loads: in data/Saves under its SaveName (as a mod would have it)
        save = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
        name = re.search(rb'"SaveName"\s*:\s*"([^"]*)"', open(save, 'rb').read()).group(1).decode()
        rig.start(X, window=True, fps=30)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{X} title', timeout=1500)
        sd = os.path.join(rig.idir(X), 'data', 'Saves'); os.makedirs(sd, exist_ok=True)
        shutil.copy2(save, os.path.join(sd, name + '.json'))
        time.sleep(3)
        for _ in range(3):                                   # old expedition results: their window first ("Later")
            if not c(cmd='mpm_fewin').get('mode'): break
            press('FE_CANCEL')
        sf = sorted(ms().get('stock_frames', []), key=lambda r: -r[3])
        sgaps = [round(sf[i][1] - sf[i + 1][3], 1) for i in range(len(sf) - 1)]
        stock_gap = min(sgaps) if sgaps else None
        r1 = tc(action='register', id='test-arena', label='Test Arena', tip='A dummy mode with its own page.', draw=1, save=name, page_err='', lobby_err='')
        r2 = tc(action='register', id='test-hub', label='Test Hub', tip='A dummy mode with the Expedition Rules.', draw=0)
        log('registered', r1.get('rc'), r2.get('rc'), json.dumps(r2.get('choices')))
        # Custom Modes may have registered real choices first (Foreign Expedition, PvP): take the button ids from the list
        chs = r2.get('choices') or []
        btn = {ch['id']: ch['button'] for ch in chs}
        B['C1'], B['C2'] = btn.get('test-arena', 124), btn.get('test-hub', 125)
        fe_v2 = next((ch.get('v2') for ch in chs if ch['id'] == 'foreign-expedition'), 0)
        # 1. three choices, stock gap
        press('MULTIPLAYER'); time.sleep(1)
        s = ms(); fr = s.get('frames', {}); t = s.get('title', {})
        col = sorted([fr[str(i)] for i in (B['HOST_SAVE'], 122, *[ch['button'] for ch in chs], B['JOIN_ADDR']) if str(i) in fr], key=lambda r: -r[3])   # 122 New save
        gaps = [round(col[i][1] - col[i + 1][3], 1) for i in range(len(col) - 1)]
        labels = [t.get(str(ch['button']), {}).get('text') for ch in chs]
        shot('1_multiplayer_page')
        check('1. Foreign Expedition, Test Arena, Test Hub under "Host a game", in registration order, stock gap',
              r1.get('rc') == 0 and r2.get('rc') == 0 and labels == [ch['label'] for ch in chs] and labels[0] == 'Foreign Expedition' and labels[-2:] == ['Test Arena', 'Test Hub'] and len(col) == len(chs) + 3
              and stock_gap and all(abs(g - stock_gap) <= 1 for g in gaps), f'labels {labels} stock gap {stock_gap} gaps {gaps}')
        # 2. Arena page: Start + Back, draw_page with a panel, then NULL once on Back
        press('C1'); time.sleep(1)
        s = ms(); q = tc()
        shot('2_arena_page')
        ok2a = s.get('state') == 'title_host' and not act(s, 'LOAD_SAVE') and not act(s, 'NEW_CAMPAIGN') and act(s, 'HOST_START') and act(s, 'BACK')
        ok2b = q.get('draw_calls', 0) > 3 and q.get('panel_rt') == 1 and q.get('panel_on') == 1 and q.get('draw_null') == 0 and q.get('choice_now') == 'test-arena'
        n_draw = q.get('draw_calls', 0)
        press('BACK'); time.sleep(1); q2 = tc()
        check('2. Test Arena page: Start + Back only; draw_page(panel) each frame, draw_page(NULL) once on Back',
              ok2a and ok2b and q2.get('draw_null') == 1 and q2.get('choice_now') == '' and q2.get('panel_on') == 0,
              f'state {s.get("state")} start {act(s, "HOST_START")} load {act(s, "LOAD_SAVE")} new {act(s, "NEW_CAMPAIGN")} | {json.dumps({k: q.get(k) for k in ("draw_calls", "panel_rt", "panel_on", "panel_rect", "choice_now")})} | after Back {json.dumps({k: q2.get(k) for k in ("draw_calls", "draw_null", "panel_on", "choice_now")})}')
        # 3. Hub page: Expedition Rules open without Done, draw_page not called
        press('C2'); time.sleep(1)
        s = ms(); q = tc(); rp = s.get('rules_panel', {})
        shot('3_hub_page')
        check('3. Test Hub page: Start + Back, the Expedition Rules open without Done, no draw_page',
              s.get('state') == 'title_host' and act(s, 'HOST_START') and not act(s, 'LOAD_SAVE') and rp.get('fe') == 1 and rp.get('fe_done') == 0
              and rp.get('title') == 0 and q.get('draw_calls') == q2.get('draw_calls') and q.get('choice_now') == 'test-hub' and q.get('panel_on') == 0,
              f'rules {rp} draw {q.get("draw_calls")} now {q.get("choice_now")}')
        press('BACK')
        # 4. FE page: old API (src fallback) = Load save, no Start; registered by Custom Modes with register_host_choice2 and
        #    no draw_page = Start + Back, no Load save. Both: the Expedition Rules open, no Done.
        press('HOST_FE'); time.sleep(1); s = ms(); rp = s.get('rules_panel', {})
        check('4. Foreign Expedition page: ' + ('v2 (Custom Modes): Start, no Load save' if fe_v2 else 'old API: Load save, no Start') + ', Expedition Rules open, no Done',
              s.get('state') == 'title_host' and act(s, 'LOAD_SAVE') == (not fe_v2) and act(s, 'HOST_START') == bool(fe_v2)
              and rp.get('fe') == 1 and not rp.get('fe_done'),
              f'v2 {fe_v2} load {act(s, "LOAD_SAVE")} start {act(s, "HOST_START")} rules {rp}')
        press('BACK')
        # 5. page_start error shown on the page
        press('C1'); tc(action='set', page_err='Test: no arena map.'); press('HOST_START'); time.sleep(1)
        s = ms(); q = tc()
        shot('5_page_error')
        check('5. page_start error: shown on the page, the page stays',
              s.get('state') == 'title_host' and s.get('msg') == 'Test: no arena map.' and act(s, 'INFO') and q.get('page_calls') == 1,
              f'state {s.get("state")} msg {s.get("msg")!r} info {t.get(str(B["INFO"]))} page_calls {q.get("page_calls")}')
        # 6. Start -> page_start -> loading -> lobby
        tc(action='set', page_err=''); nn = tc().get('draw_null')
        press('HOST_START')
        wait('lobby', lambda: (lambda st: st.get('state') == 'lobby' and st.get('obs', {}).get('ready'))(ms()), timeout=600)
        time.sleep(3)
        s = ms(); q = tc(); st = c(cmd='status')
        for _ in range(3):
            if not c(cmd='mpm_fewin').get('mode'): break
            press('FE_CANCEL')
        shot('6_lobby')
        check('6. Start: page_start queued the load, the menu waited like Your save -> lobby (session hosted); draw_page(NULL) on close',
              q.get('page_calls') == 2 and s.get('state') == 'lobby' and st.get('mp', {}).get('started') and q.get('draw_null') == nn + 1
              and q.get('choice_now') == 'test-arena',
              f'page_calls {q.get("page_calls")} state {s.get("state")} mp {st.get("mp", {}).get("started")} draw_null {nn}->{q.get("draw_null")} now {q.get("choice_now")}')
        # 7. lobby_start error: the lobby stays
        tc(action='set', lobby_err='Test: the arena is not ready.')
        c(cmd='mpm_press', button=B['START']); time.sleep(2)
        s = ms(); q = tc()
        check('7. lobby_start error: the lobby stays', q.get('lobby_calls') == 1 and s.get('state') == 'lobby', f'lobby_calls {q.get("lobby_calls")} state {s.get("state")}')
        # 8. lobby Start -> lobby_start -> playing
        tc(action='set', lobby_err='')
        c(cmd='mpm_press', button=B['START']); time.sleep(4)
        s = ms(); q = tc()
        shot('8_playing')
        check('8. lobby Start: lobby_start called, the game runs', q.get('lobby_calls') == 2 and s.get('state') == 'playing' and q.get('choice_now') == 'test-arena',
              f'lobby_calls {q.get("lobby_calls")} state {s.get("state")} now {q.get("choice_now")}')
    except Exception as e:
        log('ERROR', repr(e)); results['error'] = repr(e)
    finally:
        try: rig.stop(X)
        except Exception as e: log('stop', X, e)
        rig.release(pair, LANE)
        log('prefs identical after:', rig.check_prefs())
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        n = sum(1 for k, v in results.items() if k != 'error' and v)
        log(f'RESULT {n}/{len([k for k in results if k != "error"])} pass', 'ERROR' if 'error' in results else '', OUT)


main()
