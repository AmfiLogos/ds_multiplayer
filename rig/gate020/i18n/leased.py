#!/usr/bin/env python3
"""leased.py <cmd...>: run one command under a rig pair lease (lane ship020i-<pid>), always released after."""
import os, subprocess, sys
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py'); lane = f'ship020i-{os.getpid()}'
r = subprocess.run([sys.executable, RIG, 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
pair = r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else None
print('lease', pair, r.stderr.strip()[-200:], flush=True)
if not pair: sys.exit(2)
try: rc = subprocess.run(sys.argv[1:]).returncode
finally: print(subprocess.run([sys.executable, RIG, 'release', pair, lane], capture_output=True, text=True).stdout.strip(), flush=True)
sys.exit(rc)
