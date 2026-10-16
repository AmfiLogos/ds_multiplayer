#!/usr/bin/env python3
"""Control socket owner test (loaderui finding: the loader's hidden prewarm copy took ctl.sock from the running game).
  ctl_owner.py --dylib PATH [--lane mpfix]
One leased pair, its first copy X (hidden, batchmode):
 1. X runs and answers on ctl.sock.
 2. A second copy of the SAME folder starts (like the prewarm copy): it must not take the socket (its rig.log says it
    waits), X still answers; the second copy is killed: X still answers (before the fix: no socket at all).
 3. Second copy with DSRIG_NO_CTL=1: logs "control socket off"; X still answers; killed.
 4. A second copy waits, X stops: the second copy takes the socket over and answers.
Owner prefs checked before / after."""
import json, os, signal, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1])
LANE = A[A.index('--lane') + 1] if '--lane' in A else 'mpfix'
res = {}
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''): res[name] = bool(ok); log('PASS' if ok else 'FAIL', name, str(info)[:300])
def answers(x):
    try: return rig.send(x, {'cmd': 'status'}, timeout=10).get('ok') is True
    except Exception: return False
def rlog(x): return open(os.path.join(rig.idir(x), 'rig.log'), errors='replace').read()
def second(x, extra=None):
    spec = json.load(open(os.path.join(rig.idir(x), 'launch.json')))
    env = dict(spec['env']); env.update(extra or {})
    args = [a if a != os.path.join(rig.idir(x), 'player.log') else os.path.join(rig.idir(x), 'player2.log') for a in spec['args']]
    if '-batchmode' not in args: args.append('-batchmode')
    env['DSRIG_BATCH'] = '1'
    return subprocess.Popen(args, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            preexec_fn=lambda: os.nice(10), start_new_session=True)
def kill(p):
    try: os.kill(p.pid, signal.SIGKILL)
    except OSError: pass
    p.wait(timeout=30)

pair = None
for _ in range(80):
    pair = rig.lease(LANE, os.getpid())
    if pair: break
    time.sleep(30)
if not pair: sys.exit('no free pair')
X = pair[0]; p2 = None
prefs0 = rig.prefs_snapshot() if hasattr(rig, 'prefs_snapshot') else None
try:
    rig.start(X, fps=30)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=900)
    check('1. X answers', answers(X))
    n0 = len(rlog(X))
    p2 = second(X); time.sleep(45)
    tail = rlog(X)[n0:]
    check('2a. second copy waits, does not take the socket', 'another running copy owns it; waiting' in tail and p2.poll() is None,
          [l for l in tail.splitlines() if 'control socket' in l][-3:])
    check('2b. X still answers while the second copy runs', answers(X))
    kill(p2); p2 = None; time.sleep(3)
    check('2c. X still answers after the second copy is killed', answers(X))
    n0 = len(rlog(X))
    p2 = second(X, {'DSRIG_NO_CTL': '1'}); time.sleep(45)
    tail = rlog(X)[n0:]
    check('3. DSRIG_NO_CTL=1: no socket for the second copy, X answers', 'control socket off (DSRIG_NO_CTL=1)' in tail and answers(X))
    kill(p2); p2 = None; time.sleep(3)
    p2 = second(X); time.sleep(45)
    rig.stop(X)
    ok = False
    for _ in range(15):
        time.sleep(2)
        if answers(X): ok = True; break
    check('4. after X stops, the waiting copy takes the socket and answers', ok)
finally:
    if p2: kill(p2)
    rig.stop(X)
    rig.release(pair, LANE)
log('prefs identical after:', rig.check_prefs() if hasattr(rig, 'check_prefs') else 'n/a')
log('RESULT %d/%d pass' % (sum(res.values()), len(res)))
