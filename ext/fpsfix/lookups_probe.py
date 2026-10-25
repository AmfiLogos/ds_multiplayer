#!/usr/bin/env python3
"""Which views are looked up (FindObjectOfType) and how often: title 30 s, then 30 s in the loaded world. lookups_probe.py <dylib>"""
import os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
rig.DYLIB = sys.argv[1]; x = 'C'
def show(n):
    p = rig.send(x, {'cmd': 'perf', 'reset': 1}); print(n, 'frames', p['frames'], 'all avg', p['parts']['all']['avg_ms'], p['mpm_scans'])
    for k in sorted(p['view_lookups'], key=lambda v: -v[2])[:12]: print('   ', k)
mpj = os.path.join(rig.idir(x), 'mp.json')
if os.path.exists(mpj): os.replace(mpj, mpj + '.fpsfix_aside')   # no mp.json = no auto host: single player
try:
    rig.start(x, fps=240); rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    time.sleep(5); show('boot'); time.sleep(30); show('title30')
    work = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
    rig.send(x, {'cmd': 'load_save', 'file': src, 'det': 0}); time.sleep(3)
    rig.wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'loaded', timeout=600, every=1)
    rig.send(x, {'cmd': 'set_speed', 'speed': 1}); show('load'); time.sleep(30); show('world30')
finally:
    if os.path.exists(mpj + '.fpsfix_aside'): os.replace(mpj + '.fpsfix_aside', mpj)
    rig.stop(x); print('prefs ok' if rig.check_prefs() else 'PREFS CHANGED')
