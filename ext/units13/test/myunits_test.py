#!/usr/bin/env python3
"""My Units window on hidden test copy U (solo: custom-modes without multiplayer; a test library file, a private test
pasteboard). Home page -> My Units; one-click "Add from wiki link" with a long link, a short link and no link (-> the
paste box); Save as team; Rename; Delete; text / icon overflow on every card page; the library file after each step.
  myunits_test.py [out] [--no-build] [--scale=0.75] [--net=0]"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmrig as C

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opt = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
SCALE = float(opt['scale']) if 'scale' in opt else None
NET = opt.get('net', '1') != '0'
OUT = os.path.abspath(os.path.expanduser(args[0] if args else
      f'~/ds_mp_units13/ext/units13/out/myunits_{("s%s_" % SCALE) if SCALE else ""}{time.strftime("%Y%m%d-%H%M%S")}'))
os.makedirs(OUT, exist_ok=True)
FX = os.path.expanduser('~/ds_mp_units13/custom/units/test/data')
LONG = open(os.path.join(FX, 'team_jEkTAd.txt')).read().strip()                     # 4 units
ONE = json.load(open(os.path.join(FX, 'launcher_links.json')))[1]['link']           # 1 unit, a name with accents
SHORT = 'https://dungeonsettlers.wiki/fr/build/NNUGfJ'                              # short link, 1 unit
SHORT_T = 'https://dungeonsettlers.wiki/t/rey8Rw'                                   # short team link, 4 units
ck = C.Checks(OUT)
if '--no-build' not in sys.argv:
    C.build_driver()
C.install()
LIB = os.path.join(C.out_lib_dir(OUT), 'library.json')


def lib():
    try:
        return json.load(open(LIB))
    except Exception:
        return {'units': [], 'teams': []}


def mu(c):
    return c.state().get('cm', {}).get('mu', {})


def wait_mu(c, pred, limit=15):
    t0 = time.time(); s = mu(c)
    while time.time() - t0 < limit and not pred(s):
        time.sleep(0.4); s = mu(c)
    return s


def overflow(c, tag):
    rc, out = c.call('mu_test_overflow')
    ck(f'{tag}: no text or icon outside its card', rc == 0, out[-160:])
    c.cap(tag)


with C.Copy(['custom-modes', 'units-driver'], OUT, ui_scale=SCALE) as c:
    c.wait_title()
    c.click('title:Units Laboratory', wait=1.2)
    h = c.state().get('cm', {}).get('home', {})
    ck('home page lists My Units', h.get('buttons', '').split(';')[:1] == ['My Units'], h)
    c.click('home:My Units', wait=2.0)
    s = mu(c)
    ck('My Units opens', s.get('open') == 1, s)
    ck('empty library', s.get('units') == 0 and s.get('cards') == 0, s)
    c.cap('01_empty')
    # one click, long link
    c.call('mu_test_clip', LONG)
    t0 = time.time(); c.click('mu:add', wait=0.2)
    s = wait_mu(c, lambda s: s.get('units', 0) >= 4)
    ck('one click: long team link -> 4 units, 1 team', s.get('units') == 4 and s.get('teams') == 1, f'{s} in {time.time() - t0:.1f}s')
    ck('one click: no paste box', s.get('box') == 0, s.get('msg'))
    ck('library file written', len(lib()['units']) == 4, len(lib()['units']))
    overflow(c, '02_long_link')
    c.call('mu_test_clip', ONE); c.click('mu:add', wait=0.2)
    s = wait_mu(c, lambda s: s.get('units', 0) >= 5)
    ck('one click: one-unit link', s.get('units') == 5, s.get('msg'))
    if NET:
        c.call('mu_test_clip', SHORT); c.click('mu:add', wait=0.2)
        s = wait_mu(c, lambda s: s.get('units', 0) >= 6, 12)
        ck('one click: short link /fr/build/', s.get('units') == 6, s.get('msg'))
        c.call('mu_test_clip', SHORT_T); c.click('mu:add', wait=0.2)
        s = wait_mu(c, lambda s: s.get('units', 0) >= 10, 12)
        ck('one click: short team link /t/', s.get('units') == 10, s.get('msg'))
        overflow(c, '03_short_links')
    n0 = mu(c).get('units')
    # no link -> box
    c.call('mu_test_clip', 'hello, no link here')
    c.click('mu:add', wait=1.0)
    s = mu(c)
    ck('no link on the clipboard -> paste box', s.get('box') == 1 and s.get('units') == n0, s.get('msg'))
    c.cap('04_box')
    # the test copy has no keyboard focus: the link is put in the field (= the player pasting it)
    c.req('field_set', target='mu:box_field', text=ONE)
    c.click('mu:box_add', wait=0.3)
    s = wait_mu(c, lambda s: s.get('units', 0) > n0)
    ck('paste box: link typed + Add', s.get('units') == n0 + 1 and s.get('box') == 0, s.get('msg'))
    # All units, tick 2, Save as team
    c.click('mu:all', wait=1.0)
    s = mu(c); t0 = s.get('teams')
    c.click('mu:tick0', wait=0.8); c.click('mu:tick1', wait=0.8)
    ck('2 ticked', mu(c).get('ticked') == 2)
    c.click('mu:save_team', wait=1.2)
    s = mu(c)
    ck('Save as team', s.get('teams') == t0 + 1 and s.get('shown') == 2, s)
    ck('team in the file', len(lib()['teams']) == t0 + 1)
    c.cap('05_team')
    # Rename the first unit of the team to a very long name
    c.click('mu:tick0', wait=0.8)
    c.click('mu:rename', wait=0.8)
    LONGNAME = 'Wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww'
    c.req('field_set', target='mu:rename_field', text=LONGNAME)
    c.click('mu:rename_ok', wait=1.2)
    names = [u.get('name') for u in lib()['units']]
    ck('Rename (long name)', any(n and n.startswith('Wwwwwwwwww') for n in names), names[:3])
    overflow(c, '06_long_name')
    # Delete it (still ticked after Rename)
    s = mu(c); n1 = s.get('units')
    ck('still 1 ticked after Rename', s.get('ticked') == 1, s.get('ticked'))
    c.click('mu:delete', wait=1.0)
    c.click('notice_ok', wait=1.5)
    s = mu(c)
    ck('Delete', s.get('units') == n1 - 1 and len(lib()['units']) == n1 - 1, s)
    # every page of All units: overflow
    c.click('mu:all', wait=1.0)
    s = mu(c); pages = (s.get('shown', 0) + max(s.get('per_page', 1), 1) - 1) // max(s.get('per_page', 1), 1)
    rects = {t: c.req('rect', target='mu:' + t).get('at') for t in ('win', 'right', 'card0', 'card1', 'card2', 'all', 'close')}
    json.dump(rects, open(os.path.join(OUT, 'rects.json'), 'w'), indent=1)
    C.log('screen rects (game pixels, x0 y0 x1 y1):', rects)
    for p in range(pages):
        overflow(c, f'07_page{p + 1}')
        if p + 1 < pages:
            c.click('mu:next', wait=1.2)
    c.click('mu:close', wait=1.0)
    ck('Close', mu(c).get('open') == 0)
    ck('no guard violation', not c.violations, c.violations)
json.dump(lib(), open(os.path.join(OUT, 'library_after.json'), 'w'), indent=1)
sys.exit(1 if ck.save() else 0)
