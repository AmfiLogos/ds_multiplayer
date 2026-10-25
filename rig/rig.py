#!/usr/bin/env python3
"""Two-instance test rig for Dungeon Settlers (build 25846389).

  rig.py start A|B|both [--window] [--fps N]   start hidden copies (default: -batchmode, no window, 30 fps)
  rig.py stop  A|B|both               quit (socket), then kill by PID if still alive
  rig.py cmd   A|B '<json>'           send one command, print the answer
  rig.py status                       pid / alive / socket per instance, then per pair: lease holder + every process in its folders
  rig.py check-prefs                  owner's settings as at this process's snapshot? (a new process takes it: identical)
  rig.py smoke                        full smoke test (A alone, then A and B at once)
  rig.py lease <lane> [--pid N]       take a free pair (no lease AND no process in its folders); holder = N (default: the caller)
  rig.py release <pair> [lane]        stop the pair's copies, give it back (only its lane, or without lane its holder process)
  rig.py leases                       list leases
Instances A..J; "both" = A+B, a pair name (AB..IJ) = its two copies. Max 5 pairs at once.
  rig.py port <pair>                  host TCP port of the pair's run (rigports.py: leased with the pair, never fixed)
  rig.py smoke [PAIR]                 no PAIR: A alone then AB; with PAIR: both copies of that pair at once

Copies live in ~/ds_multiplayer/instances/<X>/ (built by make_instance.sh). Nothing of the real
install, saves or prefs is written. Copies run under nice 5 (no background QoS: it starved them on a loaded Mac), muted, 30 fps.
DSRIG_BG=1 in the environment of `rig.py start` restores nice 19 + background QoS.
A guard process per copy kills it (by PID) if it ever shows a visible window or becomes frontmost.
"""
import json, os, signal, socket, subprocess, sys, time, shutil, hashlib, glob, plistlib
if os.path.dirname(os.path.abspath(__file__)) not in sys.path: sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctl_token                                         # noqa: E402  test control channel token (first line)
import rigports                                          # noqa: E402  each run's own TCP port + the EXTRA PLAYER guard

ROOT = os.path.expanduser('~/ds_multiplayer')
INST = os.environ.get('DSRIG_INSTANCES') or os.path.join(ROOT, 'instances')   # kit_dryrun: a copy whose ../../build holds frozen det_ranges
DYLIB = os.path.join(ROOT, 'build', 'dsrig.dylib')
TOOLS = os.path.join(ROOT, 'rig', 'tools')
BACKUP = os.path.join(ROOT, 'backup_prefs_2026-10-12')
REAL_PLIST = os.path.expanduser('~/Library/Preferences/com.CanOpener.Dungeon-Settlers.plist')
REAL_DATA = os.path.expanduser('~/Library/Application Support/CanOpener/Dungeon Settlers')
SEED_DATA = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827')
EXE_REL = 'DungeonSettlers.app/Contents/MacOS/Dungeon Settlers'
HERE = os.path.dirname(os.path.abspath(__file__))
SHIM_SRC = os.path.join(HERE, 'hostport_shim.c')
SHIM = os.path.join(TOOLS, 'hostport_shim.dylib')


def idir(x): return os.path.join(INST, x)


def pair_port(x):
    """the TCP port of copy x's run (rigports.py, 2026-10-25): leased with x's pair lease (holder = the lease's holder),
    else by this process for the pair. Unique on the Mac, never 47777, never an old fixed pair port (stale copies of
    other lanes point at those: 2026-10-25 runs joined each other on 7891 / 7831)."""
    pair = next((p for p in PAIRS if x in p), x)
    def f(d):
        l = d.get(pair)
        p = rigports.port_for([idir(c) for c in pair], f'rig {pair} ' + (l['lane'] if l else os.path.basename(sys.argv[0])),
                              l.get('pid') if l else None)
        if l:
            l['port'] = p
        return p
    return _with_leases(f)


def hostport_shim():
    """test-only dylib (hostport_shim.c, built into rig/tools): a copy without the loader hosts from the menu on
    DSRIG_HOST_PORT instead of 47777 (the owner's game port). Refused if it cannot be built."""
    try:
        if not os.path.exists(SHIM) or os.path.getmtime(SHIM) < os.path.getmtime(SHIM_SRC):
            os.makedirs(TOOLS, exist_ok=True); tmp = f'{SHIM}.{os.getpid()}'
            subprocess.run(['cc', '-dynamiclib', '-O2', '-o', tmp, SHIM_SRC], check=True, capture_output=True)
            os.replace(tmp, SHIM)
        return SHIM
    except Exception as e:
        raise SystemExit(f'refused: the host-port shim does not build ({e}): a menu host would listen on 47777')
def pidfile(x): return os.path.join(idir(x), 'game.pid')
def sockpath(x): return os.path.join(idir(x), 'ctl.sock')


def ctl_env():
    """Test copies only: DSRIG_CTL=1 opens the control socket (<DSRIG_DIR>/ctl.sock; a player's game never has it), and
    the folders where test commands may write (screenshot / mpm_shot / ui_capture paths) or read (load_save file,
    plan_file) besides the copy's own folder: the test worktrees (~/ds_*), the scratch roots, the seed saves (read)."""
    h = os.path.expanduser('~')
    tmp = [os.environ.get('TMPDIR', ''), '/tmp']
    w = [os.path.join(h, 'ds_*')] + [t.rstrip('/') for t in tmp if t]
    r = w + [os.path.join(h, 'DungeonSettlers_SaveBackups')]
    return {'DSRIG_CTL': '1', 'DSRIG_WRITE_DIRS': ':'.join(w), 'DSRIG_READ_DIRS': ':'.join(r)}


def read_pid(x):
    try:
        return int(open(pidfile(x)).read().split()[0])
    except Exception:
        return None


def alive(pid):
    if not pid:
        return False
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    # make sure the PID is still OUR copy (never touch the owner's game)
    try:
        cmd = subprocess.run(['ps', '-o', 'command=', '-p', str(pid)], capture_output=True, text=True).stdout
    except Exception:
        return False
    return INST in cmd


def front_pid():
    return _shared('front', _WTTL, _front_pid)


def _front_pid():
    out = subprocess.run(['lsappinfo', 'info', subprocess.run(['lsappinfo', 'front'], capture_output=True, text=True).stdout.strip()],
                         capture_output=True, text=True).stdout
    for tok in out.split('\n'):
        if 'pid = ' in tok:
            try:
                return int(tok.split('pid = ')[1].split()[0])
            except Exception:
                pass
    return None


_WCACHE = '/tmp/dsrig_winlist_cache.json'   # 2026-10-14: ONE window list for all guards (WindowServer died under per-copy polling)
_WTTL = 2.5


def _shared(kind, ttl, fetch):
    """one system-wide value per ttl seconds, shared by every guard process through a locked file"""
    import fcntl
    path = f'{_WCACHE}.{kind}'
    with open(path + '.lock', 'a') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        try:
            if time.time() - os.path.getmtime(path) < ttl:
                with open(path) as f:
                    return json.load(f)
        except (OSError, ValueError):
            pass
        val = fetch()
        tmp = f'{path}.{os.getpid()}'
        with open(tmp, 'w') as f:
            json.dump(val, f)
        os.replace(tmp, path)
        return val


def _all_windows():
    out = subprocess.run([os.path.join(TOOLS, 'winlist')], capture_output=True, text=True).stdout
    try:
        return json.loads(out)
    except Exception:
        return []


def windows(pids):
    want = {int(p) for p in pids}
    return [w for w in _shared('win', _WTTL, _all_windows) if w.get('pid') in want]


def active_space():
    """managed space id of the desktop the user is looking at (SkyLight, read only)"""
    import ctypes
    sl = ctypes.CDLL('/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight')
    sl.SLSMainConnectionID.restype = ctypes.c_int
    sl.SLSGetActiveSpace.restype = ctypes.c_uint64; sl.SLSGetActiveSpace.argtypes = [ctypes.c_int]
    return sl.SLSGetActiveSpace(sl.SLSMainConnectionID())


def window_spaces(wid):
    """managed space ids a window belongs to (SkyLight, read only)"""
    import ctypes
    sl = ctypes.CDLL('/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight')
    cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    sl.SLSMainConnectionID.restype = ctypes.c_int
    sl.SLSCopySpacesForWindows.restype = ctypes.c_void_p; sl.SLSCopySpacesForWindows.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    cf.CFNumberCreate.restype = ctypes.c_void_p; cf.CFNumberCreate.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    cf.CFArrayCreate.restype = ctypes.c_void_p; cf.CFArrayCreate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_long, ctypes.c_void_p]
    cf.CFArrayGetCount.restype = ctypes.c_long; cf.CFArrayGetCount.argtypes = [ctypes.c_void_p]
    cf.CFArrayGetValueAtIndex.restype = ctypes.c_void_p; cf.CFArrayGetValueAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_long]
    cf.CFNumberGetValue.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    cb = ctypes.c_void_p.in_dll(cf, 'kCFTypeArrayCallBacks')
    v = ctypes.c_int(wid); n = cf.CFNumberCreate(None, 9, ctypes.byref(v))
    arr = (ctypes.c_void_p * 1)(n); a = cf.CFArrayCreate(None, arr, 1, ctypes.addressof(cb))
    r = sl.SLSCopySpacesForWindows(sl.SLSMainConnectionID(), 7, a); out = []
    for i in range(cf.CFArrayGetCount(r) if r else 0):
        x = ctypes.c_int64(); cf.CFNumberGetValue(cf.CFArrayGetValueAtIndex(r, i), 4, ctypes.byref(x)); out.append(x.value)
    return out


def visible_windows(pid):
    return [w for w in windows([pid]) if w['onscreen'] and w['alpha'] > 0.0]


# ------------------------------------------------------------------ owner data safety
def file_sha(p):
    try:
        return hashlib.sha256(open(p, 'rb').read()).hexdigest()
    except FileNotFoundError:
        return None


# The owner's own settings: his prefs domains (stock + the app's .Modded game) and the stock UserSetting.json. A test run
# takes a snapshot of them at its start (first check_prefs() or start() of the process, only while his game is closed) in
# rig/prefs_snap/<time>-<pid>/ and compares at its end. Until 2026-10-24 the guard compared with a fixed 10-11 backup and
# put it back over every setting he had changed in his game since. A test copy with his bundle id (dmg_start: .Modded)
# writes his real domain: CFFIXED_USER_HOME does not move preferences. Unity's per-start counters are not settings.
# Test: prefs_guard_test.py in ds_multiplayer rig/ (path of this file as argument): DSRIG_PREFS_TEST_ROOT = a folder for
# fake owner files + the snapshot folders, DSRIG_PREFS_FAKE_GAME = 1 / 0 (his game open / closed).
PREFS_TEST = os.environ.get('DSRIG_PREFS_TEST_ROOT')
OWNER_DATA = os.path.join(PREFS_TEST, 'owner') if PREFS_TEST else REAL_DATA
OWNER_DOMAINS = [os.path.join(OWNER_DATA, d) if PREFS_TEST else d                 # a plist path is a domain for `defaults`
                 for d in ('com.CanOpener.Dungeon-Settlers', 'com.CanOpener.Dungeon-Settlers.Modded')]
PREFS_SNAP = os.path.join(PREFS_TEST or os.path.join(ROOT, 'rig'), 'prefs_snap')
PREFS_KEPT = os.path.join(PREFS_TEST or os.path.join(ROOT, 'rig'), 'prefs_overwritten')   # kept for good, never ToDelete
SESSION_KEYS = ('unity.player_session_count', 'unity.player_sessionid')
_SNAP = {}                                                 # this process's run snapshot: dir, t


def _owner_items():
    """(name in a snapshot, domain or None, file or None) per owner settings file"""
    return [(os.path.basename(d) + '.plist', d, None) for d in OWNER_DOMAINS] + [('UserSetting.json', None, os.path.join(OWNER_DATA, 'UserSetting.json'))]


def _read_domain(dom):
    r = subprocess.run(['defaults', 'export', dom, '-'], capture_output=True)           # what cfprefsd holds ({} if none)
    return plistlib.loads(r.stdout) if r.returncode == 0 and r.stdout else {}


def _values(name, dom, f, snap=None):
    """the item now (snap None) or in a snapshot: domain values without Unity's counters, or the file's bytes; None = no file"""
    p = os.path.join(snap, name) if snap else f
    try:
        if dom:
            d = plistlib.load(open(p, 'rb')) if snap else _read_domain(dom)
            return {k: v for k, v in d.items() if k not in SESSION_KEYS}
        return open(p, 'rb').read()
    except FileNotFoundError:
        return None


def _diff(old, new, n=20):
    """'key: old -> new' lines for a person (domain keys, UserSetting.json keys as Section.Key)"""
    def flat(v, p=''):
        if isinstance(v, bytes):
            try:
                v = json.loads(v)
            except ValueError:
                return {p or 'contents': hashlib.sha256(v).hexdigest()[:12]}
        if isinstance(v, dict):
            return {k2: v2 for k, x in v.items() for k2, v2 in flat(x, f'{p}.{k}' if p else str(k)).items()}
        return {p: v}
    a, b, no = flat(old) if old is not None else {}, flat(new) if new is not None else {}, '(none)'
    out = [f'{k}: {str(a.get(k, no))[:60]} -> {str(b.get(k, no))[:60]}' for k in sorted(set(a) | set(b)) if a.get(k, no) != b.get(k, no)]
    return out[:n] + ([f'... {len(out) - n} more'] if len(out) > n else [])


def prefs_snapshot():
    """this run's snapshot folder of the owner's settings, taken once and only while his game is closed; None while it is open"""
    if _SNAP:
        return _SNAP['dir']
    if owner_game_running():
        return None
    t = time.time(); d = os.path.join(PREFS_SNAP, time.strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}'); os.makedirs(d, exist_ok=True)
    for name, dom, f in _owner_items():
        if dom:
            subprocess.run(['defaults', 'export', dom, os.path.join(d, name)], check=True)
        elif os.path.exists(f):
            shutil.copy2(f, os.path.join(d, name))
    _SNAP.update(dir=d, t=t)
    print(f'owner prefs: run snapshot {d}', file=sys.stderr)
    return d


def _keep_overwritten(name, dom, f):
    """copy of the owner's current file before the snapshot goes over it, + one line in prefs_overwritten/overwritten.log"""
    if not dom and not os.path.exists(f):
        return '(no file to keep)'
    os.makedirs(PREFS_KEPT, exist_ok=True); ts = time.strftime('%Y%m%d-%H%M%S')
    dst = os.path.join(PREFS_KEPT, f'{ts}-{name}')
    if os.path.exists(dst):
        dst = os.path.join(PREFS_KEPT, f'{ts}-{os.getpid()}-{name}')
    if dom:
        subprocess.run(['defaults', 'export', dom, dst], check=True)
    else:
        shutil.copy2(f, dst)
    with open(os.path.join(PREFS_KEPT, 'overwritten.log'), 'a') as lg:
        lg.write(f'{time.strftime("%Y-%m-%d %H:%M:%S")} pid {os.getpid()} {os.path.basename(sys.argv[0])}: {dom or f} differed from '
                 f'run snapshot {_SNAP["dir"]}: kept as {dst}, snapshot put back\n')
    return dst


def _put_back(name, dom, f, snap):
    src = os.path.join(snap, name)
    if dom:
        for k in set(_read_domain(dom)) - set(plistlib.load(open(src, 'rb'))):      # `defaults import` merges: a key the
            subprocess.run(['defaults', 'delete', dom, k], capture_output=True)      # test added would stay
        subprocess.run(['defaults', 'import', dom, src], check=True)
    else:
        shutil.copy2(src, f + '.rigtmp'); os.replace(f + '.rigtmp', f)


def check_prefs(restore=True):
    """True if the owner's settings are as at the start of this test run (the first call takes the run's snapshot). A difference
    while his game stayed closed came from the test: the snapshot is put back (restore=True), the file it replaces is kept in
    prefs_overwritten/. A difference after his game ran is his own: printed for a person, never put back."""
    snap = _SNAP.get('dir')
    if not snap:
        if not prefs_snapshot():
            print('owner prefs: his game is open: no snapshot, nothing compared', file=sys.stderr)
        return True
    ran = owner_game_ran_since(_SNAP['t'])
    ok = True
    for name, dom, f in _owner_items():
        old, new = _values(name, dom, f, snap), _values(name, dom, f)
        if old == new:
            continue
        ok = False
        print(f'PREFS CHANGED since the run snapshot {snap}: {dom or f}')
        for line in _diff(old, new):
            print('  ' + line)
        if not restore:
            continue
        if ran:
            print('  his game ran during this run: he may have changed it himself; NOT restored, check by hand')
        elif old is None:
            print('  not in the run snapshot: NOT restored, check by hand')
        else:
            kept = _keep_overwritten(name, dom, f); _put_back(name, dom, f, snap)
            print(f'  restored from the run snapshot (the replaced one is kept: {kept}): now identical = {_values(name, dom, f) == old}')
    return ok


def owner_game_running():
    """his stock game (Steam) or his modded game (the app's game copy) runs"""
    if PREFS_TEST:
        return os.environ.get('DSRIG_PREFS_FAKE_GAME') == '1'
    return subprocess.run(['pgrep', '-f', 'steamapps/common/Dungeon Settlers/|DSModLauncher/games/'], capture_output=True).returncode == 0


def owner_game_ran_since(t):
    """his game runs, or ran since t: while it runs it writes these files in its data folder (stock, or the app's profiles)"""
    if owner_game_running():
        return True
    dirs = [OWNER_DATA] + ([] if PREFS_TEST else glob.glob(os.path.expanduser('~/Library/Application Support/DSModLauncher/profiles/*/data')))
    return any(os.path.exists(p) and os.path.getmtime(p) >= t
               for d in dirs for p in (os.path.join(d, n) for n in ('ErrorLog.txt', 'eventcache', 'SaveHeaderCache.json', 'UserCampaignLog.json')))


# ------------------------------------------------------------------ instance data
def fix_mp_port(x):
    """an mp.json from before this run is set aside (another lane's: 2026-10-25 copy T joined a pair on 7831); this
    run's mp.json always uses the run's port (pair_port), whatever the lane wrote"""
    rigports.clear_stale_mp(idir(x))
    p = os.path.join(idir(x), 'mp.json')
    if not os.path.exists(p):
        return
    try:
        d = json.load(open(p))
    except Exception:
        return
    if d.get('port') != pair_port(x):
        print(f'{x}: mp.json port {d.get("port")} -> {pair_port(x)} (pair port)')
        d['port'] = pair_port(x)
        json.dump(d, open(p + '.tmp', 'w')); os.replace(p + '.tmp', p)


def prepare_data(x):
    d = os.path.join(idir(x), 'data')
    os.makedirs(os.path.join(d, 'Saves'), exist_ok=True)
    os.makedirs(os.path.join(idir(x), 'home'), exist_ok=True)
    us = os.path.join(d, 'UserSetting.json')
    if True:                                                   # regenerated every start (our own file)
        s = json.load(open(os.path.join(BACKUP, 'UserSetting.json')))
        snd = s['SoundSettingData']
        for k in list(snd):
            if k.endswith('Percent'):
                snd[k] = 0.0
            if k.endswith('Mute'):
                snd[k] = True
        g = s['GraphicSettingData']
        g['VSyncEnabled'] = False
        for a, b in (('ResolutionWidth', 'ResolutionHeight'), ('WindowedResolutionWidth', 'WindowedResolutionHeight'),
                     ('FullScreenResolutionWidth', 'FullScreenResolutionHeight')):
            g[a], g[b] = 960, 540
        g['SaveCurrentScreenType'] = 1
        s['GeneralSettingData']['IsKeyboardEnvironmentNoticeAcknowledged'] = True
        json.dump(s, open(us, 'w'), indent=1)
    og = os.path.join(d, 'OutGame.json')
    if not os.path.exists(og) and os.path.exists(os.path.join(SEED_DATA, 'OutGame.json')):
        shutil.copy2(os.path.join(SEED_DATA, 'OutGame.json'), og)
    # copy's own prefs domain: small window, windowed (Unity reads these before any game code)
    dom = f'com.CanOpener.Dungeon-Settlers.mp{x}'
    for k, v in (('Screenmanager Fullscreen mode', 3), ('Screenmanager Resolution Width', 960),
                 ('Screenmanager Resolution Height', 540), ('Screenmanager Resolution Use Native', 0)):
        subprocess.run(['defaults', 'write', dom, k, '-int', str(v)])
    # a copy that would still open full screen never starts (a full-screen test copy switched the owner's Space, 2026-10-16)
    m = subprocess.run(['defaults', 'read', dom, 'Screenmanager Fullscreen mode'], capture_output=True, text=True).stdout.strip()
    if m in ('0', '1'):
        raise SystemExit(f'refused: test copy {x} ({dom}) would open full screen (Screenmanager Fullscreen mode {m})')


# ------------------------------------------------------------------ start / stop
# ---- cap on test copies (2026-10-17 22:10: every app on the Mac froze at load ~230, swap filled the disk). A new test
# copy starts only while fewer than 6 test copies run (a 4-player game + 2), the 1-minute load is at most 40 and swap
# use at most 20 GB; else it waits (10 min at most), then refuses. Test copy = a running "Dungeon Settlers" whose app is
# background-only (LSBackgroundOnly, make_instance.sh): the owner's own game never counts and is never held. The
# loader checks the same in the copy itself (dsl_start_mac.c). Tests: DSRIG_CAP_FAKE_COUNT / _LOAD / _SWAP_GB, DSRIG_CAP_WAIT (s).
CAP_COPIES, CAP_LOAD, CAP_SWAP_GB = 6, 40.0, 20.0


def _cap_env(k, f):
    v = os.environ.get(k)
    return f(v) if v else None


def running_test_copies():
    n = _cap_env('DSRIG_CAP_FAKE_COUNT', int)
    if n is not None:
        return n
    import ctypes, plistlib
    lp = ctypes.CDLL('/usr/lib/libproc.dylib'); buf = ctypes.create_string_buffer(4096); n = 0
    for pid in subprocess.run(['pgrep', '-x', 'Dungeon Settlers'], capture_output=True, text=True).stdout.split():
        if lp.proc_pidpath(int(pid), buf, 4096) <= 0:
            continue
        exe = buf.value.decode(errors='replace'); i = exe.find('/Contents/MacOS/')
        try:
            n += i > 0 and plistlib.load(open(exe[:i] + '/Contents/Info.plist', 'rb')).get('LSBackgroundOnly') in (True, 1, '1')
        except Exception:
            pass
    return n


def swap_gb():
    v = _cap_env('DSRIG_CAP_FAKE_SWAP_GB', float)
    if v is not None:
        return v
    t = subprocess.run(['sysctl', '-n', 'vm.swapusage'], capture_output=True, text=True).stdout   # "total = .. used = 2827.75M .."
    u = t.split('used = ')[1].split()[0] if 'used = ' in t else '0M'
    return float(u[:-1]) / (1 if u[-1] == 'G' else 1024)


def cap_wait(what):
    """before a new test copy starts: wait while the Mac is at the cap (10 min at most), then refuse (SystemExit)"""
    limit = _cap_env('DSRIG_CAP_WAIT', float)
    limit = 600.0 if limit is None else limit
    t0 = time.time(); said = 0
    while True:
        n, sw = running_test_copies(), swap_gb()
        ld = _cap_env('DSRIG_CAP_FAKE_LOAD', float)
        ld = os.getloadavg()[0] if ld is None else ld
        why = [s for s, bad in ((f'{n} test copies run (cap {CAP_COPIES})', n >= CAP_COPIES),
                                (f'1-minute load {ld:.0f} (cap {CAP_LOAD:.0f})', ld > CAP_LOAD),
                                (f'swap {sw:.1f} GB used (cap {CAP_SWAP_GB:.0f})', sw > CAP_SWAP_GB)) if bad]
        if not why:
            return
        if time.time() - t0 >= limit:
            raise SystemExit(f'{what}: refused, the Mac stayed at the test copy cap for ' + (f'{limit / 60:.0f} min' if limit >= 60 else f'{limit:.0f} s') + ': ' + '; '.join(why))
        if time.time() - said >= 60:
            print(f'{what}: waiting (test copy cap): ' + '; '.join(why), flush=True); said = time.time()
        time.sleep(max(0.5, min(15, limit - (time.time() - t0))))


EXTRA_ENV = {}                                                 # copy letter -> {name: value} added to its start env (tests)


def start(x, window=False, fps=30, show=None):
    """show='left'|'right': VISIBLE window on that half of the screen (owner demo only; tests never pass it)."""
    if alive(read_pid(x)):
        print(f'{x}: already running (pid {read_pid(x)})'); return read_pid(x)
    prefs_snapshot()                                           # the run's snapshot of the owner's settings, before its first copy
    cap_wait(f'{x}: start')
    exe = os.path.join(idir(x), EXE_REL)
    if not os.path.exists(exe):
        subprocess.run([os.path.join(ROOT, 'rig', 'make_instance.sh'), x], check=True)
    prepare_data(x)
    fix_mp_port(x)
    port = pair_port(x)
    rigports.check_start(idir(x))                              # GUARD: mp.json at a port this run does not own = refused
    shim = hostport_shim()
    rigports.note_started(idir(x), x)
    for p in (sockpath(x),):
        if os.path.exists(p):
            os.unlink(p)
    front_before = front_pid()
    env = {'HOME': os.path.join(idir(x), 'home'), 'CFFIXED_USER_HOME': os.path.join(idir(x), 'home'),
           'USER': os.environ.get('USER', ''), 'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin',
           'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': idir(x), 'DSRIG_FPS': str(fps),
           'DYLD_INSERT_LIBRARIES': DYLIB + ':' + shim, 'DSRIG_HOST_PORT': str(port)}   # menu host: the run's port, never 47777
    env.update(ctl_env())                                      # test copy: control socket on (test tools)
    env.update({k: v for k, v in os.environ.items() if k.startswith('DSRIG_') and k not in env})   # DSRIG_DIAG, DSRIG_DETAIL_SYNC...
    env.update(EXTRA_ENV.get(x, {}))                           # a test's own variables for this copy (e.g. DSML_GAME_SAVES = a test folder)
    args = [exe, '-logFile', os.path.join(idir(x), 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', '960', '-screen-height', '540']
    if window and show:
        env['DSRIG_SHOWWIN'] = show                            # demo: visible, placed on one half of the screen
    elif window:
        env['DSRIG_HIDEWIN'] = '1'
    else:
        env['DSRIG_BATCH'] = '1'
        args.append('-batchmode')
    # The guard process is the game's parent: it lowers the priority, execs the game, watches its
    # windows / front state for its whole life and logs how it ended (exit code or signal).
    spec = json.dumps({'args': args, 'env': env, 'front_before': front_before or 0, 'bg': bool(os.environ.get('DSRIG_BG')), 'show': bool(window and show)})
    open(os.path.join(idir(x), 'launch.json'), 'w').write(spec)
    open(pidfile(x), 'w').write('')
    g = subprocess.Popen([sys.executable, os.path.abspath(__file__), '_guard', x],
                         stdin=subprocess.DEVNULL, stdout=open(os.path.join(idir(x), 'guard.log'), 'ab'),
                         stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(100):
        pid = read_pid(x)
        if pid and alive(pid):
            break
        time.sleep(0.1)
    print(f'{x}: started pid {read_pid(x)} ({"hidden window" if window else "batchmode"}), guard pid {g.pid}')
    return read_pid(x)


def guard(x):
    """Parent of the game copy. Kills it at once (by PID) if it shows a visible window or becomes frontmost."""
    spec = json.load(open(os.path.join(idir(x), 'launch.json')))
    out = open(os.path.join(idir(x), 'stdout.log'), 'ab')

    # nice/taskpolicy are SIP-protected binaries: exec'ing through them strips DYLD_INSERT_LIBRARIES.
    # So lower the priority in the child before exec instead. Default nice 5 (owner OK 2026-10-12);
    # DSRIG_BG=1 = nice 19 + PRIO_DARWIN_BG (what taskpolicy -b does).
    bg = spec.get('bg')
    def lower():
        import ctypes
        try:
            os.nice(19 if bg else 5)
        except OSError:
            pass                                              # already at nice 19 (driver run under nice): EPERM is harmless
        if bg:
            ctypes.CDLL(None).setpriority(4, 0, 0x1000)      # PRIO_DARWIN_PROCESS, self, PRIO_DARWIN_BG
    p = subprocess.Popen(spec['args'], env=spec['env'], stdin=subprocess.DEVNULL, preexec_fn=lower,
                         stdout=out, stderr=out, cwd=idir(x))
    open(pidfile(x), 'w').write(f'{p.pid}\n')
    log = lambda m: print(f'{time.strftime("%H:%M:%S")} {x} pid {p.pid}: {m}', flush=True)
    log('started')
    t0 = time.time()
    while p.poll() is None:
        vis = visible_windows(p.pid)
        fp = front_pid()
        target = int(spec['env'].get('DSRIG_SPACE', 0) or 0)
        bad = vis or fp == p.pid
        if spec.get('show'):
            # demo on another desktop: a window counts only if it belongs to the space the user is looking at
            act = active_space()
            vis = [dict(w, spaces=window_spaces(w['id'])) for w in windows([p.pid]) if w['alpha'] > 0]
            vis = [w for w in vis if act in w['spaces'] or (w['onscreen'] and not w['spaces'])]
            bad = (bool(vis) or fp == p.pid) and (not target or act != target)
        if bad:
            os.kill(p.pid, signal.SIGKILL)
            log(f'GUARD KILLED: visible={vis} front_pid={fp}')
            break
        time.sleep(0.5 if time.time() - t0 < 60 else 3.0)        # each check = 3 helper processes + a full WindowServer window list; 2026-10-14 WindowServer died under ~290 checks/min
    rc = p.wait()
    log(f'ended, return code {rc} ({"signal " + str(-rc) if rc < 0 else "exit"}); no visible window seen before that')


def stop(x):
    pid = read_pid(x)
    if not alive(pid):
        print(f'{x}: not running'); return
    try:
        rigports.end_check(send, x, idir(x))                   # EXTRA PLAYER: more players in its session than this run's copies
    except Exception as e:
        print(f'{x}: stop check failed ({e})')
    try:
        send(x, {'cmd': 'quit'}, timeout=5)
    except Exception as e:
        print(f'{x}: quit over socket failed ({e})')
    for _ in range(50):
        if not alive(pid):
            break
        time.sleep(0.1)
    if alive(pid):
        os.kill(pid, signal.SIGKILL)
        print(f'{x}: killed pid {pid}')
    else:
        print(f'{x}: stopped')


# ------------------------------------------------------------------ socket
def send(x, obj, timeout=90, connect_wait=60):
    """connect_wait: a copy that is still starting (socket file missing or not listening yet) is retried for up to
    this many seconds, as long as its process lives; nothing was sent yet, so a retry is safe."""
    t0 = time.time()
    while True:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        try:
            s.connect(os.path.realpath(sockpath(x))); break
        except (ConnectionRefusedError, FileNotFoundError):
            s.close()
            if time.time() - t0 > connect_wait or not alive(read_pid(x)):
                raise
            time.sleep(0.5)
    s.sendall(ctl_token.token_line(idir(x)) + (json.dumps(obj) + '\n').encode())   # token first (rig/ctl_token.py)
    buf = b''
    while not buf.endswith(b'\n'):
        k = s.recv(1 << 20)
        if not k:
            break
        buf += k
    s.close()
    return json.loads(buf.decode())


def wait_for(x, pred, what, timeout=240, every=1.0):
    t0 = time.time(); last = None
    while time.time() - t0 < timeout:
        if not alive(read_pid(x)):
            raise RuntimeError(f'{x}: process died while waiting for {what}')
        try:
            last = send(x, {'cmd': 'status'}, timeout=30)
            if pred(last):
                return last
        except (FileNotFoundError, ConnectionRefusedError, socket.timeout, json.JSONDecodeError):
            pass
        time.sleep(every)
    raise RuntimeError(f'{x}: timeout waiting for {what}; last status {last}')


# ------------------------------------------------------------------ smoke test
SAVE = os.path.join(SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')


def smoke_one(x, out):
    log = lambda *a: print(f'[{x}]', *a, flush=True)
    wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title screen')
    log('title reached:', send(x, {'cmd': 'status'}))
    work = os.path.join(idir(x), 'save_in')
    os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(SAVE))
    shutil.copy2(SAVE, src)                                  # a copy; the backup itself is never opened for writing
    log('load_save:', send(x, {'cmd': 'load_save', 'file': src}))
    st = wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('map', -1) >= 0 and s.get('tick', -1) > 0,
                  'campaign loaded', timeout=400, every=2)
    log('loaded:', st)
    send(x, {'cmd': 'set_speed', 'speed': 1})
    time.sleep(3)
    u = send(x, {'cmd': 'list_units'})
    players = [v for v in u.get('units', []) if v.get('faction') == 1 and not v.get('dead') and 'cell' in v]
    log(f'units: {len(u.get("units", []))}, player alive: {len(players)}')
    for v in players[:4]:
        log('  ', v)
    assert players, 'no player unit found'
    tgt = players[0]
    cx, cy = tgt['cell'][0] + 3, tgt['cell'][1]
    log('order_move:', send(x, {'cmd': 'order_move', 'guid': tgt['guid'], 'x': cx, 'y': cy}))
    time.sleep(4)
    moved = [v for v in send(x, {'cmd': 'list_units'})['units'] if v['guid'] == tgt['guid']][0]
    log('after move:', {k: moved.get(k) for k in ('cell', 'x', 'y', 'task')})
    f1 = send(x, {'cmd': 'fingerprint'}); time.sleep(1); f2 = send(x, {'cmd': 'fingerprint'})
    log('fingerprint 1:', f1); log('fingerprint 2:', f2)
    shot = os.path.join(out, f'{x}_dungeon.png')
    log('screenshot:', send(x, {'cmd': 'screenshot', 'path': shot, 'w': 960, 'h': 540}))
    res = dict(start_cell=tgt['cell'], target=[cx, cy], end_cell=moved.get('cell'), task=moved.get('task'),
               fp_tick_advanced=f2['tick'] > f1['tick'], fp_differs=f1['hash'] != f2['hash'], screenshot=shot)
    log('RESULT', res)
    return res


def smoke_pair(pair):
    import threading
    out = os.path.join(ROOT, 'rig', 'smoke_out', pair); os.makedirs(out, exist_ok=True)
    assert check_prefs(), 'owner prefs differ from backup before start'
    res = {}
    try:
        for x in pair:
            start(x)
        th = [threading.Thread(target=lambda k=k: res.__setitem__(k, smoke_one(k, out))) for k in pair]
        [t.start() for t in th]; [t.join() for t in th]
    finally:
        for x in pair:
            stop(x)
    print('prefs identical after', pair, check_prefs())
    json.dump(res, open(os.path.join(out, 'results.json'), 'w'), indent=1)
    print(json.dumps(res, indent=1))


def smoke(pair=None):
    """pair: two instance letters (default AB, or env RIG_PAIR)"""
    X, Y = (pair or os.environ.get('RIG_PAIR') or 'AB')[:2]
    for z in (X, Y):                    # smoke is single-player: an mp.json left by a lane would start a session (det armed)
        mj = os.path.join(idir(z), 'mp.json')
        if os.path.exists(mj):
            os.replace(mj, mj + '.smoke_aside'); print(f'{z}: mp.json set aside for the smoke run')
    out = os.path.join(ROOT, 'rig', 'smoke_out'); os.makedirs(out, exist_ok=True)
    assert check_prefs(), 'owner prefs differ from backup before start'
    results = {}
    try:
        start(X)
        results['A_alone'] = smoke_one(X, out)
    finally:
        stop(X)
    print('prefs identical after A alone:', check_prefs())
    import threading
    try:
        start(X); start(Y)
        res = {}
        th = [threading.Thread(target=lambda k=k: res.__setitem__(k, smoke_one(k, out + '/pair'))) for k in (X, Y)]
        os.makedirs(out + '/pair', exist_ok=True)
        [t.start() for t in th]; [t.join() for t in th]
        results['pair'] = res
    finally:
        stop(X); stop(Y)
    print('prefs identical after pair:', check_prefs())
    json.dump(results, open(os.path.join(out, 'results.json'), 'w'), indent=1)
    print(json.dumps(results, indent=1))

# ------------------------------------------------------------------ pair leases
PAIRS = ['AB', 'CD', 'EF', 'GH', 'IJ']
class _PairPorts(dict):
    """PAIR_PORTS[pair] / .get(pair): the pair's run port (pair_port). Until 2026-10-25 fixed numbers (AB 7791 .. IJ 7871)
    that stale copies of other lanes kept pointing at."""
    def __getitem__(self, pair):
        return pair_port(pair[0])

    def get(self, pair, default=None):
        return pair_port(pair[0]) if pair else default

    def __contains__(self, pair):
        return pair in PAIRS


PAIR_PORTS = _PairPorts()                                    # host TCP port per pair (net tests)
LDIR = os.environ.get('DSRIG_LEASE_DIR') or os.path.join(ROOT, 'rig')    # tests: a temp folder (never the real leases)
LEASES = os.path.join(LDIR, 'leases.json')
LEASE_LOCK = os.path.join(LDIR, 'leases.lock')
STALE_S = 2 * 3600


def _with_leases(fn):
    """Run fn(leases) under an exclusive file lock; leases is saved afterwards."""
    import fcntl
    with open(LEASE_LOCK, 'a') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        try:
            d = json.load(open(LEASES))
        except Exception:
            d = {'AB': {'lane': 'determinism', 'pid': None, 'since': time.time(), 'used': time.time()}}  # in use before leases existed
        r = fn(d)
        tmp = LEASES + '.tmp'
        json.dump(d, open(tmp, 'w'), indent=1); os.replace(tmp, LEASES)
        return r


def _pid_alive(pid):
    try:
        os.kill(pid, 0); return True
    except (OSError, TypeError):
        return False


def _free_stale(d):
    now = time.time()
    for pair, l in list(d.items()):
        used = max(l.get('used', 0), *[_inst_used(x) for x in pair])
        # 2026-10-17: a lease whose holder process died is reclaimed only after its copies are killed (they
        # kept running and corrupted the next lane's runs); 10 min grace after the lease for slow starters
        if l.get('pid') and not _pid_alive(l['pid']) and now - l.get('since', 0) > 600:
            if _kill_copies(pair, f'holder {l["lane"]} pid {l["pid"]} dead'):
                _free_log(f'{pair} {l["lane"]} pid={l["pid"]}: holder dead; copies stopped; freed')
                print(f'freed lease {pair} of {l["lane"]} (holder pid {l["pid"]} dead, its copies stopped)', file=sys.stderr)
                _drop_port(d.pop(pair))
            else:
                _free_log(f'{pair} {l["lane"]} pid={l["pid"]}: holder dead; KEPT, processes survived SIGKILL')
            continue
        if now - used > STALE_S:
            # 2026-10-16: never free a pair while any process runs from its instances/<X>/ folder (a copy started
            # without a pid file, its guard, a test's helper); every auto-free is logged in rig/lease_free.log
            procs = [q for x in pair for q in _procs_in(x)]
            why = f'unused {int(now - used)} s'
            _free_log(f'{pair} {l["lane"]} pid={l.get("pid")}: {why}; ' + (f'KEPT, processes in its folders: {procs[:6]}' if procs else 'freed'))
            if procs:
                continue
            print(f'freed stale lease {pair} ({l["lane"]})', file=sys.stderr)
            _drop_port(d.pop(pair))


def _drop_port(l):
    """the run port of a pair lease that ends goes back (rigports; it then rests 6 h)"""
    if l and l.get('port'):
        rigports.release(l['port'])


def _ancestors():
    out, p = {os.getpid()}, os.getpid()
    while p > 1:
        r = subprocess.run(['ps', '-o', 'ppid=', '-p', str(p)], capture_output=True, text=True).stdout.strip()
        if not r.isdigit():
            break
        p = int(r); out.add(p)
    return out


def _procs_all():
    """{copy: pids} of every process running from instances/<x>/: its command line (pgrep -f) or any open file,
    cwd or executable in that folder (lsof; instances/ is a link, both spellings count). This process and its
    parents never count. 2026-10-17: lease files alone gave pairs whose copies still ran (AB twice in one night)."""
    roots = sorted({INST, os.path.realpath(INST)}, key=len, reverse=True)
    res = {x: set() for x in 'ABCDEFGHIJ'}
    for x in res:
        for r in roots:
            out = subprocess.run(['pgrep', '-f', os.path.join(r, x) + '/'], capture_output=True, text=True).stdout
            res[x].update(int(q) for q in out.split())
    lo = subprocess.run(['lsof', '-nP', '-w', '-Fpn'], capture_output=True, text=True).stdout
    pid = None
    for l in lo.split('\n'):
        if l.startswith('p'):
            pid = int(l[1:])
        elif l.startswith('n') and pid is not None:
            for r in roots:
                if l.startswith('n' + r + '/'):
                    x = l[len(r) + 2:].split('/', 1)[0]
                    if x in res:
                        res[x].add(pid)
                    break
    me = _ancestors()
    return {x: sorted(v - me) for x, v in res.items()}


def _procs_in(x, allp=None):
    """pids of every process running from instances/<x>/ (see _procs_all)"""
    return (allp or _procs_all())[x]


def _kill_copies(pair, why):
    """stop the pair's copies (quit, then SIGKILL) and every other process in their folders; True = none left"""
    for x in pair:
        if alive(read_pid(x)):
            try: stop(x)
            except Exception as e: print(f'{x}: stop failed ({e})', file=sys.stderr)
    for sig, pause in ((signal.SIGTERM, 5), (signal.SIGKILL, 2)):
        allp = _procs_all(); left = [q for x in pair for q in allp[x]]
        if not left:
            break
        _free_log(f'{pair}: {why}: {"TERM" if sig == signal.SIGTERM else "KILL"} {left[:8]}')
        for q in left:
            try: os.kill(q, sig)
            except OSError: pass
        t = time.time() + pause
        while time.time() < t and any(_pid_alive(q) for q in left):
            time.sleep(0.2)
    allp = _procs_all()
    return not any(allp[x] for x in pair)


def _free_log(msg):
    try:
        with open(os.path.join(LDIR, 'lease_free.log'), 'a') as f:
            f.write(time.strftime('%Y-%m-%d %H:%M:%S ') + msg + '\n')
    except OSError:
        pass


def _inst_used(x):
    """Last activity of a copy: socket / pid file / guard log mtime, or now if the copy runs."""
    if alive(read_pid(x)):
        return time.time()
    ts = [os.path.getmtime(p) for p in (sockpath(x), pidfile(x), os.path.join(idir(x), 'guard.log')) if os.path.exists(p)]
    return max(ts) if ts else 0


def lease(lane, pid=None):
    pid = pid or os.getpid()                              # 2026-10-17: the holder is always recorded (default: the caller)
    def take(d):
        _free_stale(d)
        # a lane gets its own pair back only for the SAME holder (its pid, or a parent of the caller). 2026-10-17:
        # two runs of one lane name (pvp_v1_pair.py "fepvp", merger13 + netlimits) shared AB at the same time
        me = _ancestors() | {pid}
        for pair, l in list(d.items()):
            if l['lane'] != lane:
                continue
            if l.get('pid') in me:
                l['used'] = time.time(); return pair
            if l.get('pid') and not _pid_alive(l['pid']):     # same lane, holder gone: stop its copies, then hand over
                if not _kill_copies(pair, f'holder {lane} pid {l["pid"]} dead, new holder pid {pid}'):
                    continue
                _free_log(f'{pair} {lane} pid={l["pid"]}: holder dead; copies stopped; given to pid {pid}')
                d[pair] = {'lane': lane, 'pid': pid, 'since': time.time(), 'used': time.time()}
                return pair
        if os.path.exists(os.path.join(LDIR, 'release.lock')) and os.environ.get('DSRIG_RELEASE') != '1':
            print(f'lease refused for {lane}: release gate running (rig/release.lock)', file=sys.stderr)
            return None                                   # the release gate owns every pair while it runs
        rsv = os.path.join(LDIR, 'reserve.lock')     # 2026-10-15: a lane that needs both slots writes its lane prefix here
        if os.path.exists(rsv):
            who = open(rsv).read().strip()
            if who and not any(lane.startswith(w.strip()) for w in who.split(',') if w.strip()) and time.time() - os.path.getmtime(rsv) < 3600:
                print(f'lease refused for {lane}: slots reserved for {who} (rig/reserve.lock)', file=sys.stderr)
                return None
        limit = int(os.environ.get('DSRIG_MAX_PAIRS', '4'))     # owner limit 4 (restored 2026-10-15 after the night run)
        if len(d) >= limit:
            return None
        allp = _procs_all()
        for pair in PAIRS:
            if pair in d:
                continue
            busy = [q for x in pair for q in allp[x]]
            if busy:                                      # no lease, but copies or helpers still run there: not free
                print(f'{pair} skipped: no lease but processes run in its folders: {busy[:6]}', file=sys.stderr)
                continue
            d[pair] = {'lane': lane, 'pid': pid, 'since': time.time(), 'used': time.time()}
            return pair
        return None
    return _with_leases(take)


def release(pair, lane=None):
    # 2026-10-15: with a lane, only that lane's lease is removed (a late release by a finished lane removed a new holder's lease)
    def r(d):
        l = d.get(pair)
        if l is None:
            return False
        if lane and l['lane'] != lane:
            print(f'release refused: {pair} is held by {l["lane"]}, not {lane}', file=sys.stderr)
            return False
        # 2026-10-17: without a lane name only the holder process (or one of its children) may release
        if not lane and l.get('pid') and l['pid'] not in _ancestors() and _pid_alive(l['pid']):
            print(f'release refused: {pair} is held by {l["lane"]} pid {l["pid"]}; use: rig.py release {pair} <lane>', file=sys.stderr)
            return False
        if not _kill_copies(pair, f'release by {lane or l["lane"]}'):
            print(f'release refused: processes in {pair} survived SIGKILL', file=sys.stderr)
            return False
        _drop_port(d.pop(pair))
        rigports.note_released([idir(x) for x in pair])
        return True
    return _with_leases(r)


def touch(x):
    """Mark the pair holding copy x as used (called by start / cmd)."""
    def t(d):
        for pair, l in d.items():
            if x in pair:
                l['used'] = time.time()
    _with_leases(t)



def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return
    which = lambda w: ['A', 'B'] if w == 'both' else list(w) if w in PAIRS else [w]
    if a[0] == 'start':
        fps = int(a[a.index('--fps') + 1]) if '--fps' in a else 30
        for x in which(a[1]):
            touch(x)
            start(x, window='--window' in a, fps=fps)
    elif a[0] == 'stop':
        for x in which(a[1]):
            stop(x)
    elif a[0] == 'cmd':
        touch(a[1])
        print(json.dumps(send(a[1], json.loads(a[2]))))
    elif a[0] == 'status':
        for x in 'ABCDEFGHIJ':
            pid = read_pid(x)
            print(x, pid, 'alive' if alive(pid) else 'dead', 'sock' if os.path.exists(sockpath(x)) else '')
        d = _with_leases(lambda d: dict(d)); allp = _procs_all()
        for pair in PAIRS:
            l = d.get(pair)
            who = f"{l['lane']} pid={l.get('pid')} ({'alive' if _pid_alive(l.get('pid')) else 'DEAD'})" if l else 'no lease'
            ps = [q for x in pair for q in allp[x]]
            desc = subprocess.run(['ps', '-o', 'pid=,etime=,comm=', '-p', ','.join(map(str, ps))], capture_output=True, text=True).stdout if ps else ''
            print(f'{pair}: {who}; processes: ' + ('; '.join(' '.join(z.split())[:90] for z in desc.strip().split('\n')) if ps else 'none')
                  + ('   <- NOT FREE without a lease' if ps and not l else ''))
    elif a[0] == 'check-prefs':
        print('identical' if check_prefs() else 'DIFFERENT')
    elif a[0] == 'smoke':
        smoke_pair(a[1]) if len(a) > 1 else smoke()
    elif a[0] == 'port':
        if not _with_leases(lambda d: a[1] in d):
            print(f'{a[1]} is not leased: its port would end with this command (lease it first)', file=sys.stderr); sys.exit(1)
        print(pair_port(a[1][0]))
    elif a[0] == 'lease':
        pid = int(a[a.index('--pid') + 1]) if '--pid' in a else os.getppid()   # the caller holds it
        p = lease(a[1], pid)
        if not p:
            print('no free pair', file=sys.stderr); sys.exit(1)
        print(p)
    elif a[0] == 'release':
        print('released' if release(a[1], a[2] if len(a) > 2 else None) else 'not released')
    elif a[0] == 'leases':
        d = _with_leases(lambda d: (_free_stale(d), dict(d))[1])
        for pair in PAIRS:
            l = d.get(pair)
            print(pair, f"{l['lane']} pid={l.get('pid')} since={time.strftime('%H:%M', time.localtime(l['since']))}" if l else 'free')
    elif a[0] == '_guard':
        guard(a[1])
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
