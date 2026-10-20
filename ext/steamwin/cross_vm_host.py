#!/usr/bin/env python3
"""Lane steamwin step 5: the VM hosts a fresh-guid save over Steam, Mac copy M joins with mp_steam_join (no steam:// on the Mac). Then: xsteam.py play 130 vm_host"""
import sys, re, time, json, os, subprocess
sys.argv = ['x']; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xsteam as X, winrig as W, win_checklist as WC
T = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
w = X.vm(); w.kill_all(); w.prepare(lang=0, active=WC.BASE_MODS, settings={}, consent='no'); T('VM title in', w.start())
save = WC.put_save(w, os.path.join(WC.SAVES, 'base_25846389.json'), label='SteamVM')
h = w.host(w.expand(save)); T('VM host', {k: h.get(k) for k in ('state', 'tick')})
t0 = time.time()
while time.time() - t0 < 15 and not X.no_box(w.cmd):
    time.sleep(1)
st = X.wait('VM Steam lobby', lambda: (lambda s: s if s.get('lobby') not in (None, '0') else None)(w.cmd({'cmd': 'mp_steam_state'})), 60)
T('VM Steam', {k: st.get(k) for k in ('state', 'steam', 'host')})
X.wait('Mac title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(X.mac({'cmd': 'status'})), 150, 3)
r = X.mac({'cmd': 'mp_steam_join', 'lobby': st['lobby']}); T('Mac mp_steam_join', r.get('ok'), r.get('err'))
def lobby():
    X.no_box(w.cmd)
    m = X.mac({'cmd': 'mpm_status'}).get('state')
    if m == 'failed':
        raise SystemExit('Mac join failed: ' + str(X.mac({'cmd': 'mpm_status'}).get('msg')))
    return m in ('guest_lobby', 'playing') and m
T('Mac menu state', X.wait('Mac guest in the VM lobby', lobby, 240, 3))
