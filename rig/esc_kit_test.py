#!/usr/bin/env python3
"""Escape closes the multiplayer mod's windows (owner 2026-10-25: "make sure Escape works to close our custom windows").

One hidden LOADER test copy (default M, DSL_TEST_COPY; lrig of the loader checkout LD, default ~/ds_launcher) with the
loader's dsload.dylib (kit 0.1.28 Escape: DSLUi esc_push / esc_drop), the multiplayer MODULE built from a tree of this
repo (--mp-tree, default this checkout; the loader's recipe, into the out folder) and the loader's test module uikit-demo
(REAL Escape key events into the game's view, the kit's Escape stack, which stock views show). No session needed.
Each case opens our window, presses Escape through the game's own key path and checks: our window is gone, the game
did not act on that press (the Esc menu / stock Load list / Multiplayer page under it stays), the next press does the next
thing.
 title 1: "Remove the multiplayer tag?" (our box on the stock CommonNoticeView) over the stock Load list: Escape = its
          Cancel (the tag stays), the Load list stays.
 title 2: Expedition Results window over the Multiplayer page: Escape closes it, the page stays; the next press = Back.
 title 3: our "host's mods" box (stock CommonNoticeView) over the Multiplayer page: Escape = Close, the page stays.
 world 0: Escape with none of ours opens / closes the Esc menu (the key path works).
 world 1: PvP Match window over the Esc menu: Escape = Close, the Esc menu stays; the next press closes the Esc menu.
 world 2: Bring Units card window over the PvP window: Escape closes only the card, then the PvP window, then the Esc menu.
 world 3: PvP result window: Escape = "Back to the World" (the result and the Esc menu close); the Esc menu opened again
          shows no result window.
 world 4: "Remove the multiplayer tag?" over Esc menu > Load (a world): Escape = Cancel, the Load list and Esc menu stay.
Old builds (origin/main before dsesc) fail title 2-3 and world 1-3 (title 1 and world 4: the box paths that kept working).
  esc_kit_test.py [out] [--mp-tree DIR] [--no-build]   Exit 0 = PASS. <out>/summary.json, logs. LD = loader checkout."""
import glob, json, os, re, shutil, signal, subprocess, sys, threading, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
L = os.path.join(os.path.expanduser(os.environ.get('LD', '~/ds_launcher')), 'loader')
sys.path.insert(0, os.path.join(L, 'test'))
import lrig as r
A = sys.argv[1:]
TREE = os.path.abspath(os.path.expanduser(A[A.index('--mp-tree') + 1])) if '--mp-tree' in A else ROOT
POS = [a for i, a in enumerate(A) if not a.startswith('--') and (i == 0 or A[i - 1] != '--mp-tree')]
OUT = os.path.abspath(os.path.expanduser(POS[0] if POS else os.path.join(ROOT, 'build', 'esc_kit_out', time.strftime('%H%M%S'))))
X = os.environ.get('DSL_TEST_COPY', 'M'); I = r.idir(X); APP = os.path.join(I, 'DungeonSettlers.app')
DSLOAD = os.environ.get('ESC_DSLOAD') or os.path.join(L, 'build', 'dsload.dylib')
MODS = os.path.join(OUT, 'mods'); UDRV = os.path.join(OUT, 'uikit')
SAVE = 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0'; GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
RUN = '0123456789abcdef0123456789abcdef'                                # a seeded Foreign Expedition result (the list has one row)
ESC = 53                                                                # macOS key code of Escape
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []; violations = []; stop = threading.Event()


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if p.returncode: raise SystemExit(f'failed ({p.returncode}): {cmd[:300]}\n{p.stdout[-1500:]}\n{p.stderr[-1500:]}')
    return p.stdout


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(src, dst + '.tmp'); shutil.copymode(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)


def buttons():
    """enum mpm_button of the tree under test (src/mpmenu.h): name -> value"""
    t = open(os.path.join(TREE, 'src', 'mpmenu.h')).read(); t = t[t.index('enum mpm_button {') + 17:]; t = t[:t.index('};')]
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S); v = -1; out = {}
    for w in [w.strip() for w in t.split(',') if w.strip()]:
        n, _, e = w.partition('='); v = int(e) if e.strip() else v + 1; out[n.strip()[4:]] = v
    return out


def build():
    """multiplayer module from TREE (the loader's build.sh recipe: mp_module.c + the tree's src/build.sh list), uikit-demo"""
    o = os.path.join(MODS, 'multiplayer'); os.makedirs(o, exist_ok=True)
    t = open(os.path.join(TREE, 'src', 'build.sh')).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
    srcs = ' '.join(f'"{TREE}/src/{w}"' for w in t.split() if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-'))
    sh(f'cd "{L}" && nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -D_DARWIN_C_SOURCE -dynamiclib '
       f'-framework AppKit -framework CoreGraphics -framework CoreFoundation -install_name @rpath/multiplayer.dylib -Isdk -I"{TREE}/src" '
       f'-I"{TREE}/win/math" -DDSMATH_DEFAULT_ON=1 -o "{o}/multiplayer.dylib.tmp" modules/multiplayer/mp_module.c {srcs} '
       f'"{TREE}/win/math/build/libdsmath_arm64.a" -lz && codesign -f -s - "{o}/multiplayer.dylib.tmp" 2>/dev/null; mv -f "{o}/multiplayer.dylib.tmp" "{o}/multiplayer.dylib"')
    shutil.copyfile(os.path.join(L, 'modules', 'multiplayer', 'mod.json'), os.path.join(o, 'mod.json'))
    dr = next(p for p in (os.path.join(L, 'build', 'mods', 'multiplayer', 'det_ranges.tsv'), os.path.expanduser('~/ds_multiplayer/build/det_ranges.tsv')) if os.path.exists(p))
    shutil.copyfile(dr, os.path.join(o, 'det_ranges.tsv'))
    d = os.path.join(MODS, 'uikit-demo'); os.makedirs(d, exist_ok=True)
    sh(f'cd "{L}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc '
       f'-install_name @rpath/uikit-demo.dylib -o "{d}/uikit-demo.dylib.tmp" test/mods_src/uikit-demo/uikit_demo.c '
       f'&& codesign -f -s - "{d}/uikit-demo.dylib.tmp" 2>/dev/null; mv -f "{d}/uikit-demo.dylib.tmp" "{d}/uikit-demo.dylib"')
    shutil.copyfile(os.path.join(L, 'test', 'mods_src', 'uikit-demo', 'mod.json'), os.path.join(d, 'mod.json'))
    shutil.copyfile(os.path.join(L, 'modules', 'modmenu', 'slogel.png'), os.path.join(d, 'slogel.png'))
    put(DSLOAD, os.path.join(OUT, 'dsload.dylib'))                    # frozen for this run
    log('built: multiplayer from', TREE, '| dsload', DSLOAD)


def game_pids():
    out = subprocess.run(['pgrep', '-f', '^' + os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():
    while not stop.is_set():
        pids = game_pids()
        if pids:
            bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids: os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop.wait(0.5)


def aside(p):
    if os.path.exists(p):
        os.makedirs(os.path.join(os.path.dirname(p), 'old'), exist_ok=True); os.replace(p, os.path.join(os.path.dirname(p), 'old', os.path.basename(p) + '.%d' % time.time()))


def install():
    if game_pids(): raise SystemExit(f'copy {X} is running: not installing over it')
    put(os.path.join(OUT, 'dsload.dylib'), os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'))
    for mid in ('multiplayer', 'uikit-demo'):
        src = os.path.join(MODS, mid)
        for f in os.listdir(src):
            if os.path.isfile(os.path.join(src, f)): put(os.path.join(src, f), os.path.join(APP, 'Contents', 'Resources', 'mods', mid, f))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def start():
    r.require_background_only(X); r.prepare_data(X); r.require_windowed(X)
    pd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    shutil.copy2(os.path.join(r.SEED_DATA, 'Saves', SAVE + '.json'), os.path.join(I, 'data', 'Saves', SAVE + '.json'))
    mods = os.path.join(I, 'data', 'Mods'); os.makedirs(mods, exist_ok=True); aside(os.path.join(mods, 'worlds.json'))
    aside(os.path.join(mods, 'loadlist.json'))                         # the loader's Load list panel: Multiplayer worlds shown (the seed world is tagged)
    open(os.path.join(mods, 'loadlist.json'), 'w').write('{"vanilla": true, "modded": true, "multiplayer": true, "latest_only": false, "show_tags": true}\n')
    # the multiplayer mod's files in the copy (rig_dir = DSRIG_DIR = I): the seed world has the Multiplayer tag, one FE result
    aside(os.path.join(I, 'mp_tags.txt')); open(os.path.join(I, 'mp_tags.txt'), 'w').write(f'{GUID}\tAlex\tSam\n')
    res = os.path.join(I, 'data', 'fe', 'results'); os.makedirs(res, exist_ok=True)
    json.dump({'rules': {'host_name': 'Tester'}, 'units': []}, open(os.path.join(res, RUN + '.json'), 'w'))
    aside(os.path.join(I, 'data', 'fe', 'claims.json'))
    prof = os.path.join(I, 'esc_kit_profile.json'); json.dump({'enabled': ['multiplayer', 'uikit-demo'], 'order': ['multiplayer', 'uikit-demo']}, open(prof, 'w'))
    os.makedirs(UDRV, exist_ok=True)
    for f in ('req.json', 'resp.json'):
        if os.path.exists(os.path.join(UDRV, f)): os.replace(os.path.join(UDRV, f), os.path.join(UDRV, f + '.old'))
    crd = os.path.join(OUT, 'crashrep'); os.makedirs(crd, exist_ok=True); open(os.path.join(crd, 'answer'), 'w').write('no')
    if os.path.exists(r.sockpath(X)): os.replace(r.sockpath(X), r.sockpath(X) + '.old')
    for f in ('rig.log', 'dsload.log', 'player.log'): aside(os.path.join(I, f))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'),
           'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data'), 'DSML_SESSION': os.path.join(mods, 'worlds.json'),
           'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSL_UPDATE': '0',
           'DSL_CLOUD': '0', 'DSL_PREWARM': '0', 'DSL_CRASHREP_TEST': crd, 'DSL_BUGREP_DRY': '1', 'DSL_UIKIT_TEST': UDRV}
    env.update(r.soft_test_env())
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('started', p.pid)
    return p.pid


SEQ = [0]
def ui(cmd, timeout=15, **kw):
    SEQ[0] += 1; kw['seq'] = SEQ[0]; kw['cmd'] = cmd
    tmp = os.path.join(UDRV, 'req.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UDRV, 'req.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            j = json.load(open(os.path.join(UDRV, 'resp.json')))
            if str(j.get('seq')) == str(SEQ[0]): return j
        except Exception: pass
        time.sleep(0.05)
        if not game_pids(): raise RuntimeError('the game copy is gone (crash?)')
    return {'timeout': kw}


def ctl(timeout=20, **obj):
    try: return r.send(X, obj, timeout=timeout, connect_wait=0)
    except Exception as e: return {'error': str(e)}


def wait_for(pred, limit, step=0.25):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        time.sleep(step)
    return None


st = lambda: ui('state')
pvp = lambda **k: ctl(cmd='mpm_pvp', **k)
fewin = lambda: ctl(cmd='mpm_fewin')
mstate = lambda: ctl(cmd='mpm_status').get('state')
bring = lambda: ctl(cmd='mpm_bring_state')


def press():
    """one real Escape (keyDown + keyUp on the game's view), then a few frames for the game, the kit and the mod to act"""
    j = ui('key', code=ESC); time.sleep(1.2); return j


def esc_menu(on):
    """the Esc menu shown (on=1) / closed (on=0) with real Escape presses (at most 2)"""
    for _ in range(2):
        if (st().get('sysmenu') == 1) == bool(on): return True
        press(); wait_for(lambda: (st().get('sysmenu') == 1) == bool(on), 3)
    return (st().get('sysmenu') == 1) == bool(on)


def tag_box(limit=10):
    """'Remove tag' of the seed world's row in the open Load list: our box shows (mpm_tag box = BX_UNTAG)"""
    def row():
        t = ctl(cmd='mpm_tag'); return next((x for x in t.get('rows', []) if x.get('guid', '').lower() == GUID and x.get('remove')), None)
    w = wait_for(row, limit, 0.5)
    if not w: return None, ctl(cmd='mpm_tag')
    ctl(cmd='mpm_tag', click=w['i']); time.sleep(0.8)
    return w, ctl(cmd='mpm_tag')


def title_cases():
    B = buttons()
    # 1. our box over the stock Load list
    ui('stock', what='title_load'); s = wait_for(lambda: (lambda s: s if s.get('loadlist') == 1 else None)(st()), 10)
    check('title 1: the stock Load list shows', bool(s), st().get('loadlist'))
    w, t = tag_box(); up = bool(w and t.get('box', 0) > 0)             # every check after an open needs the open (no pass on nothing)
    check('title 1: "Remove the multiplayer tag?" box shows over it', up, t)
    press(); t = ctl(cmd='mpm_tag'); s = st()
    check('title 1: Escape = the box\'s Cancel (box gone)', up and t.get('box') == 0, t.get('box'))
    check('title 1: the tag stays (Cancel, not Remove)', up and open(os.path.join(I, 'mp_tags.txt')).read().strip().startswith(GUID), t.get('rows'))
    check('title 1: the stock Load list under it stays', up and s.get('loadlist') == 1, s.get('loadlist'))
    press(); s = st(); log('info: next Escape on the stock Load list alone: loadlist', s.get('loadlist'))
    for _ in range(2):
        if st().get('loadlist') == 1: press()
    # 2. Expedition Results window over the Multiplayer page
    ctl(cmd='mpm_click', button=B['MULTIPLAYER']); s0 = wait_for(lambda: (lambda m: m if m not in (None, 'idle') else None)(mstate()), 8)
    check('title 2: Multiplayer page open', bool(s0), mstate())
    f = wait_for(lambda: (ctl(cmd='mpm_click', button=B['FE_RESLIST']), time.sleep(0.6), (lambda j: j if j.get('mode') == 3 else None)(fewin()))[-1], 12, 0.2)
    check('title 2: Expedition Results window open over it', bool(f), fewin())
    up = bool(f); press(); f = fewin(); m = mstate()
    check('title 2: Escape closes the Expedition Results window', up and f.get('mode') == 0, f.get('mode'))
    check('title 2: the Multiplayer page under it stays (no Back on the same press)', up and m == s0, (s0, m))
    press(); m = mstate()
    check('title 2: the next Escape = Back (the page closes)', up and m != s0, (s0, m))
    # 3. our "host's mods" box over the Multiplayer page (as after a refused join)
    ctl(cmd='mpm_click', button=B['MULTIPLAYER']); s0 = wait_for(lambda: (lambda m: m if m not in (None, 'idle') else None)(mstate()), 8)
    pf = os.path.join(I, 'esc_hostmods_plan.json')
    json.dump({'rows': [{'id': 'esc-test-mod', 'name': 'Escape Test Mod', 'host_ver': '1.0.0', 'my_ver': '', 'action': 'install', 'official': 1}],
               'can_fix': 0, 'downloads': 0, 'names_pending': 0}, open(pf, 'w'))
    ctl(cmd='mpm_hostmods', plan_file=pf); h = wait_for(lambda: (lambda j: j if j.get('box') == 1 else None)(ctl(cmd='mpm_hostmods')), 10)
    up = bool(s0 and h)
    check('title 3: our "host\'s mods" box shows over the Multiplayer page', up, (s0, ctl(cmd='mpm_hostmods')))
    press(); h = ctl(cmd='mpm_hostmods'); m = mstate()
    check('title 3: Escape closes the box', up and h.get('box') == 0 and ctl(cmd='mpm_tag').get('box') == 0, h)
    check('title 3: the Multiplayer page under it stays (no Back on the same press)', up and m == s0, (s0, m))
    press(); m = mstate()
    check('title 3: the next Escape = Back (the page closes)', up and m != s0, (s0, m))


def world_cases():
    # 0. none of ours: the game's own Escape
    check('world 0: Escape with none of ours opens the Esc menu (the key path works)', esc_menu(1), st().get('sysmenu'))
    press(); s = wait_for(lambda: (lambda s: s if s.get('sysmenu') != 1 else None)(st()), 4) or st()
    check('world 0: the next Escape closes it', s.get('sysmenu') != 1, s.get('sysmenu'))
    # 1. PvP Match window
    esc_menu(1); pvp(action='esc'); p = wait_for(lambda: (lambda j: j if j.get('window') == 1 else None)(pvp()), 6)
    check('world 1: PvP Match window open over the Esc menu', bool(p), pvp().get('window'))
    up = bool(p); press(); p = pvp(); s = st()
    check('world 1: Escape closes the PvP Match window', up and p.get('window') == 0 and p.get('roots', {}).get('window') == 0, (p.get('window'), p.get('roots')))
    check('world 1: the Esc menu under it stays', up and s.get('sysmenu') == 1, s.get('sysmenu'))
    press(); s = st()
    check('world 1: the next Escape closes the Esc menu', s.get('sysmenu') != 1, s.get('sysmenu'))
    # 2. Bring Units card window over the PvP window
    esc_menu(1); pvp(action='esc'); wait_for(lambda: pvp().get('window') == 1, 6)
    pvp(action='bring'); b = wait_for(lambda: (lambda j: j if j.get('open') == 1 else None)(bring()), 8)
    check('world 2: Bring Units card window open over the PvP window', bool(b), bring())
    up = bool(b); press(); b = bring(); p = pvp(); s = st()
    check('world 2: Escape closes the Bring Units window', up and b.get('open') == 0, b.get('open'))
    check('world 2: the PvP window and the Esc menu under it stay', up and p.get('window') == 1 and s.get('sysmenu') == 1, (p.get('window'), s.get('sysmenu')))
    press(); p = pvp(); s = st()
    check('world 2: the 2nd Escape closes the PvP window, the Esc menu stays', up and p.get('window') == 0 and s.get('sysmenu') == 1, (p.get('window'), s.get('sysmenu')))
    press(); s = st()
    check('world 2: the 3rd Escape closes the Esc menu', up and s.get('sysmenu') != 1, s.get('sysmenu'))
    # 3. PvP result window
    esc_menu(1); a = pvp(action='result_show'); p = wait_for(lambda: (lambda j: j if j.get('result') == 1 else None)(pvp()), 6)
    check('world 3: PvP result window open over the Esc menu', bool(p), a)
    up = bool(p); press(); time.sleep(0.5); p = pvp(); s = st()
    check('world 3: Escape = "Back to the World": the result window closes', up and p.get('result') == 0 and p.get('roots', {}).get('result') == 0, (p.get('result'), p.get('roots')))
    check('world 3: ... and the Esc menu with it (as the button)', up and s.get('sysmenu') != 1, s.get('sysmenu'))
    esc_menu(1); time.sleep(0.5); p = pvp()
    check('world 3: the Esc menu opened again shows no result window', up and st().get('sysmenu') == 1 and p.get('roots', {}).get('result') == 0, (st().get('sysmenu'), p.get('roots')))
    # 4. our box over Esc menu > Load (a world): the in-world path keeps working
    esc_menu(1); ui('stock', what='esc_load'); s = wait_for(lambda: (lambda s: s if s.get('loadlist') == 1 else None)(st()), 10)
    check('world 4: Esc menu > Load list shows', bool(s), st().get('loadlist'))
    w, t = tag_box(); up = bool(w and t.get('box', 0) > 0)
    check('world 4: "Remove the multiplayer tag?" box shows over it', up, t)
    press(); t = ctl(cmd='mpm_tag'); s = st()
    check('world 4: Escape = the box\'s Cancel (box gone)', up and t.get('box') == 0, t.get('box'))
    check('world 4: the Load list and the Esc menu under it stay', up and s.get('loadlist') == 1 and s.get('sysmenu') == 1, (s.get('loadlist'), s.get('sysmenu')))


def main():
    if game_pids(): raise SystemExit(f'copy {X} already running: {game_pids()}')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in A: build()
    install()
    g = threading.Thread(target=guard, daemon=True); g.start()
    try:
        start()
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(ctl(5, cmd='status')), 240, 1)
        check('title reached', bool(t), t)
        s = wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
        check('uikit-demo driver up', bool(s), s)
        time.sleep(4)
        title_cases()
        for _ in range(3):
            if mstate() not in (None, 'idle'): press()
        a = ctl(30, cmd='load_save', name=SAVE); log('load_save', a)
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') else None)(ctl(5, cmd='status')), 180, 1)
        check('world loaded', bool(t), t)
        time.sleep(8)
        world_cases()
    except Exception as e:
        check('no exception', False, repr(e))
    finally:
        try: ctl(5, cmd='quit')
        except Exception: pass
        time.sleep(3)
        for p in game_pids(): os.kill(p, signal.SIGKILL)
        stop.set()
        for f in ('dsload.log', 'player.log', 'rig.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
    lg = os.path.join(OUT, 'dsload.log')
    miss = [l for l in open(lg, errors='replace').read().splitlines() if 'ESC-MISSING' in l] if os.path.exists(lg) else ['no dsload.log']
    check('sweep: no ESC-MISSING line (every window of ours has its Escape registration; loader NOTES "Escape")', not miss, miss[:3])
    fails = [c for c in checks if not c['ok']]
    check('copy never on screen / frontmost', not violations, violations)
    json.dump({'tree': TREE, 'checks': checks, 'violations': violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log(f'RESULT esc_kit_test: {len(checks) - len(fails) - (1 if violations else 0)} PASS, {len(fails) + (1 if violations else 0)} FAIL')
    sys.exit(1 if fails or violations else 0)


if __name__ == '__main__':
    main()
