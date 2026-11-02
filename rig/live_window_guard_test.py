#!/usr/bin/env python3
"""live_window_guard_test.py [rig.py | lrig.py]: the rig refuses DSRIG_LIVE in a run that shows a window
(rigports.check_live_window). No game copy and no window: a fake 'Dungeon Settlers' script in a temp folder writes a
marker if it runs; start() runs with its helpers stubbed, its copy folder in the temp folder and the guard process
replaced by a fake that writes a marker.
Breaks it catches (2026-11-02 18:48: DSRIG_LIVE=1 copied into a window run showed a 480x298 test window on the owner's
screen, twice):
  1. start(X, window=True) or the demo start (show='left') with DSRIG_LIVE starts a copy
  2. a test's own start (subprocess.Popen of the game, esc_test.py style) without -batchmode and with DSRIG_LIVE
     ('1' or '': the loader only checks that it is set) runs the game
  3. the guard blocks what must still run: a batchmode start with DSRIG_LIVE (events_test.py) keeps it; a window start
     without DSRIG_LIVE runs"""
import importlib.util, json, os, stat, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ.setdefault('DSRIG_RIGDIR', HERE)                  # lrig.py loads rigports.py from here
T = tempfile.mkdtemp(prefix='live_guard_')
path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'rig.py'))
spec = importlib.util.spec_from_file_location('guarded_rig', path); r = importlib.util.module_from_spec(spec); spec.loader.exec_module(r)
res = []


def check(name, ok, info=''):
    res.append(bool(ok)); print('PASS' if ok else 'FAIL', name, f'[{str(info)[:300]}]' if not ok else '', flush=True)


X = 'Q'
I = os.path.join(T, X)
EXE = os.path.join(I, r.EXE_REL)
RAN = os.path.join(os.path.dirname(EXE), 'ran.txt')          # the fake game ran
GUARD = os.path.join(T, 'guard.txt')                          # start() went on to its guard process (= a game start)
os.makedirs(os.path.dirname(EXE))
open(EXE, 'w').write('#!/bin/sh\necho "live=${DSRIG_LIVE-unset} args=$*" >> "$(dirname "$0")/ran.txt"\n')
os.chmod(EXE, os.stat(EXE).st_mode | stat.S_IXUSR)
fake_guard = os.path.join(T, 'fake_guard.py')
open(fake_guard, 'w').write(f'import os, time\nopen({GUARD!r}, "a").write("spawned\\n")\n'
                            f'open({os.path.join(I, "game.pid")!r}, "w").write(str(os.getpid()))\ntime.sleep(1)\n')

# never near real copies, ports, prefs or the owner's screen: the copy folder is in T, every helper with a side effect is a no-op
r.idir = lambda x: os.path.join(T, x)
r.__file__ = fake_guard                                       # start() spawns [python, __file__, '_guard', X]
noop = lambda *a, **k: None
for n in ('prefs_snapshot', 'cap_wait', 'prepare_data', 'fix_mp_port', 'require_background_only', 'install_lang', 'require_windowed'):
    if hasattr(r, n):
        setattr(r, n, noop)
r.pair_port = lambda *a, **k: 0
r.hostport_shim = lambda *a, **k: ''
r.front_pid = lambda *a, **k: 0
for n in ('check_start', 'note_started', 'set_host_port', 'loader_settings_file', 'clear_stale_mp'):
    setattr(r.rigports, n, noop)


def clean():
    for f in (RAN, GUARD, os.path.join(I, 'launch.json'), os.path.join(I, 'game.pid')):
        if os.path.exists(f):
            os.replace(f, f + '.prev')


def refused(fn):
    try:
        fn()
    except SystemExit as e:
        return str(e)
    return ''


# 1. the 18:48 path: rig / lrig start() of a window run (and the owner demo) with DSRIG_LIVE
os.environ['DSRIG_LIVE'] = '1'
for kw in ({'window': True}, {'window': True, 'show': 'left'}):
    clean()
    msg = refused(lambda: r.start(X, fps=30, **kw))
    check(f'start({kw}) with DSRIG_LIVE=1: refused with a clear error', 'refused' in msg and 'DSRIG_LIVE' in msg and '-batchmode' in msg, msg)
    check(f'start({kw}) with DSRIG_LIVE=1: nothing started', not os.path.exists(GUARD) and not os.path.exists(RAN))

# 3a. a batchmode start keeps DSRIG_LIVE (events_test.py, scan_rate_test.py)
clean()
msg = refused(lambda: r.start(X, fps=60))
spec_ = json.load(open(os.path.join(I, 'launch.json'))) if os.path.exists(os.path.join(I, 'launch.json')) else {}
check('start(batchmode) with DSRIG_LIVE=1: goes on, DSRIG_LIVE=1 and -batchmode in the launch',
      not msg and os.path.exists(GUARD) and spec_.get('env', {}).get('DSRIG_LIVE') == '1' and '-batchmode' in spec_.get('args', []), msg or spec_)
del os.environ['DSRIG_LIVE']

# 2. a test's own start (esc_test.py style): subprocess.Popen of the game binary
ENV = {'PATH': '/usr/bin:/bin', 'HOME': T}
for v in ('1', ''):
    clean()
    def run():
        subprocess.Popen([EXE, '-screen-width', '960', '-screen-height', '540'], env=dict(ENV, DSRIG_LIVE=v)).wait()
    msg = refused(run)
    check(f'own Popen, window, DSRIG_LIVE={v!r}: refused, the game did not run', 'DSRIG_LIVE' in msg and not os.path.exists(RAN), msg)

# 3b. what must still run
clean()
subprocess.Popen([EXE, '-batchmode'], env=dict(ENV, DSRIG_LIVE='1')).wait()
check('own Popen, -batchmode, DSRIG_LIVE=1: runs and the game sees DSRIG_LIVE=1',
      os.path.exists(RAN) and 'live=1 ' in open(RAN).read())
clean()
subprocess.Popen([EXE, '-screen-width', '960'], env=ENV).wait()
check('own Popen, window, no DSRIG_LIVE: runs', os.path.exists(RAN) and 'live=unset' in open(RAN).read())

ok = all(res)
print(f'RESULT live_window_guard {"PASS" if ok else "FAIL"} {sum(res)}/{len(res)} ({os.path.basename(path)})', flush=True)
sys.exit(0 if ok else 1)
