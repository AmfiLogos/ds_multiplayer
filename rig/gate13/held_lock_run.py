#!/usr/bin/env python3
"""held_lock_run.py <script.py> [args...]: run a Windows test while the GATE holds rig/wine.lock for its whole run
(owner "gate13-hotfix", team-lead 2026-10-16: no lane may slip in between the gate's Windows steps). The test's own
take/release of that lock becomes a no-op: the lock looks free to it, its mkdir / owner write / owner move / rmdir on that
path do nothing. Everything else runs unchanged. Refuses unless the lock exists and names gate13-hotfix."""
import builtins, os, runpy, sys
LOCK = os.path.expanduser('~/ds_multiplayer/rig/wine.lock')
owner = open(f'{LOCK}/owner').read().split('\n')[0].strip() if os.path.exists(f'{LOCK}/owner') else ''
if owner != 'gate13-hotfix': sys.exit(f'held_lock_run: wine.lock is not held by gate13-hotfix (owner {owner!r})')
mine = lambda p: os.path.abspath(os.fspath(p)).startswith(LOCK)
_exists, _mkdir, _rmdir, _replace, _open = os.path.exists, os.mkdir, os.rmdir, os.replace, builtins.open
os.path.exists = lambda p: False if mine(p) and os.path.abspath(os.fspath(p)) == LOCK else _exists(p)
os.mkdir = lambda p, *a, **k: None if mine(p) else _mkdir(p, *a, **k)
os.rmdir = lambda p, *a, **k: None if mine(p) else _rmdir(p, *a, **k)
os.replace = lambda s, d, *a, **k: None if mine(s) else _replace(s, d, *a, **k)
builtins.open = lambda p, mode='r', *a, **k: _open(os.devnull, mode, *a, **k) if isinstance(p, (str, bytes, os.PathLike)) and mine(p) and any(c in mode for c in 'wa') else _open(p, mode, *a, **k)
script = os.path.abspath(sys.argv[1]); sys.argv = [script] + sys.argv[2:]; sys.path.insert(0, os.path.dirname(script))
runpy.run_path(script, run_name='__main__')
