#!/usr/bin/env python3
"""Lane steamwin step 4: Mac copy M hosts over Steam, invites the VM test account from the in-game picker, the VM joins (Steam callback or steam://joinlobby). Then: xsteam.py play 130"""
import sys, re, time, json, os
sys.argv = ['x']; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xsteam as X, winrig as W
T = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
X.wait('Mac title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(X.mac({'cmd': 'status'})), 150, 3)
X.mac_host()
w = X.vm(); X.no_box(w.cmd)
X.mac({'cmd': 'mpm_friends', 'action': 'open'}); time.sleep(3)
pns = re.findall(r'"PersonaName"\s*"([^"]*)"', w.text(r'C:\Program Files (x86)\Steam\config\loginusers.vdf') or '')
f = X.mac({'cmd': 'mpm_friends', 'action': 'state'})
rows = [i for i, r in enumerate(f['rows']) if r.get('name') in pns]
T('picker rows', len(f['rows']), 'test account row', rows, 'state', [f['rows'][i]['state'] for i in rows])
r = X.mac({'cmd': 'mpm_friends', 'action': 'row', 'i': rows[0]}); T('invite click ok', r.get('ok'))
mark = len(w.text(W.PROF + r'\rig.log') or '')
def accepted():
    t = (w.text(W.PROF + r'\rig.log') or '')[mark:]
    return 'invite accepted' in t
try:
    X.wait('invite accepted on the VM by itself', accepted, 20, 2); T('VM: invite accepted (Steam callback, no URL)')
except SystemExit:
    host = re.findall(r'steam: connecting to (\d{17})', w.text(W.PROF + r'\rig.log') or '')[-1]
    lob = X.mac({'cmd': 'mp_steam_state'})['lobby']
    w.a('run', exe=r'C:\Program Files (x86)\Steam\steam.exe', args=f'steam://joinlobby/2798330/{lob}/{host}', wait_ms=0)
    X.wait('invite accepted after steam://joinlobby', accepted, 60, 2); T('VM: accepted through steam://joinlobby')
X.wait('VM connected over Steam', lambda: 'steam: connected to host' in (w.text(W.PROF + r'\rig.log') or '')[mark:], 60, 2)
T('VM connected to host over Steam P2P')
X.wait('VM guest in the lobby/world', lambda: (X.no_box(X.mac) and False) or w.cmd({'cmd': 'mpm_status'}).get('state') not in ('guest_loading', 'joining', 'failed', 'idle'), 240, 3)
T('VM menu state', w.cmd({'cmd': 'mpm_status'}).get('state'))
