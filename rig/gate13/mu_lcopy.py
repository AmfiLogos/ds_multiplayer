#!/usr/bin/env python3
"""gate13 mu_dive helper: LOADER test copies (dsload + modules) on the Mac, hidden + muted, for mu_dive_mac.py.

Based on ext/units13/test/cmrig.py (lane units13), but any instance letter and two copies at once. The copy's env is set
here (not through lrig.start, which forwards only DSRIG_* / DSL_*), so the TEST My Units library (DSMP_UNITS_LIB) and a
private pasteboard (DSMP_CLIP_PB) reach the game. Never the owner's install, saves, prefs or library.

Modules: when ~/ds_launcher/loader/build/mods/multiplayer is pinned to this worktree's HEAD and has custom-modes, the
loader build is used as it is. Otherwise the multiplayer MODULE is built from this worktree's src (the loader build.sh
recipe, into the run's out folder, never into build/) and custom-modes = this worktree's frozen build/. The test-only
units-driver (ext/units13/driver: fake mouse + captures) is built into the out folder."""
import hashlib, json, os, shutil, signal, subprocess, sys, threading, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

WT = os.path.expanduser('~/ds_mp_gate13')
B = os.path.join(WT, 'build')
L = os.path.expanduser('~/ds_launcher/loader')
LB = os.path.join(L, 'build')
DSLOAD = os.path.join(LB, 'dsload.dylib')
STAGE = '-20000,-20000'
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
OWNER_LIB = os.path.expanduser('~/Library/Application Support/CanOpener/Dungeon Settlers/Mods/units/library.json')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if p.returncode:
        raise SystemExit(f'failed ({p.returncode}): {cmd}\n{p.stdout[-2000:]}\n{p.stderr[-2000:]}')
    return p.stdout


def sha(p):
    try:
        return hashlib.sha256(open(p, 'rb').read()).hexdigest()
    except FileNotFoundError:
        return None


def fstate(p):
    """(sha256, mtime) or (None, None) when the file does not exist"""
    return (sha(p), os.path.getmtime(p)) if os.path.exists(p) else (None, None)


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst + '.tmp'); shutil.copymode(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)


def modules(out):
    """{'mp': dir with multiplayer.dylib + mod.json + det_ranges.tsv, 'cm': dir with custom-modes*.dylib + mod.json,
    'drv': dir with units-driver.dylib + mod.json, 'how': text}"""
    head = sh(f'git -C "{WT}" rev-parse HEAD').strip()
    dirty = sh(f'git -C "{WT}" status --porcelain src custom win sdk').strip()
    if dirty:
        raise SystemExit(f'worktree src not clean (frozen build = HEAD {head[:7]}):\n{dirty}')
    pin = open(os.path.join(LB, 'mods/multiplayer/PINNED_COMMIT')).read().strip() if os.path.exists(os.path.join(LB, 'mods/multiplayer/PINNED_COMMIT')) else ''
    res = {}
    if pin == head and os.path.exists(os.path.join(LB, 'mods/custom-modes/custom-modes.dylib')):
        res['mp'] = os.path.join(LB, 'mods/multiplayer'); res['cm'] = os.path.join(LB, 'mods/custom-modes')
        res['how'] = f'loader build as it is (pinned {head[:7]})'
    else:
        o = os.path.join(out, 'mods', 'multiplayer'); os.makedirs(o, exist_ok=True)
        t = open(os.path.join(WT, 'src/build.sh')).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
        srcs = [w for w in t.split() if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-')]
        srcs = ' '.join(os.path.join(WT, 'src', w) for w in srcs)
        sh(f'cd "{L}" && nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -D_DARWIN_C_SOURCE '
           f'-dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation -install_name @rpath/multiplayer.dylib '
           f'-Isdk -I"{WT}/src" -I"{WT}/win/math" -DDSMATH_DEFAULT_ON=1 -o "{o}/multiplayer.dylib.tmp" modules/multiplayer/mp_module.c {srcs} '
           f'"{WT}/win/math/build/libdsmath_arm64.a" -lz && codesign -f -s - "{o}/multiplayer.dylib.tmp" 2>/dev/null; mv -f "{o}/multiplayer.dylib.tmp" "{o}/multiplayer.dylib"')
        shutil.copyfile(os.path.join(L, 'modules/multiplayer/mod.json'), os.path.join(o, 'mod.json'))
        shutil.copyfile(os.path.join(LB, 'mods/multiplayer/det_ranges.tsv'), os.path.join(o, 'det_ranges.tsv'))
        open(os.path.join(o, 'PINNED_COMMIT'), 'w').write(head + '\n')
        c = os.path.join(out, 'mods', 'custom-modes'); os.makedirs(c, exist_ok=True)
        for f in ('custom-modes.dylib', 'custom-modes-mp.dylib'):
            shutil.copyfile(os.path.join(B, f), os.path.join(c, f)); shutil.copymode(os.path.join(B, f), os.path.join(c, f))
        shutil.copyfile(os.path.join(WT, 'custom/mod.json'), os.path.join(c, 'mod.json'))
        res['mp'], res['cm'] = o, c
        res['how'] = (f'loader build pinned {pin[:7] or "none"} != HEAD {head[:7]}: multiplayer module built from the worktree src '
                      f'(loader recipe) into {o}; custom-modes = {B} (frozen)')
    d = os.path.join(out, 'mods', 'units-driver'); os.makedirs(d, exist_ok=True)
    sh(f'cd "{WT}" && nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -Isdk -dynamiclib -framework CoreFoundation '
       f'-framework CoreGraphics -lobjc -install_name @rpath/units-driver.dylib -o "{d}/units-driver.dylib.tmp" '
       f'ext/units13/driver/units_driver.c && codesign -f -s - "{d}/units-driver.dylib.tmp" 2>/dev/null; mv -f "{d}/units-driver.dylib.tmp" "{d}/units-driver.dylib"')
    shutil.copyfile(os.path.join(WT, 'ext/units13/driver/mod.json'), os.path.join(d, 'mod.json'))
    res['drv'] = d
    for k in ('mp', 'cm', 'drv'):
        res[k + '_sha'] = {f: sha(os.path.join(res[k], f))[:12] for f in sorted(os.listdir(res[k])) if f.endswith('.dylib')}
    return res


def lease(lane, pid):
    env = dict(os.environ, DSRIG_RELEASE='1')
    t0 = time.time()
    while time.time() - t0 < 1800:
        p = subprocess.run([sys.executable, RIG, 'lease', lane, '--pid', str(pid)], capture_output=True, text=True, env=env)
        if not p.returncode and p.stdout.strip():
            return p.stdout.strip().split()[-1]
        log('waiting for a pair:', (p.stderr.strip() or p.stdout.strip())[-100:]); time.sleep(20)
    raise SystemExit('no pair lease')


def release(pair, lane):
    env = dict(os.environ, DSRIG_RELEASE='1')
    p = subprocess.run([sys.executable, RIG, 'release', pair, lane], capture_output=True, text=True, env=env)
    return (p.stdout + p.stderr).strip()


def _nice():
    try:
        os.nice(5)
    except OSError:
        pass                                                    # already at nice 19 + 5 > 20 (the test runs under nice 19)


class Copy:
    MODS = ['multiplayer', 'custom-modes', 'units-driver']

    def __init__(self, x, out, mods, env=None, fps=30):
        self.x, self.out, self.m, self.extra, self.fps = x, out, mods, env or {}, fps
        self.I = r.idir(x); self.APP = os.path.join(self.I, 'DungeonSettlers.app')
        self.p = None; self.seq = 0; self.violations = []; self.stop_guard = threading.Event()
        self.drv = os.path.join(out, f'drv_{x}'); os.makedirs(os.path.join(self.drv, 'cap'), exist_ok=True)

    def pids(self):
        o = subprocess.run(['pgrep', '-f', os.path.join(self.I, r.EXE_REL)], capture_output=True, text=True).stdout
        return [int(p) for p in o.split()]

    def install(self):
        if self.pids():
            raise SystemExit(f'copy {self.x} is running (another lane?): not installing over it')
        put(DSLOAD, os.path.join(self.APP, 'Contents/Frameworks/dsload.dylib'))
        md = os.path.join(self.APP, 'Contents/Resources/mods')
        for name, k in (('multiplayer', 'mp'), ('custom-modes', 'cm'), ('units-driver', 'drv')):
            for f in os.listdir(self.m[k]):
                if os.path.isfile(os.path.join(self.m[k], f)):
                    put(os.path.join(self.m[k], f), os.path.join(md, name, f))
        sh(f'codesign -f -s - "{self.APP}/Contents/MacOS/Dungeon Settlers"')

    def guard(self):
        while not self.stop_guard.is_set():
            pids = self.pids()
            if pids:
                bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
                fp = r.front_pid()
                if bad or fp in pids:
                    self.violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                    for p in pids:
                        os.kill(p, signal.SIGKILL)
                    log('GUARD KILLED', self.x, bad, fp)
            self.stop_guard.wait(1.0)

    def start(self):
        if self.pids():
            raise SystemExit(f'copy {self.x} already running')
        r.prepare_data(self.x)                                     # muted settings, 960x540, own data folder
        prof = os.path.join(self.I, 'units_profile.json')
        json.dump({'enabled': self.MODS, 'order': self.MODS}, open(prof, 'w'))
        for f in ('req.json', 'resp.json'):
            if os.path.exists(os.path.join(self.drv, f)):
                os.replace(os.path.join(self.drv, f), os.path.join(self.drv, f + '.old'))
        if os.path.exists(r.sockpath(self.x)):
            os.makedirs(os.path.join(self.I, 'old'), exist_ok=True)
            os.replace(r.sockpath(self.x), os.path.join(self.I, 'old', 'ctl.sock.' + time.strftime('%H%M%S')))
        env = {'HOME': os.path.join(self.I, 'home'), 'CFFIXED_USER_HOME': os.path.join(self.I, 'home'), 'USER': os.environ.get('USER', ''),
               'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
               'DSRIG_DIR': self.I, 'DSRIG_FPS': str(self.fps), 'DYLD_INSERT_LIBRARIES': os.path.join(self.APP, 'Contents/Frameworks/dsload.dylib'),
               'DSL_PROFILE': prof, 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
               'DSL_UNITS_TEST': self.drv, 'DSMP_TEST': '1', 'DSL_LOG_DIR': self.I}
        env.update(self.extra)
        for f in ('player.log', 'dsload.log', 'rig.log', 'fp.log'):
            p = os.path.join(self.I, f)
            if os.path.exists(p):
                os.replace(p, p + '.prev')
        a = [os.path.join(self.I, r.EXE_REL), '-logFile', os.path.join(self.I, 'player.log'), '-screen-fullscreen', '0',
             '-screen-width', '960', '-screen-height', '540']
        self.p = subprocess.Popen(a, env=env, cwd=self.I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(self.I, 'stdout.log'), 'ab'),
                                  stderr=subprocess.STDOUT, preexec_fn=_nice, start_new_session=True)
        open(r.pidfile(self.x), 'w').write(f'{self.p.pid}\n')
        threading.Thread(target=self.guard, daemon=True).start()
        log('started', self.x, 'pid', self.p.pid)

    def stop(self):
        self.stop_guard.set()
        if self.p and self.p.poll() is None:
            self.p.send_signal(signal.SIGTERM)
            try:
                self.p.wait(15)
            except subprocess.TimeoutExpired:
                self.p.kill(); self.p.wait(5)
        for p in self.pids():
            os.kill(p, signal.SIGKILL)
        if not self.p:
            return
        for f in ('player.log', 'dsload.log', 'rig.log', 'fp.log'):
            if os.path.exists(os.path.join(self.I, f)):
                shutil.copy2(os.path.join(self.I, f), os.path.join(self.out, f'{self.x}_{f}'))
        log('stopped', self.x)

    def alive(self):
        return bool(self.p and self.p.poll() is None)

    # ---- rig socket (multiplayer module)
    def rig(self, cmd, timeout=180, **kw):
        if not self.alive():
            raise RuntimeError(f'copy {self.x} exited (rc {self.p.returncode if self.p else None})')
        return r.send(self.x, dict(kw, cmd=cmd), timeout=timeout)

    # ---- units-driver (fake mouse, captures)
    def req(self, cmd, timeout=20, **kw):
        self.seq += 1
        q = dict(kw, seq=self.seq, cmd=cmd)
        tmp = os.path.join(self.drv, 'req.json.tmp')
        open(tmp, 'w', encoding='utf-8').write(json.dumps(q)); os.replace(tmp, os.path.join(self.drv, 'req.json'))
        t0 = time.time()
        while time.time() - t0 < timeout:
            if not self.alive():
                raise RuntimeError(f'copy {self.x} exited during {q}')
            try:
                a = json.load(open(os.path.join(self.drv, 'resp.json')))
                if a.get('seq') == self.seq:
                    return a
            except Exception:
                pass
            time.sleep(0.05)
        raise RuntimeError(f'no driver answer to {q}')

    def click(self, target, wait=1.0):
        a = self.req('click', target=target); time.sleep(wait); return a

    def cap(self, name):
        try:
            return self.req('capture', name=name).get('rc', 0) > 0
        except Exception:
            return False
