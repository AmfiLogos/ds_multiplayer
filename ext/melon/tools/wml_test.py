#!/usr/bin/env python3
"""MelonLoader on Windows through the loader kit, DSMP bottle (graphics null, audio off), wine lock taken here.
  wml_test.py lock | unlock          take / give back ~/ds_multiplayer/rig/wine.lock (pgrep -fl wine must be empty)
  wml_test.py install                build the kit (MODS = Windows multiplayer + hello-melon) and run its install.bat
  wml_test.py run <out> [limit_s]    profile = multiplayer + hello-melon, start (-batchmode -nographics), wait for the title and
                                     the HELLOMELON lines in MelonLoader's Latest.log, stop, copy logs to <out>
  wml_test.py off <out>              profile = multiplayer only: MelonLoader off (Loader.cfg disable = true), game at the title
The registry of the bottle gets version=native,builtin for DungeonSettlers.exe (MelonLoader's version.dll proxy under wine)."""
import json, os, shutil, subprocess, sys, time
H = os.path.expanduser('~'); M = f'{H}/ds_multiplayer/ext/melon'
sys.path.insert(0, f'{H}/ds_multiplayer/win'); import wloader as w
LOCK = f'{H}/ds_multiplayer/rig/wine.lock'
GAME = f'{w.ROOT}/game'; MODS = f'{w.ROOT}/mods'; ACTIVE = f'{w.PROF}/data/Mods/active.json'
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []
def check(n, ok, d=''):
    checks.append({'name': n, 'ok': bool(ok), 'detail': str(d)[:300]}); log('PASS' if ok else 'FAIL', n, '' if ok else d)

def lock():
    # wine / .exe processes younger than 1 day (the owner's own CrossOver bottles run for days: never ours, never touched)
    busy = subprocess.run(['/bin/sh', '-c', "ps -axo etime=,comm= | grep -iE 'wine|\\.exe$' | grep -ve '-[0-9][0-9]:'"], capture_output=True, text=True).stdout.strip()
    if busy: sys.exit('wine busy: ' + busy[:300])
    os.mkdir(LOCK); open(f'{LOCK}/owner', 'w').write('melon\n'); log('wine lock taken')
def unlock():
    os.makedirs(f'{H}/ds_multiplayer/rig/old', exist_ok=True)
    if os.path.exists(f'{LOCK}/owner'): os.replace(f'{LOCK}/owner', f'{H}/ds_multiplayer/rig/old/wine.lock.owner.melon.{int(time.time())}')
    os.rmdir(LOCK); log('wine lock released')
def owned(): return open(f'{LOCK}/owner').read().strip() == 'melon'

def install():
    assert owned()
    env = dict(os.environ, MODS=f'{H}/ds_multiplayer/win/build/mods/multiplayer {M}/plugin/mods/hello-melon')
    r = subprocess.run(['zsh', f'{H}/ds_launcher_melon/loader/start/win/build_win.sh'], env=env, capture_output=True, text=True)
    log(r.stdout.strip().splitlines()[-2:] if r.stdout else r.stderr[-500:])
    rc = w.install(f'{H}/ds_launcher_melon/loader/build/win/kit'); log('install rc', rc)
    subprocess.run([f'{w.C}/wine', '--bottle', w.BOTTLE, 'reg', 'add', 'HKCU\\Software\\Wine\\AppDefaults\\DungeonSettlers.exe\\DllOverrides',
                    '/v', 'version', '/d', 'native,builtin', '/f'], capture_output=True)
    return rc

def profile(ids):
    os.makedirs(os.path.dirname(ACTIVE), exist_ok=True)
    json.dump({'enabled': ids, 'order': ids}, open(ACTIVE, 'w'))

def mlog():
    try: return open(f'{GAME}/MelonLoader/Latest.log', errors='replace').read()
    except OSError: return ''

def start():
    """wloader.start without its 3-minute title watchdog (MelonLoader's first start downloads + generates for minutes)"""
    assert owned() and w.bottle_silent()
    if not subprocess.run(['pgrep', '-f', 'dsmp_guard.py'], capture_output=True).stdout:
        subprocess.Popen(['python3', f'{H}/ds_multiplayer/win/tools/dsmp_guard.py', '7200'], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DSRIG_PORT=str(w.PORT), DSMP_GUARD_TAG='1', DSL_NO_ALERT='1', WINEDLLOVERRIDES='winhttp=n,b;version=n,b')
    for k in ('DSRIG_DIR', 'DSRIG_RANGES', 'DSL_MODS_DIR', 'DSL_PROFILE', 'DSL_LOG_DIR', 'DSML_SESSION'): env.pop(k, None)
    # not --no-wait: the starter's and the game's output (WINEDEBUG) land in build.noindex/wml_wine.out
    dbg = ['--cx-log', os.environ['WML_CXLOG'], '--debugmsg', os.environ.get('WML_DEBUGMSG', '+seh')] if os.environ.get('WML_CXLOG') else []
    subprocess.Popen(['nice', '-n', '19', f'{w.C}/wine', '--bottle', w.BOTTLE] + dbg + [w.WROOT + '\\Dungeon Settlers Modded.exe',
                      '-batchmode', '-nographics'], env=env, stdout=open(f'{M}/build.noindex/wml_wine.out', 'a'), stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True)
    for _ in range(60):                              # the caller looks for the process right away
        if [p for p, c in w.dsmp_pids() if 'DungeonSettlers' in c]: break
        time.sleep(1)

def title(limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            s = w.cmd({'cmd': 'status'}, timeout=5)
            if s.get('boot_ready'): return s
        except Exception: pass
        if not [p for p, c in w.dsmp_pids() if 'DungeonSettlers' in c or 'Dungeon Settlers Modded' in c]: return None
        time.sleep(5)
    return None

def save(out):
    os.makedirs(out, exist_ok=True)
    for src, dst in ((f'{GAME}/MelonLoader/Latest.log', 'Latest.log'), (f'{w.PROF}/starter.log', 'starter.log'), (f'{w.PROF}/dsload.log', 'dsload.log'),
                     (f'{w.PROF}/dsload_modset.json', 'dsload_modset.json'), (f'{GAME}/UserData/Loader.cfg', 'Loader.cfg')):
        if os.path.exists(src): shutil.copy2(src, f'{out}/{dst}')
    json.dump(checks, open(f'{out}/checks.json', 'w'), indent=1)

def run(out, limit, on):
    profile(['multiplayer', 'hello-melon'] if on else ['multiplayer'])
    old = f'{GAME}/MelonLoader/Latest.log'
    if os.path.exists(old): os.makedirs(f'{M}/old.noindex', exist_ok=True); os.replace(old, f'{M}/old.noindex/win_Latest.{int(time.time())}.log')
    t0 = time.time(); start()
    s = title(limit)
    check('title reached', s, s)
    log('title after', round(time.time() - t0), 's')
    time.sleep(20 if on else 5)
    L = mlog(); D = open(f'{w.PROF}/dsload.log', errors='replace').read() if os.path.exists(f'{w.PROF}/dsload.log') else ''
    st = open(f'{w.PROF}/starter.log', errors='replace').read()
    if on:
        check('starter: melon mod on', 'melon: 1 MelonLoader mod(s) on' in st, st[-600:])
        check('ML: support module loaded', 'Support Module Loaded' in L, [l for l in L.splitlines() if 'Support' in l][:3])
        check('HelloMelon loaded', 'HELLOMELON load' in L)
        check('HelloMelon Harmony patched (2)', L.count('HELLOMELON PATCHED') == 2, [l for l in L.splitlines() if 'PATCH' in l][:4])
        check('HelloMelon Harmony postfix fires', 'HELLOMELON PATCH-HIT' in L)
        check('HelloMelon injected MonoBehaviour Update fires', 'HELLOMELON TICK' in L)
        check('no ML errors', '[ERROR]' not in L, [l for l in L.splitlines() if 'ERROR' in l][:5])
    else:
        cfg = open(f'{GAME}/UserData/Loader.cfg', errors='replace').read() if os.path.exists(f'{GAME}/UserData/Loader.cfg') else ''
        check('starter: MelonLoader off', 'melon: 0 MelonLoader mod(s) on' in st, st[-600:])
        check('Loader.cfg disable = true', 'disable = true' in cfg, cfg[:300])
        check('no HelloMelon line this start', 'HELLOMELON' not in L, L[-300:])
    check('dsload: multiplayer running', 'multiplayer' in D and 'running gameplay set' in D, D[-400:])
    try:
        f1 = w.cmd({'cmd': 'status'}, timeout=5).get('frames', 0); time.sleep(3); f2 = w.cmd({'cmd': 'status'}, timeout=5).get('frames', 0)
        check('multiplayer frame counter moves', f2 > f1 > 0, (f1, f2))
    except Exception as e: check('multiplayer status', False, e)
    w.stop(); time.sleep(3)
    check('copy stopped', not [p for p, c in w.dsmp_pids() if 'DungeonSettlers' in c], w.dsmp_pids())
    save(out); log(f"{sum(c['ok'] for c in checks)}/{len(checks)} checks")

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'lock': lock()
    elif a[0] == 'unlock': unlock()
    elif a[0] == 'install': sys.exit(install())
    elif a[0] == 'run': run(os.path.expanduser(a[1]), int(a[2]) if len(a) > 2 else 540, True)
    elif a[0] == 'off': run(os.path.expanduser(a[1]), 300, False)
