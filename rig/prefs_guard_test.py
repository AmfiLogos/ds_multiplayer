#!/usr/bin/env python3
"""prefs_guard_test.py [rig.py | lrig.py]: the owner-settings guard (check_prefs) on fake owner files in a temp folder
(DSRIG_PREFS_TEST_ROOT; his game open / closed = DSRIG_PREFS_FAKE_GAME). Never reads or writes his real settings.
Breaks it catches (2026-10-24: the old guard put a fixed 10-11 backup back over his newer settings at every test):
  1. a test's change to his settings stays after the run, or the file it replaced is lost
  2. his own change, made in his game before the run, is put back to older values
  3. his change made while the run was going (game closed again before its end) is put back"""
import importlib.util, json, os, plistlib, subprocess, sys, tempfile, time

T = tempfile.mkdtemp(prefix='prefs_guard_')
os.environ.update(DSRIG_PREFS_TEST_ROOT=T, DSRIG_PREFS_FAKE_GAME='0')
path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rig.py'))
spec = importlib.util.spec_from_file_location('guarded_rig', path); r = importlib.util.module_from_spec(spec); spec.loader.exec_module(r)
res = []


def check(name, ok, info=''):
    res.append(bool(ok)); print('PASS' if ok else 'FAIL', name, f'[{str(info)[:200]}]' if not ok else '')


# never let a guard without the test mode near his real files (the old one restores them from the old backup)
inside = getattr(r, 'PREFS_TEST', None) == T and all(d.startswith(T) for d in [r.OWNER_DATA, r.PREFS_SNAP, r.PREFS_KEPT] + r.OWNER_DOMAINS)
check('guard runs on the temp folder only', inside, path)
if not inside:
    sys.exit(1)
STOCK, MOD = r.OWNER_DOMAINS; US = os.path.join(r.OWNER_DATA, 'UserSetting.json'); os.makedirs(r.OWNER_DATA)


def dw(dom, k, v): subprocess.run(['defaults', 'write', dom, k, '-int', str(v)], check=True)
def dr(dom, k): return r._read_domain(dom).get(k)
def us(): return json.load(open(US))['SoundSettingData']['MasterPercent']
def us_set(v): json.dump({'SoundSettingData': {'MasterPercent': v}, 'GraphicSettingData': {'VSyncEnabled': True}}, open(US, 'w'))
def his_game(v): os.environ['DSRIG_PREFS_FAKE_GAME'] = v
def his_play():                                            # his game writes these while it runs
    for n in ('ErrorLog.txt', 'eventcache'):
        open(os.path.join(r.OWNER_DATA, n), 'a').write('x\n')
def new_run(): r._SNAP.clear(); time.sleep(0.05)


dw(STOCK, 'Screenmanager Resolution Width', 3456); dw(MOD, 'Screenmanager Fullscreen mode', 1); us_set(0.5); his_play()
time.sleep(0.05)

# 1. a test changes his settings (all three files, + a new key): put back at the end, the replaced files kept
new_run(); check('run 1 start: snapshot, True', r.check_prefs() and r._SNAP.get('dir'))
dw(STOCK, 'Screenmanager Resolution Width', 960); dw(STOCK, 'TestOnlyKey', 7); dw(MOD, 'Screenmanager Fullscreen mode', 3); us_set(0.0)
check('run 1 end: the test change is reported (False)', r.check_prefs() is False)
check('run 1: stock domain put back (3456, no test key)', dr(STOCK, 'Screenmanager Resolution Width') == 3456 and dr(STOCK, 'TestOnlyKey') is None,
      r._read_domain(STOCK))
check('run 1: .Modded domain put back', dr(MOD, 'Screenmanager Fullscreen mode') == 1, r._read_domain(MOD))
check('run 1: UserSetting.json put back', us() == 0.5, us())
kept = sorted(os.listdir(r.PREFS_KEPT)) if os.path.isdir(r.PREFS_KEPT) else []
ku = [k for k in kept if k.endswith('-UserSetting.json')]; ks = [k for k in kept if k.endswith('-com.CanOpener.Dungeon-Settlers.plist')]
check('run 1: replaced files kept with the test values', ku and ks and json.load(open(os.path.join(r.PREFS_KEPT, ku[0])))['SoundSettingData']['MasterPercent'] == 0.0
      and plistlib.load(open(os.path.join(r.PREFS_KEPT, ks[0]), 'rb')).get('Screenmanager Resolution Width') == 960, kept)

# 2. he plays between runs and changes settings: the next run keeps them
his_game('1'); dw(STOCK, 'Screenmanager Resolution Width', 1728); us_set(0.8); his_play(); his_game('0')
new_run(); check('run 2 start: True', r.check_prefs())
check('run 2 end: no difference (True)', r.check_prefs())
check('run 2: his change survives', dr(STOCK, 'Screenmanager Resolution Width') == 1728 and us() == 0.8, (dr(STOCK, 'Screenmanager Resolution Width'), us()))

# 3. he opens his game during a run, changes a setting, closes it before the run ends: reported, never put back
new_run(); r.check_prefs(); time.sleep(0.05)
his_game('1'); us_set(0.3); dw(MOD, 'Screenmanager Fullscreen mode', 2); his_play(); his_game('0')
check('run 3 end: difference reported (False)', r.check_prefs() is False)
check('run 3: his change during the run survives', us() == 0.3 and dr(MOD, 'Screenmanager Fullscreen mode') == 2, (us(), dr(MOD, 'Screenmanager Fullscreen mode')))

print(f'RESULT {sum(res)}/{len(res)} pass  (temp {T})')
sys.exit(0 if all(res) else 1)
