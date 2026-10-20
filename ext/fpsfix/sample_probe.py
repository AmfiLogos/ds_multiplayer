#!/usr/bin/env python3
"""Start copy X with <dylib>, `sample` it 20 s at the title and 30 s in the loaded campaign (single player, no session)."""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
dylib, label = sys.argv[1], sys.argv[2]; x = sys.argv[3] if len(sys.argv) > 3 else 'C'
rig.DYLIB = dylib; d = os.path.dirname(os.path.abspath(__file__))
mpj = os.path.join(rig.idir(x), 'mp.json')
if os.path.exists(mpj): os.replace(mpj, mpj + '.fpsfix_aside')   # no mp.json = no auto host: single player
try:
    pid = rig.start(x, fps=240)
    rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    time.sleep(5); pid = rig.read_pid(x)
    subprocess.run(['sample', str(pid), '20', '-file', f'{d}/sample_{label}_title.txt'], capture_output=True)
    work = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
    rig.send(x, {'cmd': 'load_save', 'file': src, 'det': 0}); time.sleep(3)
    rig.wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'loaded', timeout=600, every=1)
    rig.send(x, {'cmd': 'set_speed', 'speed': 1}); time.sleep(5)
    subprocess.run(['sample', str(pid), '30', '-file', f'{d}/sample_{label}_camp.txt'], capture_output=True)
finally:
    if os.path.exists(mpj + '.fpsfix_aside'): os.replace(mpj + '.fpsfix_aside', mpj)
    rig.stop(x); print('prefs ok' if rig.check_prefs() else 'PREFS CHANGED')
