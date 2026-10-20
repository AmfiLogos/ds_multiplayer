#!/usr/bin/env python3
"""Host port setting (multiplayer mod.json "host_port", scope local) on a leased pair of hidden loader copies.

Run 1: host's data/Mods/settings.json = {"multiplayer": {"host_port": PORT}}. Host: title > Multiplayer > Host > Load
save (menu flow, mpmenu MPA_HOST_START). Checks: the session listens on PORT (mpm_status port), the lobby's
"open port N" line names PORT (lw.inv_note), the guest (no setting = default) joins 127.0.0.1:PORT by the menu.
Run 2: no setting on either copy. Checks: the host listens on 47777, the guest joins "127.0.0.1" (default port).
Both runs: gcd-cdr (a gameplay mod) on both copies, so the mod-set hash ("modset" in data/Mods/worlds.json) is
non-empty; it must be identical for host run 1, host run 2 and the guest (host_port never enters it).

  HP_BUILD=<loader build folder: dsload.dylib + mods/multiplayer + mods/gcd-cdr> hostport_pair.py [--port 12345]

Copies HPH / HPG in ~/ds_launcher/loader/test_env/instances (APFS clones of Z: own bundle id, background-only,
windowed 960x540, muted by the rig env, DSL_NOTCH=0, nice 5). Lease from ~/ds_multiplayer/rig/rig.py. Never rm."""
import glob, json, os, shutil, signal, socket, subprocess, sys, threading, time

LT = os.path.expanduser('~/ds_launcher/loader/test')
sys.path.insert(0, LT)
import lrig  # noqa: E402
sys.path.insert(1, os.path.expanduser('~/ds_multiplayer/rig'))
import rig as realrig  # noqa: E402  (leases only)

H, G = 'HPH', 'HPG'
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, JOIN_ADDR=14, CONNECT=15)
PORT = int(sys.argv[sys.argv.index('--port') + 1]) if '--port' in sys.argv else 12345
BUILD = os.environ['HP_BUILD']
GP = 'gcd-cdr'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
checks = []
stop_guard = threading.Event()


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]})
    log('PASS' if ok else 'FAIL', name, detail)


def c(x, **k):
    return lrig.send(x, k, timeout=120)


def st(x):
    try:
        return c(x, cmd='status')
    except Exception:
        return {}


def ms(x):
    try:
        return c(x, cmd='mpm_status')
    except Exception:
        return {}


def wait(what, pred, timeout=600, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
        except (OSError, ValueError, RuntimeError, KeyError):
            r = None
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def park(p):
    if os.path.lexists(p):
        d = os.path.join(os.path.dirname(p), 'old'); os.makedirs(d, exist_ok=True)
        os.replace(p, os.path.join(d, f'{os.path.basename(p)}.{time.time():.0f}'))


def exe(x):
    return os.path.join(lrig.idir(x), lrig.EXE_REL)


def pids(x):
    out = subprocess.run(['pgrep', '-f', exe(x)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():
    while not stop_guard.is_set():
        ps = pids(H) + pids(G)
        if ps:
            bad = [w for w in lrig.windows(ps) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = lrig.front_pid()
            if bad or fp in ps:
                for p in ps:
                    try:
                        os.kill(p, signal.SIGKILL)
                    except OSError:
                        pass
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(1.0)


def make_copy(x):
    app = os.path.join(lrig.idir(x), 'DungeonSettlers.app')
    if not os.path.exists(app):
        src = os.path.join(lrig.INST, 'Z', 'DungeonSettlers.app')
        os.makedirs(lrig.idir(x), exist_ok=True)
        subprocess.run(['cp', '-c', '-R', src, app], check=True)
        pl = os.path.join(app, 'Contents', 'Info.plist')
        for k, t, v in (('CFBundleIdentifier', '-string', f'com.CanOpener.Dungeon-Settlers.mp{x}'), ('CFBundleName', '-string', f'DS rig {x}'),
                        ('LSBackgroundOnly', '-bool', 'YES'), ('LSUIElement', '-bool', 'YES')):
            subprocess.run(['plutil', '-replace', k, t, v, pl], check=True)
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)
    lrig.require_background_only(x)
    for d in ('home', 'data/Saves', 'data/Mods', 'crashrep'):
        os.makedirs(os.path.join(lrig.idir(x), d), exist_ok=True)
    open(os.path.join(lrig.idir(x), 'crashrep', 'answer'), 'w').write('no')


def kit(x, settings):
    I = lrig.idir(x); k = os.path.join(I, 'kit'); park(k)
    md = os.path.join(k, 'loader', 'mods'); os.makedirs(md); os.makedirs(os.path.join(k, 'mods')); os.makedirs(os.path.join(k, 'update'))
    shutil.copy2(os.path.join(BUILD, 'dsload.dylib'), os.path.join(k, 'loader', 'dsload.dylib'))
    shutil.copy2(os.path.join(os.path.dirname(os.path.dirname(BUILD)), 'RELEASE'), os.path.join(k, 'loader', 'RELEASE'))
    for i in ('multiplayer', GP):
        shutil.copytree(os.path.join(BUILD, 'mods', i), os.path.join(md, i))
    mods = os.path.join(I, 'data', 'Mods')
    for f in glob.glob(os.path.join(mods, '*.json')):
        park(f)
    json.dump({'enabled': ['multiplayer', GP], 'order': ['multiplayer', GP]}, open(os.path.join(mods, 'active.json'), 'w'))
    if settings:
        json.dump(settings, open(os.path.join(mods, 'settings.json'), 'w'))
    for f in ('rig.log', 'dsload.log', 'fp.log', 'player.log', 'mp.json', 'clipboard.txt'):
        park(os.path.join(I, f))


def start(x):
    I = lrig.idir(x); k = os.path.join(I, 'kit')
    lrig.require_background_only(x); lrig.prepare_data(x); lrig.require_windowed(x)
    if os.path.exists(lrig.sockpath(x)):
        os.unlink(lrig.sockpath(x))                             # our copy's own socket file
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '30', **lrig.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(k, 'loader', 'dsload.dylib'),
           'DSL_MODS_DIR': os.path.join(k, 'loader', 'mods'), 'DSML_ROOT': k, 'DSML_USER_MODS_DIR': os.path.join(k, 'mods'),
           'DSL_PROFILE': os.path.join(I, 'data', 'Mods', 'active.json'), 'DSML_SESSION': os.path.join(I, 'data', 'Mods', 'worlds.json'),
           'DSML_DATA': os.path.join(I, 'data'), 'DSL_UPDATE_API': 'http://127.0.0.1:9', 'DSL_CRASHREP_TEST': os.path.join(I, 'crashrep'),
           'DSL_SOFT_TEST': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSMP_TEST': '1'}
    env.update(lrig.soft_test_env())
    args = [exe(x), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(lrig.pidfile(x), 'w').write(f'{p.pid}\n')
    log(x, 'started pid', p.pid)


def stop(x):
    for p in pids(x):
        try:
            lrig.send(x, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception:
            pass
        for _ in range(50):
            if p not in pids(x):
                break
            time.sleep(0.1)
        if p in pids(x):
            os.kill(p, signal.SIGKILL); log(x, 'killed pid', p)


def modset(x):
    try:
        return json.load(open(os.path.join(lrig.idir(x), 'data', 'Mods', 'worlds.json'))).get('modset')
    except Exception:
        return None


def port_free(p):
    s = socket.socket()
    try:
        s.bind(('0.0.0.0', p)); return True
    except OSError:
        return False
    finally:
        s.close()


def press(x, n):
    r = c(x, cmd='mpm_press', button=B[n]); time.sleep(1.2); return r


def one_run(name, host_set, want_port, join_text):
    kit(H, host_set); kit(G, None)
    start(H); start(G)
    for x in (H, G):
        lrig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'title {x}', timeout=1500)
    time.sleep(3)
    hp = {x: ms(x).get('host_port') for x in (H, G)}
    check(f'{name}: setting read at the title (host {want_port}, guest without setting 47777)', hp[H] == want_port and hp[G] == 47777, hp)
    press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'hostport_base.json'); shutil.copy2(lrig.SAVE, src)
    log(H, 'stock list pick', c(H, cmd='load_save', file=src, det=0))
    m = wait('host listening', lambda: (lambda r: r if r.get('port') else None)(ms(H)), timeout=900, every=2)
    check(f'{name}: host listens on {want_port}', m.get('port') == want_port, f"port {m.get('port')} direct {m.get('direct')}")
    note = wait('lobby invite note', lambda: (lambda r: r.get('lw', {}).get('inv_note') or None)(ms(H)), timeout=120, every=1)
    lw = ms(H).get('lw', {})
    check(f'{name}: lobby line names port {want_port}', f'port {want_port} ' in note and lw.get('addr', '').endswith(f':{want_port}'),
          f"inv_note {note!r} addr {lw.get('addr')!r}")
    press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
    c(G, cmd='mpm_type', text=join_text); press(G, 'CONNECT')
    try:
        sg = wait('guest joined', lambda: (lambda s: s if s.get('mp', {}).get('loads', 0) >= 1 and s.get('mp', {}).get('started') else None)(st(G)), timeout=900, every=2)
        check(f'{name}: guest joined by address {join_text!r}', True, f"map {sg.get('map')} tick {sg.get('tick')}")
    except RuntimeError as e:
        check(f'{name}: guest joined by address {join_text!r}', False, f'{e}; guest mpm msg {ms(G).get("msg")!r}')
    res = {'host_modset': modset(H), 'guest_modset': modset(G)}
    for x in (H, G):
        for f in ('rig.log', 'dsload.log'):
            p = os.path.join(lrig.idir(x), f)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f'{name}_{x}_{f}'))
    stop(H); stop(G)
    return res


def main():
    assert lrig.check_prefs(), 'owner prefs changed before the run'
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < 3600:                   # the machine cap (4 pairs): wait for a free one
        pair = realrig.lease('hostport')
        if not pair:
            time.sleep(30)
    assert pair, 'no free pair for 60 min'
    log('pair', pair)
    threading.Thread(target=guard, daemon=True).start()
    try:
        for x in (H, G):
            make_copy(x)
        assert port_free(PORT), f'{PORT} is taken on this Mac'
        r1 = one_run('setting', {'multiplayer': {'host_port': PORT}}, PORT, f'127.0.0.1:{PORT}')
        rs = [r1]
        if port_free(47777) and '--no-default' not in sys.argv:   # never bind 47777 while another game (the owner's) listens there
            rs.append(one_run('default', None, 47777, '127.0.0.1'))
        else:
            log('default run skipped: 47777 is in use on this Mac (or --no-default)')
        hs = {r[k] for r in rs for k in ('host_modset', 'guest_modset')}
        check('mod-set hash identical (host with host_port, host default, guests)', len(hs) == 1 and None not in hs and '' not in hs, rs)
    except Exception as e:
        check('run completed', False, repr(e))
    finally:
        stop_guard.set()
        for x in (H, G):
            stop(x)
        realrig.release(pair, 'hostport')
        json.dump(checks, open(os.path.join(OUT, 'results.json'), 'w'), indent=1)
        log('RESULT', 'ALL PASS' if checks and all(k['ok'] for k in checks) else 'FAIL', OUT)
        log('prefs identical:', lrig.check_prefs())


if __name__ == '__main__':
    main()
