#!/usr/bin/env python3
"""PvP pair test (lane dsuxmp, sub-lane uxmp-pvp: UX review 2026-10-25 rows M2, M5, P1, P2; lane dsuxmp2, sub-lane
dsuxmp2-pvp: the owner's CLOSE, MOVE and CARD rules on the PvP windows). Two hidden loader copies (UPA hosts as "Alex", UPB
joins by address as "Sam") on a port the OS picks, with the loader and mods of the dsuxmp2 shared build
(~/ds_launcher_wt_dsuxmp2, ds_modloader 83636d0: the kit's red X, UI Scale, right-drag move) and THIS multiplayer dylib (a
fake-mouse build: ext/dsuxmp/build_mouse.sh).

  pvp_pair.py --dylib <multiplayer.dylib> [--lang en|de|ru] [--size 960x540] [--scale 1.0] [--tag name] [--shots dir]
              [--src <tree>]   (the tree whose lang/ goes with the dylib; default this worktree)

Checks (each prints CHECK PASS|FAIL; the last line is RESULT PASS|FAIL <passed>/<checks>):
  P1  the guest's open PvP window shows the host's rules (all rule rows) and teams after the host changes them, and after
      the guest opens the window again
  M5  the PvP window and the result window follow the game's UI scale: on-screen size at 1.4 = the size at 1.0 x 1.4, or
      fit to the screen with 16 px free on each side; a mouse click on a scaled button still works
  P2  the planning-pause message (ours or the game's notice) does not overlap the Teams button, the Players panel or the
      score panel, at the start UI scale (run it at 1.0 and 1.4: the game's HUD keeps its layout when the scale is set while
      running)
  M2  Start Match with a team that has no unit: the window stays open, the reason shows in red, no match is set up
  ESC   a plain mpm_esc (test command) opens the game's Esc menu (other tests open it so)
  CLOSE the game's red X shows at the top-right of the title band of the PvP Match window, the text Bring Units list and the
        result window (mpm_esc {list:1}); the PvP window's bottom Close is gone (the X does it); a fake-mouse click on the X
        closes the window (Bring list: back to the PvP window)
  MOVE  a fake right-drag moves the PvP Match window (by the drag) and it opens again at the moved place
  CARD  in the text Bring list a click on a unit's portrait, and on the empty part of its tile, picks / unpicks it
Needs: two dsuxmp2 copy slots + the pair lock (~/ToDelete_2026-10-25/dsuxmp2/slot.sh, taken and released here), a rig pair
lease, load < 15, instances UPA / UPB (ext/dsuxmp/make_instance_pvp.sh). Copies are hidden, muted, windowed; the owner's
game is never touched."""
import argparse, json, os, sys, time, glob, shutil, subprocess, threading, signal, traceback, socket, hashlib, re

ap = argparse.ArgumentParser()
ap.add_argument('--dylib', required=True); ap.add_argument('--lang', default='en'); ap.add_argument('--size', default='960x540')
ap.add_argument('--scale', type=float, default=1.0); ap.add_argument('--tag', default=''); ap.add_argument('--shots', default='')
ap.add_argument('--src', default='')
A = ap.parse_args()
HERE = os.path.dirname(os.path.abspath(__file__)); WT = os.path.abspath(os.path.join(HERE, '..', '..'))
LB = os.path.expanduser('~/ds_launcher_wt_dsuxmp2/loader'); LT = os.path.join(LB, 'test')
SRCT = os.path.abspath(os.path.expanduser(A.src)) if A.src else WT        # its lang/ goes with the dylib
LANGS = {'en': 0, 'de': 3, 'ru': 4}
W, H_ = (int(v) for v in A.size.split('x'))
TAG = A.tag or f'{A.lang}_{W}x{H_}_ui{A.scale:.1f}'
RUN = os.path.expanduser(f'~/ToDelete_2026-10-25/dsuxmp2/pvp/runs/{TAG}' + time.strftime('_%H%M%S')); os.makedirs(RUN, exist_ok=True)
SHOTS = A.shots
os.environ['LRIG_SIZE'] = f'{W}x{H_}'
sys.path.insert(0, LT)
import lrig as r                                           # noqa: E402  (instances, prefs, guard helpers; read only)
sys.path.insert(1, os.path.expanduser('~/ds_multiplayer/rig'))
import rig as realrig                                      # noqa: E402  leases only
HOST, GUEST = 'UPA', 'UPB'
LANE = 'dsuxmp2-pvp'
SLOTSH = os.path.expanduser('~/ToDelete_2026-10-25/dsuxmp2/slot.sh')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
SOLO_G = 'b0c0ffee-0000-4000-8000-0000000000a1'; SOLO_STEM = 'Brightwood__AutoSave_' + SOLO_G.replace('-', '') + '_0'
MYPIDS = {}; violations = []; stop_guard = threading.Event(); RESULTS = []
LOGF = open(os.path.join(RUN, 'log.txt'), 'a')
TR = json.load(open(os.path.join(SRCT, 'lang', f'{A.lang}.json'))) if A.lang != 'en' else {}
def tr(k): return TR.get(k) or k
PLAN_MSG = tr('Planning pause: give your orders, then press Ready')


def btn_ids():
    t = open(os.path.join(WT, 'src', 'mpmenu.h')).read(); a = t.index('MPB_NONE = 0,'); b = t.index('MPB_COUNT', a)
    names = [x.strip() for x in re.sub(r'/\*.*?\*/', '', t[a:b], flags=re.S).split(',') if x.strip()]
    d, i = {}, 0
    for n in names:
        if '=' in n: k, v = n.split('='); i = int(v); n = k.strip()
        d[n[4:]] = i; i += 1
    return d
BTN = btn_ids()


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOGF.write(s + '\n'); LOGF.flush()


def check(name, ok, info=''):
    RESULTS.append((name, bool(ok))); log('CHECK', 'PASS' if ok else 'FAIL', name, '|', str(info)[:600])


def idir(x): return r.idir(x)


# ------------------------------------------------------------------ install + seed
def install(x):
    app = os.path.join(idir(x), 'DungeonSettlers.app'); import plistlib
    pl = plistlib.load(open(os.path.join(app, 'Contents', 'Info.plist'), 'rb'))
    assert pl.get('LSBackgroundOnly') and pl.get('LSUIElement'), f'{x} not a hidden copy'
    fw = os.path.join(app, 'Contents', 'Frameworks'); md = os.path.join(app, 'Contents', 'Resources', 'mods')
    ld = os.path.join(fw, 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(ld): os.replace(ld, aside)                      # codesign refuses loose files in Frameworks
    shutil.copy2(os.path.join(LB, 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp'))
    os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    os.makedirs(md, exist_ok=True)
    for m in ('multiplayer', 'custom-modes'):
        d = os.path.join(md, m)
        if os.path.exists(d):
            os.makedirs(os.path.join(idir(x), 'old'), exist_ok=True); shutil.move(d, os.path.join(idir(x), 'old', f'{m}.{time.time():.0f}'))
        shutil.copytree(os.path.join(LB, 'build', 'mods_all', 'mac', m), d)
    # this lane's multiplayer: its dylib (new inode) + its texts; mod.json names the dylib's sha256 (the loader checks it)
    mp = os.path.join(md, 'multiplayer')
    shutil.copy2(A.dylib, os.path.join(mp, 'multiplayer.dylib.tmp')); os.replace(os.path.join(mp, 'multiplayer.dylib.tmp'), os.path.join(mp, 'multiplayer.dylib'))
    mj = json.load(open(os.path.join(mp, 'mod.json')))
    mj['platforms']['mac-arm64']['files']['multiplayer.dylib'] = hashlib.sha256(open(A.dylib, 'rb').read()).hexdigest()
    json.dump(mj, open(os.path.join(mp, 'mod.json'), 'w'), indent=1)
    for f in glob.glob(os.path.join(SRCT, 'lang', '*.json')): shutil.copy2(f, os.path.join(mp, 'lang'))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    if os.path.isdir(aside): os.replace(aside, ld)
    os.makedirs(ld, exist_ok=True)
    for f in glob.glob(os.path.join(LB, 'lang', '*.json')): shutil.copy2(f, ld)


def world_save(path, guid, stem, clan):
    w = json.load(open(TPL)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'
    # +60 Constitution on every clan unit (as hub_dive_test): a team out in 4 s ended the round before the first planning
    # pause (7 s of fight), so P2 had nothing to measure
    ec, uc = w['EntityComponentSaveData']['EntityComponents'], w['EntityContainerSaveData']['UnitContainerSaveData']
    for g in [g for sd in uc for g in (uc[sd].get('PlayerGuidList') or [])]:
        st = next((q['Data'] for q in ec.get(g) or [] if q and q.get('Type') == 0), None)
        if st is not None: gs = st.setdefault('GeneratedStats', {}); gs['Constitution'] = gs.get('Constitution', 0.0) + 60.0
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


def seed(x, name, port):
    I = idir(x)
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for p in ('data/Saves', 'mp_solo.txt', 'mp_tags.txt', 'mp_worlds.txt', 'mp_menu.json', 'mp.json', 'mp_history.json',
              'data/fe', 'data/mp_owners', 'data/SaveHeaderCache.json', 'data/Mods/worlds.json', 'data/Mods/save_copies',
              'data/Mods/settings.json', 'data/Mods/ui_places.tsv', 'data/ModData', 'rig.log', 'dsload.log', 'player.log'):
        q = os.path.join(I, p)
        if os.path.exists(q): shutil.move(q, os.path.join(old, p.replace('/', '_')))
    sv = os.path.join(I, 'data', 'Saves'); os.makedirs(sv)
    if x == HOST: world_save(os.path.join(sv, SOLO_STEM + '.json'), SOLO_G, SOLO_STEM, 'Brightwood')
    json.dump({'name': name}, open(os.path.join(I, 'mp_menu.json'), 'w'))          # a test name (no real name in shots)
    os.makedirs(os.path.join(I, 'data', 'Mods'), exist_ok=True)
    json.dump({'multiplayer': {'host_port': port}}, open(os.path.join(I, 'data', 'Mods', 'settings.json'), 'w'))
    open(os.path.join(I, 'ux_extip.txt'), 'w').write('203.0.113.7\n')
    cr = os.path.join(I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')
    os.makedirs(os.path.join(I, 'gameroot', 'data', 'Saves'), exist_ok=True)


def start(x):
    I = idir(x)
    r.prepare_data(x)
    p = os.path.join(I, 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANGS[A.lang]; s['GeneralSettingData']['IsLanguageInitialized'] = True
    s['GeneralSettingData']['HiddenUpdateNoticeVersion'] = 1
    s['GraphicSettingData']['UIScale'] = A.scale
    json.dump(s, open(p, 'w'), indent=1)
    mods = os.path.join(I, 'data', 'Mods'); os.makedirs(mods, exist_ok=True)
    en = ['multiplayer', 'custom-modes']
    json.dump({'enabled': en, 'order': en}, open(os.path.join(mods, 'active.json'), 'w'))
    json.dump({'worlds': {}}, open(os.path.join(mods, 'worlds.json'), 'w'))
    if os.path.exists(r.sockpath(x)): os.unlink(r.sockpath(x))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_PROFILE': os.path.join(mods, 'active.json'), 'DSML_SESSION': os.path.join(mods, 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSML_GAME_SAVES': os.path.join(I, 'gameroot', 'data', 'Saves'), 'DSL_CLOUD': '0', 'DSL_UPDATE': '0',
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0',
           'DSRIG_HIDEWIN': '1', 'DSL_CRASHREP_TEST': os.path.join(I, 'crashrep_test'),
           'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': 'file://' + os.path.join(I, 'ux_extip.txt')}
    env.update(r.soft_test_env())
    r.require_windowed(x)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', str(W), '-screen-height', str(H_)]
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(19), start_new_session=True)
    open(r.pidfile(x), 'w').write(f'{p.pid}\n'); MYPIDS[x] = p.pid; log('copy started', x, 'pid', p.pid, TAG)


def alive(pid):
    try: os.kill(pid, 0); return True
    except OSError: return False


def guard():                                               # a copy that shows a window or comes to the front is killed
    while not stop_guard.is_set():
        pids = [p for p in MYPIDS.values() if alive(p)]
        if pids:
            try:
                bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
                fp = r.front_pid()
            except Exception:
                bad, fp = [], None
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids: os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(0.5)


# ------------------------------------------------------------------ helpers
def c(who, timeout=60, **k): return r.send(who, k, timeout=timeout, connect_wait=0)
def ms(x): return c(x, cmd='mpm_status')
def st(x): return c(x, cmd='status')
def pv(x, **k): return c(x, cmd='mpm_pvp', **k)
def probe(x, **k): return c(x, cmd='mpm_pvp', action='probe', **k)
def press(x, n, pause=1.3):
    a = c(x, cmd='mpm_press', button=BTN[n]); time.sleep(pause); return a


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


def shot(x, name, settle=0.8):
    if not SHOTS: return
    time.sleep(settle); os.makedirs(SHOTS, exist_ok=True)
    p = os.path.join(SHOTS, f'{name}_{"host" if x == HOST else "guest"}.png')
    if os.path.exists(p): os.makedirs(os.path.join(SHOTS, 'old'), exist_ok=True); os.replace(p, os.path.join(SHOTS, 'old', os.path.basename(p)[:-4] + f'.{time.time():.0f}.png'))
    tmp = os.path.join(WT, 'build', 'shots_tmp', os.path.basename(p)); os.makedirs(os.path.dirname(tmp), exist_ok=True)   # copies write only under ~/ds_*
    c(x, cmd='mpm_shot', path=tmp, how='backbuffer'); t0 = time.time()
    while time.time() - t0 < 8 and not os.path.exists(tmp): time.sleep(0.2)
    if os.path.exists(tmp): time.sleep(0.3); shutil.move(tmp, p)
    log('shot', os.path.basename(p), 'ok' if os.path.exists(p) else 'MISSING')


def rows(x): return [tuple(q) for q in pv(x).get('rows', [])]
def teams(x): return sorted(tuple(t) for t in pv(x).get('shown_teams') or [])
def state_is(x, *names): return lambda: (lambda s: s if s.get('state') in names else None)(ms(x))
def touch_lease():
    try: realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == LANE])
    except Exception: pass


def overlap(a, b, pad=0.0):
    return bool(a and b) and a[0] < b[2] - pad and b[0] < a[2] - pad and a[1] < b[3] - pad and b[1] < a[3] - pad


def safe(name, fn):
    try:
        fn(); return True
    except Exception as e:
        check(name + ' (ran to the end)', False, repr(e)); log(traceback.format_exc()); return False


def set_rule(x, key, want, row, limit=6):                  # host: arrows until the window shows the wanted value
    for _ in range(limit):
        if pv(x).get(key) == want: return True
        pv(x, action='arrow', row=row, dir=1); time.sleep(0.6)
    return pv(x).get(key) == want


def set_scale(x, v):
    a = pv(x, action='uiscale', v=v); time.sleep(1.2); return a


# ------------------------------------------------------------------ the checks
def p1_rules():
    """P1: the guest's window shows the host's window (rules and teams)."""
    set_rule(HOST, 'bo', 5, 1); set_rule(HOST, 'secs', 300, 2)
    if not pv(HOST).get('pause'): pv(HOST, action='arrow', row=3, dir=1); time.sleep(0.6)
    pv(HOST, action='arrow', row=4, dir=1); time.sleep(0.6)                  # Arena / Map: one step
    pv(HOST, action='arrow', row=7, dir=1); time.sleep(0.6)                  # Enemy vision: Shown
    q = pv(HOST); log('host rules now', q.get('bo'), q.get('secs'), q.get('pause'), rows(HOST))
    time.sleep(2.0)
    hr, gr = rows(HOST), rows(GUEST)
    check('P1 the host changed rules (Best of 5, 5:00, planning pause, map, enemy vision)', q.get('bo') == 5 and q.get('secs') == 300 and q.get('pause') == 1, q.get('rows'))
    check("P1 the guest's open window shows the host's rules (every row)", hr and hr == gr, f'host {hr} | guest {gr}')
    shot(GUEST, 'P11_pvp_window', 0.5)
    pv(GUEST, action='close'); time.sleep(1.0); pv(GUEST, action='esc'); time.sleep(2.5)
    check("P1 after the guest opens its window again: still the host's rules", rows(GUEST) == rows(HOST), f'host {rows(HOST)} | guest {rows(GUEST)}')
    th0 = teams(HOST); check("P1 the guest's window shows the host's teams", th0 and teams(GUEST) == th0, f'host {th0} | guest {teams(GUEST)}')
    gid = [t[0] for t in th0 if t[0] != 0]
    i = [t[0] for t in th0].index(gid[0]) if gid else 1
    pv(HOST, action='player', i=i); time.sleep(2.0)                          # the host moves the guest to the other team
    th1, tg1 = teams(HOST), teams(GUEST)
    check("P1 after the host moves the guest to the other team: the guest's window shows it too", th1 != th0 and tg1 == th1, f'before {th0} | host {th1} | guest {tg1}')
    pv(HOST, action='player', i=i); time.sleep(1.0)                          # back
    # the rules for the match: best of 1, 1:00, planning pause, this settlement, enemy vision hidden
    for _ in range(3):
        if pv(HOST).get('bo') == 1: break
        pv(HOST, action='arrow', row=1, dir=-1); time.sleep(0.5)
    for _ in range(6):
        if pv(HOST).get('secs') == 60: break
        pv(HOST, action='arrow', row=2, dir=-1); time.sleep(0.5)
    pv(HOST, action='arrow', row=4, dir=-1); time.sleep(0.5); pv(HOST, action='arrow', row=7, dir=-1); time.sleep(2.0)
    check("P1 after more changes (best of 1, 1:00, back to the first map): the guest's window follows", rows(GUEST) == rows(HOST) and teams(GUEST) == teams(HOST), f'host {rows(HOST)} | guest {rows(GUEST)}')


def wsize(p, k):
    q = p.get(k); return (q[2] - q[0], q[3] - q[1]) if q else None


def m5_window(name, key, other, sw, sh):
    """M5: the window's size at UI 1.0 and 1.4 (ours at the start scale and the other one)."""
    a = probe(HOST); set_scale(HOST, other); b = probe(HOST)
    lo, hi = (a, b) if a.get('ui_scale', 1) < b.get('ui_scale', 1) else (b, a)
    wl, wh = wsize(lo, key), wsize(hi, key)
    if not wl or not wh: check(f'M5 {name}: shown at both scales', False, f'{lo.get(key)} {hi.get(key)}'); return a, b
    kl = lo.get('pv_k', 1) if key == 'window' else None
    w0, h0 = wl[0] / (lo.get('pv_k') if key == 'window' else 1), wl[1] / (lo.get('pv_k') if key == 'window' else 1)
    if key != 'window':                                                    # the result window: its size at scale 1 from the 1.0 probe (it fits at 1.0)
        w0, h0 = wl
    def kexp(s): return min(s, (sw - 32) / w0, (sh - 32) / h0)
    ew, eh = w0 * kexp(hi['ui_scale']), h0 * kexp(hi['ui_scale'])
    ok = abs(wh[0] - ew) <= 3 and abs(wh[1] - eh) <= 3 and hi['ui_scale'] > lo['ui_scale'] + 0.3
    inside = hi[key][0] >= 15 and hi[key][1] >= 15 and hi[key][2] <= sw - 15 and hi[key][3] <= sh - 15
    check(f'M5 {name}: at UI {hi["ui_scale"]:.2f} its size = {kexp(hi["ui_scale"]) / kexp(lo["ui_scale"]):.3f} x its size at UI {lo["ui_scale"]:.2f} (fit to the screen)',
          ok, f'{wl[0]:.0f}x{wl[1]:.0f} -> {wh[0]:.0f}x{wh[1]:.0f}, expected {ew:.0f}x{eh:.0f} (screen {sw}x{sh}, k {kl})')
    check(f'M5 {name}: inside the screen with 16 px free at UI {hi["ui_scale"]:.2f}', inside, hi[key])
    return a, b


def click(x, rect, what):
    cx, cy = (rect[0] + rect[2]) / 2, (rect[1] + rect[3]) / 2
    a = c(x, cmd='ui_teams', action='mouse', x=cx, y=cy, button=0); log('mouse', what, cx, cy, a); time.sleep(2.2)


def m5_pvp(sw, sh):
    other = 1.0 if A.scale > 1.2 else 1.4
    m5_window('PvP window', 'window', other, sw, sh)
    big = 1.4
    if probe(HOST).get('ui_scale', 1) < 1.2: set_scale(HOST, big)
    shot(HOST, 'M5_pvp_window_ui1.4', 0.5)
    p = probe(HOST); bo0 = pv(HOST).get('bo')
    if p.get('win_right'):
        click(HOST, p['win_right'], 'To win >')
        bo1 = pv(HOST).get('bo')
        check('M5 a mouse click on a button of the scaled PvP window works (To win >)', bo1 != bo0, f'{bo0} -> {bo1} at {p["win_right"]}')
        for _ in range(3):                                                 # back to best of 1
            if pv(HOST).get('bo') == 1: break
            pv(HOST, action='arrow', row=1, dir=-1); time.sleep(0.5)
    else:
        check('M5 a mouse click on a button of the scaled PvP window works (To win >)', False, 'no arrow rect')
    set_scale(HOST, A.scale)


def p2_measure(label):
    """P2: during a planning pause, on both copies: every message text that shows the planning sentence."""
    for x in (HOST, GUEST):
        p = probe(x); bad = []
        msgs = []
        if p.get('msg') and PLAN_MSG in (p.get('msg_text') or ''): msgs.append(('ours', p['msg']))
        for t in p.get('stock_notice') or []:
            if PLAN_MSG in (t.get('text') or ''): msgs.append(('game notice', t['rect']))
        if not msgs:
            check(f'P2 {label} {"host" if x == HOST else "guest"}: the planning message shows', False, json.dumps(p)[:500]); continue
        for who, m in msgs:
            ph = probe(x, x0=m[0], y0=m[1], x1=m[2], y1=m[3]).get('panel_hits', -1)
            if overlap(m, p.get('teams_btn')): bad.append(f'{who} over the Teams button {p.get("teams_btn")}')
            if overlap(m, p.get('score_panel')): bad.append(f'{who} over the score panel {p.get("score_panel")}')
            if ph != 0: bad.append(f'{who}: {ph} points on the Players panel')
        check(f'P2 {label} {"host" if x == HOST else "guest"}: the planning message is clear of the Teams button, the Players panel and the score panel',
              not bad and p.get('score_panel') is not None, f'{msgs} | {bad} | ui {p.get("ui_scale")}')


def m2_refused():
    """M2: the guest on the host's team: Red has no player and no unit. Start Match: window open, red reason, no match."""
    for x in (HOST,):
        if not pv(x).get('window'): open_esc(x); pv(x, action='press_esc'); time.sleep(2)
    th = teams(HOST); gid = [t[0] for t in th if t[0] != 0]
    i = [t[0] for t in th].index(gid[0]) if gid else 1
    if dict(th).get(gid[0] if gid else 1) == 2: pv(HOST, action='player', i=i); time.sleep(1.2)
    log('teams before Start', teams(HOST))
    pv(HOST, action='press_start'); time.sleep(3.0)
    q = pv(HOST); s = c(HOST, cmd='pvp_status')
    shot(HOST, '40_pvp_start_refused', 0.3)
    check('M2 Start Match with an empty team: the PvP window stays open', q.get('window') == 1, f'window {q.get("window")}')
    want = (tr('Each team needs at least one player. Click a player\'s name to move them to the other team.'), tr("Each team needs at least one unit. Right-click a unit's portrait to take it."))
    check('M2 the reason shows in red', q.get('note') in want and q.get('note_err') == 1, f'note {q.get("note")!r} red {q.get("note_err")}')
    check('M2 no match was set up', not s.get('active') and s.get('phase', 0) == 0, {k: s.get(k) for k in ('phase', 'active', 'arena')})
    pv(HOST, action='player', i=i); time.sleep(1.0)


# ------------------------------------------------------------------ CLOSE / MOVE / CARD (dsuxmp2-pvp, owner rules 2026-10-25)
WIN_H = {'window': 906, 'bring': 700, 'result': 360}                     # window heights at scale 1 (pvp_ui.c PV_H, br_build, pr_build)


def rect(p, k):                                           # a screen rect of the probe (origin/main has no probe: its status has "window": 0|1)
    v = p.get(k); return v if isinstance(v, list) and len(v) == 4 else None


def open_esc(x):                                          # the Esc menu: this branch's test action, else the plain mpm_esc (origin/main)
    pv(x, action='esc_menu'); time.sleep(1.5)
    if not pv(x).get('esc_menu'): c(x, cmd='mpm_esc'); time.sleep(1.5)


def red_x(x, name):
    """our window whose name has `name`, as mpm_esc {list:1} lists it (name, x = its red X shows, x_rect = its screen rect)"""
    try: q = c(x, cmd='mpm_esc', list=1)
    except Exception as e: log('mpm_esc list', repr(e)); return None
    for w in (q.get('windows') or []) if isinstance(q, dict) else []:
        if name in w.get('name', ''): return w
    log('mpm_esc list: no', repr(name), json.dumps(q)[:300]); return None


def x_on_band(w, win, key):
    """the red X shows, inside the window, at its top-right, in the title band (60 units from the top)"""
    r = (w or {}).get('x_rect')
    if not (w and w.get('x') and r and win and r[2] > r[0]): return False
    k = (win[3] - win[1]) / WIN_H[key]; cx, cy = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
    return win[0] - 1 <= r[0] and r[2] <= win[2] + 1 and r[3] <= win[3] + 1 and cy >= win[3] - 62 * k and cx >= win[2] - 0.15 * (win[2] - win[0])


def drag(x, sx, sy, dx, dy, button=1, frames=20):         # fake mouse: press at sx, sy, move by dx, dy over frames, let go
    a = c(x, cmd='ui_teams', action='mouse', x=sx, y=sy, button=button, x1=sx + dx, y1=sy + dy, frames=frames)
    log('mouse drag', button, sx, sy, '->', sx + dx, sy + dy, a); time.sleep(2.5)


def pvp_window_rules(sw, sh):
    """CLOSE + MOVE on the PvP Match window (host)."""
    p = probe(HOST); r0 = rect(p, 'window')
    w = red_x(HOST, 'PvP Match window')
    check('CLOSE the PvP Match window shows the game\'s red X at the top-right of its title band', x_on_band(w, r0, 'window'), f'{w} window {r0}')
    check('CLOSE the PvP Match window\'s bottom Close button is gone (the red X closes it)', r0 is not None and p.get('close') is None, p.get('close'))
    shot(HOST, 'X1_pvp_window_red_x', 0.3)
    if not r0:
        for n in ('MOVE a right-drag moves the PvP Match window by the drag', 'CLOSE a mouse click on the red X closes the PvP Match window', 'MOVE the PvP Match window opens again at the moved place'):
            check(n, False, 'no window rect')
        return
    k = (r0[3] - r0[1]) / WIN_H['window']
    sx, sy = r0[0] + 300 * k, r0[3] - 84 * k                            # the hint row under the title (no button there)
    left, right, down, up = r0[0], sw - r0[2], r0[1], sh - r0[3]
    dx = -min(120, left - 4) if left >= right else min(120, right - 4)
    dy = (-min(10, down - 2) if down >= up else min(10, up - 2)) if max(down, up) > 4 else 0
    drag(HOST, sx, sy, dx, dy)
    r1 = rect(probe(HOST), 'window')
    moved = r1 and abs((r1[0] - r0[0]) - dx) <= 3 and abs((r1[1] - r0[1]) - dy) <= 3 and abs((r1[2] - r1[0]) - (r0[2] - r0[0])) <= 2
    check('MOVE a right-drag moves the PvP Match window by the drag', moved, f'drag {dx:.0f}, {dy:.0f} px: {r0} -> {r1}')
    shot(HOST, 'X2_pvp_window_moved', 0.3)
    w = red_x(HOST, 'PvP Match window')
    if w and w.get('x') and w.get('x_rect'):
        click(HOST, w['x_rect'], 'red X of the PvP Match window')
        q = pv(HOST)
        check('CLOSE a mouse click on the red X closes the PvP Match window', q.get('window') == 0 and not (q.get('roots') or {}).get('window'), {k_: q.get(k_) for k_ in ('window', 'roots')})
    else:
        check('CLOSE a mouse click on the red X closes the PvP Match window', False, f'no red X: {w}')
        pv(HOST, action='close'); time.sleep(1.2)
    if not pv(HOST).get('esc_menu', 1): open_esc(HOST)
    pv(HOST, action='press_esc'); time.sleep(2)
    r2 = rect(probe(HOST), 'window')
    check('MOVE the PvP Match window opens again at the moved place', bool(moved) and r2 and all(abs(a - b) <= 2 for a, b in zip(r1, r2)), f'moved {r1} | again {r2}')


def bring_list_rules():
    """CLOSE + CARD on the text Bring Units list (host: it has a save with units). With the loader's kit the PvP window's
    Bring opens the card window (mu_bring.c); the text list is its fallback, opened here by the test action bring_text."""
    pv(HOST, action='bring_text'); time.sleep(1.5)
    if not (pv(HOST).get('bring') or {}).get('open'):
        for n in ('CLOSE the text Bring Units list shows the red X at the top-right of its title band', "CARD a click on a unit's portrait in the text Bring list picks it",
                  "CARD a click on the empty part of a unit's tile in the text Bring list unpicks it", 'CLOSE a mouse click on the red X of the text Bring list goes back to the PvP window'):
            check(n, False, 'the text Bring list did not open')
        return
    p = probe(HOST); w = red_x(HOST, 'PvP Bring Units window')
    check('CLOSE the text Bring Units list shows the red X at the top-right of its title band', x_on_band(w, rect(p, 'bring'), 'bring'), f'{w} list {p.get("bring")}')
    pv(HOST, action='br_save', i=0); time.sleep(1.5)
    def pick0():
        u = (pv(HOST).get('bring') or {}).get('units') or []; return u[0][3] if u else None
    a0 = pick0(); tile = rect(probe(HOST), 'br_tile0')
    if a0 is None or not tile:
        check("CARD a click on a unit's portrait in the text Bring list picks it", False, f'no unit (pick {a0}, tile {tile})'); return
    k = (tile[3] - tile[1]) / 108; cx, py = (tile[0] + tile[2]) / 2, tile[3] - 26 * k   # the portrait spot (pvp_ui.c br_build)
    shot(HOST, 'X3_bring_list_red_x', 0.3)
    click(HOST, [cx - 2, py - 2, cx + 2, py + 2], 'unit 0 portrait'); a1 = pick0()
    check("CARD a click on a unit's portrait in the text Bring list picks it", a1 is not None and a1 != a0, f'{a0} -> {a1}')
    ex = tile[0] + 8 * k                                                  # left of the portrait, inside the tile
    click(HOST, [ex - 2, py - 2, ex + 2, py + 2], 'empty part of unit 0 tile'); a2 = pick0()
    check("CARD a click on the empty part of a unit's tile in the text Bring list unpicks it", a1 != a0 and a2 == a0, f'{a0} -> {a1} -> {a2}')
    w = red_x(HOST, 'PvP Bring Units window')
    if w and w.get('x') and w.get('x_rect'):
        click(HOST, w['x_rect'], 'red X of the text Bring list'); q = pv(HOST)
        check('CLOSE a mouse click on the red X of the text Bring list goes back to the PvP window', not q['bring'].get('open') and q.get('window') == 1 and (q.get('roots') or {}).get('window') == 1,
              {k_: q.get(k_) for k_ in ('window', 'roots')})
    else:
        check('CLOSE a mouse click on the red X of the text Bring list goes back to the PvP window', False, f'no red X: {w}')
        pv(HOST, action='br_back'); time.sleep(1.2)


# ------------------------------------------------------------------ the run
def run(port):
    on_title = lambda x: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(st(x))
    wait('host title', lambda: on_title(HOST), 900, 2); wait('guest title', lambda: on_title(GUEST), 900, 2); time.sleep(3)
    press(HOST, 'MULTIPLAYER'); press(HOST, 'HOST_SAVE'); wait('host page', state_is(HOST, 'title_host'), 20); press(HOST, 'LOAD_SAVE', 2.5)
    log('pick', c(HOST, cmd='load_save', file=os.path.join(idir(HOST), 'data', 'Saves', SOLO_STEM + '.json'), det=0, timeout=180))
    def hosting():
        m = ms(HOST)
        if m.get('solo', {}).get('state') == 2 and m.get('box', {}).get('on'): c(HOST, cmd='mpm_box', action='cancel')
        s = st(HOST)
        return s if s.get('mp', {}).get('started') and m.get('state') == 'lobby' else None
    wait('host lobby', hosting, 600, 1)
    touch_lease()
    press(GUEST, 'MULTIPLAYER'); press(GUEST, 'JOIN_ADDR')
    c(GUEST, cmd='mpm_type', text=f'127.0.0.1:{ms(HOST).get("port") or port}'); time.sleep(0.5); press(GUEST, 'CONNECT', 0.2)
    wait('guest in the world', lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st(GUEST)), 600, 1)
    try: wait('guest waiting lobby', state_is(GUEST, 'guest_lobby'), 60, 0.5)
    except Exception as e: log('guest lobby:', repr(e))
    time.sleep(3); press(HOST, 'START', 3.0)
    wait('host playing', state_is(HOST, 'playing'), 60, 1); wait('guest playing', state_is(GUEST, 'playing'), 90, 1); time.sleep(3)
    us = [u for u in c(HOST, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    us.sort(key=lambda u: u['guid'])
    for u in us[:2]: c(HOST, cmd='mp_claim', guid=u['guid'])
    for u in us[2:4]: c(GUEST, cmd='mp_claim', guid=u['guid'])
    time.sleep(3); touch_lease()
    for x in (HOST, GUEST): c(x, cmd='ui_teams', action='close')        # a player who arrived with no unit gets the Teams window
    time.sleep(1)
    sw, sh = probe(HOST).get('screen', [W, H_])
    c(HOST, cmd='mpm_esc'); time.sleep(2); q = pv(HOST)                    # the plain command (other tests open the menu so)
    check('ESC a plain mpm_esc opens the Esc menu', q.get('esc_menu', q.get('esc_entry')) == 1, {k_: q.get(k_) for k_ in ('esc_menu', 'esc_entry')})
    for x in (HOST, GUEST):
        open_esc(x); pv(x, action='press_esc'); time.sleep(2)
    check('setup: the PvP window is open on both copies', pv(HOST).get('window') == 1 and pv(GUEST).get('window') == 1)
    safe('P1', p1_rules); touch_lease()
    safe('M5 PvP window', lambda: m5_pvp(sw, sh)); touch_lease()
    safe('CLOSE/MOVE PvP window', lambda: pvp_window_rules(sw, sh)); touch_lease()
    safe('CLOSE/CARD Bring list', bring_list_rules); touch_lease()
    # the match: best of 1, 1:00, planning pause, here; the guest on Red
    q = pv(HOST); log('match rules', {k: q.get(k) for k in ('mode', 'bo', 'secs', 'pause')}, teams(HOST))
    log('Start Match', pv(HOST, action='press_start'))
    try:
        wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (HOST, GUEST)), 300, 1)
    except Exception as e:
        check('the match starts (for P2 and the result window)', False, repr(e)); return
    time.sleep(6)
    try:
        log('propose', c(GUEST, cmd='pvp_vote', op='propose'))
        wait('vote open', lambda: c(HOST, cmd='pvp_status').get('vote_open'), 60, 0.5); time.sleep(0.8)
        log('agree', c(HOST, cmd='pvp_vote', op='agree'))
        wait('fight begins', lambda: (lambda s: s if s.get('fight_start', 0) > 0 and not s.get('placing') else None)(c(HOST, cmd='pvp_status')), 120, 0.5)
    except Exception as e:
        log('vote', repr(e)); log('Fight button fallback', pv(HOST, action='press_fight'))
    try:
        s0 = c(HOST, cmd='pvp_status'); cl = {u['guid']: u for u in c(HOST, cmd='list_units')['units']}
        al = [u for u in s0.get('units', []) if 'cell' in cl.get(u['guid'], {})]
        for u in al:
            en = [v for v in al if v['team'] != u['team']]
            if en:
                tg = en[0]; tc = cl[tg['guid']]['cell']
                c(HOST if u['player'] == 0 else GUEST, cmd='ord_run_attack', unit=u['guid'], target=tg['guid'], x=tc[0], y=tc[1], mp=1)
    except Exception as e:
        log('orders', repr(e))
    other = 1.0 if A.scale > 1.2 else 1.4
    def planning():                                                       # at the start scale: the game's HUD does not re-lay out at a
        wait('planning pause', lambda: c(HOST, cmd='pvp_status').get('planning'), 60, 0.2); time.sleep(0.4)   # scale set while running
        p2_measure(f'(UI {A.scale:.1f})')
        shot(HOST, f'P15_planning_ui{A.scale:.1f}', 0.0)
    safe('P2', planning)
    touch_lease()
    shown = False
    try:
        wait('round over', lambda: (lambda q: q if q.get('ending') or not q.get('active') or q.get('resetting') else None)(c(HOST, cmd='pvp_status')), 240, 0.5)
        wait('result window on both', lambda: all(pv(x).get('result') for x in (HOST, GUEST)), 300, 1)
        time.sleep(1.5); shown = True
    except Exception as e:
        check('the match ends and the result window shows', False, repr(e)); log(traceback.format_exc())
    def result_m5():
        m5_window('result window', 'result', other, sw, sh)
        if probe(HOST).get('ui_scale', 1) < 1.2: set_scale(HOST, 1.4)
        shot(HOST, 'P17_result_ui1.4', 0.5)
        p = probe(HOST)
        if rect(p, 'result_ok'):
            click(HOST, p['result_ok'], 'Back to the World')
            check('M5 a mouse click on the scaled result window works (Back to the World closes it)', not pv(HOST).get('result'), p['result_ok'])
        else: check('M5 a mouse click on the scaled result window works (Back to the World closes it)', False, 'no button rect')
        set_scale(HOST, A.scale)
    def result_close():                                                   # CLOSE on the guest's result window
        w = red_x(GUEST, 'PvP result window'); pg = probe(GUEST)
        check('CLOSE the result window shows the red X at the top-right of its title band (guest)', x_on_band(w, rect(pg, 'result'), 'result'), f'{w} window {pg.get("result")}')
        shot(GUEST, 'X4_result_red_x', 0.3)
        if w and w.get('x') and w.get('x_rect'):
            click(GUEST, w['x_rect'], 'red X of the result window')
            check('CLOSE a mouse click on the red X closes the result window (guest)', not pv(GUEST).get('result'), w['x_rect'])
        else: check('CLOSE a mouse click on the red X closes the result window (guest)', False, f'no red X: {w}')
    if shown:
        safe('M5 result window', result_m5)
        safe('CLOSE result window', result_close)
    for x in (HOST, GUEST):
        if pv(x).get('result'): pv(x, action='press_ok'); time.sleep(2)
    touch_lease()
    safe('M2', m2_refused)
    log('desyncs', {x: st(x).get('mp', {}).get('desyncs') for x in (HOST, GUEST)})


def main():
    for x in (HOST, GUEST):
        if not os.path.isdir(os.path.join(idir(x), 'DungeonSettlers.app')): raise SystemExit(f'no instance {x}: run ext/dsuxmp/make_instance_pvp.sh {x}')
        if subprocess.run(['pgrep', '-f', os.path.join(idir(x), r.EXE_REL)], capture_output=True).returncode == 0: raise SystemExit(f'copy {x} already running')
    owner = subprocess.run(['pgrep', '-f', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers'], capture_output=True, text=True).stdout.split()
    log('owner game pids (never touched):', owner, '| load', os.getloadavg(), '| dylib', A.dylib, hashlib.sha256(open(A.dylib, 'rb').read()).hexdigest()[:12])
    sl = subprocess.run(['zsh', SLOTSH, 'take', LANE, '2', '1800'], capture_output=True, text=True)   # waits for load < 15 and 2 free slots
    log('slots:', sl.stdout.strip(), sl.stderr.strip()[:200])
    if sl.returncode != 0: raise SystemExit('no 2 free copy slots (slot.sh): not started')
    pair = None; g = threading.Thread(target=guard, daemon=True)
    try:
        pair = realrig.lease(LANE, os.getpid())
        if not pair: raise RuntimeError('no free pair lease now')
        log('lease', pair)
        with socket.socket() as s_:
            s_.bind(('127.0.0.1', 0)); port = s_.getsockname()[1]
        for x, nm in ((HOST, 'Alex'), (GUEST, 'Sam')):
            install(x); seed(x, nm, port)
        g.start()
        for x in (HOST, GUEST):
            if os.getloadavg()[0] >= 15: raise RuntimeError(f'load {os.getloadavg()[0]:.1f} >= 15 before starting {x}')
            start(x); time.sleep(2)
        run(port)
    except Exception as e:
        check('run', False, repr(e)); log(traceback.format_exc())
    finally:
        stop_guard.set()
        check('the copies never showed a window or came to the front', not violations, violations)
        for x, p in MYPIDS.items():
            try: r.send(x, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
        time.sleep(5)
        for x, p in MYPIDS.items():
            if alive(p): os.kill(p, signal.SIGKILL); log('killed own copy', x, p)
        for x in (HOST, GUEST):
            for f in ('dsload.log', 'rig.log', 'player.log'):
                q = os.path.join(idir(x), f)
                if os.path.exists(q): shutil.copy2(q, os.path.join(RUN, f'{x}_{f}'))
        if pair:
            try: log('released', realrig.release(pair, LANE))
            except Exception as e: log('release', repr(e))
        log('slots released:', subprocess.run(['zsh', SLOTSH, 'release', LANE], capture_output=True, text=True).stdout.strip())
        n = sum(1 for _, ok in RESULTS if ok)
        log(f'RESULT {"PASS" if RESULTS and n == len(RESULTS) else "FAIL"} {n}/{len(RESULTS)}', TAG, RUN)


if __name__ == '__main__':
    main()
