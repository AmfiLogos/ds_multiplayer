#!/usr/bin/env python3
"""gate13 v13 gate (PLAN_V13 G4, My Units + Bring Units), WINDOWS: the loader kit (~/ds_launcher/loader/build/win/kit, used as
it is) in the CrossOver bottle DSMP (win/wloader.py: -batchmode -nographics, null graphics, no audio), ONE wine copy, one
rig pair lease + rig/wine.lock. TEST My Units library (env DSMP_UNITS_LIB = a Windows path to a copy in the run folder).

  Windows copy as host: Multiplayer > PvP > Start -> PvP lobby -> lobby "Bring Units" -> the new window opens (check 2) ->
  close -> title -> Multiplayer > Foreign Expedition (Death Off) > Start -> hub lobby -> "Bring Units" opens (check 1).
  Checks 3 / 4 (a My Units unit ticked in the window, then a dive) need a click on a card's tick: the only driver that can
  click the Bring Units window is the Mac-only test module units-driver (ext/units13/driver: NSWindow events, mac-arm64
  entry only), and no rig command picks a My Units unit (mu_* commands: open / state / result / close). So on Windows
  they are reported as impossible with the existing test hooks, not run.
  Check 5: test library byte-identical; 6: the owner's Mac My Units library + prefs unchanged (the DSMP bottle is test-only).

  python3 ~/ds_mp_gate13/rig/gate13/mu_dive_win.py [--kit DIR] [--no-install]
Prints "PASS name ..." / "FAIL name ..." per check, last line "RESULT x/y pass". Out: ext/gate13/out/mudive/win_<time>/."""
import json, os, re, shutil, signal, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mu_lcopy as M
H = os.path.expanduser('~')
sys.path.insert(0, f'{H}/ds_multiplayer/win'); sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import wloader as w
import lrig

A = sys.argv
KIT = os.path.abspath(os.path.expanduser(A[A.index('--kit') + 1])) if '--kit' in A else os.path.expanduser('~/ds_launcher/loader/build/win/kit')
WT = M.WT; LANE = 'gate13_mu'
OUT = os.path.join(WT, 'ext', 'gate13', 'out', 'mudive', 'win_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
CM = os.environ.get('CM_REPO') or os.path.expanduser('~/ds_custommodes')   # Custom Modes' own repo (post-v13)
LIBFIX = os.path.join(CM, 'units/test/data/launcher_library.json')
BTN = json.loads(os.popen(f'python3 {WT}/ext/fepvp13/tools/mpm_ids.py {WT}').read())
WL = f'{H}/ds_multiplayer/rig/wine_lease.py'
LOGF = open(os.path.join(OUT, 'test.log'), 'a'); results = []
IMPOSSIBLE = ('impossible with the existing test hooks on Windows: ticking a card in Bring Units needs the units-driver fake mouse, '
              'which is Mac only (ext/units13/driver/mod.json: platforms mac-arm64; NSWindow events), and no rig command picks a '
              'My Units unit (custom/units/mu_init.c mu_rig_exec: mu_bring_open / state / result / close only)')


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOGF.write(s + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok)))
    s = ('PASS ' if ok else 'FAIL ') + name + (f'  [{str(info)[:500]}]' if info != '' else '')
    print(s, flush=True); LOGF.write(time.strftime('%H:%M:%S ') + s + '\n'); LOGF.flush()


def kit_facts():
    cm = os.path.join(KIT, 'files', 'mods', 'custom-modes')
    dlls = [f for f in os.listdir(cm) if f.endswith('.dll')] if os.path.isdir(cm) else []
    has_bring = any(b'Bring Units' in open(os.path.join(cm, f), 'rb').read() for f in dlls)
    mods = sorted(os.listdir(os.path.join(KIT, 'files', 'mods'))) if os.path.isdir(os.path.join(KIT, 'files', 'mods')) else []
    t = time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(os.path.join(KIT, 'files'))))
    return dict(mods=mods, cm_dlls=dlls, bring=has_bring, built=t)


def wait(what, pred, timeout=600, every=2):
    t1 = time.time()
    while time.time() - t1 < timeout:
        try:
            v = pred()
        except Exception as e:
            v = None; log('  (wait', what, repr(e)[:80], ')')
        if v: return v
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    owner0 = M.fstate(M.OWNER_LIB); prefs0 = lrig.check_prefs(restore=False)
    lib = os.path.join(OUT, 'lib', 'library.json'); os.makedirs(os.path.dirname(lib)); shutil.copyfile(LIBFIX, lib); lib0 = M.fstate(lib)
    kf = kit_facts(); log('kit', KIT, kf)
    pair = None; have_lock = False; started = False
    try:
        if not kf['cm_dlls'] or not kf['bring']:
            why = f"the kit (files built {kf['built']}) has mods {kf['mods']}: no custom-modes DLL with Bring Units, so no Bring Units window and no Foreign Expedition / PvP on Windows"
            check('2 Bring Units window opens in the PvP lobby (Windows)', False, why)
            check('1 Bring Units window opens in the hub lobby (Windows)', False, why)
        else:
            pair = M.lease(LANE, os.getpid()); log('lease', pair)
            have_lock = subprocess.run([sys.executable, WL, 'take', 'gate13_mu', '--pid', str(os.getpid()), '--wait', '3600']).returncode == 0
            if not have_lock: raise RuntimeError('no wine lock in 1 h')
            if '--no-install' not in A: log('kit install rc', w.install(KIT))
            md = os.path.join(w.PROF, 'data', 'Mods'); os.makedirs(md, exist_ok=True)
            en = ['multiplayer', 'custom-modes']; json.dump({'enabled': en, 'order': en}, open(os.path.join(md, 'active.json'), 'w'))
            cache = os.path.join(w.PROF, 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
            shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
            for f in ('dsload.log', 'rig.log', 'player.log'):
                p = os.path.join(w.PROF, f)
                if os.path.exists(p): os.replace(p, os.path.join(OUT, 'prev_' + f))
            os.environ.update(DSMP_UNITS_LIB=w.winpath(lib), DSMP_CLIP_PB='dsmp-gate13-mu-test', DSMP_TEST='1')
            started = True
            if w.start([]): raise RuntimeError('wloader: the DSMP bottle stayed busy')
            run()
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); check('test ran to the end without an error', False, repr(e))
    finally:
        if started:
            try: w.stop()
            except Exception as e: log('stop:', e)
            for f in ('dsload.log', 'rig.log', 'starter.log', 'player.log'):
                p = os.path.join(w.PROF, f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, 'win_' + f))
            log('left:', w.dsmp_pids())
        if have_lock:
            subprocess.run([sys.executable, WL, 'release', 'gate13_mu'])
        if pair: log('released', M.release(pair, LANE))
        check('3 a My Units unit + a save unit go into a hub dive (Death Off) and it plays (Windows)', False, IMPOSSIBLE)
        check("4 after the dive the save unit's result applied, the My Units unit's not (Windows)", False, IMPOSSIBLE)
        check('5 test My Units library file byte-identical before / after', M.fstate(lib) == lib0, f'{lib0} -> {M.fstate(lib)}; copy started: {started}')
        o1 = M.fstate(M.OWNER_LIB); p1 = lrig.check_prefs(restore=False)
        check("6 owner's real My Units library + prefs unchanged", o1 == owner0 and p1 == prefs0 and p1, f'library {owner0} -> {o1}; prefs {prefs0} -> {p1}')
        n = sum(1 for _, ok in results if ok)
        print(f'RESULT {n}/{len(results)} pass', flush=True); LOGF.write(f'RESULT {n}/{len(results)} pass | out {OUT}\n')


def run():
    c = lambda cmd, **k: w.cmd(dict(k, cmd=cmd), timeout=120)
    st = lambda: c('status')
    press = lambda n: (c('mpm_press', button=BTN[n]), time.sleep(1.5))[0]
    wait('Windows title', lambda: (lambda s: s.get('boot_ready') and s.get('scene') == 1 and not s.get('busy'))(st()), timeout=600, every=5)
    time.sleep(8)

    def bring(tag):
        wait(f'{tag} lobby', lambda: c('mpm_status').get('state') in ('lobby', 'guest_lobby'), timeout=180, every=2)
        via = None
        for _ in range(8):
            via = c('mpm_press', button=BTN['FE_BRINGWIN']); time.sleep(3)
            if c('mu_bring_state').get('open') == 1: break
        s = c('mu_bring_state'); log(tag, 'press', via, '| state', s)
        c('mu_bring_close'); time.sleep(1)
        return s
    press('MULTIPLAYER'); press('HOST_C1'); press('HOST_START')
    wait('PvP world + lobby', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('det', {}).get('locked'))(st())
         and c('mpm_status').get('state') == 'lobby', timeout=900, every=5)
    s = bring('pvp_lobby')
    check('2 Bring Units window opens in the PvP lobby (Windows, lobby button)', s.get('open') == 1, s)
    c('mp_quit', mode='title')
    wait('title again', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st()), timeout=300, every=5)
    time.sleep(6)
    press('MULTIPLAYER'); press('HOST_FE'); press('SETTINGS')
    for k, b in (('fe_death', 'FED_NEXT'),):
        if c('mpm_status')['settings'].get(k) != 0: press(b)
    log('FE rules', c('mpm_status').get('settings'))
    press('FE_DONE'); press('HOST_START')
    wait('hub + lobby', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st())
         and c('mpm_status').get('state') == 'lobby', timeout=900, every=5)
    s = bring('hub_lobby')
    check('1 Bring Units window opens in the hub lobby (Windows, lobby button)', s.get('open') == 1 and s.get('tab') in (1, 2), s)


if __name__ == '__main__':
    main()
