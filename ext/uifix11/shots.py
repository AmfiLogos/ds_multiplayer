#!/usr/bin/env python3
"""uifix11 check frames (one leased pair): Host page World row (Your save, Foreign Expedition) and the title screen
"Multiplayer" button hovered (it must show no tooltip). Usage: shots.py <dylib>. Frames in ~/ds_multiplayer/out/uifix11_<time>/."""
import json, os, sys, time

sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

rig.DYLIB = os.path.abspath(sys.argv[1])
B = json.load(open(os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/tests/mpm_ids.json')))
OUT = os.path.expanduser('~/ds_multiplayer/out/uifix11_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
c = lambda x, **k: rig.send(x, k, timeout=180)
press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
shot = lambda x, n: c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, n))

pair = rig.lease('uifix11', os.getpid())
if not pair: sys.exit('no free pair')
H, G = pair[0], pair[1]
try:
    for x in (H, G): rig.start(x, fps=30, window=True)
    for x in (H, G): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    time.sleep(3)
    c(G, cmd='mpm_tip', title='Multiplayer'); time.sleep(1.5)
    shot(G, 'title_multiplayer_hover.png'); print('title hover', c(G, cmd='mpm_status').get('tip'))
    c(G, cmd='mpm_tip')
    press(H, 'MULTIPLAYER'); press(H, 'HOST')
    for i in range(2):
        fe = c(H, cmd='mpm_status')['settings'].get('fe')
        shot(H, 'host_world_%s.png' % ('fe' if fe else 'save')); print('world fe =', fe)
        press(H, 'WORLD')
    c(H, cmd='mpm_tip', title='World'); time.sleep(1.5); shot(H, 'host_world_tip.png'); c(H, cmd='mpm_tip')
finally:
    for x in (H, G): rig.stop(x)
    rig.release(pair, 'uifix11')
print('frames', OUT)
