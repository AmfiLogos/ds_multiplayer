#!/usr/bin/env python3
"""one copy: does pvp_cfg cam_x/cam_y/cam_size move the view? frames before / after (ext/pvp/out/camprobe)"""
import json, os, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer'); sys.path.insert(0, os.path.join(ROOT, 'rig')); import rig
LANE = f'pvp-{os.getpid()}'                 # unique per run: two runs of one test never share a pair
rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
sys.path.insert(0, os.path.join(ROOT, 'research', 'ux_shots')); import ux
OUT = os.path.join(ROOT, 'ext', 'pvp', 'out', 'camprobe'); os.makedirs(OUT, exist_ok=True)
c = lambda _i, **k: rig.send(_i, k, timeout=120)
pair = None
for i in range(240):
    pair = rig.lease(LANE, os.getpid())
    if pair: break
    time.sleep(30)
X = pair[0]
for p in (os.path.join(rig.idir(X), 'mp.json'),):
    if os.path.exists(p): os.replace(p, p + '.last')
try:
    ux.LEASED = pair; ux.start(X, 1512, 982)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
    print(c(X, cmd='load_save', file=src, det=0))
    rig.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', 0) > 0, 'campaign', timeout=600)
    time.sleep(8)
    us = [u for u in c(X, cmd='list_units')['units'] if u['faction'] == 1 and 'cell' in u]
    print('units', [(u['name'], u['x'], u['y']) for u in us])
    r0 = c(X, cmd='pvp_cfg', cam_read=1); print('cam', r0)
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'before.png'))
    cx, cy = int(us[0]['x']), int(us[0]['y'])
    print('banner plain', c(X, cmd='pvp_banner_test', x=cx + 2, y=cy))
    print('banner in scope', c(X, cmd='pvp_banner_test', x=cx - 2, y=cy, scope=1))
    time.sleep(3)
    print('zoom', c(X, cmd='pvp_cfg', cam_x=cx + 0.5, cam_y=cy + 0.5, cam_size=6)); time.sleep(2)
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'banners.png'))
    tx, ty = us[0]['x'] + 20, us[0]['y'] + 10
    print('set', c(X, cmd='pvp_cfg', cam_x=tx, cam_y=ty, cam_size=r0['cam_size_before'] / 2)); time.sleep(2)
    print('cam now', c(X, cmd='pvp_cfg', cam_read=1))
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'after.png'))
finally:
    rig.stop(X); rig.release(pair, LANE)   # rule 10: only our own lease; print('prefs', rig.check_prefs())
