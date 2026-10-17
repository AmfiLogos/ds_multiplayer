#!/usr/bin/env python3
"""Lane fights test copy O (copy of lane units13's ext/units13/test/cmrig.py, copy U -> O, build_c4/game) (loader layout: dsload + modules), hidden, muted, never the owner's install / saves / prefs.

Built on the loader lane's ~/ds_launcher/loader/test/lrig.py (instance U from make_instance.sh U) and the uikit_test.py
start / guard pattern. Modules come from THIS worktree's build/ (custom-modes, multiplayer = dsrig.dylib) plus the
test-only units-driver (ext/units13/driver). dsload.dylib = ~/ds_launcher/loader/build/dsload.dylib (read only).
A pair lease is held while the copy runs (LANE_RULES 6: a loader copy counts as a pair).

  from cmrig import Copy
  with Copy(['multiplayer', 'custom-modes', 'units-driver'], out) as c: c.wait_title(); c.req('click', target='title:Units Laboratory')
"""
import json, os, shutil, signal, subprocess, sys, threading, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'O'
I = r.idir(X)
APP = os.path.join(I, 'DungeonSettlers.app')
WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))   # the worktree
B = os.path.join(WT, 'build_c4', 'game')
DSLOAD = os.path.expanduser('~/ds_launcher/loader/build/dsload.dylib')
MP_MOD = os.path.expanduser('~/ds_launcher/loader/build/mods/multiplayer')
STAGE = '-20000,-20000'
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def sh(cmd, **kw):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)
    if p.returncode:
        raise SystemExit(f'failed ({p.returncode}): {cmd}\n{p.stdout[-2000:]}\n{p.stderr[-2000:]}')
    return p.stdout


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst + '.tmp'); shutil.copymode(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)


def game_pids():
    out = subprocess.run(['pgrep', '-f', os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def install():
    """dsload + multiplayer (our dsrig.dylib, the loader's mod.json) + custom-modes + units-driver into copy U"""
    if game_pids():
        raise SystemExit('copy O is running: not installing over it')
    put(DSLOAD, os.path.join(APP, 'Contents/Frameworks/dsload.dylib'))
    md = os.path.join(APP, 'Contents/Resources/mods')
    for f in os.listdir(MP_MOD):
        if f not in ('multiplayer.dylib', 'PINNED_COMMIT'):
            put(os.path.join(MP_MOD, f), os.path.join(md, 'multiplayer', f))
    put(os.path.join(B, 'dsrig.dylib'), os.path.join(md, 'multiplayer', 'multiplayer.dylib'))
    for f in ('custom-modes.dylib', 'custom-modes-mp.dylib'):
        put(os.path.join(B, f), os.path.join(md, 'custom-modes', f))
    put(os.path.join(B, 'mod.json'), os.path.join(md, 'custom-modes', 'mod.json'))
    put(os.path.join(B, 'units-driver.dylib'), os.path.join(md, 'units-driver', 'units-driver.dylib'))
    put(os.path.join(B, 'units-driver.mod.json'), os.path.join(md, 'units-driver', 'mod.json'))
    sh(f'codesign -f -s - "{APP}/Contents/MacOS/Dungeon Settlers"')
    log('installed loader + multiplayer + custom-modes + units-driver in', APP)


def build_driver():
    sh(f'cd "{WT}" && nice -n 19 clang -arch arm64 -O2 -Wall -Wno-unused-function -Isdk -dynamiclib -framework CoreFoundation '
       f'-framework CoreGraphics -lobjc -install_name @rpath/units-driver.dylib -o build/units-driver.dylib.tmp '
       f'ext/units13/driver/units_driver.c && codesign -f -s - build/units-driver.dylib.tmp 2>/dev/null; '
       f'mv -f build/units-driver.dylib.tmp build/units-driver.dylib')


class Copy:
    def __init__(self, mods, out, extra=None, lane='fights', ui_scale=None):
        self.mods, self.out, self.extra, self.lane, self.ui_scale = mods, out, extra or {}, f'{lane}-{os.getpid()}', ui_scale
        self.p = None; self.pair = None; self.seq = 0; self.steps = []; self.violations = []
        self.stop_guard = threading.Event()
        self.drv = os.path.join(out, 'drv'); os.makedirs(os.path.join(self.drv, 'cap'), exist_ok=True)

    # ---- lease
    def lease(self):
        t0 = time.time()
        while True:                                       # max 4 pairs machine-wide: wait for one (up to 15 min)
            p = subprocess.run([sys.executable, RIG, 'lease', self.lane, '--pid', str(os.getpid())], capture_output=True, text=True)
            if not p.returncode or time.time() - t0 > 900:
                break
            time.sleep(10)
        if p.returncode:
            raise SystemExit(f'no pair lease: {p.stderr.strip() or p.stdout.strip()}')
        self.pair = p.stdout.strip().split()[-1]
        log('lease', self.pair)

    def release(self):
        if self.pair:
            subprocess.run([sys.executable, RIG, 'release', self.pair, self.lane], capture_output=True, text=True)
            log('released', self.pair); self.pair = None

    # ---- copy
    def guard(self):
        while not self.stop_guard.is_set():
            pids = game_pids()
            if pids:
                bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
                fp = r.front_pid()
                if bad or fp in pids:
                    self.violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                    for p in pids:
                        os.kill(p, signal.SIGKILL)
                    log('GUARD KILLED', bad, fp)
            self.stop_guard.wait(1.0)

    def settings(self):
        r.prepare_data(X)
        dirs = [os.path.join(I, 'data')]
        if 'multiplayer' not in self.mods:      # without MP the data folder is the copy's own home (uikit_test.py note)
            pd = os.path.join(I, f'home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
            for f in ('UserSetting.json', 'OutGame.json'):
                if os.path.exists(os.path.join(I, 'data', f)):
                    shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
            dirs.append(pd)
        if self.ui_scale is not None:
            for d in dirs:
                p = os.path.join(d, 'UserSetting.json'); s = json.load(open(p))
                g = s.get('GeneralSettingData', {})
                keys = [k for k in g if 'scale' in k.lower()]
                for k in keys:
                    g[k] = self.ui_scale
                json.dump(s, open(p, 'w'), indent=1)
                log('UI scale', self.ui_scale, 'keys', keys, 'in', p)
        return dirs

    def start(self):
        if game_pids():
            raise SystemExit('copy O already running')
        self.lease()
        self.data_dirs = self.settings()
        prof = os.path.join(I, 'fights_profile.json')
        json.dump({'enabled': self.mods, 'order': self.mods}, open(prof, 'w'))
        for f in ('req.json', 'resp.json'):
            if os.path.exists(os.path.join(self.drv, f)):
                os.replace(os.path.join(self.drv, f), os.path.join(self.drv, f + '.old'))
        env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
               'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
               'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents/Frameworks/dsload.dylib'),
               'DSL_PROFILE': prof, 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1',
               'DSL_UNITS_TEST': self.drv, 'DSMP_TEST': '1', 'DSL_LOG_DIR': I}
        env.update(self.extra)
        for f in ('player.log', 'dsload.log'):
            p = os.path.join(I, f)
            if os.path.exists(p):
                os.replace(p, p + '.prev')
        a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
             '-screen-width', '960', '-screen-height', '540']
        self.p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                                  stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
        open(r.pidfile(X), 'w').write(f'{self.p.pid}\n')
        threading.Thread(target=self.guard, daemon=True).start()
        log('started pid', self.p.pid, self.mods)

    def stop(self):
        self.stop_guard.set()
        if self.p and self.p.poll() is None:
            self.p.send_signal(signal.SIGTERM)
            try:
                self.p.wait(15)
            except subprocess.TimeoutExpired:
                self.p.kill(); self.p.wait(5)
        for p in game_pids():
            os.kill(p, signal.SIGKILL)
        for f in ('player.log', 'dsload.log'):
            if os.path.exists(os.path.join(I, f)):
                shutil.copy2(os.path.join(I, f), os.path.join(self.out, f))
        json.dump(self.steps, open(os.path.join(self.out, 'steps.json'), 'w'), indent=1)
        log('stopped')
        self.release()

    def __enter__(self):
        self.start(); return self

    def __exit__(self, *a):
        self.stop()

    # ---- driver
    def req(self, cmd, timeout=20, **kw):
        self.seq += 1
        q = dict(kw, seq=self.seq, cmd=cmd)
        tmp = os.path.join(self.drv, 'req.json.tmp')
        open(tmp, 'w', encoding='utf-8').write(json.dumps(q, ensure_ascii=False)); os.replace(tmp, os.path.join(self.drv, 'req.json'))
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.p and self.p.poll() is not None:
                raise RuntimeError(f'game exited (rc {self.p.returncode}) during {q}')
            try:
                a = json.load(open(os.path.join(self.drv, 'resp.json')))
                if a.get('seq') == self.seq:
                    self.steps.append({'req': q, 'resp': a})
                    return a
            except Exception:
                pass
            time.sleep(0.05)
        raise RuntimeError(f'no answer to {q}')

    def state(self):
        return self.req('state')

    def click(self, target, wait=1.0):
        a = self.req('click', target=target); time.sleep(wait); return a

    def cap(self, name):
        return self.req('capture', name=name)['rc'] > 0

    def call(self, fn, s=''):
        a = self.req('call', fn=fn, s=s)
        return a['rc'], a.get('out', '')

    def column(self, st=None):
        st = st or self.state(); out = []
        for e in st.get('title_column', '').split(';'):
            if e.count('|') >= 2:
                l, a, s = e.rsplit('|', 2); out.append((l, int(a), int(s)))
        return out

    def wait_title(self, need=('Units Laboratory',), limit=240):
        t0 = time.time()
        while time.time() - t0 < limit:
            try:
                labels = [c[0] for c in self.column() if c[1]]
                if all(x in labels for x in need):
                    log('title seen - waiting 8 s for the fade in'); time.sleep(8)
                    return self.state()
            except RuntimeError:
                raise
            except Exception:
                pass
            time.sleep(1)
        raise RuntimeError('title not seen')


class Checks:
    def __init__(self, out):
        self.out = out; self.res = []

    def __call__(self, name, ok, detail=''):
        self.res.append({'step': name, 'ok': bool(ok), 'detail': detail})
        log(('PASS ' if ok else 'FAIL ') + name, detail)
        return ok

    def save(self):
        n = sum(1 for x in self.res if x['ok'])
        json.dump({'pass': n, 'fail': len(self.res) - n, 'checks': self.res}, open(os.path.join(self.out, 'summary.json'), 'w'), indent=1)
        log(f'{n} PASS {len(self.res) - n} FAIL')
        return len(self.res) - n
