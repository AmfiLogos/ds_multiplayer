#!/usr/bin/env python3
"""chatfix: which chat fonts have which characters (TMP_FontAsset.HasCharacter_Internal), on one hidden copy at the title."""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
rig.DYLIB = os.path.abspath(sys.argv[1])
def owner_playing():                                     # the owner's game (launcher copy or Modded app); shells that only name the path do not count
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/|Dungeon Settlers Modded.app'], capture_output=True, text=True).stdout
    return any(l.split(None, 1)[1].split()[0].split('/')[-1] not in ('zsh', 'bash', 'sh', 'python3', 'Python', 'pgrep', 'grep')
               for l in out.splitlines() if len(l.split(None, 1)) == 2)
if owner_playing(): sys.exit('owner playing')
pair = None
for _ in range(120):
    pair = rig.lease('chatfix', os.getpid())
    if pair: break
    time.sleep(10)
X = pair[0]
try:
    p = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(p): os.replace(p, p + '.last')
    rig.start(X, fps=30, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500); time.sleep(5)
    for t in sys.argv[2:] or ('Salut', 'Привет', 'こんにちは', '안녕', '你好', '🎉'):
        for rep in range(1): print(t, rep, json.dumps(rig.send(X, {'cmd': 'mpm_chat', 'probe': t}), ensure_ascii=False))
finally:
    rig.stop(X); rig.release(pair, 'chatfix')
