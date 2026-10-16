#!/usr/bin/env python3
"""Bring Units window (custom/units/mu_bring.c) on a hidden test copy, WITH multiplayer (MP moves persistentDataPath to
<instance>/data, so the copy's saves are <instance>/data/Saves). Two saves are COPIED (never moved) from the owner's
backup into the copy; a test My Units library (fixture, 30 units, 3 teams) goes to the run's out/lib.

Proves: opens from mu_test_bring_open with both tabs; From a save lists the saves and shows cards; tick 2 save units;
My Units tab, tick 1 unit; the cap (max 3) refuses a 4th; ticks survive tab and page changes; Bring returns 3 picks;
reopen + Cancel returns -1; a team ticks its units up to the cap; mu_test_overflow == 0 on every
page shown. Captures in <out>/drv/cap.

  bring_test.py [out] [--no-build] [--scale=0.75]      (CMRIG_INST = the copy letter, default U)
"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmrig as C

args = [a for a in sys.argv[1:] if not a.startswith('--')]
SCALE = next((float(a.split('=')[1]) for a in sys.argv if a.startswith('--scale=')), None)
tag = f'_s{SCALE}' if SCALE else ''
OUT = os.path.abspath(os.path.expanduser(args[0] if args else f'~/ds_mp_units13/ext/units13/out/bring{tag}_{time.strftime("%Y%m%d-%H%M%S")}'))
os.makedirs(OUT, exist_ok=True)
BACKUP = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves')
SAVES = ['MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json',
         'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json']
FIXTURE = os.path.join(C.WT, 'custom/units/test/fixtures/launcher_library.json')
MODS = ['multiplayer', 'custom-modes', 'units-driver']
ck = C.Checks(OUT)


class Copy(C.Copy):
    """cmrig's ui_scale writes GeneralSettingData *scale* keys (CameraMovementSpeedScale); the game's UI scale is
    GraphicSettingData.UIScale, so this test sets that one itself (after cmrig's own settings)."""
    def settings(self):
        dirs = super().settings()
        if SCALE is not None:
            for d in dirs:
                p = os.path.join(d, 'UserSetting.json'); s = json.load(open(p))
                s.setdefault('GraphicSettingData', {})['UIScale'] = SCALE
                json.dump(s, open(p, 'w'), indent=1)
                C.log('GraphicSettingData.UIScale', SCALE, 'in', p)
        return dirs


def prepare():
    sd = os.path.join(C.I, 'data', 'Saves'); os.makedirs(sd, exist_ok=True)
    for f in SAVES:
        if not os.path.exists(os.path.join(sd, f)):
            shutil.copyfile(os.path.join(BACKUP, f), os.path.join(sd, f))          # copy, never move
    shutil.copyfile(FIXTURE, os.path.join(C.out_lib_dir(OUT), 'library.json'))
    return sd


def bst(c):
    return c.state().get('cm', {}).get('bring', {})


def overflow(c, name):
    rc, out = c.call('mu_test_overflow')
    ck(f'{name}: no text or icon outside its card', rc == 0, out[-200:])


def picks(res):
    n, _, rest = res.partition(':')
    return int(n), [p.split('|') for p in rest.split(';') if p]


def main():
    if '--no-build' not in sys.argv:
        C.sh(f'cd "{C.WT}" && OUT="{C.B}" nice -n 19 zsh custom/build_module.sh')
        C.build_driver()
    C.install()
    sd = prepare()
    with Copy(MODS, OUT, ui_scale=None) as c:
        c.wait_title()
        rc, out = c.call('mu_test_bring_open', '3,3')
        time.sleep(1.0); st = bst(c); size1 = (st.get('w'), st.get('h'))
        ck('opens from mu_test_bring_open', rc == 0 and st.get('open') == 1, st)
        ck('both tabs shown', c.req('rect', target='bring:tab_save')['rc'] == 1 and c.req('rect', target='bring:tab_lib')['rc'] == 1)
        ck('first tab = From a save', st.get('tab') == 1, st.get('tab'))
        ck('From a save lists the 2 saves', st.get('left_rows') == 2 and st.get('saves') == 2, st)
        ck('save cards shown', st.get('cards', 0) >= 1 and c.req('rect', target='bring:card0')['rc'] == 1, st)
        ck('New unit only with the builder', (c.req('rect', target='bring:new')['rc'] == 1) == bool(st.get('builder')), st.get('builder'))
        c.cap('01_from_save'); overflow(c, 'save page 1')
        # the other save
        c.click('bring:left1', wait=1.5); st = bst(c)
        ck('picking the other save shows its units', st.get('sel_save') == 1 and st.get('cards', 0) >= 1, st)
        c.cap('02_other_save'); overflow(c, 'other save page 1')
        c.click('bring:left0', wait=1.5)
        # tick 2 save units
        c.click('bring:tick0', wait=0.6); c.click('bring:tick1', wait=0.6); st = bst(c)
        ck('ticked 2 save units', st.get('picked') == 2, st)
        c.cap('03_two_ticked')
        if st.get('pages', 1) > 1:
            c.click('bring:next', wait=1.2); st = bst(c)
            ck('save page 2 shows', st.get('page') == 1, st); c.cap('04_save_page2'); overflow(c, 'save page 2')
            c.click('bring:prev', wait=1.2)
        # My Units
        c.click('bring:tab_lib', wait=1.5); st = bst(c)
        ck('My Units tab: All units + 3 teams', st.get('tab') == 2 and st.get('left_rows') == 4 and st.get('units') == 30, st)
        ck('ticks survive the tab change', st.get('picked') == 2, st)
        c.cap('05_my_units'); overflow(c, 'My Units page 1')
        c.click('bring:tick0', wait=0.6); st = bst(c)
        ck('ticked 1 My Units unit', st.get('picked') == 3, st)
        c.click('bring:tick1', wait=0.8); st = bst(c)
        ck('cap refuses a 4th (max 3)', st.get('picked') == 3 and 'at most' in st.get('msg', ''), st)
        c.cap('06_cap_refused')
        c.click('bring:next', wait=1.2); st = bst(c)
        ck('My Units page 2', st.get('page') == 1, st); c.cap('07_lib_page2'); overflow(c, 'My Units page 2')
        c.click('bring:prev', wait=1.2); st = bst(c)
        ck('ticks survive page changes', st.get('picked') == 3, st)
        c.click('bring:tab_save', wait=1.5); st = bst(c)
        ck('save ticks still shown after coming back', st.get('picked') == 3 and st.get('tab') == 1, st)
        c.click('bring:bring', wait=1.5)
        st = bst(c); rc, res = c.call('mu_test_bring_result')
        n, ps = picks(res)
        ck('Bring closes the window', st.get('open') == 0, st)
        ck('Bring returns 3 picks', rc == 0 and n == 3 and len(ps) == 3, res)
        ck('2 save picks (guid + save file) + 1 library pick (lib id)',
           [p[0] for p in ps].count('1') == 2 and [p[0] for p in ps].count('2') == 1
           and all(len(p[1]) == 36 and p[3] in SAVES for p in ps if p[0] == '1')
           and all(len(p[1]) == 16 for p in ps if p[0] == '2'), ps)
        # reopen + Cancel
        c.call('mu_test_bring_open', '3,3'); st = bst(c)
        ck('reopens', st.get('open') == 1 and st.get('picked') == 0, st)
        ck('first window had the screen size of later ones', size1 == (st.get('w'), st.get('h')), [size1, st.get('w'), st.get('h')])
        c.cap('08a_reopen')
        c.click('bring:cancel', wait=1.5); rc, res = c.call('mu_test_bring_result')
        ck('Cancel returns -1', res.startswith('-1:') and bst(c).get('open') == 0, res)
        # My Units only: one source = no tab strip; a team ticks its units up to the cap
        c.call('mu_test_bring_open', '2,2'); st = bst(c)
        ck('one source: no tab strip', st.get('tab') == 2 and c.req('rect', target='bring:tab_save')['rc'] != 1
           and c.req('rect', target='bring:tab_lib')['rc'] != 1, st)
        c.click('bring:left2', wait=1.5); st = bst(c)
        ck('team Trio ticks its first 2 units (cap 2) with a message', st.get('picked') == 2 and 'at most' in st.get('msg', ''), st)
        c.cap('08_team_cap'); overflow(c, 'team page')
        # Escape is not tested: the driver's key sends down + up in one go, the window polls Input.GetKey every 50 ms
        c.click('bring:cancel', wait=1.5); rc, res = c.call('mu_test_bring_result')
        ck('Cancel (one source) returns -1', res.startswith('-1:') and bst(c).get('open') == 0, res)
        c.cap('09_closed')
        ck('no guard violation', not c.violations, c.violations)
    print('saves in', sd, '| captures in', os.path.join(OUT, 'drv', 'cap'))
    return ck.save()


sys.exit(1 if main() else 0)
