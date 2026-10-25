#!/usr/bin/env python3
"""uxmp-text shot driver (lane dsuxmp; adapted from the dsux review driver ~/ds_launcher_wt_dsux/ux_uxmp/shoot.py).
One hidden loader test copy UXT (ext/dsuxmp/make_instance_text.sh) with the shared loader build of lane dsuxmp
(~/ds_launcher_wt_dsuxmp/loader: dsload + mods_all/mac multiplayer + custom-modes, read only) and THIS worktree's
multiplayer module (ext/dsuxmp/build_module_text.sh -> build/module/multiplayer.dylib) and lang tables. Shoots the screens the uxmp-text fixes change: M4 (Your save load
list of a first-time host), M8 (lobby, host Esc menu, its box), M11 (Title Case labels), M12 (refusal box).
  shoot_text.py <en|de|ru> <W>x<H> <ui scale> [parts=title,game]
Take the lane's copy slot first (ext/dsuxmp/run_text.sh does). Shots (LOCAL ONLY, never commit):
$DSUXMP_SHOTS/<lang>_<W>x<H>_ui<scale>/<state>.png, default <worktree>/build/shots_text (git-ignored; the rig writes
only under ~/ds_*); run_text.sh then moves them to ~/ToDelete_2026-10-25/dsuxmp/shots/text."""
import json, os, sys, time, glob, shutil, subprocess, threading, signal, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
MPW = os.path.dirname(os.path.dirname(HERE))                                      # this worktree
LB = os.path.expanduser('~/ds_launcher_wt_dsuxmp/loader'); LT = os.path.join(LB, 'test')
LANGS = {'en': 0, 'de': 3, 'ru': 4}
LANG, SIZE, SCALE = sys.argv[1], sys.argv[2], float(sys.argv[3])
PARTS = (sys.argv[4] if len(sys.argv) > 4 else 'title,game').split(',')
W, H = (int(v) for v in SIZE.split('x'))
TAG = f'{LANG}_{W}x{H}_ui{SCALE:.1f}'
SHOTS = os.environ.get('DSUXMP_SHOTS', os.path.join(MPW, 'build', 'shots_text'))   # the rig writes only under ~/ds_* (run_text.sh moves them)
OUT = os.path.join(SHOTS, TAG); os.makedirs(OUT, exist_ok=True)
RUN = os.path.join(SHOTS, 'runs', TAG + time.strftime('_%H%M%S')); os.makedirs(RUN, exist_ok=True)
os.environ['DSL_TEST_COPY'] = 'UXT'; os.environ['LRIG_SIZE'] = f'{W}x{H}'
sys.argv[1:] = [RUN]
sys.path.insert(0, LT)
import l1_saves_test as S
T, r = S.T, S.r
X, I = S.X, S.I
assert X == 'UXT' and I.endswith('/UXT'), (X, I)
log = T.log
BTN = json.load(open(os.path.join(HERE, 'btn.json')))
TR = {} if LANG == 'en' else json.load(open(os.path.join(MPW, 'lang', LANG + '.json')))
tr = lambda s: TR.get(s, s) or s
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public base save
SOLO_G = 'b0c0ffee-0000-4000-8000-0000000000a1'; SOLO_STEM = 'Brightwood__AutoSave_' + SOLO_G.replace('-', '') + '_0'
NOTES = []


def note(k, v):
    NOTES.append({'state': k, 'info': v}); log('note', k, str(v)[:300])


# ------------------------------------------------------------------ install + seed
def install():
    app = os.path.join(I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    ld = os.path.join(fw, 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(ld): os.replace(ld, aside)                      # codesign refuses loose files in Frameworks
    shutil.copy2(os.path.join(LB, 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp'))
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    os.makedirs(md, exist_ok=True)
    for m in ('multiplayer', 'custom-modes'):
        d = os.path.join(md, m)
        if os.path.exists(d):
            os.makedirs(os.path.join(I, 'old'), exist_ok=True); shutil.move(d, os.path.join(I, 'old', f'{m}.{time.time():.0f}'))
        shutil.copytree(os.path.join(LB, 'build', 'mods_all', 'mac', m), d)
    mp = os.path.join(md, 'multiplayer')                             # this worktree's build and its string tables
    shutil.copy2(os.path.join(MPW, 'build', 'module', 'multiplayer.dylib'), os.path.join(mp, 'multiplayer.dylib.tmp'))   # build_module_text.sh
    os.replace(os.path.join(mp, 'multiplayer.dylib.tmp'), os.path.join(mp, 'multiplayer.dylib'))
    import hashlib                                                    # the loader loads only files that match mod.json
    mj = os.path.join(mp, 'mod.json'); m = json.load(open(mj))
    m['platforms']['mac-arm64']['files']['multiplayer.dylib'] = hashlib.sha256(open(os.path.join(mp, 'multiplayer.dylib'), 'rb').read()).hexdigest()
    json.dump(m, open(mj, 'w'), indent=1)
    for f in glob.glob(os.path.join(MPW, 'lang', '*.json')): shutil.copy2(f, os.path.join(mp, 'lang'))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    if os.path.isdir(aside): os.replace(aside, ld)
    os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(LB, 'lang', '*.json')): shutil.copy2(f, ld)
    v = subprocess.run(['codesign', '-v', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True, text=True).stderr
    log('installed', sorted(os.listdir(md)), 'sig', v.strip() or 'ok')


def world_save(path, guid, stem, clan):
    w = json.load(open(TPL)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


def seed():
    """a first-time host: one single-player world, no Multiplayer tag (mp_tags.txt moved aside), no recent worlds"""
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for p in ('data/Saves', 'mp_solo.txt', 'mp_tags.txt', 'mp_worlds.txt', 'mp_menu.json', 'mp.json', 'mp_history.json',
              'data/fe', 'data/mp_owners', 'data/SaveHeaderCache.json', 'data/Mods/worlds.json', 'data/Mods/save_copies'):
        q = os.path.join(I, p)
        if os.path.exists(q): shutil.move(q, os.path.join(old, p.replace('/', '_')))
    sv = os.path.join(I, 'data', 'Saves'); os.makedirs(sv)
    world_save(os.path.join(sv, SOLO_STEM + '.json'), SOLO_G, SOLO_STEM, 'Brightwood')
    open(os.path.join(I, 'mp_history.json'), 'w').write('[]\n')
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
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(T.MODS, 'active.json'), 'DSML_SESSION': os.path.join(T.MODS, 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': S.GSAVES, 'DSL_CLOUD': '0', 'DSL_UPDATE': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': T.STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0',
           'DSRIG_HIDEWIN': '1', 'DSL_CRASHREP_TEST': os.path.join(I, 'crashrep_test'),
           'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': 'file://' + os.path.join(I, 'ux_extip.txt')}
    env.update(r.soft_test_env())
    r.require_windowed(X)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', str(W), '-screen-height', str(H)]
    os.makedirs(S.GSAVES, exist_ok=True)
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('copy started pid', p.pid, TAG)
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


def shot(name, settle=1.0):
    time.sleep(settle)
    p = os.path.join(OUT, name + '.png')
    if os.path.exists(p): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(p, os.path.join(OUT, 'old', f'{name}.{time.time():.0f}.png'))
    a = c(cmd='mpm_shot', path=p, how='backbuffer')
    t0 = time.time()
    while time.time() - t0 < 8 and not os.path.exists(p): time.sleep(0.2)
    log('shot', name, 'ok' if os.path.exists(p) else f'MISSING {a}')
    return os.path.exists(p)


def box_on(): s = ms(); return s if s.get('box', {}).get('on') else None
def box_off(): s = ms(); return s if not s.get('box', {}).get('on') else None
def state_is(*names): return lambda: (lambda s: s if s.get('state') in names else None)(ms())


def safe(name, fn):
    try:
        fn()
    except Exception as e:
        note(name, 'FAILED ' + repr(e)); log(traceback.format_exc())


def to_title_menu():
    s = ms()
    for _ in range(4):
        if s.get('state') in ('idle', 'failed'): break
        press('BACK'); s = ms()
    return s


def active_texts(part):
    return {k: v.get('text') for k, v in ms().get(part, {}).items() if v.get('active')}


# ------------------------------------------------------------------ title screens
def part_title():
    def mp_page():
        press('MULTIPLAYER'); wait('mp page', state_is('title_menu'), 20); shot('01_mp_page', 1.5)
        note('01_mp_page', active_texts('title'))
    safe('01_mp_page', mp_page)
    def join():
        press('JOIN_ADDR'); wait('join page', state_is('title_join'), 20); shot('04_join_page', 1.0)
        note('04_join_page', active_texts('title')); press('BACK')
    safe('04_join', join)
    def host_page():
        s = ms()
        if s.get('state') != 'title_menu': to_title_menu(); press('MULTIPLAYER')
        press('HOST_SAVE'); wait('host page', state_is('title_host'), 20); shot('09_host_page', 1.5)
        note('09_host_page', active_texts('title'))
        press('BACK'); press('BACK')
    safe('09_host', host_page)
    def refused_full():                                          # M12: the reason's first letter and full stop (ru)
        to_title_menu()
        msg = tr('Could not join: %s').replace('%s', tr('the game is full'))
        log('fail_join', c(cmd='mpm_fail_join', msg=msg, host='Alex'))
        b = wait('refusal box', box_on, 20)
        note('13_refused_full', {'text': b['box'].get('text'), 'notice': b.get('notice')})
        shot('13_refused_full', 1.0)
        c(cmd='mpm_box', action='confirm'); wait('box closed', box_off, 10); time.sleep(1.0); to_title_menu()
    safe('13_refused_full', refused_full)


# ------------------------------------------------------------------ hosting a single-player world
def part_game():
    to_title_menu()
    press('MULTIPLAYER'); press('HOST_SAVE'); wait('host page', state_is('title_host'), 20)
    press('LOAD_SAVE', 2.5)
    def load_list():                                             # M4: a first-time host sees its single-player world
        wait('load list filled', lambda: (lambda q: q if q.get('open') and q.get('applied') else None)(c(cmd='mpm_list')), 30, 0.5)
        q = c(cmd='mpm_list')
        note('19_host_load_list', {k: q.get(k) for k in ('mode', 'model_mode', 'shown', 'hidden', 'toggle', 'toggle_text', 'cases')})
        shot('19_host_load_list', 1.5)
    safe('19_host_load_list', load_list)
    log('pick', c(cmd='load_save', file=os.path.join(I, 'data', 'Saves', SOLO_STEM + '.json'), det=0, timeout=180))
    def solo():
        s = wait('single-player copy box', lambda: (lambda q: q if q.get('solo', {}).get('state') == 2 and q.get('box', {}).get('on') else None)(ms()), 600, 2)
        note('20_solo_copy_box', {'confirm': s['box'].get('confirm'), 'cancel': s['box'].get('cancel')})
        c(cmd='mpm_box', action='cancel')
    safe('20_solo', solo)
    wait('lobby', state_is('lobby'), 300, 1)
    time.sleep(3); shot('21_lobby_host', 1.5)
    def rules():
        press('LOBBY_SETTINGS', 1.5); shot('22_lobby_rules', 1.0); press('RULES_DONE', 1.0)
    safe('22_lobby_rules', rules)
    def offbox():                                                # M8: the lobby's Turn Off Multiplayer box
        press('LEAVE', 1.5); s = ms(); note('23_lobby_turnoff_confirm', s.get('box')); shot('23_lobby_turnoff_confirm', 1.0)
        c(cmd='mpm_box', action='cancel'); time.sleep(1.5)
    safe('23_lobby_turnoff', offbox)
    if ms().get('state') != 'lobby': wait('lobby again', state_is('lobby'), 30, 1)
    press('START', 2.0)
    wait('playing', state_is('playing'), 60, 1)
    time.sleep(3)
    def cog():
        c(cmd='ui_click', row=0); time.sleep(1.0)
        c(cmd='ui_pop', open=1); shot('25_players_cog', 1.5); c(cmd='ui_pop', open=0); time.sleep(0.5); c(cmd='ui_click', row=0); time.sleep(0.5)
    safe('25_cog', cog)
    def teams():
        a = c(cmd='ui_teams', action='open'); log('teams open', a); shot('26_teams_host', 2.0)
        c(cmd='ui_teams', action='close'); time.sleep(1)
    safe('26_teams', teams)
    def esc():                                                   # M8 + M11: the host's Esc menu and its box
        c(cmd='mpm_esc'); time.sleep(2); shot('29_esc_host', 1.0)
        note('29_esc_host', {'esc': active_texts('esc')})
        c(cmd='mpm_tip', title=ms().get('esc', {}).get(str(BTN['LEAVE']), {}).get('text', '')); shot('29b_esc_leave_tip', 1.5); c(cmd='mpm_tip')
        press('LEAVE', 1.5); s = ms(); note('30_leave_confirm', s.get('box')); shot('30_leave_confirm', 1.0)
        c(cmd='mpm_box', action='cancel'); time.sleep(1.0); c(cmd='mpm_return_game'); time.sleep(1)
    safe('29_esc', esc)
    c(cmd='mpm_quit_title', timeout=120)
    wait('title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st()), 300, 2)


def main():
    g = threading.Thread(target=T.guard, daemon=True)
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    install(); seed()
    g.start()
    ok = True
    try:
        start()
        time.sleep(4)
        dl = T.dlog(); note('mods', [l.strip() for l in dl.splitlines() if ': loaded (' in l or 'refused' in l.lower()][:12])
        for part in PARTS:
            t0 = time.time()
            try:
                {'title': part_title, 'game': part_game}[part]()
            except Exception as e:
                ok = False; note('part_' + part, 'FAILED ' + repr(e)); log(traceback.format_exc())
                try: shot(f'zz_fail_{part}', 0.5)
                except Exception: pass
            log(f'part {part}: {time.time() - t0:.0f} s')
    finally:
        T.stop_guard.set()
        note('guard', T.violations or 'never visible, never frontmost')
        for p in T.game_pids():
            try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(4)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'rig.log', 'player.log'):
            q = os.path.join(I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(RUN, f))
        json.dump(NOTES, open(os.path.join(OUT, 'notes.json'), 'w'), indent=1, ensure_ascii=False, default=str)
        log('done', TAG, 'ok' if ok else 'with failures', OUT)


if __name__ == '__main__':
    main()
