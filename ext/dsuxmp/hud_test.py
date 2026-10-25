#!/usr/bin/env python3
"""uxmp-hud test + shots (lane dsuxmp, UX review 2026-10-25 M7 + M5). One hidden loader test copy UXH (make it once with
hud_make_instance.sh UXH): dsload + custom-modes from the shared loader build ~/ds_launcher_wt_dsuxmp/loader/build, and the
multiplayer module = <dylib> (mod.json sha256 rewritten in the copy only). Adapted from the review's shot driver
~/ds_launcher_wt_dsux/ux_uxmp/shoot.py.

  hud_test.py <en|de|ru> <W>x<H> <dylib> [--parts game,hub] [--shots DIR]

<dylib>: a multiplayer build with -DTEAMS_MOUSE_TEST (EXTRA_CFLAGS=-DTEAMS_MOUSE_TEST OUT=<dir> zsh src/build.sh): the
fake mouse goes through the game's own UnityEngine.Input, so a click is decided by the same code as a real one.
The copy starts at UI scale 1.0; the test switches to 1.4 with the game's own UIScaleManager.SetScale (ui_scale_probe).
Real breaks caught (each FAILS on origin/main + the test probes only, ext/dsuxmp/hud_probes_on_main.patch):
  M7a  Bring Units window open over the Teams button: the button is drawn under the window (the game's windows sit above
       its HUD); old: the button (31000) is drawn over the window's title (kit layer 30900).
  M7b  a left click on the window's title where the Teams button is: the window keeps it, the Teams window does not open;
       old: the Teams window opens over the card window.
  M5a  Players panel header at UI 1.4 = 1.4 x its size at 1.0; old: the same size.
  M5b  Teams button at UI 1.4 = 1.4 x; old: the same size.
  M5c  Steam friends picker (fake friends list) at UI 1.4 = 1.4 x (or fit to the screen with 16 px free); old: the same.
Guards (pass on both; they catch a scale done wrong): M5d the open cog list stays inside the game window (16 px free) at 1.4,
M5e a cog click at 1.4 (fake mouse on the drawn cog) closes and opens the cog list (hit tests on the scaled rects).
Shots (never committed): <shots>/<lang>_<W>x<H>_ui<scale>/<state>.png. Prints RESULT lines; exit 0 = all PASS."""
import json, os, sys, time, glob, shutil, subprocess, threading, signal, traceback, hashlib, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
LB = os.path.expanduser('~/ds_launcher_wt_dsuxmp/loader'); LT = os.path.join(LB, 'test')
ap = argparse.ArgumentParser()
ap.add_argument('lang'); ap.add_argument('size'); ap.add_argument('dylib')
ap.add_argument('--parts', default='game,hub'); ap.add_argument('--shots', default=os.path.expanduser('~/ToDelete_2026-10-25/dsuxmp/shots/uxmp-hud'))
ap.add_argument('--run', default=os.path.expanduser('~/ToDelete_2026-10-25/dsuxmp/hud/runs'))
A = ap.parse_args()
LANGS = {'en': 0, 'de': 3, 'ru': 4}
LANG = A.lang; W, H = (int(v) for v in A.size.split('x')); DYLIB = os.path.abspath(os.path.expanduser(A.dylib))
TAGB = f'{LANG}_{W}x{H}'
RUN = os.path.join(A.run, TAGB + time.strftime('_%H%M%S')); os.makedirs(RUN, exist_ok=True)
os.environ['DSL_TEST_COPY'] = 'UXH'; os.environ['LRIG_SIZE'] = f'{W}x{H}'
sys.argv[1:] = [RUN]
sys.path.insert(0, LT)
import l1_saves_test as S  # noqa: E402  (loader test harness: hidden copy, guard, lrig)
T, r = S.T, S.r
X, I = S.X, S.I
assert X == 'UXH' and I.endswith('/UXH'), (X, I)
log = T.log
def btn_ids():                                       # the menu's button ids (enum MPB_* in src/mpmenu.h of this tree)
    import re
    src = open(os.path.join(HERE, '..', '..', 'src', 'mpmenu.h')).read()
    body = re.sub(r'/\*.*?\*/', '', re.search(r'MPB_NONE = 0,(.*?)MPB_COUNT', src, re.S).group(1), flags=re.S)
    names = ['NONE'] + [t.strip()[4:] for t in body.split(',') if t.strip().startswith('MPB_')]
    return {n: i for i, n in enumerate(names)}
BTN = btn_ids()
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public base save
SOLO_G = 'b0c0ffee-0000-4000-8000-0000000000a1'; SOLO_STEM = 'Brightwood__AutoSave_' + SOLO_G.replace('-', '') + '_0'
RES = []


def result(name, ok, detail):
    RES.append((name, ok)); print(f'RESULT {name} {"PASS" if ok else "FAIL"} {detail}', flush=True); log('RESULT', name, ok, detail)


# ------------------------------------------------------------------ install + seed
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
        s['GraphicSettingData']['UIScale'] = 1.0
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
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(T.MODS, 'active.json'), 'DSML_SESSION': os.path.join(T.MODS, 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': S.GSAVES, 'DSL_CLOUD': '0', 'DSL_UPDATE': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': T.STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0',
           'DSRIG_HIDEWIN': '1', 'DSL_CRASHREP_TEST': os.path.join(I, 'crashrep_test'), 'DSRIG_TEST_STEAMFRIENDS': '1',
           'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': 'file://' + os.path.join(I, 'ux_extip.txt')}
    env.update(r.soft_test_env())
    r.require_windowed(X)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', str(W), '-screen-height', str(H)]
    os.makedirs(S.GSAVES, exist_ok=True)
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('copy started pid', p.pid, TAGB)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', 900)
    return p.pid


# ------------------------------------------------------------------ helpers
def c(timeout=60, **k): return r.send(X, k, timeout=timeout, connect_wait=0)
def ms(): return c(cmd='mpm_status')
def st(): return c(cmd='status')
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


SCALE = [1.0]
def shot(name, settle=1.0):
    time.sleep(settle)
    d = os.path.join(A.shots, f'{TAGB}_ui{SCALE[0]:.1f}'); os.makedirs(d, exist_ok=True)
    p = os.path.join(d, name + '.png')
    if os.path.exists(p): os.makedirs(os.path.join(d, 'old'), exist_ok=True); os.replace(p, os.path.join(d, 'old', f'{name}.{time.time():.0f}.png'))
    q = os.path.join(I, 'hud_shots', name + '.png'); os.makedirs(os.path.dirname(q), exist_ok=True)   # the copy writes only to its own folder (or ~/ds_*)
    if os.path.exists(q): os.replace(q, q + '.old')
    a = c(cmd='mpm_shot', path=q, how='backbuffer')
    t0 = time.time()
    while time.time() - t0 < 8 and not os.path.exists(q): time.sleep(0.2)
    time.sleep(0.3)
    if os.path.exists(q): shutil.move(q, p)
    log('shot', name, 'ok' if os.path.exists(p) else f'MISSING {a}')


def set_scale(v):
    a = c(cmd='ui_scale_probe', set=v); SCALE[0] = v
    wait(f'UI scale {v}', lambda: (lambda q: q if abs(q.get('scale', 0) - v) < 0.01 else None)(c(cmd='ui_scale_probe')), 10, 0.3)
    time.sleep(1.5)                                                  # our layouts run a few times a second
    return a


def probe(): return c(cmd='ui_scale_probe')
def wid(rc): return (rc[2] - rc[0]) if rc else 0
def hgt(rc): return (rc[3] - rc[1]) if rc else 0
def centre(rc): return ((rc[0] + rc[2]) / 2, (rc[1] + rc[3]) / 2)
def overlap(a, b): return bool(a and b and a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3])
def ratio_ok(a, b, want=1.4, tol=0.04): return a > 0 and abs(b / a - want) <= tol
def fake_click(x, y, button=0, settle=1.2):
    a = c(cmd='ui_teams', action='mouse', x=x, y=y, button=button); time.sleep(settle); return a


def state_is(*names): return lambda: (lambda s: s if s.get('state') in names else None)(ms())
def safe(name, fn):
    try:
        fn()
    except Exception as e:
        result(name, False, 'driver error ' + repr(e)[:200]); log(traceback.format_exc())


# ------------------------------------------------------------------ host a single-player world: friends picker, Players panel
def part_game():
    press('MULTIPLAYER'); press('HOST_SAVE'); wait('host page', state_is('title_host'), 20)
    press('LOAD_SAVE', 2.5)
    log('pick', c(cmd='load_save', file=os.path.join(I, 'data', 'Saves', SOLO_STEM + '.json'), det=0, timeout=180))
    try:
        wait('single-player copy box', lambda: (lambda q: q if q.get('solo', {}).get('state') == 2 and q.get('box', {}).get('on') else None)(ms()), 600, 2)
        c(cmd='mpm_box', action='cancel')
    except Exception as e:
        log('no solo box', e)
    wait('lobby', state_is('lobby'), 300, 1); time.sleep(3)

    def friends():
        sz = {}
        for v in (1.0, 1.4):
            set_scale(v)
            c(cmd='mpm_friends', action='open')
            f = wait('friends picker', lambda: (lambda q: q if q.get('open') and q.get('win_shown') else None)(c(cmd='mpm_friends')), 15, 0.5)
            time.sleep(1.0); f = c(cmd='mpm_friends'); sz[v] = f.get('win_px')
            shot('15f_steam_friends', 0.5)
            c(cmd='mpm_friends', action='close'); time.sleep(1.0)
        w0, w1, h1 = wid(sz[1.0]), wid(sz[1.4]), hgt(sz[1.4])
        fit = w1 > 0 and min(sz[1.4][0], sz[1.4][1], W - sz[1.4][2], H - sz[1.4][3]) >= 15
        result('M5c_friends_scale', w0 > 0 and (ratio_ok(w0, w1) or (w1 > w0 * 1.05 and fit and (abs(h1 - (H - 32)) < 3 or abs(w1 - (W - 32)) < 3))),
               f'friends picker width {w0:.0f} px at UI 1.0, {w1:.0f} px at 1.4 (ratio {w1 / w0 if w0 else 0:.3f}; 16 px free: {fit}) {sz}')
        set_scale(1.0)
    safe('M5c_friends_scale', friends)
    press('START', 2.0)
    wait('playing', state_is('playing'), 60, 1); time.sleep(4)

    def panel():
        p = {}
        for v in (1.0, 1.4):
            set_scale(v)
            shot('24_hud_host', 0.5)
            q = probe()
            if not q.get('list_open'): c(cmd='ui_click', row=0); time.sleep(1.0)
            shot('24b_players_open', 0.5); p[(v, 'open')] = probe()
            c(cmd='ui_pop', open=1); time.sleep(1.0); shot('25_players_cog', 0.5); p[(v, 'cog')] = probe()
            c(cmd='ui_pop', open=0); time.sleep(0.6)
        a, b = p[(1.0, 'open')], p[(1.4, 'open')]
        h0, h1 = wid(a.get('hdr_px')), wid(b.get('hdr_px'))
        result('M5a_panel_scale', ratio_ok(h0, h1), f'Players panel header {h0:.0f} px at UI 1.0, {h1:.0f} px at 1.4 (ratio {h1 / h0 if h0 else 0:.3f}; layer {a.get("layer")} -> {b.get("layer")}, k {b.get("k")})')
        t0, t1 = wid(a.get('teams_px')), wid(b.get('teams_px'))
        result('M5b_teams_button_scale', ratio_ok(t0, t1), f'Teams button {t0:.0f} px at UI 1.0, {t1:.0f} px at 1.4 (ratio {t1 / t0 if t0 else 0:.3f})')
        cg = p[(1.4, 'cog')].get('plate_px')
        m = min(cg[0], cg[1], W - cg[2], H - cg[3]) if cg else -1
        result('M5d_cog_inside', cg is not None and m >= 15, f'open cog list at UI 1.4: {cg} on {W}x{H}, smallest gap to the edge {m:.0f} px (k {p[(1.4, "cog")].get("k")})')
        # M5e: the cog at UI 1.4, clicked where it is drawn (fake mouse through UnityEngine.Input)
        c(cmd='ui_pop', open=1); time.sleep(1.0)
        g = probe().get('gear_px'); ok = False; det = f'cog {g}'
        if g:
            x, y = centre(g); fake_click(x, y); a1 = probe().get('pop')
            fake_click(x, y); a2 = probe().get('pop')
            ok = a1 == 0 and a2 == 1; det = f'cog drawn at {g}: click -> pop {a1} (want 0), click -> pop {a2} (want 1)'
        result('M5e_cog_click_scaled', ok, det)
        c(cmd='ui_pop', open=0); time.sleep(0.5)
        set_scale(1.0)
    safe('M5_panel', panel)
    c(cmd='mpm_quit_title', timeout=120)
    wait('title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st()), 300, 2)
    wait('menu idle', state_is('idle', 'title_menu', 'failed'), 120, 1); time.sleep(3)
    s = ms()
    if s.get('box', {}).get('on'): c(cmd='mpm_box', action='confirm'); time.sleep(1)


# ------------------------------------------------------------------ the Hub, PvP Match, Bring Units over the Teams button
def part_hub():
    s = ms()
    for _ in range(4):
        if s.get('state') in ('idle', 'failed'): break
        press('BACK'); s = ms()
    press('MULTIPLAYER'); press('HOST_FE'); wait('hub page', state_is('title_host'), 20)
    press('HOST_START', 2.0)
    wait('hub loaded + host session', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None)(st()), 900, 2)
    wait('hub lobby', state_is('lobby'), 120, 1); time.sleep(3)
    # the hub starts only with units brought: Bring Units, tick 2 cards, Bring (fake mouse on the drawn parts)
    for _ in range(4):
        press('FE_BRINGWIN', 2.0)
        if c(cmd='mpm_bring_state').get('open') == 1: break
    time.sleep(2.0)
    for k in range(2):
        tp = c(cmd='mpm_bring_state').get('tgt_px', {})
        if f'tick{k}' in tp: x, y = centre(tp[f'tick{k}']); fake_click(x, y, settle=1.5)
    bs = c(cmd='mpm_bring_state'); log('bring picked', bs.get('picked'), bs.get('msg'))
    if 'bring' in bs.get('tgt_px', {}): x, y = centre(bs['tgt_px']['bring']); fake_click(x, y, settle=2.5)
    if c(cmd='mpm_bring_state').get('open'): log('bring window still open: closing'); c(cmd='mpm_bring_close'); time.sleep(1)
    press('START', 3.0)
    wait('hub playing', state_is('playing'), 180, 1); time.sleep(4)
    shot('36_hub_hud', 1.0)

    def bring_open():
        c(cmd='mpm_esc'); time.sleep(2)
        log('pvp esc', c(cmd='mpm_pvp', action='press_esc')); time.sleep(2.5)
        log('pvp bring', c(cmd='mpm_pvp', action='bring'))
        return wait('Bring Units window', lambda: (lambda q: q if q.get('open') == 1 else None)(c(cmd='mpm_bring_state')), 20, 0.5)

    def bring_close():
        if c(cmd='mpm_bring_state').get('open'): c(cmd='mpm_bring_close'); time.sleep(1)
        log('pvp close', c(cmd='mpm_pvp', action='press_close')); time.sleep(1)

    def m7():
        for v in (1.0, 1.4):
            set_scale(v)
            bring_open(); time.sleep(2.0)
            bs = c(cmd='mpm_bring_state'); ts = c(cmd='ui_teams'); q = probe()
            win, tb = bs.get('win_px'), q.get('teams_px')
            shot('41_pvp_bring', 0.5)
            ov = overlap(win, tb)
            hud_o, win_o = ts.get('hud_order'), bs.get('win_order')
            if not ov:
                result(f'M7a_drawn_under_ui{v}', False, f'no overlap to test: Teams button {tb}, window {win} (precondition)')
            else:
                result(f'M7a_drawn_under_ui{v}', hud_o is not None and win_o is not None and hud_o < win_o,
                       f'Teams button {tb} over the window {win}: button canvas order {hud_o}, window order {win_o} (want button < window)')
            if v == 1.0:
                if ov:
                    x, y = centre([max(win[0], tb[0]), max(win[1], tb[1]), min(win[2], tb[2]), min(win[3], tb[3])])
                    fake_click(x, y, settle=2.0)
                    ts2 = c(cmd='ui_teams'); bs2 = c(cmd='mpm_bring_state')
                    result('M7b_click_stays_in_window', ts2.get('open') == 0 and ts2.get('modal') == 0 and bs2.get('open') == 1,
                           f'left click at {x:.0f},{y:.0f} (window title, on the Teams button): Teams window open {ts2.get("open")} (want 0), Bring Units open {bs2.get("open")} (want 1)')
                    if ts2.get('open'): c(cmd='ui_teams', action='close'); time.sleep(1)
                else:
                    result('M7b_click_stays_in_window', False, 'no overlap to test (precondition)')
            bring_close()
        set_scale(1.0)
    safe('M7', m7)


def main():
    g = threading.Thread(target=T.guard, daemon=True)
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    install(); seed()
    g.start()
    try:
        start(); time.sleep(4)
        for part in A.parts.split(','):
            t0 = time.time()
            try:
                {'game': part_game, 'hub': part_hub}[part]()
            except Exception as e:
                result('part_' + part, False, 'driver error ' + repr(e)[:300]); log(traceback.format_exc())
                if part == 'game':
                    try:
                        c(cmd='mpm_quit_title', timeout=60); wait('title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st()), 300, 2); time.sleep(5)
                    except Exception: pass
            log(f'part {part}: {time.time() - t0:.0f} s')
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
    print(f'RESULT hud_test {TAGB} {np_} PASS, {nf} FAIL (multiplayer {os.path.basename(os.path.dirname(DYLIB))}/{os.path.basename(DYLIB)})', flush=True)
    sys.exit(0 if nf == 0 and np_ > 0 else 1)


if __name__ == '__main__':
    main()
