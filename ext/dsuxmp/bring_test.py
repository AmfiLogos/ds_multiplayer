#!/usr/bin/env python3
"""Bring Units card window: CARD RULE + CLOSE RULE (lane dsuxmp2, sub-lane dsuxmp2-bring; owner 2026-10-25).
One hidden loader test copy UBR (make it once: ext/dsuxmp/bring_make_instance.sh UBR) with the shared loader build
~/ds_launcher_wt_dsuxmp2/loader/build (dsload.dylib + mods_all/mac, kit with the game's red X) and the multiplayer module
<dylib> (mod.json sha256 rewritten in the copy only). Hidden, small, windowed, muted, never focused; take a copy slot first
(~/ToDelete_2026-10-25/dsuxmp2/slot.sh take dsuxmp2-bring 1).

  bring_test.py <en|de|ru> <W>x<H> <ui scale> <dylib> [--tag NAME]

<dylib>: LOADER=~/ds_launcher_wt_dsuxmp2/loader zsh ext/dsuxmp/build_mouse.sh <out> (the loader's multiplayer module with
the fake mouse, -DTEAMS_MOUSE_TEST: a click goes through the game's own UnityEngine.Input, so the game's EventSystem decides
what it hits, as for a real click). Before-build: origin/main + the read-only probe ext/dsuxmp/bring_probes_on_main.patch
(the drawn rects and the hover-card state in mpm_bring_state; no behaviour change).
The hub host lobby (Multiplayer > Hub > Start), then its Bring Units window (FE_BRINGWIN). Real breaks caught:
  CARD_middle   a click on a card's middle (not its check box) does not tick it, or a second click does not untick it.
  CARD_icon     a click on an icon inside a card (a worn item, with the game's hover card) does not tick / untick it.
  X_shown       the window has no red X (mpm_esc: the window's red X shown, inside the window's top-right corner, above
                the card list).
  X_closes      a click on the red X does not close the window, or acts as Bring (Cancel's effect: nothing sent to the
                host, fe_status received unchanged; the tick is not kept: the window opened again shows none ticked).
Guards (pass on both; they catch a break of this change): HOVER_card (the game's hover card still shows on a card icon),
BRING_sends (a ticked card + Bring still sends the unit: received + 1).
Shots (never committed): ~/ToDelete_2026-10-25/dsuxmp2/shots/dsuxmp2-bring/<lang>_<W>x<H>_ui<s>[_NAME]/.
Prints RESULT lines; exit 0 = all PASS."""
import json, os, sys, time, glob, shutil, subprocess, threading, signal, traceback, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.dirname(os.path.dirname(HERE))
LB = os.path.expanduser('~/ds_launcher_wt_dsuxmp2/loader'); LT = os.path.join(LB, 'test')   # shared loader build (read only)
LANGS = {'en': 0, 'de': 3, 'ru': 4}
args = sys.argv[1:]; NAME = ''
if '--tag' in args: i = args.index('--tag'); NAME = args[i + 1]; del args[i:i + 2]
LANG, SIZE, SCALE, DYLIB = args[0], args[1], float(args[2]), os.path.abspath(os.path.expanduser(args[3]))
W, H = (int(v) for v in SIZE.split('x'))
TAG = f'{LANG}_{W}x{H}_ui{SCALE:.1f}' + (f'_{NAME}' if NAME else '')
BASE = os.path.expanduser('~/ToDelete_2026-10-25/dsuxmp2')
OUT = os.path.join(BASE, 'shots', 'dsuxmp2-bring', TAG); os.makedirs(OUT, exist_ok=True)
RUN = os.path.join(BASE, 'dsuxmp2-bring', 'runs', TAG + time.strftime('_%H%M%S')); os.makedirs(RUN, exist_ok=True)
os.environ['DSL_TEST_COPY'] = 'UBR'; os.environ['LRIG_SIZE'] = f'{W}x{H}'
sys.argv[1:] = [RUN]
sys.path.insert(0, LT)
import l1_saves_test as S  # noqa: E402  (loader test harness: hidden copy, guard, lrig)
T, r = S.T, S.r
X, I = S.X, S.I
assert X == 'UBR' and I.endswith('/UBR'), (X, I)
log = T.log
BTN = json.load(open(os.path.join(HERE, 'btn.json')))
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public base save
SOLO_G = 'b0c0ffee-0000-4000-8000-0000000000a1'; SOLO_STEM = 'Brightwood__AutoSave_' + SOLO_G.replace('-', '') + '_0'
RES = []


def result(name, ok, detail):
    RES.append((name, ok)); print(f'RESULT {name} {"PASS" if ok else "FAIL"} {detail}', flush=True); log('RESULT', name, ok, detail)


# ------------------------------------------------------------------ install + seed (as hud_test.py)
def install():
    app = os.path.join(I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    ld = os.path.join(fw, 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(ld): os.replace(ld, aside)                      # codesign refuses loose files in Frameworks
    shutil.copy2(os.path.join(LB, 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp'))
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    os.makedirs(md, exist_ok=True)
    old = os.path.join(I, 'old'); os.makedirs(old, exist_ok=True)
    for m in ('multiplayer', 'custom-modes'):
        d = os.path.join(md, m)
        if os.path.exists(d): shutil.move(d, os.path.join(old, f'{m}.{time.time():.0f}'))
        shutil.copytree(os.path.join(LB, 'build', 'mods_all', 'mac', m), d)
    mp = os.path.join(md, 'multiplayer')                              # this build's module; its sha256 in the copy's mod.json
    shutil.copy2(DYLIB, os.path.join(mp, 'multiplayer.dylib.tmp')); os.replace(os.path.join(mp, 'multiplayer.dylib.tmp'), os.path.join(mp, 'multiplayer.dylib'))
    mj = json.load(open(os.path.join(mp, 'mod.json')))
    mj['platforms']['mac-arm64']['files']['multiplayer.dylib'] = hashlib.sha256(open(DYLIB, 'rb').read()).hexdigest()
    json.dump(mj, open(os.path.join(mp, 'mod.json'), 'w'), indent=1)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    if os.path.isdir(aside): os.replace(aside, ld)
    os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(LB, 'lang', '*.json')): shutil.copy2(f, ld)
    log('installed', sorted(os.listdir(md)), 'multiplayer =', DYLIB)


def world_save(path, guid, stem, clan):
    w = json.load(open(TPL)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


def seed():
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for p in ('data/Saves', 'mp_solo.txt', 'mp_tags.txt', 'mp_worlds.txt', 'mp_menu.json', 'mp.json', 'mp_history.json', 'mp_ui.json',
              'mp_teams_button.json', 'data/fe', 'data/mp_owners', 'data/SaveHeaderCache.json', 'data/Mods/worlds.json', 'data/Mods/save_copies'):
        q = os.path.join(I, p)
        if os.path.exists(q): shutil.move(q, os.path.join(old, p.replace('/', '_')))
    sv = os.path.join(I, 'data', 'Saves'); os.makedirs(sv)
    world_save(os.path.join(sv, SOLO_STEM + '.json'), SOLO_G, SOLO_STEM, 'Brightwood')
    json.dump({'name': 'Tester'}, open(os.path.join(I, 'mp_menu.json'), 'w'))                # never the Mac account name in shots
    cache = os.path.join(I, 'data', 'Mods', 'multiplayer', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
    open(os.path.join(I, 'ux_extip.txt'), 'w').write('203.0.113.7\n')     # documentation address: no web lookup
    cr = os.path.join(I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')


def start():
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANGS[LANG]; s['GeneralSettingData']['IsLanguageInitialized'] = True
        s['GeneralSettingData']['HiddenUpdateNoticeVersion'] = 1
        s['GraphicSettingData']['UIScale'] = SCALE
        json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep
    T.require_hidden_copy(os.path.join(I, 'DungeonSettlers.app'))
    r.cap_wait(f'{X}: start')
    r.prepare_data(X)
    os.makedirs(T.MODS, exist_ok=True)
    en = ['multiplayer', 'custom-modes']
    json.dump({'enabled': en, 'order': en}, open(os.path.join(T.MODS, 'active.json'), 'w'))
    json.dump({'worlds': {}}, open(os.path.join(T.MODS, 'worlds.json'), 'w'))
    if os.path.exists(r.sockpath(X)): os.unlink(r.sockpath(X))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': 'tester', 'LOGNAME': 'tester',
           'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '30', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(T.MODS, 'active.json'), 'DSML_SESSION': os.path.join(T.MODS, 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': S.GSAVES, 'DSL_CLOUD': '0', 'DSL_UPDATE': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': T.STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0',
           'DSRIG_HIDEWIN': '1', 'DSL_CRASHREP_TEST': os.path.join(I, 'crashrep_test'),
           'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': 'file://' + os.path.join(I, 'ux_extip.txt')}
    env.update(r.soft_test_env())
    r.require_windowed(X)
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
         '-screen-width', str(W), '-screen-height', str(H)]
    os.makedirs(S.GSAVES, exist_ok=True)
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('copy started pid', p.pid, TAG)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', 900)
    return p.pid


# ------------------------------------------------------------------ helpers
def c(timeout=60, **k): return r.send(X, k, timeout=timeout, connect_wait=0)
def ms(): return c(cmd='mpm_status')
def st(): return c(cmd='status')
def bs(): return c(cmd='mpm_bring_state')
def received(): return c(cmd='fe_status').get('received')


def press(n, pause=1.3):
    a = c(cmd='mpm_press', button=BTN[n] if isinstance(n, str) else n); time.sleep(pause); return a


def wait(what, pred, limit=120, every=0.5):
    t0 = time.time(); last = None
    while time.time() - t0 < limit:
        try:
            last = pred()
            if last: return last
        except Exception as e:
            last = repr(e)
        time.sleep(every)
    raise RuntimeError(f'timeout ({limit} s) waiting for {what}: {str(last)[:300]}')


def state_is(*names): return lambda: (lambda s: s if s.get('state') in names else None)(ms())
def centre(rc): return ((rc[0] + rc[2]) / 2, (rc[1] + rc[3]) / 2)
def inside(a, b, tol=1): return bool(a and b and a[0] >= b[0] - tol and a[1] >= b[1] - tol and a[2] <= b[2] + tol and a[3] <= b[3] + tol)


def fake_click(x, y, settle=1.0):
    """the fake mouse rests on (x, y) ~10 frames, presses, releases, and stays there ~100 frames (pixels, y up)"""
    a = c(cmd='ui_teams', action='mouse', x=x, y=y, button=0); time.sleep(settle); return a


def shot(name, settle=0.8):
    time.sleep(settle)
    p = os.path.join(OUT, name + '.png')
    if os.path.exists(p): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(p, os.path.join(OUT, 'old', f'{name}.{time.time():.0f}.png'))
    q = os.path.join(I, 'bring_shots', name + '.png'); os.makedirs(os.path.dirname(q), exist_ok=True)   # the copy writes only in its own folder
    if os.path.exists(q): os.replace(q, q + '.old')
    a = c(cmd='mpm_shot', path=q, how='backbuffer')
    t0 = time.time()
    while time.time() - t0 < 8 and not os.path.exists(q): time.sleep(0.2)
    time.sleep(0.3)
    if os.path.exists(q): shutil.move(q, p)
    log('shot', name, 'ok' if os.path.exists(p) else f'MISSING {a}')


def safe(name, fn):
    try:
        fn()
    except Exception as e:
        result(name, False, 'driver error ' + repr(e)[:200]); log(traceback.format_exc())


def open_bring():
    for _ in range(4):
        press('FE_BRINGWIN', 2.0)
        if bs().get('open') == 1: break
    s = wait('Bring Units window with cards', lambda: (lambda q: q if q.get('open') == 1 and q.get('cards', 0) >= 1 and 'row0' in q.get('tgt_px', {}) else None)(bs()), 20, 0.5)
    time.sleep(1.5)
    return bs()


# ------------------------------------------------------------------ the checks
def run():
    press('MULTIPLAYER'); press('HOST_FE'); wait('hub page', state_is('title_host'), 20)
    press('HOST_START', 2.0)
    wait('hub loaded + host session', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None)(st()), 900, 2)
    wait('hub lobby', state_is('lobby'), 120, 1); time.sleep(3)
    s = open_bring(); shot('01_bring_open')
    tp = s.get('tgt_px', {})
    log('bring state', json.dumps(s)[:1500])
    json.dump(s, open(os.path.join(RUN, 'bring_open.json'), 'w'), indent=1)

    def card_middle():
        r0, tk = tp['row0'], tp.get('tick0')
        x, y = centre(r0)
        p0 = bs().get('picked'); fake_click(x, y); p1 = bs().get('picked'); fake_click(x, y); p2 = bs().get('picked')
        off_box = tk is None or x > tk[2] + 4
        result('CARD_middle', off_box and (p1, p2) == (p0 + 1, p0),
               f'click at ({x:.0f},{y:.0f}), the middle of card 0 {r0} (check box {tk}): picked {p0} -> {p1} -> {p2} (want {p0 + 1} then {p0})')
    safe('CARD_middle', card_middle)

    def card_icon():
        ic = tp.get('icon0')
        if not ic: result('CARD_icon', False, 'no icon in card 0 (precondition)'); result('HOVER_card', False, 'no icon'); return
        x, y = centre(ic)
        p0 = bs().get('picked'); fake_click(x, y, 0.6); q1 = bs(); p1 = q1.get('picked'); shot('02_icon_hover', 0.0)
        fake_click(x, y, 0.6); q2 = bs(); p2 = q2.get('picked')
        result('CARD_icon', (p1, p2) == (p0 + 1, p0), f'click at ({x:.0f},{y:.0f}) on card 0\'s first item icon {ic}: picked {p0} -> {p1} -> {p2} (want {p0 + 1} then {p0})')
        t1, t2 = q1.get('tip', {}), q2.get('tip', {})
        result('HOVER_card', bool(t1.get('shown')) or bool(t2.get('shown')),
               f'the game\'s hover card with the mouse on that icon: shown "{t1.get("shown")}" / "{t2.get("shown")}" (tip state {t1})')
    safe('CARD_icon', card_icon)

    def x_close():
        q = bs(); win, r0 = q.get('win_px'), q.get('tgt_px', {}).get('row0')
        e = c(cmd='mpm_esc'); json.dump(e, open(os.path.join(RUN, 'mpm_esc.json'), 'w'), indent=1)
        ws = [w for w in (e.get('windows') or []) if 'Bring Units' in (w.get('name') or '')]
        if 'windows' not in e: c(cmd='mpm_return_game'); time.sleep(1)   # an older build: mpm_esc opened the Esc menu
        xr = ws[0].get('x_rect') if ws else None
        ok = bool(ws) and ws[0].get('x') == 1 and inside(xr, win) and xr[0] >= (win[0] + win[2]) / 2 and r0 is not None and xr[1] >= r0[3]
        result('X_shown', ok, f'mpm_esc: {ws or "no Bring Units window listed (" + json.dumps(e)[:160] + ")"}; window {win}, card 0 top {r0[3] if r0 else None}; Cancel button {"shown" if "cancel" in q.get("tgt_px", {}) else "gone"}')
        # Cancel's effect: tick card 0, click the red X: the window closes, nothing is sent, the tick is not kept
        tk = q.get('tgt_px', {}).get('tick0')
        if q.get('picked') == 0 and tk: fake_click(*centre(tk))
        p_before, n0 = bs().get('picked'), received()
        xb = bs().get('tgt_px', {}).get('x') or xr
        if not xb:
            result('X_closes', False, f'no red X to click (picked {p_before}, received {n0})'); c(cmd='mpm_bring_close'); time.sleep(1); return
        fake_click(*centre(xb), settle=1.5)
        try: wait('window closed', lambda: (lambda s: s if s.get('open') == 0 else None)(bs()), 5, 0.3); closed = True
        except Exception: closed = False
        shot('03_after_x', 1.0)
        time.sleep(2.0); n1 = received()
        s2 = open_bring() if closed else bs(); p_again = s2.get('picked')
        result('X_closes', closed and p_before == 1 and n1 == n0 and p_again == 0,
               f'red X {xb} clicked with {p_before} ticked: window closed {closed}, units received by the host {n0} -> {n1} (want unchanged), opened again: {p_again} ticked (want 0)')
    safe('X', x_close)

    def bring_sends():
        q = bs()
        if not q.get('open'): q = open_bring()
        tq = q.get('tgt_px', {}); n0 = received()
        if q.get('picked') == 0: fake_click(*centre(tq['tick0']))
        fake_click(*centre(bs().get('tgt_px', {})['bring']), settle=1.5)
        try: n1 = wait('unit received', lambda: (lambda n: n if n is not None and n > (n0 or 0) else None)(received()), 10, 0.5)
        except Exception: n1 = received()
        result('BRING_sends', bs().get('open') == 0 and n1 == (n0 or 0) + 1, f'tick + Bring: window open {bs().get("open")}, units received {n0} -> {n1}')
    safe('BRING_sends', bring_sends)


def main():
    g = threading.Thread(target=T.guard, daemon=True)
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    install(); seed()
    g.start()
    try:
        start(); time.sleep(4)
        log('ui scale', c(cmd='mpm_status').get('ui_scale'))
        try:
            run()
        except Exception as e:
            result('driver', False, 'driver error ' + repr(e)[:300]); log(traceback.format_exc())
            try: shot('zz_fail', 0.3)
            except Exception: pass
    finally:
        T.stop_guard.set()
        if T.violations: result('guard', False, str(T.violations))
        for p in T.game_pids():
            try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(4)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'rig.log', 'player.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(RUN, f))
    np_, nf = sum(1 for _, ok in RES if ok), sum(1 for _, ok in RES if not ok)
    print(f'RESULT bring_test {TAG} {np_} PASS, {nf} FAIL (multiplayer {os.path.basename(DYLIB)})', flush=True)
    sys.exit(0 if nf == 0 and np_ > 0 else 1)


if __name__ == '__main__':
    main()
