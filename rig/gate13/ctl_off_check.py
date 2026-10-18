#!/usr/bin/env python3
"""ctl_off_check.py [--loader-build DIR] [--win-dll DLL ...] [--no-mac]: the multiplayer test control channel is off in
a player's game (gate check, v0.1.15). One line at the end: "CTL OFF PASS" or "CTL OFF FAIL: <details>".

(a) Mac, dynamic: one hidden loader test copy (instance CTLOFF in ~/ds_launcher/loader/test_env/instances, an APFS clone
    of instances/Z) runs the release dsload.dylib + mods/multiplayer from DIR (default ~/ds_launcher/loader/build), with
    DSRIG_DIR set as the player app sets it and NO DSRIG_CTL (every DSRIG_* of this shell removed). Pass: the module
    started, its logs say "control socket off (no DSRIG_CTL)" and no ctl.sock exists 15 s later. The copy is killed by
    pid. Lease: one rig pair (rig.py lease ctloff), taken only while fewer than 2 pairs are leased; machine cap and load
    <= 40 respected (lrig caps). Never the owner's game, saves or screen.
(b) Windows, static: each DLL (default win/build/baselib.dll and win/build/mods/multiplayer/multiplayer.dll when built)
    has no "47811" default and has the "no DSRIG_PORT" off line; win/port/rig_sock_tcp.inc returns before listening
    when the port is empty; every rig_sock_start call in win/port and the generated sources passes the DSRIG_PORT value.
"""
import glob, json, os, re, shutil, signal, subprocess, sys, time

H = os.path.expanduser('~')
WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))      # this MP checkout
sys.path.insert(0, os.environ.get('LRIG_DIR', f'{H}/ds_launcher/loader/test')); import lrig            # noqa: E402
sys.path.insert(1, f'{H}/ds_multiplayer/rig'); import rig as realrig                                     # noqa: E402

X = 'CTLOFF'
FAILS = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def fail(why): FAILS.append(why); log('FAIL', why)


def arg(name, default=None, many=False):
    vals = [sys.argv[i + 1] for i, a in enumerate(sys.argv[:-1]) if a == name]
    return vals if many else (vals[-1] if vals else default)


def park(p):
    """never delete: set aside into old/ next to it"""
    if os.path.lexists(p):
        d = os.path.join(os.path.dirname(p), 'old'); os.makedirs(d, exist_ok=True)
        os.replace(p, os.path.join(d, f'{os.path.basename(p)}.{time.time():.0f}'))


def exe(): return os.path.join(lrig.idir(X), lrig.EXE_REL)


def make_copy():
    app = os.path.join(lrig.idir(X), 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.environ.get('CTL_SRC_APP', f'{H}/ds_launcher/loader/test_env/instances/Z/DungeonSettlers.app')
        if not os.path.exists(os.path.join(src, lrig.EXE_REL.split('/', 1)[1])):
            raise SystemExit(f'CTL OFF FAIL: no ready test copy to clone at {src} (make_instance.sh first)')
        os.makedirs(lrig.idir(X), exist_ok=True)
        subprocess.run(['cp', '-c', '-R', src, app], check=True)
        pl = os.path.join(app, 'Contents', 'Info.plist')
        for k, t, v in (('CFBundleIdentifier', '-string', f'com.CanOpener.Dungeon-Settlers.mp{X}'), ('CFBundleName', '-string', 'DS rig CTLOFF'),
                        ('LSBackgroundOnly', '-bool', 'YES'), ('LSUIElement', '-bool', 'YES')):
            subprocess.run(['plutil', '-replace', k, t, v, pl], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
        log('copy made from', src)
    lrig.require_background_only(X)
    for d in ('home', 'data/Saves', 'data/Mods'):
        os.makedirs(os.path.join(lrig.idir(X), d), exist_ok=True)


def lease():
    """one pair, only while fewer than 2 pairs are leased (waits 10 min at most)"""
    t0 = time.time()
    while time.time() - t0 < 600:
        n = len(realrig._with_leases(lambda d: dict(d)))
        ld = os.getloadavg()[0]
        if n < 2 and ld <= 40 and lrig.running_test_copies() + 1 <= lrig.CAP_COPIES:
            try:
                p = realrig.lease('ctloff', os.getpid())
                if p: return p
            except SystemExit:
                pass
        log(f'waiting: {n} pairs leased, load {ld:.0f}, {lrig.running_test_copies()} test copies'); time.sleep(15)
    raise SystemExit('CTL OFF FAIL: no room (pairs / load / copies) for 10 min')


def mac_check(build):
    for f in (os.path.join(build, 'dsload.dylib'), os.path.join(build, 'mods', 'multiplayer', 'multiplayer.dylib')):
        if not os.path.exists(f): return fail(f'missing {f}')
    make_copy()
    I = lrig.idir(X); k = os.path.join(I, 'kit')
    park(k)
    md = os.path.join(k, 'loader', 'mods'); os.makedirs(md); os.makedirs(os.path.join(k, 'mods'))
    shutil.copy2(os.path.join(build, 'dsload.dylib'), os.path.join(k, 'loader', 'dsload.dylib'))
    rel = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(build))), 'RELEASE')
    if os.path.exists(rel): shutil.copy2(rel, os.path.join(k, 'loader', 'RELEASE'))
    shutil.copytree(os.path.join(build, 'mods', 'multiplayer'), os.path.join(md, 'multiplayer'))
    lrig.prepare_data(X); lrig.require_windowed(X)
    mods = os.path.join(I, 'data', 'Mods')
    for f in glob.glob(os.path.join(mods, '*.json')): park(f)
    json.dump({'enabled': ['multiplayer'], 'order': ['multiplayer']}, open(os.path.join(mods, 'active.json'), 'w'))
    for f in ('rig.log', 'dsload.log', 'player.log', 'ctl.sock', 'stdout.log'): park(os.path.join(I, f))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_BATCH': '1', 'DSRIG_FPS': '30', 'DYLD_INSERT_LIBRARIES': os.path.join(k, 'loader', 'dsload.dylib'),
           'DSL_MODS_DIR': md, 'DSML_ROOT': k, 'DSML_USER_MODS_DIR': os.path.join(k, 'mods'), 'DSL_PROFILE': os.path.join(mods, 'active.json'),
           'DSML_SESSION': os.path.join(mods, 'worlds.json'), 'DSML_DATA': os.path.join(I, 'data'),
           'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSL_SOFT_NOACTIVATE': '1'}   # no DSRIG_CTL / DSRIG_PORT / DSRIG_*_DIRS (player)
    args = [exe(), '-batchmode', '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    log('started pid', p.pid)
    txt = lambda n: open(os.path.join(I, n), errors='replace').read() if os.path.exists(os.path.join(I, n)) else ''
    try:
        t0 = time.time(); seen = None
        while time.time() - t0 < 240 and p.poll() is None:
            if lrig.visible_windows(p.pid) or lrig.front_pid() == p.pid:
                fail('the copy showed a window / became frontmost: killed'); break
            if 'control socket off (no DSRIG_CTL)' in txt('rig.log') + txt('dsload.log'): seen = time.time(); break
            time.sleep(2)
        if seen:
            log(f'"control socket off (no DSRIG_CTL)" after {seen - t0:.0f} s'); time.sleep(15)
            started = re.search(r'multiplayer started \(control socket off\)', txt('dsload.log'))
            if not started: fail('dsload.log: no "multiplayer started (control socket off)" line')
            if 'listening' in txt('rig.log'): fail('rig.log has a "listening" line')
            if os.path.lexists(lrig.sockpath(X)): fail(f'{lrig.sockpath(X)} exists')
            if p.poll() is not None: fail(f'the copy quit early (exit {p.returncode})')
        elif not FAILS:
            fail(f'no "control socket off (no DSRIG_CTL)" line in 240 s (exit {p.poll()}); rig.log tail: {txt("rig.log")[-300:]!r}')
    finally:
        if p.poll() is None:
            os.kill(p.pid, signal.SIGTERM)
            for _ in range(50):
                if p.poll() is not None: break
                time.sleep(0.1)
            if p.poll() is None: os.kill(p.pid, signal.SIGKILL); p.wait()
        log('copy stopped (pid', p.pid, ')')


def win_check(dlls):
    tcp = open(os.path.join(WT, 'win/port/rig_sock_tcp.inc')).read()
    body = tcp[tcp.find('void rig_sock_start(const char *port)'):]
    g, c = body.find('if (!port || !*port)'), body.find('pthread_create')
    if body == tcp[:0] or g < 0 or c < 0 or g > c: fail('win/port/rig_sock_tcp.inc: no empty-port return before the listener thread')
    srcs = glob.glob(os.path.join(WT, 'win/port/*.c')) + glob.glob(os.path.join(WT, 'win/build/gen*/**/*.c'), recursive=True)
    for f in srcs:
        for m in re.finditer(r'rig_sock_start\(([^;{]*?)\)\s*;', open(f, errors='replace').read()):
            if m.group(1).strip() != 'port': fail(f'{os.path.relpath(f, WT)}: rig_sock_start({m.group(1)})')
    mw = open(os.path.join(WT, 'win/port/rig_main_win.c')).read()
    if 'const char *port = getenv("DSRIG_PORT");' not in mw: fail('rig_main_win.c: port is not getenv("DSRIG_PORT")')
    if not dlls: fail('no Windows DLL to check (win/build_win.sh first)')
    for d in dlls:
        b = open(d, 'rb').read()
        if b'47811' in b: fail(f'{d}: contains "47811"')
        if b'control socket off (no DSRIG_PORT)' not in b: fail(f'{d}: no "control socket off (no DSRIG_PORT)" line (old build?)')
        log('checked', d)


def main():
    dlls = arg('--win-dll', many=True) or [f for f in (os.path.join(WT, 'win/build/baselib.dll'), os.path.join(WT, 'win/build/mods/multiplayer/multiplayer.dll')) if os.path.exists(f)]
    win_check(dlls)
    if '--no-mac' not in sys.argv:
        pair = lease(); log('leased', pair)
        try:
            mac_check(os.path.abspath(os.path.expanduser(arg('--loader-build', f'{H}/ds_launcher/loader/build'))))
        finally:
            realrig.release(pair, 'ctloff')
    print('CTL OFF PASS' if not FAILS else 'CTL OFF FAIL: ' + '; '.join(FAILS), flush=True)
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
