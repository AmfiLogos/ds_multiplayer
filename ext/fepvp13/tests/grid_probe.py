#!/usr/bin/env python3
"""fepvp: one copy loads a PvP v1 world and dumps the settlement grid (pvp_cellmap) + the clan units' cells."""
import json, os, shutil, sys, time
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
LANE = f'fepvp-{os.getpid()}'                 # unique per run: two runs of one test never share a pair
rig.DYLIB = os.path.join(W, 'build', 'dsrig.dylib')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
OUT = os.path.join(W, 'ext', 'fepvp13', 'out', 'grid_' + time.strftime('%H%M%S')); os.makedirs(OUT)
pair = None
while not pair:
    pair = rig.lease(LANE, os.getpid())
    if not pair: time.sleep(30)
H = pair[0]; c = lambda **k: rig.send(H, k, timeout=180)
try:
    p = os.path.join(rig.idir(H), 'mp.json')
    if os.path.exists(p): os.replace(p, p + '.last')
    cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
    rig.start(H, fps=30, window=True)
    rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500); time.sleep(2)
    r = c(cmd='mpm_pvp', action='v1_world_file'); c(cmd='load_save', file=r['file'], det=0)
    rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy'), 'loaded', timeout=600); time.sleep(3)
    g = c(cmd='pvp_cellmap', x=110, y=115, w=80, h=70)
    us = [(u['name'], u['cell']) for u in c(cmd='list_units')['units'] if u['faction'] == 1]
    json.dump({'grid': g, 'units': us}, open(os.path.join(OUT, 'grid.json'), 'w'))
    c(cmd='screenshot', path=os.path.join(OUT, 'frame.png'))
    print('units', us); print(OUT)
finally:
    rig.stop(H)
    p = os.path.join(rig.idir(H), 'mp.json')
    if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
    rig.release(pair, LANE)
