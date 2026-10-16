#!/usr/bin/env python3
"""Bring Units opened from a RIG COMMAND (multiplayer's test socket, "mu_" commands registered by custom-modes) on the
hidden loader copy with multiplayer: mu_bring_open -> both tabs -> one save unit + one My Units unit ticked by real
clicks -> Bring -> mu_bring_result returns both picks.  bring_rig_test.py [out] [--no-build]"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmrig as C

args = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(args[0] if args else f'~/ds_mp_units13/ext/units13/out/bringrig_{time.strftime("%Y%m%d-%H%M%S")}'))
os.makedirs(OUT, exist_ok=True)
BACKUP = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves')
SAVE = 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'
ck = C.Checks(OUT)
if '--no-build' not in sys.argv:
    C.build_driver()
C.install()
sd = os.path.join(C.I, 'data', 'Saves'); os.makedirs(sd, exist_ok=True)
if not os.path.exists(os.path.join(sd, SAVE)):
    shutil.copyfile(os.path.join(BACKUP, SAVE), os.path.join(sd, SAVE))                 # copy, never move
shutil.copyfile(os.path.join(C.WT, 'custom/units/test/data/launcher_library.json'), os.path.join(C.out_lib_dir(OUT), 'library.json'))


def rig(cmd, **kw):
    return C.r.send(C.X, dict(kw, cmd=cmd), timeout=30)


with C.Copy(['multiplayer', 'custom-modes', 'units-driver'], OUT) as c:
    c.wait_title()
    a = rig('mu_bring_open', max=2, sources=3)
    ck('rig mu_bring_open', a.get('ok') is True, a)
    time.sleep(1.5)
    s = rig('mu_bring_state')
    ck('window open with both tabs', s.get('open') == 1 and c.req('rect', target='bring:tab_save')['rc'] == 1
       and c.req('rect', target='bring:tab_lib')['rc'] == 1, s)
    c.click('bring:tab_save', wait=1.0); c.click('bring:left0', wait=1.5)
    c.click('bring:tick0', wait=1.0)
    c.click('bring:tab_lib', wait=1.5); c.click('bring:tick0', wait=1.0)
    s = rig('mu_bring_state')
    ck('2 picked (save + My Units)', s.get('picked') == 2, s)
    c.cap('rig_picked')
    c.click('bring:bring', wait=1.5)
    r = rig('mu_bring_result')
    p = r.get('picks', '')
    ck('rig mu_bring_result: 2 picks, one per source', r.get('done') is True and p.startswith('2:')
       and '1|' in p and '2|' in p, r)
    ck('no guard violation', not c.violations, c.violations)
sys.exit(1 if ck.save() else 0)
