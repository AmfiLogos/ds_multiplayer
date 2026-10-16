#!/usr/bin/env python3
"""A rig pair test (default mp2_test.py) with OUR MelonLoader in the second copy (the guest): Mac host A without
MelonLoader, Mac guest B with dsrig.dylib + MelonLoader.Bootstrap (base <instance>/melon_base = inst.noindex/ml +
the guard plugin, mods = hello-melon).
  ml_mac_pair.py <pair> [script.py] [script options]   (lease the pair first: rig.py lease melon; release after)"""
import json, os, shutil, subprocess, sys
H = os.path.expanduser('~'); M = f'{H}/ds_multiplayer/ext/melon'
sys.path.insert(0, f'{H}/ds_multiplayer/rig'); import rig
PAIR = sys.argv[1]; GUEST = PAIR[1]
SCRIPT = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2].endswith('.py') else 'mp2_test.py'
sys.argv = [SCRIPT] + sys.argv[3 if len(sys.argv) > 2 and sys.argv[2].endswith('.py') else 2:]
os.environ['MP2_PAIR'] = PAIR
BASE = os.path.join(rig.idir(GUEST), 'melon_base'); MODS = os.path.join(rig.idir(GUEST), 'melon_mods', 'hello-melon')
if not os.path.exists(BASE):
    subprocess.run(['cp', '-c', '-R', f'{M}/inst.noindex/ml', BASE], check=True)
os.makedirs(os.path.join(BASE, 'Plugins'), exist_ok=True)
shutil.copy2(f'{M}/plugin/DSMelonGuard/bin/Release/net6.0/DSMelonGuard.dll', os.path.join(BASE, 'Plugins', 'DSMelonGuard.dll'))
if os.environ.get('ML_NO_GUARD') == '1':
    os.makedirs(f'{M}/old.noindex', exist_ok=True); os.replace(os.path.join(BASE, 'Plugins', 'DSMelonGuard.dll'), f'{M}/old.noindex/DSMelonGuard.off.dll')
os.makedirs(MODS, exist_ok=True)
shutil.copy2(f'{M}/plugin/mods/hello-melon/HelloMelon.dll', os.path.join(MODS, 'HelloMelon.dll'))

class _J:                                                  # rig.start writes launch.json with json.dumps: the guest gets MelonLoader
    loads, load, dump, JSONDecodeError = json.loads, json.load, json.dump, json.JSONDecodeError
    @staticmethod
    def dumps(o, **k):
        if isinstance(o, dict) and isinstance(o.get('env'), dict) and o['env'].get('DSRIG_DIR') == rig.idir(GUEST):
            e = dict(o['env'])
            e['DYLD_INSERT_LIBRARIES'] += ':' + os.path.join(BASE, 'MelonLoader.Bootstrap.dylib')
            e['MELONLOADER_BASEDIR'] = BASE; e['MELONLOADER_MOD_DIRS'] = MODS
            o = dict(o, env=e)
        return json.dumps(o, **k)
rig.json = _J
rig.lease = lambda lane, pid=None: PAIR
rig.release = lambda pair, lane=None: True
sys.path.insert(0, f'{H}/ds_multiplayer/rig')
import importlib
mod = importlib.import_module(os.path.splitext(os.path.basename(SCRIPT))[0])
print(f'ml pair {SCRIPT}: host {PAIR[0]}, guest {GUEST} with MelonLoader ({BASE})', flush=True)
mod.main()
