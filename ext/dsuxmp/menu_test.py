#!/usr/bin/env python3
"""uxmp-menu checks + shots (lane dsuxmp, sub-lane uxmp-menu; adapted from the review driver ux_uxmp/shoot.py of lane dsux).
ONE hidden loader test copy UMN (make it once: ext/dsuxmp/make_instance_menu.sh) with the shared loader build
(~/ds_launcher_wt_dsuxmp/loader/build: dsload.dylib + mods_all/mac) and the multiplayer.dylib under test (--mp, default this
worktree's build/dsrig.dylib). Hidden, small, windowed, muted, never focused; take the lane slot first (run_menu.sh does).

Each check prints `RESULT <name> PASS|FAIL <detail>`. What each one catches (a real fault a player sees):
 M3   the "Keep a single-player copy of this world?" box: in the FIRST frame it shows, the box text and its Yes button are
      drawn (the world's opening fade / black is not over them).
 M6   hub lobby: the Rules column lists the hub rules (Death, Experience, Dungeon loot, Recruiting), not Pause etc.; its Gear
      says Own storage, which is what Start sets (rules.gear 2 after Start).
 M9   "Could not join" box (mods differ): "Get the host's mods" is drawn as big as "Close" (ink height) and inside its button.
 M10  title Load list and "Your save": the row of a tagged world with a LONG name (36 letters; the loader's Modded tag too):
      "Remove Tag" is whole (its TMP preferred width fits its box, not truncated, design size), inside the row and the list's
      viewport, clear of [Change] and of the name's drawn text. A tagged world with a short name keeps its whole name.
 M5   at UI scale s: the lobby window, the host page's Rules panel and Recent Worlds are drawn at s (or smaller only to fit the
      screen with 16 px free); a click of the test build's fake mouse (-DTEAMS_MOUSE_TEST, Unity's own input path) on the
      drawn (scaled) Pause ">" arrow, the lobby's cog and Start does what the button does (scaled hit areas).
 X    CLOSE RULE: the title Expedition Results window shows the game's red X; a fake-mouse click on it closes the window.
Checks read the game's state through the rig's control channel (mpm_status, mpm_tree, mpm_tag) and the game's own frame
(mpm_shot backbuffer). On a build without a read-back the check uses the fallback written next to it, so the same
script runs on origin/main (to prove it fails there).

usage: menu_test.py <en|de|ru> <W>x<H> <ui scale> [parts=title,game,hub] --mp DYLIB [--tag NAME]
DYLIB = the multiplayer module as the loader loads it, with the fake mouse: LOADER=~/ds_launcher_wt_dsuxmp2/loader zsh
ext/dsuxmp/build_mouse.sh <out> (SRC=<tree>/src for origin/main's code). Loader: ~/ds_launcher_wt_dsuxmp2/loader (83636d0).
shots: ~/ToDelete_2026-10-25/dsuxmp2/shots/dsuxmp2-menu/<lang>_<W>x<H>_ui<s>[_NAME]/ (never committed)"""
import json, os, sys, time, glob, shutil, subprocess, threading, signal, traceback, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.dirname(os.path.dirname(HERE))                          # this worktree
LB = os.path.expanduser('~/ds_launcher_wt_dsuxmp2/loader'); LT = os.path.join(LB, 'test')   # shared loader build (read only)
LANGS = {'en': 0, 'de': 3, 'ru': 4}
args = [a for a in sys.argv[1:]]
MP = os.path.join(WT, 'build', 'dsrig.dylib'); NAME = ''
if '--mp' in args: i = args.index('--mp'); MP = os.path.abspath(os.path.expanduser(args[i + 1])); del args[i:i + 2]
if '--tag' in args: i = args.index('--tag'); NAME = args[i + 1]; del args[i:i + 2]
LANGDIR = os.path.join(WT, 'lang')                                  # the module's language files (the loader build's are older)
if '--lang' in args: i = args.index('--lang'); LANGDIR = os.path.abspath(os.path.expanduser(args[i + 1])); del args[i:i + 2]
LANG, SIZE, SCALE = args[0], args[1], float(args[2])
PARTS = (args[3] if len(args) > 3 else 'title,game,hub').split(',')
W, H = (int(v) for v in SIZE.split('x'))
TAG = f'{LANG}_{W}x{H}_ui{SCALE:.1f}' + (f'_{NAME}' if NAME else '')
BASE = os.path.expanduser('~/ToDelete_2026-10-25/dsuxmp2')
OUT = os.path.join(BASE, 'shots', 'dsuxmp2-menu', TAG); os.makedirs(OUT, exist_ok=True)
RUN = os.path.join(BASE, 'dsuxmp2-menu', 'runs', TAG + time.strftime('_%H%M%S')); os.makedirs(RUN, exist_ok=True)
os.environ['DSL_TEST_COPY'] = 'UMN'; os.environ['LRIG_SIZE'] = f'{W}x{H}'
sys.argv[1:] = [RUN]
sys.path.insert(0, LT)
import l1_saves_test as S                                           # noqa: E402  (lrig + l1 helpers of the shared loader tree)
T, r = S.T, S.r
X, I = S.X, S.I
assert X == 'UMN' and I.endswith('/UMN'), (X, I)
log = T.log
BTN = json.load(open(os.path.join(HERE, 'btn.json')))
TRT = {} if LANG == 'en' else json.load(open(os.path.join(LANGDIR, LANG + '.json')))
def tr(s): return TRT.get(s, s) or s
def trk(*keys): return next((TRT[k] for k in keys if TRT.get(k)), keys[0])   # a key that changed case (Title Case, UX M11)
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public base save
SOLO_G = 'b0c0ffee-0000-4000-8000-0000000000a1'; SOLO_STEM = 'Brightwood__AutoSave_' + SOLO_G.replace('-', '') + '_0'
MOON_G = 'b0c0ffee-0000-4000-8000-0000000000b2'; MOON_STEM = 'Moonlight Brigade__AutoSave_' + MOON_G.replace('-', '') + '_0'
MOON_NAME = 'Moonlight Brigade of the Silver Lake'                  # M10: a long world name (36 letters), tagged + Modded
ASH_G = 'b0c0ffee-0000-4000-8000-0000000000c3'; ASH_STEM = 'Ashfall__AutoSave_' + ASH_G.replace('-', '') + '_0'   # tagged, short name
RESULTS, NOTES = [], []


def result(name, ok, detail):
    line = f'RESULT {name} {"PASS" if ok else "FAIL"} {detail}'
    RESULTS.append(line); print(line, flush=True); log(line)


def note(k, v):
    NOTES.append({'state': k, 'info': v}); log('note', k, str(v)[:400])


# ------------------------------------------------------------------ install + seed
def install():
    app = os.path.join(I, 'DungeonSettlers.app'); T.require_hidden_copy(app)
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    ld = os.path.join(fw, 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(ld): os.replace(ld, aside)                      # codesign refuses loose files in Frameworks
    shutil.copy2(os.path.join(LB, 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp'))
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    os.makedirs(md, exist_ok=True)
    for m in ('multiplayer', 'custom-modes', 'gcd-cdr', 'zetu-suite'):
        d = os.path.join(md, m)
        if os.path.exists(d):
            os.makedirs(os.path.join(I, 'old'), exist_ok=True); shutil.move(d, os.path.join(I, 'old', f'{m}.{time.time():.0f}'))
        shutil.copytree(os.path.join(LB, 'build', 'mods_all', 'mac', m), d)
    # the multiplayer binary under test (its sha256 in mod.json: the loader checks it)
    dst = os.path.join(md, 'multiplayer', 'multiplayer.dylib')
    shutil.copy2(MP, dst + '.tmp'); os.replace(dst + '.tmp', dst)
    mj = os.path.join(md, 'multiplayer', 'mod.json'); j = json.load(open(mj))
    j['platforms']['mac-arm64']['files']['multiplayer.dylib'] = hashlib.sha256(open(dst, 'rb').read()).hexdigest()
    json.dump(j, open(mj, 'w'), indent=1)
    ml = os.path.join(md, 'multiplayer', 'lang'); os.makedirs(ml, exist_ok=True)   # the code's own strings (not in mod.json's hashes)
    for f in glob.glob(os.path.join(LANGDIR, '*.json')): shutil.copy2(f, os.path.join(ml, os.path.basename(f)))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    if os.path.isdir(aside): os.replace(aside, ld)
    os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(LB, 'lang', '*.json')): shutil.copy2(f, ld)
    v = subprocess.run(['codesign', '-v', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True, text=True).stderr
    log('installed', sorted(os.listdir(md)), 'mp', MP, 'lang', LANGDIR, 'sig', v.strip() or 'ok')


def world_save(path, guid, stem, clan):
    w = json.load(open(TPL)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)
    return w


def seed():
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for p in ('data/Saves', 'mp_solo.txt', 'mp_tags.txt', 'mp_worlds.txt', 'mp_menu.json', 'mp.json', 'mp_history.json',
              'data/fe', 'data/mp_owners', 'data/SaveHeaderCache.json', 'data/Mods/worlds.json', 'data/Mods/save_copies'):
        q = os.path.join(I, p)
        if os.path.exists(q): shutil.move(q, os.path.join(old, p.replace('/', '_')))
    sv = os.path.join(I, 'data', 'Saves'); os.makedirs(sv)
    w = world_save(os.path.join(sv, SOLO_STEM + '.json'), SOLO_G, SOLO_STEM, 'Brightwood')
    world_save(os.path.join(sv, MOON_STEM + '.json'), MOON_G, MOON_STEM, MOON_NAME)
    world_save(os.path.join(sv, ASH_STEM + '.json'), ASH_G, ASH_STEM, 'Ashfall')
    open(os.path.join(I, 'mp_tags.txt'), 'w').write(f'{MOON_G}\tHost\tGuest\n{ASH_G}\tHost\tGuest\n')     # M10: worlds played with others
    fr = os.path.join(I, 'data', 'fe', 'results'); os.makedirs(fr, exist_ok=True)               # X: one Expedition result (title list)
    json.dump({'rules': {'host_name': 'Alex'}, 'units': []}, open(os.path.join(fr, 'c0ffee00c0ffee00c0ffee00c0ffee01.json'), 'w'))
    json.dump({'claims': {'c0ffee00c0ffee00c0ffee00c0ffee01': {'date': '24 Oct 2026', 'file': 'Brightwood__Expedition.json'}}},   # brought home:
              open(os.path.join(I, 'data', 'fe', 'claims.json'), 'w'))   # listed on the title, never opened by itself in a session
    units = w['EntityContainerSaveData']['UnitContainerSaveData']['Base'].get('PlayerGuidList') or []   # the clan's units (fe_bring)
    json.dump({'units': [u for u in units if u]}, open(os.path.join(RUN, 'units.json'), 'w'))
    now = int(time.time())
    rows = [dict(key='a11ce000-0000-4000-8000-0000000000b2|ip|192.0.2.10:47777', world='Moonfall Keep', day=42, how='ip',
                 addr='192.0.2.10:47777', host='Alex', steamid='0', players='Sam|2;Robin|3', units='', portraits='', when=now - 3600),
            dict(key='a11ce000-0000-4000-8000-0000000000b3|ip|192.0.2.11:47777', world='Ashen Hollow', day=7, how='ip',
                 addr='192.0.2.11:47777', host='Sam', steamid='0', players='Alex|1', units='', portraits='', when=now - 86400 * 3)]
    open(os.path.join(I, 'mp_history.json'), 'w').write('[\n' + ',\n'.join(json.dumps(x) for x in rows) + '\n]\n')
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
    en = ['multiplayer', 'custom-modes', 'gcd-cdr']
    json.dump({'enabled': en, 'order': en}, open(os.path.join(T.MODS, 'active.json'), 'w'))
    json.dump({'worlds': {MOON_G: 'gcd-cdr+zetu-suite'}}, open(os.path.join(T.MODS, 'worlds.json'), 'w'))   # its row: the loader's "Modded" tag + Mods line (zetu-suite is not a fix)
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
    g = os.path.join(I, 'shots_menu'); os.makedirs(g, exist_ok=True); g = os.path.join(g, name + '.png')   # the copy writes only in its own folder
    if os.path.exists(g): os.replace(g, g + '.old')
    a = c(cmd='mpm_shot', path=g, how='backbuffer')
    t0 = time.time()
    while time.time() - t0 < 8 and not os.path.exists(g): time.sleep(0.1)
    time.sleep(0.3)
    if os.path.exists(g): shutil.move(g, p)
    log('shot', name, 'ok' if os.path.exists(p) else f'MISSING {a}')
    return p if os.path.exists(p) else None


def box_on(): s = ms(); return s if s.get('box', {}).get('on') else None
def box_off(): s = ms(); return s if not s.get('box', {}).get('on') else None
def state_is(*names): return lambda: (lambda s: s if s.get('state') in names else None)(ms())


def safe(name, fn):
    try:
        fn()
    except Exception as e:
        note(name, 'FAILED ' + repr(e)); log(traceback.format_exc())
        result(name, False, 'driver error: ' + repr(e)[:200])


def to_title_menu():
    s = ms()
    for _ in range(4):
        if s.get('state') in ('idle', 'failed'): break
        press('BACK'); s = ms()
    return s


def ppu():                                         # screen px per canvas unit of the game's UI (1080-high canvas)
    return H / 1080.0


def img(path):
    from PIL import Image
    return Image.open(path).convert('RGB')


def bright_frac(im, rect, thr=120):
    """share of pixels brighter than thr (luma) in rect (x0, y0, x1, y1, y UP, px)"""
    x0, y0, x1, y1 = (int(round(v)) for v in rect)
    if x1 <= x0 or y1 <= y0: return 0.0
    crop = im.crop((max(0, x0), max(0, H - y1), min(W, x1), min(H, H - y0))).convert('L')
    px = list(crop.getdata())
    return sum(1 for v in px if v > thr) / max(1, len(px))


def ink_box(im, rect, thr=130):
    """the bright ink's bounding box inside rect (y up): (x0, y0, x1, y1) or None"""
    x0, y0, x1, y1 = (int(round(v)) for v in rect)
    crop = im.crop((max(0, x0), max(0, H - y1), min(W, x1), min(H, H - y0))).convert('L')
    w, h = crop.size; d = crop.load(); xs, ys = [], []
    for yy in range(h):
        for xx in range(w):
            if d[xx, yy] > thr: xs.append(xx); ys.append(yy)
    if not xs: return None
    return (x0 + min(xs), y1 - max(ys), x0 + max(xs), y1 - min(ys))


def tree(what):
    return c(cmd='mpm_tree', what=what).get('tree', [])


# ------------------------------------------------------------------ M9: the host-mods box
def check_hostmods():
    to_title_menu()
    plan = {'can_fix': 1, 'rows': [
        {'id': 'hell-difficulty', 'name': 'Hell Difficulty', 'host_ver': '0.3.2', 'my_ver': '', 'action': 'download', 'official': 1},
        {'id': 'gcd-cdr', 'name': 'Cooldown Reduction', 'host_ver': '0.2.0', 'my_ver': '0.2.0', 'my_on': 0, 'action': 'turn_on', 'official': 1},
        {'id': 'zetu-suite', 'name': 'Zetu Suite', 'host_ver': '1.4.0', 'my_ver': '1.3.1', 'my_on': 1, 'action': 'update', 'official': 1},
        {'id': 'multiplayer', 'name': 'Multiplayer', 'host_ver': '0.1.27', 'my_ver': '0.1.27', 'my_on': 1, 'action': 'same', 'official': 1}]}
    pf = os.path.join(I, 'plan_modset.json'); json.dump(plan, open(pf, 'w'))   # the copy reads only its own folder (and ~/ds_*)
    c(cmd='mpm_hostmods', plan_file=pf, vanilla=0)
    b = wait('host mods box', box_on, 20)
    time.sleep(1.0)
    s = ms(); bx = s.get('box', {}); br = s.get('box_rects')
    p = shot('17_refused_modset', 0.2)
    json.dump(c(cmd='mpm_tree', what='notice', detail=1), open(os.path.join(RUN, 'tree_notice.json'), 'w'), indent=0)
    labels = (bx.get('confirm', ''), bx.get('cancel', ''))
    if br:                                                           # read-back (this branch)
        cf, cn = br['confirm'], br['cancel']
        rects = {'confirm': cf['btn'], 'cancel': cn['btn']}; lrect = {'confirm': cf['label'], 'cancel': cn['label']}
        sizes = (cf['size'], cf['normal'], cn['size'], cn['normal'])
    else:                                                            # origin/main: the buttons from the notice box's tree
        nodes = [n for n in tree('notice') if n.get('on')]
        btns = [n for n in nodes if n.get('d') == 3 and 'utton' in n.get('name', '')]
        btns.sort(key=lambda n: n['rect'][0])
        rects = {'cancel': btns[0]['rect'], 'confirm': btns[-1]['rect']} if len(btns) >= 2 else {}
        lrect = rects; sizes = None
        note('17_tree', [(n.get('d'), n.get('name'), n.get('rect')) for n in nodes][:40])
    note('17_refused_modset', {'labels': labels, 'rects': rects, 'sizes': sizes})
    ok, det = False, 'no shot / no buttons'
    if p and rects:
        im = img(p)
        ink = {k: ink_box(im, rects[k]) for k in rects}
        if ink['confirm'] and ink['cancel']:
            hc, hx = ink['confirm'][3] - ink['confirm'][1], ink['cancel'][3] - ink['cancel'][1]
            inside = ink['confirm'][0] >= rects['confirm'][0] + 2 and ink['confirm'][2] <= rects['confirm'][2] - 2
            ratio = hc / max(1, hx)
            ok = ratio >= 0.85 and inside and (sizes is None or sizes[0] >= sizes[1] - 0.5)
            det = (f'"{labels[0]}" ink {hc}px high vs "{labels[1]}" {hx}px (ratio {ratio:.2f}, need >= 0.85), ink inside its button {inside}'
                   + (f', label size {sizes[0]:.1f} of normal {sizes[1]:.1f}' if sizes else '') + f', button {rects["confirm"]}')
    result('M9_hostmods_button', ok, det)
    c(cmd='mpm_box', action='cancel'); wait('box closed', box_off, 10)


# ------------------------------------------------------------------ M5: title panels
def panel_rect(s, key, fallback_name):
    pn = (s.get('panels') or {}).get(key)
    if pn: return pn['rect'], pn['size'][0]
    return None, None


def fake_click(rect, settle=1.5):
    """the test build's fake mouse (-DTEAMS_MOUSE_TEST: Input.mousePosition / GetMouseButton* scripted, so Unity's own
    EventSystem decides what the press hits): rest, press, release at the rect's centre (screen px, y up)"""
    x, y = (rect[0] + rect[2]) / 2, (rect[1] + rect[3]) / 2
    a = c(cmd='ui_teams', action='mouse', x=x, y=y, button=0); time.sleep(settle)
    return x, y, a


def click_check(name, rect, what, after, want):
    """a fake-mouse click on what is drawn at rect does what the button does: after() returns the effect, want(effect) True"""
    if not rect: result(name, False, f'{what}: no rect in the read-back'); return False
    x, y, a = fake_click(rect)
    v = after(); ok = bool(want(v)) and a.get('ok') is not False
    if not ok:                                                       # what lies at that point (a tooltip over the button?)
        shot(name + '_fail', 0.0); note(name + '_texts_at_click', c(cmd='mpm_find_text', px=x, py=y).get('hits'))
    result(name, ok, f'{what} drawn at {[round(q) for q in rect]}: fake click at ({x:.0f}, {y:.0f}) -> {v} (mouse at {a.get("mouse")}, over UI {a.get("over_ui_raycast")})')
    return ok


def check_scale(name, rect, units_w, units_h=None, what=''):
    """drawn at the UI scale, or smaller only because the screen (16 px a side) is full"""
    if not rect: result(name, False, f'{what}: no rect'); return
    w, h = rect[2] - rect[0], rect[3] - rect[1]
    eff = w / (units_w * ppu())
    clamped = rect[0] <= 16 + 40 or rect[2] >= W - 16 - 2 or rect[1] <= 16 + 2 or rect[3] >= H - 16 - 2
    if units_h: clamped = clamped or h >= H - 32 - 2
    ok = abs(eff - SCALE) <= 0.03 or (eff < SCALE and eff > 0.95 and clamped and SCALE > 1.01)
    result(name, ok, f'{what} drawn at {eff:.3f} x (UI scale {SCALE:.2f}; {w:.0f}x{h:.0f} px{", fit to the screen" if clamped and abs(eff - SCALE) > 0.03 else ""})')


def part_title():
    shot('00_title', 1.5)
    def mp_page():
        press('MULTIPLAYER'); wait('mp page', state_is('title_menu'), 20); time.sleep(1.0)
        s = ms(); shot('01_mp_page', 0.5)
        rect, _ = panel_rect(s, 'recent', None)
        if rect is None and s.get('hist', {}).get('panel'):           # origin/main: the panel's title text
            rect = None
        check_scale('M5_recent_worlds', rect, 760, what='Recent Worlds panel')
        if rect and s.get('title_col_right', -1) > 0:
            result('M5_recent_clear_of_column', rect[0] >= s['title_col_right'], f'panel left {rect[0]:.0f} px, column right {s["title_col_right"]:.0f} px')
    safe('M5_recent', mp_page)
    def host_page():
        s = ms()
        if s.get('state') != 'title_menu': to_title_menu(); press('MULTIPLAYER')
        press('HOST_SAVE'); wait('host page', state_is('title_host'), 20); time.sleep(1.0)
        s = ms(); shot('09_host_page', 0.5)
        rect, _ = panel_rect(s, 'rules', None)
        check_scale('M5_rules_panel', rect, 430, what='host page Rules panel')
        if rect and s.get('title_col_right', -1) > 0:
            result('M5_rules_clear_of_column', rect[0] >= s['title_col_right'], f'panel left {rect[0]:.0f} px, column right {s["title_col_right"]:.0f} px')
        # a click at the drawn (scaled) Pause ">" arrow changes Pause (the fake mouse through Unity's own input)
        p0 = s.get('settings', {}).get('pause')
        click_check('M5_rules_click', (s.get('arrows') or {}).get(str(BTN['PAUSE_NEXT'])), 'host page Pause ">"',
                    lambda: ms().get('settings', {}).get('pause'), lambda v: v is not None and p0 is not None and v != p0)
        press('BACK'); press('HOST_FE'); wait('hub page', state_is('title_host'), 20); time.sleep(1.0)
        s = ms(); shot('12_hub_host_page', 0.5)
        rect, _ = panel_rect(s, 'fe', None)
        check_scale('M5_hub_rules_panel', rect, 430, what='hub host page Expedition Rules panel')
        press('BACK'); press('BACK')
    safe('M5_host_page', host_page)
    def red_x():                                                     # CLOSE RULE: the title Expedition Results window
        to_title_menu(); press('MULTIPLAYER')
        wait('results button', lambda: (lambda q: q if q.get('state') == 'title_menu' else None)(ms()), 20); time.sleep(4.5)   # results counted every 120 frames
        press('FE_RESLIST', 2.0)
        f = c(cmd='mpm_fewin'); shot('40_fe_results_list', 0.5)
        if f.get('mode') != 3: result('X_fe_results_red_x', False, f'the Expedition Results window did not open: {f}'); return
        new = ms().get('panels') is not None                         # dsuxmp2 code: mpm_esc {list:1} lists; origin/main: mpm_esc only opens the Esc menu
        e = c(cmd='mpm_esc', list=1) if new else {}
        win = [w for w in e.get('windows', []) if 'Foreign Expedition' in w.get('name', '')]
        note('40_fe_results_list', {'fewin': f, 'esc': e})
        if not (win and win[0].get('x')):
            result('X_fe_results_red_x', False, f'no red X on the title Expedition Results window ({e.get("windows") if new else "no mpesc_close_x in this build"})')
            press('FE_CANCEL', 1.0); return
        xr = win[0]['x_rect']; x, y, a = fake_click(xr, 2.0)
        f2 = c(cmd='mpm_fewin'); s2 = ms()
        ok = f2.get('mode') == 0 and s2.get('state') == 'title_menu'
        result('X_fe_results_red_x', ok, f'red X at {xr} ({xr[2] - xr[0]:.0f} px); fake click at ({x:.0f}, {y:.0f}): window mode {f.get("mode")} -> {f2.get("mode")}, page {s2.get("state")} (stays)')
        if f2.get('mode'): press('FE_CANCEL', 1.0)
    safe('X_fe', red_x)
    safe('M9', check_hostmods)
    def session_end():                                               # K13 re-check: the session-end box clear of the title column
        to_title_menu()
        c(cmd='mpm_fail_join', msg='The host left the game', host='Alex')
        wait('session end box', box_on, 20); time.sleep(1.0)
        s = ms(); shot('16_session_end', 0.3)
        bx = (s.get('box_rects') or {}).get('box'); cr = s.get('title_col_right', -1)
        if bx and cr > 0: result('K13_session_end_box_clear', bx[0] >= cr - 1, f'box left {bx[0]:.0f} px, column right {cr:.0f} px')
        c(cmd='mpm_box', action='confirm'); wait('box closed', box_off, 10); time.sleep(1.0); to_title_menu()
    safe('K13', session_end)
    def title_load():                                                # the review's screen: title > Load Campaign, tags shown
        to_title_menu(); c(cmd='mpm_open_load'); time.sleep(2.5)
        check_tag_row('M10_remove_tag_inside_title_load', '31_load_campaign_tags')
        c(cmd='mpm_list', close=1); time.sleep(1.5)
    safe('M10_title_load', title_load)


# ------------------------------------------------------------------ "Your save" list (M10), host a world (M3, M5 lobby + chat)
def loader_rows():
    """the loader's own read-back of the open Load list (dsl_rows.c lf_state, the same loader for both builds): per world its
    [Change] rect and the "Remove Tag" rect it sees (screen px, y up)"""
    st = os.path.join(T.SDIR, 'lf_state.json')
    if os.path.exists(st): os.replace(st, st + '.old')
    T.request({'cmd': 'lf_state'}); t0 = time.time()
    while time.time() - t0 < 10 and not os.path.exists(st): time.sleep(0.2)
    try: return {w['guid']: w for w in json.load(open(st)).get('worlds', [])}
    except Exception as e: return {'error': repr(e)}


def check_tag_row(name='M10_remove_tag_inside', shot_name='19_host_load_list'):
    t = wait('tag rows', lambda: (lambda q: q if any(rw.get('guid') == MOON_G for rw in q.get('rows', [])) else None)(c(cmd='mpm_tag')), 30, 1)
    c(cmd='mpm_list', scroll_to=MOON_G); time.sleep(1.5)            # the fit runs every 10 frames; the loader's rows once per rebuild
    t = c(cmd='mpm_tag'); row = [rw for rw in t.get('rows', []) if rw.get('guid') == MOON_G][0]
    ash = [rw for rw in t.get('rows', []) if rw.get('guid') == ASH_G]
    p = shot(shot_name, 0.5)
    tl = c(cmd='mpm_tree', what='load', detail=1); json.dump(tl, open(os.path.join(RUN, f'tree_{shot_name}.json'), 'w'), indent=0)
    json.dump(t, open(os.path.join(RUN, f'tag_{shot_name}.json'), 'w'), indent=0, ensure_ascii=False)
    nodes = [n for n in tl.get('tree', []) if n.get('on')]
    vpn = [n for n in nodes if n.get('name') == 'Viewport']
    geo = row.get('geo') == 1                                        # this branch's read-back; origin/main: rects only
    vr = row['vp_rect'] if geo and row.get('vp_rect', [0, 0, 0, 0])[2] > 0 else (vpn[0]['rect'] if vpn else None)
    rm, nr, cr = row['rm_rect'], row['name_rect'], row.get('row_rect')
    lr = loader_rows(); lw = lr.get(MOON_G, {}) if isinstance(lr, dict) else {}
    note(shot_name, {'row': row, 'viewport': vr, 'short': ash, 'loader': lw})
    if not row.get('remove'): result(name, False, f'no "Remove Tag" shown on {MOON_NAME!r}'); return
    edges = [e for e in ((cr[2] if cr and cr[2] > cr[0] else None), (vr[2] if vr else None)) if e]
    right = min(edges) if edges else nr[2]
    left = cr[0] if cr and cr[2] > cr[0] else nr[0]
    inside = rm[0] >= left - 1 and rm[2] <= right + 1
    w = rm[2] - rm[0]
    whole = (row.get('rm_pref', 0) <= w + 1 and row.get('rm_trunc') == 0) if geo else True   # origin/main: its box is its preferred width + 6
    size_ok = row.get('rm_fs', 0) >= 13.5 if geo else True          # never shrunk (design size 14)
    chg = lw.get('chg_rect') if lw.get('chg_rect') and lw['chg_rect'][2] > lw['chg_rect'][0] else None
    obst = list(row.get('obst') or []) + ([chg] if chg else [])     # the loader's [Change] too (origin/main has no obst read-back)
    hit = [o for o in obst if o[0] < rm[2] - 0.5 and o[2] > rm[0] + 0.5 and o[1] < rm[3] - 0.5 and o[3] > rm[1] + 0.5]
    clear_name = (row.get('name_end', 0) <= rm[0] + 1) if geo else True
    short_ok = True; sdet = ''
    if ash:                                                          # a short name: no cut, its link inside too
        a = ash[0]; short_ok = 'Ashfall' in a.get('name', '') and not a.get('cut') and (not a.get('remove') or a['rm_rect'][2] <= right + 1)
        sdet = f'; short "Ashfall" whole {short_ok} (cut {a.get("cut")!r})'
    ok = inside and whole and size_ok and not hit and clear_name and short_ok
    vis = None
    if p:
        im = img(p); vis = ink_box(im, (rm[0], rm[1], min(rm[2], right), rm[3]), 110) is not None
    result(name, ok, f'"Remove Tag" {rm[0]:.0f}..{rm[2]:.0f} px, row/list {left:.0f}..{right:.0f} px (viewport {vr}); inside {inside}, '
           f'whole {whole} (pref {row.get("rm_pref")} of {w:.0f} px, truncated {row.get("rm_trunc")}), size {row.get("rm_fs")}, '
           f'[Change] {chg}, overlaps {hit}, name text ends {row.get("name_end")} px; name now {row.get("cut") or row.get("name", "")[:60]!r}; ink {vis}{sdet}'
           + ('' if geo else ' (origin/main read-back: rects only)'))


def check_solo_box():
    t0 = time.time(); s = None
    while time.time() - t0 < 600:                                   # fast: the FIRST frame the box shows
        s = ms()
        if s.get('solo', {}).get('state') == 2 and s.get('box', {}).get('on'): break
        if s.get('state') == 'lobby': break
        time.sleep(0.1)
    if not (s.get('solo', {}).get('state') == 2 and s.get('box', {}).get('on')):
        result('M3_solo_box_visible', False, f'the box never showed (state {s.get("state")}, solo {s.get("solo")})'); return
    p = shot('20_solo_copy_box', 0.0)
    s2 = ms(); br = s2.get('box_rects') or {}
    box = br.get('box'); yes = (br.get('confirm') or {}).get('btn')
    if not box or box[2] <= box[0]:                                  # origin/main: the stock box, centred (546 x 208 at the UI scale)
        k = ppu() * SCALE; box = [W / 2 - 273 * k, H / 2 - 104 * k, W / 2 + 273 * k, H / 2 + 104 * k]
        yes = [W / 2 + 10 * k, box[1], box[2] - 20 * k, box[1] + 70 * k]
    note('20_solo', {'solo': s2.get('solo'), 'box': box, 'yes': yes, 'text': s2.get('box', {}).get('text')})
    if not p: result('M3_solo_box_visible', False, 'no frame'); c(cmd='mpm_box', action='cancel'); return
    im = img(p)
    head = (box[0], box[1] + 0.45 * (box[3] - box[1]), box[2], box[3])   # the question + body text (upper part of the box)
    fh, fy = bright_frac(im, head, 120), bright_frac(im, yes, 120) if yes else 0
    fade = s2.get('solo', {}).get('fade_at_show')
    ok = fh >= 0.01 and fy >= 0.01
    result('M3_solo_box_visible', ok, f'first frame of the box: text pixels {fh:.3f} of its upper part, Yes button {fy:.3f} (need >= 0.010 each)'
           + (f'; screen fade at show {fade:.2f} (1 = clear)' if fade is not None else ''))
    c(cmd='mpm_box', action='cancel')


def check_lobby():
    s = ms(); lw = s.get('lw', {})
    rect = lw.get('frame') if lw.get('frame') and lw['frame'][2] > lw['frame'][0] else None
    if rect: check_scale('M5_lobby', rect, 880, what='lobby window')
    else:                                                           # origin/main: the root (880 wide too)
        check_scale('M5_lobby', lw.get('rect'), 880, what='lobby window (root)')
    # a click at the drawn (scaled) cog opens the Rules (the fake mouse through Unity's own input)
    if click_check('M5_lobby_cog_click', (lw.get('buttons') or {}).get(str(BTN['LOBBY_SETTINGS'])), 'lobby cog',
                   lambda: ms().get('lw', {}).get('settings_open'), lambda v: v == 1):
        shot('22_lobby_rules', 0.5); press('RULES_DONE', 1.0)
    else:
        press('LOBBY_SETTINGS', 1.5); shot('22_lobby_rules', 0.5); press('RULES_DONE', 1.0)


def part_game():
    to_title_menu()
    press('MULTIPLAYER'); press('HOST_SAVE'); wait('host page', state_is('title_host'), 20)
    press('LOAD_SAVE', 2.5)
    safe('M10', check_tag_row)
    log('pick', c(cmd='load_save', file=os.path.join(I, 'data', 'Saves', SOLO_STEM + '.json'), det=0, timeout=180))
    safe('M3', check_solo_box)
    wait('lobby', state_is('lobby'), 300, 1)
    time.sleep(3); shot('21_lobby_host', 1.0)
    safe('M5_lobby', check_lobby)
    def quitbox():
        press('LOBBY_QUIT', 1.5); s = ms(); note('23_lobby_quit_confirm', s.get('box')); shot('23_lobby_quit_confirm', 0.5)
        c(cmd='mpm_box', action='cancel'); time.sleep(1.0)
    safe('23_lobby_quit', quitbox)
    def start_click():                                               # Start by a fake-mouse click on the drawn (scaled) button
        lw = ms().get('lw', {}); r0 = (lw.get('buttons') or {}).get(str(BTN['START']))
        def started():
            try: return wait('playing', state_is('playing'), 30, 1).get('state')
            except Exception: return ms().get('state')
        click_check('M5_lobby_start_click', r0, 'lobby Start', started, lambda v: v == 'playing')
    safe('M5_start', start_click)
    if ms().get('state') != 'playing': press('START', 2.0)
    wait('playing', state_is('playing'), 60, 1)
    time.sleep(3)
    def esc():
        c(cmd='mpm_esc'); time.sleep(2); p = shot('29_esc_host', 0.5)          # plain mpm_esc opens the Esc menu (both builds)
        te = c(cmd='mpm_tree', what='esc', detail=1); json.dump(te, open(os.path.join(RUN, 'tree_esc.json'), 'w'), indent=0)
        logo = [n['rect'] for n in te.get('tree', []) if n.get('name') == 'Img_GameLogo' and n.get('on')]
        s = ms(); sr = s.get('seed_rects') or []
        low = min([q[1] for q in sr] + [9e9]); top = logo[0][3] if logo else -1
        note('29_esc_host', {'seed_rects': sr, 'button_rects': s.get('button_rects'), 'logo': logo})
        result('K12_esc_column_on_screen', low >= 2 and 0 < top <= H - 2, f'Esc menu column from {top:.0f} (logo top) to {low:.0f} px (lowest line), window 0..{H} (UI {SCALE})')
        c(cmd='mpm_return_game'); time.sleep(1)
    safe('29_esc', esc)
    c(cmd='mpm_quit_title', timeout=120)
    wait('title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st()), 300, 2)
    wait('menu idle', state_is('idle', 'title_menu', 'failed'), 120, 1); time.sleep(3)
    s = ms()
    if s.get('box', {}).get('on'): shot('31_after_quit_box', 0.5); c(cmd='mpm_box', action='confirm'); time.sleep(1)


# ------------------------------------------------------------------ the hub (M6)
def lobby_rules_rows(s):
    lw = s.get('lw', {})
    if 'rules' in lw: return [tuple(x) for x in lw['rules']]
    # origin/main: the texts under the Rules column, found by position (label column / value column of the lobby window)
    rect = lw.get('rect'); k = (rect[2] - rect[0]) / 880.0
    xl, xv = rect[0] + (452 + 14 + 100) * k, rect[0] + (848 - 14 - 110) * k
    fr = lw.get('frame') or rect
    rows, seen = [], set()
    y = rect[3] - 170 * k
    while y > rect[1]:
        hl = c(cmd='mpm_find_text', px=xl, py=y).get('hits', []); hv = c(cmd='mpm_find_text', px=xv, py=y).get('hits', [])
        lt = next((h['text'] for h in hl if h['text'].strip()), None); vt = next((h['text'] for h in hv if h['text'].strip()), None)
        if lt and (lt, vt) not in seen: seen.add((lt, vt)); rows.append((lt, vt))
        y -= 10 * k
    return rows


def part_hub():
    to_title_menu()
    press('MULTIPLAYER'); press('HOST_FE'); wait('hub page', state_is('title_host'), 20)
    press('HOST_START', 2.0)
    wait('hub loaded + host session', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None)(st()), 900, 2)
    wait('hub lobby', state_is('lobby'), 120, 1)
    time.sleep(3)
    def rules():
        s = ms(); shot('32_hub_lobby', 0.5)
        rows = lobby_rules_rows(s)
        note('32_hub_lobby', {'rows': rows, 'cog': s.get('lw', {}).get('cog'), 'settings': s.get('settings')})
        labels = [a.lower() for a, _ in rows]
        want = [tr('Death').lower(), trk('Dungeon Loot', 'Dungeon loot').lower(), tr('Recruiting').lower()]
        gear = [v.lower() for a, v in rows if a.lower() == tr('Gear').lower()]
        ok_rows = all(w in labels for w in want) and tr('Pause').lower() not in labels
        ok_gear = gear == [trk('Own Storage', 'Own storage').lower()]
        result('M6_hub_lobby_rules', ok_rows and ok_gear, f'rows {rows}; want {want} and Gear = "{tr("Own storage")}", no "{tr("Pause")}"')
        result('M6_hub_lobby_no_cog', not s.get('lw', {}).get('cog') and str(BTN['LOBBY_SETTINGS']) not in (s.get('lw', {}).get('buttons') or {}),
               f'cog shown = {s.get("lw", {}).get("cog")}, buttons {list((s.get("lw", {}).get("buttons") or {}).keys())}')
        # Start (one unit brought from the test world): what Start sets must be what the lobby said
        u = json.load(open(os.path.join(RUN, 'units.json'))).get('units', [])
        b = c(cmd='fe_bring', file=os.path.join(I, 'data', 'Saves', SOLO_STEM + '.json'), guids=','.join(u[:1]), timeout=60)
        note('fe_bring', b); time.sleep(2)
        press('START', 3.0)
        wait('hub playing', state_is('playing'), 180, 1); time.sleep(2)
        g = ms().get('rules', {}).get('gear')
        result('M6_gear_matches_start', g == 2 and ok_gear, f'after Start rules.gear = {g} (2 = own storage); the lobby said {gear}')
    safe('M6', rules)


def main():
    g = threading.Thread(target=T.guard, daemon=True)
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    install(); seed()
    g.start()
    ok = True
    try:
        start()
        time.sleep(4)
        s = ms(); note('ui_scale', {'ui_scale': s.get('ui_scale'), 'screen': (W, H)})
        for part in PARTS:
            t0 = time.time()
            try:
                {'title': part_title, 'game': part_game, 'hub': part_hub}[part]()
            except Exception as e:
                ok = False; note('part_' + part, 'FAILED ' + repr(e)); log(traceback.format_exc())
                result('part_' + part, False, 'driver: ' + repr(e)[:200])
                try: shot(f'zz_fail_{part}', 0.5)
                except Exception: pass
                if part == 'game':
                    try:
                        c(cmd='mpm_quit_title', timeout=60); wait('title', lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st()), 300, 2); time.sleep(5)
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
        open(os.path.join(OUT, 'results.txt'), 'w').write('\n'.join(RESULTS) + '\n')
        print('\n'.join(RESULTS))
        log('done', TAG, 'ok' if ok else 'with failures', OUT)


if __name__ == '__main__':
    main()
