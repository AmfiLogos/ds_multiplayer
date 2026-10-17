#!/usr/bin/env python3
"""uifix11 check frames (one leased pair): title "Multiplayer" hovered (no tooltip), the Multiplayer page with the three
host choices, and each choice's Host page and next screen. Usage: shots.py <dylib>. Frames in ~/ds_multiplayer/out/uifix11_<time>/."""
import json, os, sys, time

sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
LANE = f'uifix11-{os.getpid()}'                 # unique per run: two runs of one test never share a pair

rig.DYLIB = os.path.abspath(sys.argv[1])
B = json.load(open(os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/tests/mpm_ids.json')))
OUT = os.path.expanduser('~/ds_multiplayer/out/uifix11_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
c = lambda x, **k: rig.send(x, k, timeout=180)
press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
shot = lambda x, n: c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, n))

pair = rig.lease(LANE, os.getpid())
if not pair: sys.exit('no free pair')
H, G = pair[0], pair[1]
try:
    for x in (H, G): rig.start(x, fps=30, window=True)
    for x in (H, G): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    time.sleep(3)
    c(G, cmd='mpm_tip', title='Multiplayer'); time.sleep(1.5)
    shot(G, 'title_multiplayer_hover.png'); print('title hover', c(G, cmd='mpm_status').get('tip'))
    c(G, cmd='mpm_tip')
    B.update(HOST_SAVE=121, HOST_NEW=122, HOST_FE=123)          # src/mpmenu.h (ids >= 64)
    press(H, 'MULTIPLAYER'); shot(H, 'mp_page.png')
    c(H, cmd='mpm_tip', title='Foreign Expedition'); time.sleep(1.5); shot(H, 'mp_page_tip_fe.png'); c(H, cmd='mpm_tip')
    press(H, 'HOST_FE'); st = c(H, cmd='mpm_status'); print('FE page fe =', st['settings'].get('fe')); shot(H, 'host_fe.png')
    press(H, 'BACK'); press(H, 'HOST_SAVE'); print('save page fe =', c(H, cmd='mpm_status')['settings'].get('fe')); shot(H, 'host_save.png')
    press(H, 'LOAD_SAVE'); time.sleep(2); shot(H, 'host_save_list.png'); print('after Host from a save:', c(H, cmd='mpm_status').get('state'))
    press(G, 'MULTIPLAYER'); press(G, 'HOST_NEW'); shot(G, 'host_new.png')
    press(G, 'NEW_CAMPAIGN'); time.sleep(2); shot(G, 'host_new_screen.png'); print('after Host a new campaign:', c(G, cmd='mpm_status').get('state'))
finally:
    for x in (H, G): rig.stop(x)
    rig.release(pair, LANE)
print('frames', OUT)
