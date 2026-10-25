#!/usr/bin/env python3
"""dsmphide lane: the game's Load list never shows the multiplayer mod's own worlds (owner 2026-10-25, Windows VM: rows
"Foreign Expedition Hub" and "PvP Base"). Multiplayer before 0.1.26 left MPHub_ / MPPvP_ files and the game's autosaves of
those worlds in the players' Saves folders; the game listed them as real worlds. The fix: a loader Load-list filter of the
multiplayer module (src/mu_ui.c ws_show, mp_worldsave.h mp_world_save_name). Hidden only: every file stays.

ONE hidden loader test copy (DSL_TEST_COPY, default MPH: an APFS clone of a ready make_instance.sh copy with its own bundle
id, as hostmods_pair.make_copy), background-only, small window, muted, notch fix off; one rig lease while it runs. The
loader's l1 helpers (LD_ROOT/loader/test: l1_saves_test install / start_modded, the dsl_rows.c lf_state request, as
l1_filter_test.py). Mods from DSL_TEST_MODS: the loader's build.sh with MP_PIN=<the multiplayer commit under test>.
Seeded in the copy's modded Saves folder (the wiki's public base save, header changed):
  Template__AutoSave_491b..._0                          a normal world: must show
  MPHub_fe5eb1a2                                        the hub temp world of multiplayer before 0.1.26: must not show
  ForeignExpeditionHub__AutoSave_fe5ea000..._0          the game's autosave of a running hub: must not show
  PvPBase__AutoSave_b45e0..._0                          the game's autosave of the PvP world: must not show
With them the loader's test module rows-demo (never shipped): a filter WITH a label ("Show demo worlds", off at each
opening = every world hidden). The Load Campaign list opens with every filter box on (Vanilla, Modded, Solo, Multiplayer):
  toggle off: rows-demo hides Template, the hidden-worlds line counts that 1 world (the player's own filter keeps its line);
  toggle on:  Template shows, the three mod worlds do not, and no hidden-worlds line names them (no box can show them).
Multiplayer before this fix shows the three mod worlds (toggle on). A loader before ds_modloader 623130c counts them as
hidden "by a filter under the list" (4 with the toggle off, a false "3 worlds" line with it on): the line checks fail there.
Usage: LD_ROOT=<ds_modloader checkout with loader/build/dsload.dylib> DSL_TEST_MODS=<built mods> python3 hub_hide_test.py [out]
       ROWS_DEMO_DIR = built rows-demo (default LD_ROOT/loader/build/test_mods/rows-demo, the gate's rowsdemo())
Output: RESULT line, <out>/summary.json, <out>/dsload.log, <out>/lf_state.json. Exit 0 = PASS."""
import json, os, shutil, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
LD = os.path.abspath(os.path.expanduser(os.environ.get('LD_ROOT', '~/ds_launcher')))
os.environ.setdefault('DSL_TEST_COPY', 'MPH')
os.environ.setdefault('DSL_TEST_MODS', os.path.join(LD, 'loader', 'build', 'mods'))
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out', 'hubhide_' + time.strftime('%Y%m%d-%H%M%S'))))
sys.argv[1:] = [OUT]
sys.path.insert(0, os.path.join(LD, 'loader', 'test'))
import l1_saves_test as S  # noqa: E402  (copy X, install, start_modded, stop_all; owner save / prefs hashes)
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig as realrig  # noqa: E402  (leases only; its copies are never touched)
T, r = S.T, S.r
check, log = T.check, T.log
X, I, MSAVES, GSAVES = S.X, S.I, S.MSAVES, S.GSAVES
LANE = 'dsmphide'
TPL_SRC = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public (the wiki's base save)
TPL_GUID = '491bc260-98aa-4cd1-b63f-82da78369aaf'                    # its own world
ROWS_DEMO = os.environ.get('ROWS_DEMO_DIR') or os.path.join(LD, 'loader', 'build', 'test_mods', 'rows-demo')


def name_of(clan, guid, n=0):
    return f'{clan}__AutoSave_{guid.replace("-", "")}_{n}'


TEMPLATE = (name_of('Template', TPL_GUID), TPL_GUID, 'Template', True)
OURS = [('MPHub_fe5eb1a2', 'fe5eb1a2-0000-4000-8000-00000000a001', 'Foreign Expedition Hub', False),
        (name_of('ForeignExpeditionHub', 'fe5ea000-0000-4000-8000-00000000a002'), 'fe5ea000-0000-4000-8000-00000000a002', 'Foreign Expedition Hub', True),
        (name_of('PvPBase', 'b45e0000-0000-4000-8000-00000000b001'), 'b45e0000-0000-4000-8000-00000000b001', 'PvP Base', True)]
STATE = os.path.join(T.SDIR, 'lf_state.json')


def owner_playing():
    """the owner's game (Steam install, the modded app, the launcher's copy): never start a test copy next to it"""
    pats = ('steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/MacOS/', 'Dungeon Settlers Modded.app/Contents/MacOS/',
            'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers')
    return [p for p in pats if subprocess.run(['pgrep', '-f', p], capture_output=True).returncode == 0]


def make_copy():
    """an APFS clone of a ready hidden test copy (make_instance.sh output) with its own bundle id (hostmods_pair.make_copy)"""
    app = os.path.join(I, 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.environ.get('HM_SRC_APP', os.path.join(r.INST, 'Z', 'DungeonSettlers.app'))
        if not os.path.exists(os.path.join(src, r.EXE_REL.split('/', 1)[1])):
            raise SystemExit(f'no ready test copy to clone at {src} (make_instance.sh first)')
        os.makedirs(I, exist_ok=True)
        subprocess.run(['cp', '-c', '-R', src, app], check=True)
        pl = os.path.join(app, 'Contents', 'Info.plist')
        for k, t, v in (('CFBundleIdentifier', '-string', f'com.CanOpener.Dungeon-Settlers.mp{X}'), ('CFBundleName', '-string', f'DS rig {X}'),
                        ('LSBackgroundOnly', '-bool', 'YES'), ('LSUIElement', '-bool', 'YES')):
            subprocess.run(['plutil', '-replace', k, t, v, pl], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
        log(X, 'copy made from', src)
    r.require_background_only(X)
    for d in ('home', 'data/Saves', 'data/Mods', 'softrestart'):
        os.makedirs(os.path.join(I, d), exist_ok=True)


def seed():
    """the four worlds in the modded Saves folder, nothing else (earlier files to <copy>/old/<time>)"""
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (MSAVES, S.GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(T.MODS, 'save_copies'),
              os.path.join(I, 'mp_tags.txt'), os.path.join(I, 'mp.json'), os.path.join(T.MODS, 'loadlist.json')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    for k, (name, guid, clan, auto) in enumerate([TEMPLATE] + OURS):
        t = json.load(open(TPL_SRC)); h = t['CampaignSaveHeader']
        h.update(CampaignGuid=guid, ClanName=clan, SaveName=name, DisplayName=clan, SaveTime=f'2026-10-2{k} 08:00:00', IsAutoSave=auto)
        json.dump(t, open(os.path.join(MSAVES, name + '.json'), 'w'))


def rows_of(st):
    return {w['guid'].lower(): w for w in st.get('worlds', [])}


def lf_state():
    """the open Load list as the loader sees it (dsl_rows.c test request lf_state)"""
    if os.path.exists(STATE): os.replace(STATE, STATE + '.old')
    T.request({'cmd': 'lf_state'}); t0 = time.time()
    while time.time() - t0 < 10 and not os.path.exists(STATE): time.sleep(0.2)
    try: return json.load(open(STATE))
    except Exception as e: return {'error': repr(e)}


def run():
    S.install()
    seed()
    cr = os.path.join(I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')   # a fresh copy's consent box: No
    d = os.path.join(I, 'DungeonSettlers.app', 'Contents', 'Resources', 'mods', 'rows-demo'); os.makedirs(d, exist_ok=True)
    for f in ('rows-demo.dylib', 'mod.json'): shutil.copy2(os.path.join(ROWS_DEMO, f), os.path.join(d, f))
    r.cap_wait(f'{X}: start')                                        # the machine-wide test copy cap (copies, load, swap)
    S.start_modded(('multiplayer', 'rows-demo'), worlds={}, extra_env={'DSL_UPDATE': '0', 'DSL_CRASHREP_TEST': cr})
    T.request({'cmd': 'warn_cancel'}); time.sleep(1)
    mark = len(T.dlog())
    r.send(X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
    check('setup: the Load list opened (rows: Load list rebuilt)', S.wait_line('rows: Load list rebuilt', mark, 30))
    time.sleep(2.5); lf_state(); time.sleep(2)                       # the multiplayer list filter, then the loader's poll
    a = lf_state(); ra = rows_of(a)                                  # rows-demo's toggle is off at each opening
    log('toggle off: rows', [(w['guid'][:8], w['shown']) for w in a.get('worlds', [])], '| line', a.get('hidden_line'), a.get('hid_lines'))
    seen = [n for n, g, _, _ in [TEMPLATE] + OURS if g in ra]
    check('setup: the game listed all four seeded worlds (a row each, shown or not)', len(seen) == 4, (seen, list(ra)))
    end = (a.get('hid_lines') or [{}, {}])[1]
    check('toggle off: the filter WITH a label (rows-demo) hides Template and its line counts that 1 world, not the mod\'s own',
          ra.get(TPL_GUID, {}).get('shown') == 0 and end.get('shown') and a.get('hidden_line') == 1, (a.get('hidden_line'), end))
    T.request({'cmd': 'toggles', 'on': True}); t0 = time.time()
    while os.path.exists(os.path.join(T.SDIR, 'reg_request.json')) and time.time() - t0 < 5: time.sleep(0.1)   # one request file: read first
    b = lf_state()
    while rows_of(b).get(TPL_GUID, {}).get('shown') != 1 and time.time() - t0 < 10: time.sleep(0.5); b = lf_state()
    time.sleep(1); b = lf_state(); rb = rows_of(b)
    log('toggle on: rows', [(w['guid'][:8], w['shown']) for w in b.get('worlds', [])], '| line', b.get('hidden_line'), b.get('hid_lines'))
    json.dump({'toggle_off': a, 'toggle_on': b}, open(os.path.join(OUT, 'lf_state.json'), 'w'), indent=1)
    check('toggle on: Template (a normal world) shows', rb.get(TPL_GUID, {}).get('shown') == 1, rb.get(TPL_GUID))
    for name, guid, _, _ in OURS:
        check(f'toggle on: {name} (the mod\'s own world) does not show', guid in rb and rb[guid].get('shown') == 0, rb.get(guid))
    end = (b.get('hid_lines') or [{}, {}])[1]                       # no box can show them: a line about them is false
    check('toggle on: no hidden-worlds line for the mod\'s own worlds ("hidden by a filter under the list")', not end.get('shown'), end)
    S.stop_all()


def main():
    if T.game_pids(): raise SystemExit(f'copy {X} already running')
    who = owner_playing()
    if who: raise SystemExit(f'the owner\'s game runs ({who}): no test copy now')
    os.makedirs(OUT, exist_ok=True)
    pair = realrig.lease(LANE, os.getpid())
    if not pair: raise SystemExit('no free rig lease (rig/leases): try again later')
    log('lease', pair, '| copy', X, '| loader', S.LBUILD, '| mods', S.MODSRC)
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    make_copy()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    try:
        run()
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        S.stop_all()
        realrig.release(pair, LANE)
        if os.path.exists(os.path.join(I, 'dsload.log')): shutil.copy2(os.path.join(I, 'dsload.log'), os.path.join(OUT, 'dsload.log'))
        check("owner's real save folder unchanged (hash before/after)", S.tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        n = sum(not c['ok'] for c in T.checks)
        print(f"RESULT {'PASS' if T.checks and not n else 'FAIL'} {len(T.checks) - n} PASS, {n} FAIL  (out {OUT})", flush=True)


if __name__ == '__main__':
    main()
    sys.exit(1 if not T.checks or not all(c['ok'] for c in T.checks) or T.violations else 0)
