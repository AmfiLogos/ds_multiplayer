#!/usr/bin/env python3
"""Custom Modes home (PLAN_V13 R4) on hidden test copy U: title button below Mods, no hover card, page with the
registered entries + Back in the title column, stock column hidden while it shows, Back restores it.
Run 1 solo (custom-modes without multiplayer), run 2 with multiplayer.  home_test.py [out] [--no-build] [--run=1|2]"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmrig as C

args = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(args[0] if args else f'~/ds_mp_units13/ext/units13/out/home_{time.strftime("%Y%m%d-%H%M%S")}'))
runs = [int(a.split('=')[1]) for a in sys.argv if a.startswith('--run=')] or [1, 2]
os.makedirs(OUT, exist_ok=True)
ck = C.Checks(OUT)
if '--no-build' not in sys.argv:
    C.build_driver()
C.install()


def one(n, mods, expect_entries):
    d = os.path.join(OUT, f'run{n}'); os.makedirs(d, exist_ok=True)
    with C.Copy(mods, d) as c:
        st = c.wait_title()
        col = c.column(st); labels = [x[0] for x in col if x[1]]
        ck(f'r{n} title has Custom Modes', 'Custom Modes' in labels, labels)
        if 'Mods' in labels and 'Custom Modes' in labels:
            sm = [x[2] for x in col if x[0] == 'Mods'][0]; sc = [x[2] for x in col if x[0] == 'Custom Modes'][0]
            ck(f'r{n} Custom Modes right below Mods', sc == sm + 1, f'Mods {sm} Custom Modes {sc}')
        c.cap(f'r{n}_title')
        c.req('hover', target='title:Custom Modes'); time.sleep(1.5)
        st = c.state(); ck(f'r{n} no hover card on Custom Modes', not st['tip']['shown'], st['tip'])
        c.req('mouse_off')
        a = c.click('title:Custom Modes', wait=1.5)
        st = c.state(); h = st.get('cm', {}).get('home', {})
        ck(f'r{n} click opens the home page', a['rc'] == 0 and h.get('open') == 1, h)
        btns = h.get('buttons', '').split(';') if h.get('buttons') else []
        # the entries other lanes register (Unit Builder 20, Gallery 30, Test Fights 40) come when their parts are built in:
        # My Units first, Back last, the known entries in their order
        known = ['My Units', 'Unit Builder', 'Gallery', 'Test Fights']
        ok = btns[-1:] == ['Back'] and btns[:1] == expect_entries[:1] and btns[:-1] == [k for k in known if k in btns[:-1]] and len(btns) >= 2
        ck(f'r{n} page buttons', ok, btns)
        mods_on = c.req('rect', target='title:Mods')['rc']
        ck(f'r{n} stock column hidden', mods_on == 0, f'Mods on_tree {mods_on}')
        back = c.req('rect', target='home:Back')
        ck(f'r{n} Back shows', back['rc'] == 1, back.get('at'))
        rects = {b: c.req('rect', target='home:' + b).get('at') for b in btns}
        ref = c.req('rect', target='title:Mods').get('at')
        json.dump({'page': rects, 'stock_mods_button': ref}, open(os.path.join(d, 'rects.json'), 'w'), indent=1)
        c.cap(f'r{n}_home')
        c.click('home:Back', wait=1.5)
        st = c.state(); h = st.get('cm', {}).get('home', {})
        ck(f'r{n} Back closes the page', h.get('open') == 0, h)
        ck(f'r{n} stock column back', c.req('rect', target='title:Mods')['rc'] == 1)
        c.cap(f'r{n}_back')
        c.click('title:Custom Modes', wait=1.5)
        ck(f'r{n} opens again', c.state().get('cm', {}).get('home', {}).get('open') == 1)
        c.click('home:Back', wait=1.0)
        ck(f'r{n} no guard violation', not c.violations, c.violations)


if 1 in runs:
    one(1, ['custom-modes', 'units-driver'], ['My Units'])
if 2 in runs:
    one(2, ['multiplayer', 'custom-modes', 'units-driver'], ['My Units'])
sys.exit(1 if ck.save() else 0)
