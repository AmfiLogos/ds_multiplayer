#!/usr/bin/env python3
"""held_lock_run.py <script.py> [args...]: run a Windows test while the GATE holds rig/wine.lock for its whole run
(owner GATE_WINE_OWNER, team-lead 2026-10-16: no lane may slip in between the gate's Windows steps). The test's own
take/release of that lock becomes a no-op: the lock looks free to it, its mkdir / owner write / owner move / rmdir on that
path do nothing. Everything else runs unchanged. Tests that use rig/wine_lease.py get WINE_LEASE_LOCK = a private lock folder next to the real one, so their take /
release works on that one. Refuses unless the real lock exists and names the gate (GATE_WINE_OWNER, default gate13-gate)."""
import builtins, os, runpy, sys
LOCK = os.path.expanduser('~/ds_multiplayer/rig/wine.lock')
owner = open(f'{LOCK}/owner').read().split('\n')[0].strip() if os.path.exists(f'{LOCK}/owner') else ''
GATE = os.environ.get('GATE_WINE_OWNER', 'gate13-gate')
if owner not in (GATE, 'gate13-hotfix'): sys.exit(f'held_lock_run: wine.lock is not held by {GATE} (owner {owner!r})')
os.environ['WINE_LEASE_LOCK'] = LOCK + '.gate13-inner'   # wine_lease.py users take this one while the gate holds the real one
mine = lambda p: os.path.abspath(os.fspath(p)).startswith(LOCK)
_exists, _mkdir, _rmdir, _replace, _open = os.path.exists, os.mkdir, os.rmdir, os.replace, builtins.open
os.path.exists = lambda p: False if mine(p) and os.path.abspath(os.fspath(p)) == LOCK else _exists(p)
os.mkdir = lambda p, *a, **k: None if mine(p) else _mkdir(p, *a, **k)
os.rmdir = lambda p, *a, **k: None if mine(p) else _rmdir(p, *a, **k)
os.replace = lambda s, d, *a, **k: None if mine(s) else _replace(s, d, *a, **k)
builtins.open = lambda p, mode='r', *a, **k: _open(os.devnull, mode, *a, **k) if isinstance(p, (str, bytes, os.PathLike)) and mine(p) and any(c in mode for c in 'wa') else _open(p, mode, *a, **k)
script = os.path.abspath(sys.argv[1]); sys.argv = [script] + sys.argv[2:]; sys.path.insert(0, os.path.dirname(script))
runpy.run_path(script, run_name='__main__')
