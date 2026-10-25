#!/usr/bin/env python3
"""Run a rig tool or test on THIS worktree's build instead of the shared ~/ds_multiplayer/build/dsrig.dylib (rig.py
hard-codes that path; gate.sh therefore tests the shared file). Usage, from anywhere:
  DYLIB=<path> python3 ext/dsmpsave/with_dylib.py rig/rig.py smoke
  DYLIB=<path> python3 ext/dsmpsave/with_dylib.py rig/mp2_test.py --ticks 3000
DYLIB defaults to this worktree's build/dsrig.dylib. The tool runs with its own folder as the working folder."""
import os, runpy, sys

W = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(W, 'rig'))
import rig                                            # this worktree's rig.py (copies stay in ~/ds_multiplayer/instances)

rig.DYLIB = os.path.abspath(os.environ.get('DYLIB') or os.path.join(W, 'build', 'dsrig.dylib'))
tool = os.path.abspath(os.path.join(W, sys.argv[1])) if not os.path.isabs(sys.argv[1]) else sys.argv[1]
print('with_dylib:', os.path.basename(tool), 'on', rig.DYLIB, flush=True)
os.chdir(os.path.dirname(tool)); sys.path.insert(0, os.path.dirname(tool))
sys.argv = [tool] + sys.argv[2:]
if os.path.basename(tool) == 'rig.py':
    rig.main()                                        # the already imported module: its DYLIB is the one set above
else:
    runpy.run_path(tool, run_name='__main__')
