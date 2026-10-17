#!/usr/bin/env python3
"""Lane builder13: the Unit Builder in the game, on hidden test copy R (solo: custom-modes without multiplayer; a test
My Units library file; never the owner's install, saves, prefs or library). Uses lane units' copy tooling (cmrig.py) and
test driver (ext/units13/driver), with CMRIG_INST=R.
  1. title > Custom Modes: "Unit Builder" is on the page, right after My Units
  2. Unit Builder: the wiki model starts; race / background / trait / skill / gear / inscription clicks change the unit;
     every section drawn (capture); Save -> the unit is in the library file, with its name
  3. Edit that unit (the My Units door): same id, build replaced, still one unit
  4. My Units > "New unit" -> builder -> Save -> My Units shows one more unit
  5. Bring Units > "New unit" -> builder -> Save -> ticked in Bring Units -> Bring hands it back as a My Units pick
  6. no file written in the Saves folder (a custom unit never enters a regular save)
  builder_test.py [out] [--scale=0.75] [--short]   (--short: open + every section + window inside the screen only)"""
import json, os, sys, time
os.environ.setdefault('CMRIG_INST', 'R')
HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(WT, 'ext/units13/test'))
import cmrig as C

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opt = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
SCALE = float(opt['scale']) if 'scale' in opt else None
SHORT = '--short' in sys.argv
OUT = os.path.abspath(os.path.expanduser(args[0] if args else
      os.path.join(WT, f'ext/builder13/out/builder_{("s%s_" % SCALE) if SCALE else ""}{time.strftime("%Y%m%d-%H%M%S")}')))
os.makedirs(OUT, exist_ok=True)
ck = C.Checks(OUT)
C.build_driver()
C.install()
LIB = os.path.join(C.out_lib_dir(OUT), 'library.json')
SAVES = [os.path.join(C.I, 'data', 'Saves'),          # the copy's data folders (with / without multiplayer)
         os.path.join(C.I, 'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mpR/Saves')]
SECTIONS = ['Race', 'Background', 'Traits', 'Skills', 'Gear', 'Inscriptions']


def lib():
    try:
        return json.load(open(LIB))
    except Exception:
        return {'units': [], 'teams': []}


def cm(c):
    return c.state().get('cm', {})


def bld(c):
    return cm(c).get('bld', {})


def wait(c, pred, limit=20, what='bld'):
    t0 = time.time(); s = cm(c).get(what, {})
    while time.time() - t0 < limit and not pred(s):
        time.sleep(0.3); s = cm(c).get(what, {})
    return s


def idle(c, limit=15):
    return wait(c, lambda s: s.get('open') and s.get('engine') == 2 and not s.get('busy'), limit)


def keys(c, kind):
    rc, out = c.call('bld_test_list', kind)
    return [tuple(x.split(',')) for x in out.split(';') if x]


def click_key(c, target, wait_s=0.6):
    a = c.click('bld:' + target, wait=wait_s)
    idle(c)
    return a.get('rc', -1) > 0


def inside(c, tag):
    """the window's rect (game pixels) inside the copy's 960 x 540 screen (cmrig starts it at that size)"""
    w = c.req('rect', target='bld:win').get('at') or [0, 0, 0, 0]
    ok = w[2] > w[0] and w[3] > w[1] and w[0] >= -1 and w[1] >= -1 and w[2] <= 961 and w[3] <= 541
    ck(f'{tag}: window inside the screen', ok, f'win {w}')


def saves_list():
    return [sorted(os.listdir(d)) if os.path.isdir(d) else [] for d in SAVES]


saves0 = saves_list()
with C.Copy(['custom-modes', 'units-driver'], OUT, lane='builder13', ui_scale=SCALE) as c:
    c.wait_title()
    c.click('title:Units Laboratory', wait=1.2)
    h = cm(c).get('home', {})
    btns = h.get('buttons', '').split(';')
    ck('home page: Unit Builder right after My Units', btns[:2] == ['My Units', 'Unit Builder'], h)
    c.click('home:Unit Builder', wait=1.0)
    s = wait(c, lambda s: s.get('engine') in (2, 3), 120)
    ck('Unit Builder opens and the wiki model starts', s.get('open') == 1 and s.get('engine') == 2, s)
    s = idle(c)
    for i, sec in enumerate(SECTIONS):
        click_key(c, 'nav:' + sec, 0.8)
        ck(f'section {sec} drawn', bld(c).get('section') == i and bld(c).get('widgets', 0) > 8, bld(c))
        c.cap(f'0{i + 1}_{sec.lower()}')
    inside(c, 'new unit')
    if not SHORT:
        # race
        click_key(c, 'nav:Race')
        races = keys(c, 'races'); other = [k for k, on, lk in races if on == '0']
        ck('races listed', len(races) >= 2, races)
        if other:
            click_key(c, 'race:' + other[0])
            ck('race click changes the race', bld(c).get('race') == other[0], bld(c))
        # background
        click_key(c, 'nav:Background')
        bgs = [k for k, on, lk in keys(c, 'bgs') if on == '0']
        if bgs:                                         # one in view (the list scrolls; the driver clicks what is on screen)
            click_key(c, 'bg:' + bgs[1 if len(bgs) > 1 else 0])
            ck('background click', bld(c).get('bg') == bgs[1 if len(bgs) > 1 else 0], bld(c))
        # level 12 (the level row; the driver cannot hit its arrows)
        c.call('bld_test_act', '{"t":"lvl","v":12}'); idle(c)
        ck('level 12', bld(c).get('lvl') == 12, bld(c))
        # traits
        click_key(c, 'nav:Traits')
        inds = [k for k, on, lk in keys(c, 'inds') if on == '0' and lk == '0']
        before = sum(1 for k, on, lk in keys(c, 'inds') if on == '1')
        if inds:
            click_key(c, 'ind:' + inds[0])
            after = sum(1 for k, on, lk in keys(c, 'inds') if on == '1')
            ck('trait click', after == before + 1, (before, after))
        # skills: learn three learnable nodes
        click_key(c, 'nav:Skills')
        n0 = bld(c).get('learned', 0)
        for _ in range(3):
            ks = [k for k, on, lk in keys(c, 'skills') if on == '0' and lk == '0']
            if ks:
                click_key(c, 'skill:' + ks[0])
        ck('skill clicks learn skills', bld(c).get('learned', 0) >= n0 + 3, bld(c))
        locked = [k for k, on, lk in keys(c, 'skills') if lk == '1']
        if locked:
            n1 = bld(c).get('learned', 0); c.click('bld:skill:' + locked[0], wait=0.6); idle(c)
            ck('a locked skill does not learn', bld(c).get('learned', 0) == n1, locked[0])
        c.cap('10_skills_learned')
        # gear
        click_key(c, 'nav:Gear')
        click_key(c, 'slot:Primary')
        items = keys(c, 'items:Primary')
        ck('weapon list from the data', len(items) >= 10, len(items))
        click_key(c, 'item:' + items[3][0])
        c.cap('11_gear')
        # inscriptions
        click_key(c, 'nav:Inscriptions')
        ins = [k for k, on, lk in keys(c, 'inscs') if on == '0' and lk == '0']
        if ins:
            click_key(c, 'insc:' + ins[0])
            ck('inscription click', any(on == '1' for k, on, lk in keys(c, 'inscs')), ins[0])
        c.cap('12_insc')
        # name + Save
        c.req('field_set', target='bld:name', text='Builder Test Unit')
        n_lib = len(lib()['units'])
        c.click('bld:save', wait=0.5)
        s = wait(c, lambda s: not s.get('open'), 15)
        ck('Save closes the window', not s.get('open'), s)
        L = lib(); u = L['units'][-1] if L['units'] else {}
        ck('Save: one unit more in My Units', len(L['units']) == n_lib + 1, len(L['units']))
        ck('Save: the name typed', u.get('name') == 'Builder Test Unit' and u.get('build', {}).get('name') == 'Builder Test Unit', u.get('name'))
        b = u.get('build', {})
        ck('Save: the choices made', b.get('lvl') == 12 and len(b.get('learned', [])) >= 3 and b.get('eq', {}).get('Primary') == items[3][0], b)
        json.dump(u, open(os.path.join(OUT, 'saved_unit.json'), 'w'), indent=1)
        uid = u.get('id', '')
        # Edit (the My Units door): same id, build replaced
        c.call('bld_test_open', uid)
        s = idle(c, 30)
        ck('Edit opens that unit', s.get('edit') == uid and s.get('lvl') == 12, s)
        click_key(c, 'nav:Race')
        r2 = [k for k, on, lk in keys(c, 'races') if on == '0']
        if r2:
            click_key(c, 'race:' + r2[0])
        c.click('bld:save', wait=0.5); wait(c, lambda s: not s.get('open'), 15)
        rc, sid = c.call('bld_test_saved')
        L = lib(); u2 = next((x for x in L['units'] if x['id'] == uid), {})
        ck('Edit: same id, still one unit, build replaced', sid == uid and len(L['units']) == n_lib + 1 and u2.get('build', {}).get('race') == (r2[0] if r2 else b.get('race')), (sid, len(L['units']), u2.get('build', {}).get('race')))
        # My Units > New unit
        c.click('title:Units Laboratory', wait=0.5)
        c.click('home:My Units', wait=1.5)
        m = wait(c, lambda s: s.get('open'), 10, 'mu')
        ck('My Units shows the Unit Builder door', m.get('builder') == 1, m)
        nu = m.get('units', 0)
        c.click('mu:new', wait=0.8)
        idle(c, 30)
        ck('My Units > New unit opens the Unit Builder', bld(c).get('open') == 1, bld(c))
        c.click('bld:save', wait=0.5); wait(c, lambda s: not s.get('open'), 15)
        m = wait(c, lambda s: s.get('units', 0) > nu, 10, 'mu')
        ck('My Units lists the new unit after Save', m.get('units') == nu + 1 and m.get('ticked') == 1, m)
        c.cap('13_myunits')
        c.click('mu:close', wait=0.8)
        # Bring Units > New unit
        c.call('bld_test_bring_open')
        bs = wait(c, lambda s: s.get('open'), 10, 'bring')
        ck('Bring Units shows "New unit"', bs.get('builder') == 1, bs)
        c.click('bring:new', wait=0.8)
        idle(c, 30)
        c.req('field_set', target='bld:name', text='From Bring Units')
        c.click('bld:save', wait=0.5); wait(c, lambda s: not s.get('open'), 15)
        bs = wait(c, lambda s: s.get('msg', '').startswith('Saved'), 10, 'bring')
        ck('Bring Units: saved and ticked', 'ticked' in bs.get('msg', ''), bs)
        c.cap('14_bring')
        c.click('bring:bring', wait=1.0)
        rc, res = c.call('bld_test_bring_result')
        nid = next((x['id'] for x in lib()['units'] if x['name'] == 'From Bring Units'), None)
        ck('Bring hands the new unit back as a My Units pick', nid and ('lib:' + nid) in res.split(';'), (res, nid))
    ck('no file written in the Saves folder', saves_list() == saves0, (saves0, saves_list()))
    ck('no guard violation', not c.violations, c.violations)
json.dump(lib(), open(os.path.join(OUT, 'library_after.json'), 'w'), indent=1)
sys.exit(1 if ck.save() else 0)
