#!/usr/bin/env python3
"""winrig.py - bot-style driver for the Windows test VM (and the DSMP Wine bottle), same idea as rig.py on the Mac.

The game's control port listens on 127.0.0.1 inside the guest. winagent.exe (agent/, protocol in PROTOCOL.md) runs in
the guest's desktop session and connects OUT to the hub on the Mac; the hub hands each request to a free agent
connection. So one command costs a few ms instead of 4-5 vmrun calls, and waits are on events, not fixed sleeps.

  winrig.py up                      hub running + agent in the guest (copies and starts winagent.exe once)
  winrig.py hub | hub-stop | hub-status
  winrig.py start [K=V ...]         prepare (windowed, no update notice), start the game, wait for the title
  winrig.py stop                    quit over the port, then kill the game by pid
  winrig.py cmd '<json>' ...        game control commands (one answer line each)
  winrig.py status                  short status (scene, map, mp, menu)
  winrig.py host <guest save path> | join <ip:port> | press <BUTTON> | box confirm|cancel | load <guest save path>
  winrig.py ui '<json op>'          DSL_UI_TEST command (loader Mods window), prints result.json
  winrig.py shot <png> | gshot <png>   VM screen (vmrun captureScreen, no focus change) | game frame (DSL_UI_TEST shot)
  winrig.py logs <dir>              copy the profile logs + state files to <dir>
  winrig.py a '<json>'              raw agent request (op ...)
Env: WINBOT_TARGET=vm (default) | wine, WINBOT_STATE (default ~/.ds_winbot), WINBOT_HUB_HOST (vm: the Mac's vmnet
address, found from ifconfig), WINBOT_PORT (game control port, default 47811).
"""
import base64, json, os, re, secrets, select, socket, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
STATE = os.environ.get('WINBOT_STATE') or os.path.expanduser('~/.ds_winbot')
SOCK = os.path.join(STATE, 'hub.sock')
HUB_PORT = int(os.environ.get('WINBOT_HUB_PORT', '47920'))
AGENT_EXE = os.path.join(HERE, 'agent', 'build', 'winagent.exe')
TARGET = os.environ.get('WINBOT_TARGET', 'vm')
GAME_PORT = int(os.environ.get('WINBOT_PORT', '47811'))

VMRUN = '/Applications/VMware Fusion.app/Contents/Library/vmrun'
VMX = os.environ.get('WINBOT_VMX', '/Volumes/XTRA/Windows 11 64-bit Arm.vmwarevm/Windows 11 64-bit Arm.vmx')
GUEST_USER = 'jamesjackson'
WINE = os.path.expanduser('~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine')

# guest paths (the agent expands %VAR%)
ROOT = r'%LOCALAPPDATA%\Dungeon Settlers Modded'
PROF = ROOT + r'\profiles\modded'
DATA = PROF + r'\data'
GAME = ROOT + r'\game'
WORK = r'C:\dsm'
UITEST = WORK + r'\uitest'
AGENT_GUEST = WORK + r'\winbot\winagent.exe'
B = dict(MULTIPLAYER=1, HOST=2, BACK=3, LOAD_SAVE=7, START=10, LEAVE=12, JOIN_ADDR=14, CONNECT=15)   # enum mpm_button (mpmenu.h)
LOGS = ['starter.log', 'dsload.log', 'player.log', 'player.soft.log', 'rig.log', 'dsload_status.json', 'ctl.port', 'ctl.pid',
        r'data\ErrorLog.txt', r'data\Mods\active.json', r'data\Mods\settings.json', r'data\Mods\worlds.json', r'data\UserSetting.json']


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, file=sys.stderr, flush=True)


# ------------------------------------------------------------------ hub (background process)
def _token():
    os.makedirs(STATE, exist_ok=True)
    p = os.path.join(STATE, 'token')
    if not os.path.exists(p):
        with open(p + '.tmp', 'w') as f:
            f.write(secrets.token_hex(16))
        os.chmod(p + '.tmp', 0o600); os.replace(p + '.tmp', p)
    return open(p).read().strip()


def hub_host():
    """the Mac's address on the VM's network (vmnet8, for example 172.16.175.1); 127.0.0.1 for the Wine bottle"""
    if os.environ.get('WINBOT_HUB_HOST'):
        return os.environ['WINBOT_HUB_HOST']
    if TARGET == 'wine':
        return '127.0.0.1'
    out = subprocess.run(['ifconfig'], capture_output=True, text=True).stdout
    for m in re.finditer(r'inet (172\.16\.\d+\.1) ', out):
        return m.group(1)
    raise SystemExit('no vmnet address (172.16.x.1) on this Mac: is VMware Fusion running?')


class _Conn:
    def __init__(self, s, hello):
        self.s, self.hello, self.f = s, hello, s.makefile('rb')


class Hub:
    def __init__(self):
        self.tok = _token()
        self.lock = threading.Condition()
        self.idle = []                   # free agent connections
        self.all = []
        self.next_id = 1

    def serve_agents(self, host):
        ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind((host, HUB_PORT)); ls.listen(16)
        log('hub: agents on', f'{host}:{HUB_PORT}')
        while True:
            s, a = ls.accept()
            threading.Thread(target=self._hello, args=(s, a), daemon=True).start()

    def _hello(self, s, a):
        try:
            s.settimeout(10); f = s.makefile('rb')
            h = json.loads(f.readline() or b'{}')
            if h.get('hello') != 'winagent' or h.get('token') != self.tok:
                log('hub: refused', a); s.close(); return
            s.settimeout(None); s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            c = _Conn(s, h); c.f = f
            with self.lock:
                self.all.append(c); self.idle.append(c); self.lock.notify_all()
            log('hub: agent', h.get('computer'), 'pid', h.get('pid'), 'conn', h.get('conn'), 'from', a[0])
        except Exception as e:
            log('hub: hello failed', a, e); s.close()

    @staticmethod
    def _dead(c):
        try:
            if not select.select([c.s], [], [], 0)[0]:
                return False
            return c.s.recv(1, socket.MSG_PEEK) == b''
        except OSError:
            return True

    def _drop(self, c):
        with self.lock:
            if c in self.all:
                self.all.remove(c)
            if c in self.idle:
                self.idle.remove(c)
        try:
            c.s.close()
        except OSError:
            pass

    def request(self, req):
        if req.get('op') == 'hub_status':
            with self.lock:
                for d in [c for c in self.idle if self._dead(c)]:
                    self.idle.remove(d); self.all.remove(d); d.s.close()
                return {'ok': True, 'agents': [{k: v for k, v in c.hello.items() if k != 'token'} for c in self.all], 'idle': len(self.idle)}
        want = req.pop('target', None)
        t_end = time.time() + 20
        with self.lock:
            while True:
                for d in [c for c in self.idle if self._dead(c)]:     # an agent that quit: its idle sockets read EOF
                    self.idle.remove(d); self.all.remove(d); d.s.close()
                c = next((c for c in self.idle if not want or want.lower() in str(c.hello.get('computer', '')).lower()), None)
                if c:
                    self.idle.remove(c); break
                if not self.all:
                    return {'ok': False, 'err': 'no agent connected'}
                if time.time() > t_end:
                    return {'ok': False, 'err': 'all agent connections busy'}
                self.lock.wait(0.5)
            req['id'] = self.next_id; self.next_id += 1
        wait_s = max(req.get('timeout_ms', 0), req.get('wait_ms', 0)) / 1000 + 60
        try:
            c.s.settimeout(wait_s)
            c.s.sendall(json.dumps(req).encode() + b'\n')
            line = c.f.readline()
            if not line:
                raise OSError('agent closed the connection')
            r = json.loads(line)
            c.s.settimeout(None)
        except (OSError, ValueError) as e:
            self._drop(c)
            return {'ok': False, 'err': f'agent connection lost: {e}'}
        with self.lock:
            self.idle.append(c); self.lock.notify_all()
        return r

    def serve_clients(self):
        if os.path.exists(SOCK):
            os.replace(SOCK, SOCK + '.old')      # a stale socket file of a dead hub (never deleted: owner rule)
        ls = socket.socket(socket.AF_UNIX); ls.bind(SOCK); os.chmod(SOCK, 0o600); ls.listen(64)
        while True:
            s, _ = ls.accept()
            threading.Thread(target=self._client, args=(s,), daemon=True).start()

    def _client(self, s):
        f = s.makefile('rb')
        try:
            for line in f:
                try:
                    req = json.loads(line)
                except ValueError:
                    r = {'ok': False, 'err': 'bad json'}
                else:
                    if req.get('op') == 'hub_stop':
                        s.sendall(b'{"ok":true}\n'); os._exit(0)
                    r = self.request(req)
                s.sendall(json.dumps(r).encode() + b'\n')
        except OSError:
            pass
        finally:
            s.close()


def hub_main():
    h = Hub()
    hosts = {'127.0.0.1'}
    try:
        hosts.add(hub_host())
    except SystemExit as e:
        log(e)
    for host in hosts:
        threading.Thread(target=h.serve_agents, args=(host,), daemon=True).start()
    h.serve_clients()


def hub_req(req, timeout=None):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(timeout or max(req.get('timeout_ms', 0), req.get('wait_ms', 0)) / 1000 + 90)
    s.connect(SOCK)
    s.sendall(json.dumps(req).encode() + b'\n')
    buf = b''
    while not buf.endswith(b'\n'):
        d = s.recv(1 << 20)
        if not d:
            break
        buf += d
    s.close()
    return json.loads(buf) if buf else {'ok': False, 'err': 'hub closed'}


def hub_alive():
    try:
        return hub_req({'op': 'hub_status'}, timeout=5).get('ok')
    except OSError:
        return False


def hub_start():
    if hub_alive():
        return
    os.makedirs(STATE, exist_ok=True)
    subprocess.Popen([sys.executable, os.path.abspath(__file__), '_hub'], start_new_session=True, stdin=subprocess.DEVNULL,
                     stdout=open(os.path.join(STATE, 'hub.log'), 'a'), stderr=subprocess.STDOUT,
                     env=dict(os.environ, WINBOT_TARGET=TARGET))
    for _ in range(50):
        if hub_alive():
            return
        time.sleep(0.1)
    raise SystemExit('hub did not start (see %s)' % os.path.join(STATE, 'hub.log'))


# ------------------------------------------------------------------ vmrun (only to bootstrap the agent, and for screenshots)
def _secret(*a):
    return subprocess.run(['security', 'find-generic-password', *a, '-w'], capture_output=True, text=True).stdout.strip()


def vmrun(*args, timeout=90):
    """guest operation through vmrun (passwords read from the Keychain, never printed)"""
    g = ['-T', 'fusion', '-vp', _secret('-s', VMX), '-gu', GUEST_USER, '-gp', _secret('-s', 'vmware-win-tester', '-a', GUEST_USER)]
    return subprocess.run([VMRUN, *g, args[0], VMX, *args[1:]], capture_output=True, text=True, timeout=timeout)


# ------------------------------------------------------------------ the guest
class GameError(RuntimeError):
    pass


class Win:
    """one Windows game copy (the VM's, or the DSMP bottle's under Wine) reached through the agent"""

    def __init__(self, target=TARGET, port=None):
        self.target = target
        self.port = port or GAME_PORT
        self.pid = None
        self.marks = {}
        self.t0 = time.time()

    # -- agent
    def a(self, op, **k):
        r = hub_req(dict(op=op, **k))
        if not r.get('ok'):
            raise GameError(f'{op}: {r.get("err")}')
        return r

    def up(self, timeout=30):
        """hub + agent; starts the agent in the guest when none is connected"""
        hub_start()
        if hub_req({'op': 'hub_status'}).get('agents'):
            r = hub_req({'op': 'ping'})
            if r.get('ok'):
                return r
        tok, host = _token(), hub_host()
        args = f'{host}:{HUB_PORT} {tok} 4 720'
        if self.target == 'vm':
            if 'Arm.vmx' not in subprocess.run([VMRUN, '-T', 'fusion', 'list'], capture_output=True, text=True).stdout:
                raise SystemExit('the VM is not running (start it visibly: vmrun -T fusion -vp <pw> start <vmx> gui)')
            vmrun('createDirectoryInGuest', WORK + r'\winbot')
            r = vmrun('copyFileFromHostToGuest', AGENT_EXE, AGENT_GUEST)
            if r.returncode:
                raise SystemExit('copy winagent.exe: ' + (r.stdout + r.stderr).strip()[-300:])
            # vmrun hands the arguments over as one quoted string: start it through cmd's start (no window, returns at once)
            r = vmrun('runProgramInGuest', '-noWait', '-interactive', r'C:\Windows\System32\cmd.exe', f'/c start "" "{AGENT_GUEST}" {args}')
            if r.returncode:
                raise SystemExit('start winagent.exe: ' + (r.stdout + r.stderr).strip()[-300:])
        else:
            subprocess.run(['nice', '-n', '19', WINE, '--bottle', 'DSMP', '--no-wait', 'Z:' + AGENT_EXE.replace('/', '\\'), *args.split()],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t = time.time()
        while time.time() - t < timeout:
            if hub_req({'op': 'hub_status'}).get('agents'):
                return self.a('ping')
            time.sleep(0.2)
        raise SystemExit('the agent did not connect within %d s (guest %%TEMP%%\\winagent.log)' % timeout)

    # -- files
    def read(self, path, offset=0, max=4 << 20):
        r = self.a('read', path=path, offset=offset, max=max)
        return base64.b64decode(r['b64']), r

    def text(self, path):
        try:
            return self.read(path)[0].decode('utf-8', 'replace')
        except GameError:
            return None

    def write(self, path, data, mkdirs=True):
        if isinstance(data, str):
            data = data.encode()
        return self.a('write', path=path, b64=base64.b64encode(data).decode(), mkdirs=mkdirs)

    def put(self, local, path):
        return self.write(path, open(local, 'rb').read())

    def get(self, path, local):
        d, _ = self.read(path, max=256 << 20)
        os.makedirs(os.path.dirname(os.path.abspath(local)), exist_ok=True)
        open(local, 'wb').write(d)
        return len(d)

    def upload(self, local, path):
        """a big file (Setup.exe): one vmrun copy on the VM, a plain copy into the bottle under Wine"""
        if self.target == 'vm':
            self.write(path.rsplit('\\', 1)[0] + r'\.keep', b'')   # makes the folder (the agent's mkdirs)
            r = vmrun('copyFileFromHostToGuest', local, self.expand(path), timeout=600)
            if r.returncode:
                raise GameError('upload: ' + (r.stdout + r.stderr).strip()[-200:])
        else:
            import shutil
            d = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/') + self.expand(path)[3:].replace('\\', '/')
            os.makedirs(os.path.dirname(d), exist_ok=True); shutil.copyfile(local, d)
        return self.a('stat', path=path)

    def install(self, setup, timeout=600):
        """the release Setup.exe, silent (/S, per user, no admin); returns its exit code"""
        self.kill_all()
        g = WORK + r'\winbot\Setup.exe'
        self.upload(setup, g)
        r = self.a('run', exe=self.expand(g), args='/S', wait_ms=timeout * 1000, cwd=self.expand(WORK))
        # the starter's dry run makes the profile as the first real start does (active.json: the default mod set)
        st = self.expand(ROOT + r'\Dungeon Settlers Modded.exe')
        if self.exists(st):
            self.a('run', exe=st, args='--dry', cwd=self.expand(ROOT), wait_ms=60000)
        return r.get('exit')

    def exists(self, path):
        return self.a('stat', path=path).get('exists')

    def move(self, src, dst):
        return self.a('move', src=src, dst=dst, mkdirs=True)

    def json_file(self, path):
        t = self.text(path)
        if not t:
            return None
        t = t.lstrip('\ufeff')
        try:
            return json.loads(t)
        except ValueError:                                   # the loader's result.json holds raw Windows paths (C:\dsm\...)
            return json.loads(re.sub(r'\\(?!")', r'\\\\', t))

    # -- logs as events
    def mark(self, name):
        """the current end of a profile log: wait_log(name, ...) after an action looks only at newer lines"""
        st = self.a('stat', path=PROF + '\\' + name)
        self.marks[name] = st.get('size', 0) if st.get('exists') else 0
        return self.marks[name]

    def wait_log(self, name, any, timeout=120, since=None):
        off = self.marks.get(name, 0) if since is None else since
        r = self.a('wait_file', path=PROF + '\\' + name, any=list(any), offset=off, timeout_ms=int(timeout * 1000))
        if not r.get('found'):
            raise GameError(f'timeout ({timeout} s): no {any} in {name}')
        self.marks[name] = r['offset']
        return r['line']

    # -- processes
    def procs(self, name='DungeonSettlers.exe'):
        return self.a('ps', name=name)['procs']

    def kill_all(self):
        for n in ('DungeonSettlers.exe', 'UnityCrashHandler64.exe', 'WerFault.exe'):
            try:
                self.a('kill', name=n)
            except GameError:
                pass
        self.wait(lambda: not self.procs(), 'the game processes to end', 20, 0.3)   # files stay locked until then

    def alive(self):
        """our game pid runs, no crash dialog, its window answers"""
        ps = self.procs()
        mine = [p for p in ps if p['pid'] == self.pid] if self.pid else ps
        crash = self.a('ps', name='WerFault.exe')['procs']
        hung = any(w.get('hung') for p in mine for w in p.get('windows', []))
        return bool(mine) and not crash and not hung

    # -- the game's control port
    def cmd(self, obj=None, timeout=90, **k):
        line = json.dumps(obj if obj is not None else k)
        r = hub_req({'op': 'game', 'line': line, 'port_file': PROF + r'\ctl.port', 'timeout_ms': int(timeout * 1000)})
        if not r.get('ok'):
            raise GameError(f'game port: {r.get("err")}')
        try:
            return json.loads(r['resp'])
        except ValueError:
            return {'raw': r['resp'][-300:]}

    def ping(self):
        try:
            return self.cmd(cmd='ping', timeout=5)
        except GameError:
            return None

    def status(self):
        return self.cmd(cmd='status')

    def wait(self, pred, what, timeout=120, every=0.5):
        t, last = time.time(), None
        while time.time() - t < timeout:
            try:
                last = pred()
            except GameError as e:
                last = None; err = e
            if last:
                return last
            time.sleep(every)
        raise GameError(f'timeout ({timeout} s) waiting for {what}')

    def on_title(self):
        s = self.status()
        return s if s.get('scene') == 1 and not s.get('busy') and s.get('boot_ready') else None

    def loaded(self, map=None):
        s = self.status()
        ok = s.get('scene') == 2 and not s.get('busy') and not (s.get('map_loading') or {}).get('is_loading')
        return s if ok and (map is None or s.get('map') == map) else None

    # -- start / stop
    def screen_mode_ok(self):
        """owner rule: the game windowed at a quarter of the screen, checked before every run"""
        if self.target != 'vm':
            return True
        k = r'Software\CanOpener\Dungeon Settlers'
        want = {'Screenmanager Fullscreen mode_h3630240806': 3, 'Screenmanager Resolution Use Native_h1405027254': 0,
                'Screenmanager Resolution Width_h182942802': 1728, 'Screenmanager Resolution Height_h2627697771': 1000}
        for n, v in want.items():
            if self.a('reg_get', key=k, name=n).get('value') != v:
                self.a('reg_set', key=k, name=n, type='dword', value=v)
        return self.a('reg_get', key=k, name='Screenmanager Fullscreen mode_h3630240806').get('value') == 3

    def prepare(self, lang=None, update_notice=False, consent=None, active=None, settings=None, saves=None):
        """profile files before a start. lang = SaveCurrentLanguageType (0 English, 4 German ... as the game numbers them);
        update_notice False hides the stock Update Notice (HiddenUpdateNoticeVersion 1, like the Mac rig's prefs);
        consent 'no'/'yes' pre-answers the crash-report question (None = leave it); active = enabled mod ids;
        settings = {mod: {key: value}}; saves = {guest name: local file} copied into the profile's Saves."""
        us = self.json_file(DATA + r'\UserSetting.json') or json.load(open(os.path.join(HERE, 'UserSetting.template.json')))
        if us:                                               # a fresh profile gets the template: no first-run Language picker
            g, gr = us.setdefault('GeneralSettingData', {}), us.setdefault('GraphicSettingData', {})
            g['IsLanguageInitialized'] = True
            if lang is not None:
                g['SaveCurrentLanguageType'] = lang
            if not update_notice:
                g['HiddenUpdateNoticeVersion'] = max(1, g.get('HiddenUpdateNoticeVersion', -1))
            gr.update(SaveCurrentScreenType=1, ResolutionWidth=1728, ResolutionHeight=1000, WindowedResolutionWidth=1728,
                      WindowedResolutionHeight=1000)
            self.write(DATA + r'\UserSetting.json', json.dumps(us, indent=1))
        if self.exists(DATA + r'\Mods\worlds.json'):                 # no remembered world mod sets from an earlier case (moved)
            self.move(DATA + r'\Mods\worlds.json', WORK + r'\to_delete\worlds_' + time.strftime('%Y%m%d-%H%M%S') + '.json')
        if consent:
            self.write(ROOT + r'\crash\consent.json', json.dumps({'consent': consent}))
        if active is not None:
            self.write(DATA + r'\Mods\active.json', json.dumps({'enabled': list(active), 'order': list(active)}))
        if settings is not None:
            self.write(DATA + r'\Mods\settings.json', json.dumps(settings))
        for name, local in (saves or {}).items():
            self.put(local, DATA + '\\Saves\\' + name)

    def expand(self, t):
        """%VAR% from the agent's environment (CreateProcess does not expand environment values or arguments)"""
        if not getattr(self, '_genv', None):
            out = self.a('run', exe=r'C:\Windows\System32\cmd.exe', args='/c set', wait_ms=15000, capture=True)['out']
            self._genv = {k.upper(): v for k, _, v in (l.partition('=') for l in out.splitlines()) if k}
        return re.sub(r'%(\w+)%', lambda m: self._genv.get(m.group(1).upper(), m.group(0)), t)

    def start_env(self, extra=None):
        e = {'SteamAppId': '2798330', 'SteamGameId': '2798330', 'DSRIG_DIR': PROF, 'DSRIG_LIVE': '1', 'DSRIG_FPS': '60',
             'DSRIG_PORT': str(self.port),                   # the control channel opens only with DSRIG_PORT (Windows, v0.1.15 fix)
             'DSL_MODS_DIR': ROOT + r'\mods', 'DSL_PROFILE': DATA + r'\Mods\active.json',
             'DSL_LOG_DIR': PROF, 'DSML_SESSION': DATA + r'\Mods\worlds.json', 'DSML_BUILD': '25846389', 'DSML_APP': GAME,
             'DSML_DATA': DATA, 'DSML_GAME_SAVES': WORK + r'\fake_gamesaves', 'DSL_UI_TEST': UITEST, 'DSL_STEAM': '0',
             'DSMODDED_NO_STEAM': '1', 'DSML_ROOT': ROOT, 'DOORSTOP_DISABLE': '1', 'DSL_UPDATE': '0'}
        e.update(extra or {})                                # a value None leaves that variable out
        return [f'{k}={self.expand(v)}' for k, v in e.items() if v is not None]

    def start(self, env=None, args='', title=True, timeout=240):
        """kill leftovers, park the old logs (moved, never deleted), start the game, wait for the title over the port"""
        t0 = time.time()
        self.kill_all()
        park = WORK + r'\to_delete\logs_' + time.strftime('%Y%m%d-%H%M%S')
        for f in ('starter.log', 'dsload.log', 'player.log', 'player.soft.log', 'rig.log', 'dsload_status.json', r'data\ErrorLog.txt'):
            p = PROF + '\\' + f
            if self.exists(p):
                self.move(p, park + '\\' + f.replace('\\', '_'))
        for d in (WORK + r'\fake_gamesaves', UITEST):
            self.write(d + r'\.keep', b'')
        if not self.screen_mode_ok():
            raise GameError('screen mode is not windowed (3): refusing to start')
        a = self.expand(f'-logFile "{PROF}\\player.log" {args}'.strip())
        if self.target == 'wine':
            a += ' -batchmode -nographics'
        r = self.a('run', exe=self.expand(GAME + r'\DungeonSettlers.exe'), args=a, cwd=self.expand(GAME), env=self.start_env(env), show=True, wait_ms=0)
        self.pid = r['pid']; self.marks = {}
        log(f'game pid {self.pid}')
        self.wait(lambda: (self.ping() or {}).get('pid') == self.pid, 'the control port of the new game', timeout)
        if title:
            self.wait(self.on_title, 'the title screen', timeout - (time.time() - t0))
        log(f'title after {time.time() - t0:.1f} s')
        return time.time() - t0

    def stop(self):
        try:
            self.cmd(cmd='quit', timeout=5)
        except GameError:
            pass
        try:
            self.wait(lambda: not [p for p in self.procs() if p['pid'] == self.pid], 'the game to quit', 15)
        except GameError:
            if self.pid:
                self.a('kill', pid=self.pid)
        self.kill_all()

    def wait_restart(self, old_pid, timeout=300):
        """soft restart: B takes the control port once A has quit (game fix winbot: bind retry). Returns B's pid."""
        p = self.wait(lambda: (lambda r: r['pid'] if r and r.get('pid') != old_pid else None)(self.ping()), 'the restarted game on the port', timeout)
        self.pid = p
        return p

    def logs(self, out):
        os.makedirs(out, exist_ok=True)
        got = []
        for f in LOGS + [r'..\..\crash\consent.json']:
            try:
                self.get(PROF + '\\' + f, os.path.join(out, f.split('\\')[-1]))
                got.append(f)
            except GameError:
                pass
        return got

    # -- screen
    def shot(self, local):
        """the VM screen (vmrun captureScreen: no focus change on the Mac)"""
        os.makedirs(os.path.dirname(os.path.abspath(local)), exist_ok=True)
        r = vmrun('captureScreen', local)
        if r.returncode:
            raise GameError('captureScreen: ' + (r.stdout + r.stderr).strip()[-200:])
        return local

    def ui(self, op, timeout=15):
        """one DSL_UI_TEST command (loader Mods window): cmd.json written whole, result.json matched by seq"""
        seq = f'{time.time():.3f}'
        self.write(UITEST + r'\cmd.json', json.dumps(dict(op, seq=seq)))
        def res():
            r = self.json_file(UITEST + r'\result.json')
            return r if r and str(r.get('seq')) == seq else None
        return self.wait(res, f'ui {op.get("op")}', timeout, 0.2)

    def gshot(self, local, name='winbot'):
        """the game's own frame (DSL_UI_TEST shot), copied to local"""
        r = self.ui({'op': 'shot', 'name': name})
        self.get(UITEST + '\\' + name + '.png', local)
        return r

    # -- multiplayer menu (same flows as the Mac's mac.py, with waits on the menu state)
    def mpm(self):
        return self.cmd(cmd='mpm_status')

    def press(self, name, want=None, timeout=20):
        r = self.cmd(cmd='mpm_press', button=B[name])
        if want:
            self.wait(lambda: self.mpm().get('state') in (want if isinstance(want, (list, tuple)) else [want]), f'menu {want} after {name}', timeout, 0.25)
        return r

    def back_to_menu(self):
        for _ in range(4):
            if self.mpm().get('state') in ('title_join', 'title_host', 'title_friends', 'failed', 'pick_save'):
                self.cmd(cmd='mpm_press', button=B['BACK']); time.sleep(0.3)

    def box(self, action='confirm'):
        return self.cmd(cmd='mpm_box', action=action)

    def load(self, guest_save, timeout=300):
        r = self.cmd(cmd='load_save', file=guest_save, det=0)
        self.wait(self.loaded, 'the save to load', timeout)
        return r

    def host(self, guest_save, timeout=300):
        self.wait(self.on_title, 'the title', 60)
        self.back_to_menu()
        if self.mpm().get('state') != 'title_menu':
            self.press('MULTIPLAYER', 'title_menu')
        self.press('HOST', 'title_host'); self.press('LOAD_SAVE', 'pick_save')
        r = self.cmd(cmd='load_save', file=guest_save, det=0)
        s = self.wait(lambda: (lambda s: s if (s.get('det') or {}).get('locked') and (s.get('mp') or {}).get('started') else None)(self.status()),
                      'host session + lockstep', timeout)
        m = self.mpm()
        return {'load': r, 'port': m.get('port'), 'state': m.get('state'), 'tick': s.get('tick')}

    def join(self, addr, wait='world', timeout=600):
        """title > Multiplayer > Join by address > type > Connect. wait: None | 'lobby' (in the host's lobby, or
        playing) | 'world' (the host's world loaded + lockstep; the v2 lobby holds the world until the host's Start)"""
        self.wait(self.on_title, 'the title', 60)
        self.back_to_menu()
        if self.mpm().get('state') != 'title_menu':
            self.press('MULTIPLAYER', 'title_menu')
        self.press('JOIN_ADDR', 'title_join')
        self.cmd(cmd='mpm_type', text=addr)
        self.press('CONNECT')
        if wait == 'lobby':
            def lobby():
                m = self.mpm()
                if m.get('state') == 'failed':
                    raise SystemExit(f"join {addr} failed: {m.get('msg')}")
                return m if m.get('state') in ('guest_lobby', 'guest_loading', 'playing') else None
            return self.wait(lobby, f'in the lobby of {addr}', timeout, 0.5)
        if wait == 'world':
            return self.wait(self.in_mp_game, f'joined {addr}: world loaded + lockstep', timeout, 1)
        return self.mpm()

    def in_mp_game(self):
        s = self.status(); m = s.get('mp') or {}
        return s if m.get('started') and (s.get('det') or {}).get('locked') and m.get('loads', 0) >= 1 and not s.get('busy') else None

    def start_game(self, timeout=120):
        """host: Start in the lobby (closes it; the guests get the world)"""
        self.press('START')
        return self.wait(lambda: self.mpm().get('state') == 'playing', 'menu state playing after START', timeout)

    def mp(self):
        return self.status().get('mp') or {}


# ------------------------------------------------------------------ CLI
def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return
    if a[0] == '_hub':
        hub_main(); return
    if a[0] == 'hub':
        hub_start(); print(json.dumps(hub_req({'op': 'hub_status'}))); return
    if a[0] == 'hub-status':
        print(json.dumps(hub_req({'op': 'hub_status'}) if hub_alive() else {'hub': False})); return
    if a[0] == 'hub-stop':
        print(json.dumps(hub_req({'op': 'hub_stop'}) if hub_alive() else {'hub': False})); return
    w = Win()
    pidf = os.path.join(STATE, f'game_{TARGET}.pid')
    try:
        w.pid = int(open(pidf).read())
    except (OSError, ValueError):
        pass
    if a[0] == 'up':
        print(json.dumps(w.up()))
    elif a[0] == 'a':
        print(json.dumps(hub_req(json.loads(a[1]))))
    elif a[0] == 'start':
        w.up()
        env = dict(x.split('=', 1) for x in a[1:] if '=' in x)
        w.prepare()
        sec = w.start(env=env)
        open(pidf, 'w').write(str(w.pid))
        print(json.dumps({'pid': w.pid, 'title_s': round(sec, 1)}))
    elif a[0] == 'stop':
        w.stop(); print(json.dumps({'stopped': w.pid}))
    elif a[0] == 'cmd':
        for x in a[1:]:
            print(json.dumps(w.cmd(json.loads(x))))
    elif a[0] == 'status':
        s = w.status(); m = s.get('mp') or {}
        print(json.dumps({'scene': s.get('scene'), 'map': s.get('map'), 'busy': s.get('busy'), 'tick': s.get('tick'),
                          'det_locked': (s.get('det') or {}).get('locked'),
                          'mp': {k: m.get(k) for k in ('started', 'host', 'player', 'loads', 'desyncs', 'desynced', 'present') if k in m},
                          'menu': {k: v for k, v in w.mpm().items() if k in ('state', 'msg', 'notice', 'port', 'box')}}))
    elif a[0] == 'host':
        print(json.dumps(w.host(a[1])))
    elif a[0] == 'join':
        print(json.dumps(w.join(a[1]).get('mp')))
    elif a[0] == 'press':
        print(json.dumps(w.press(a[1])))
    elif a[0] == 'box':
        print(json.dumps(w.box(a[1] if len(a) > 1 else 'confirm')))
    elif a[0] == 'load':
        print(json.dumps(w.load(a[1])))
    elif a[0] == 'ui':
        print(json.dumps(w.ui(json.loads(a[1]))))
    elif a[0] == 'shot':
        print(w.shot(a[1]))
    elif a[0] == 'gshot':
        print(json.dumps(w.gshot(a[1])))
    elif a[0] == 'logs':
        print(json.dumps(w.logs(a[1])))
    else:
        print(__doc__); sys.exit(2)


if __name__ == '__main__':
    main()
