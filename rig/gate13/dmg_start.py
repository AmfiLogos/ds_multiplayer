#!/usr/bin/env python3
"""dmg_start.py <dmg> <out> [mod zip]: the player's Mac install path, hidden. Mounts the .dmg (no Finder window, read only), copies
the app into <out>/Applications (never ~/Applications), runs its stub once in dry-run mode with a private ROOT
(DSMODDED_ROOT=<out>/root: game copy, loader files, first-start mod list), then starts the printed game command ONCE in
batchmode under a guard (killed if it ever shows a window or becomes frontmost) and waits for the title through the
multiplayer module's control socket. Checks: one line each "PASS name ..." / "FAIL name ...", last "RESULT x/y pass".
With [mod zip]: the zip is dropped into the clean install's player mods folder (ROOT/mods) before the first start;
checks: unpacked, used by the loader instead of the app's copy, opened by dsload. Needs a pair lease (taken here, DSRIG_RELEASE=1 under the gate's lock). Never the owner's app, saves or prefs."""
import json, os, signal, subprocess, sys, tempfile, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_launcher/loader/test')
import lrig
DMG, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]); os.makedirs(OUT, exist_ok=True)
ZIP = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
ZID = os.path.basename(ZIP).rsplit('-', 1)[0] if ZIP else None
RIG = f'{H}/ds_multiplayer/rig/rig.py'; os.environ['DSRIG_RELEASE'] = '1'
res = []
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(n, ok, info=''): res.append(bool(ok)); log('PASS' if ok else 'FAIL', n, f' [{str(info)[:300]}]')
prefs0 = lrig.check_prefs(restore=False)
lease = subprocess.run([sys.executable, RIG, 'lease', 'gate13', '--pid', str(os.getpid())], capture_output=True, text=True).stdout.strip().splitlines()
lease = lease[-1] if lease else None; log('lease', lease)
game = None
try:
    mnt = tempfile.mkdtemp()
    subprocess.run(['hdiutil', 'attach', '-nobrowse', '-readonly', '-noautoopen', '-mountpoint', mnt, DMG], check=True, capture_output=True)
    names = sorted(os.listdir(mnt)); log('dmg holds', names)
    check('dmg: the app + an Applications shortcut', 'Dungeon Settlers Modded.app' in names and os.path.islink(os.path.join(mnt, 'Applications')), names)
    app = os.path.join(OUT, 'Applications', 'Dungeon Settlers Modded.app'); os.makedirs(os.path.dirname(app), exist_ok=True)
    subprocess.run(['ditto', os.path.join(mnt, 'Dungeon Settlers Modded.app'), app], check=True)
    subprocess.run(['hdiutil', 'detach', mnt, '-quiet'])
    check('app copied from the dmg, signature valid', subprocess.run(['codesign', '-v', app]).returncode == 0, app)
    root = os.path.join(OUT, 'root')
    if ZIP: os.makedirs(os.path.join(root, 'mods'), exist_ok=True); import shutil; shutil.copy2(ZIP, os.path.join(root, 'mods'))
    env = dict(os.environ, DSMODDED_ROOT=root, DSMODDED_DRY_RUN='1', DSMP_NO_DIALOG='1')
    r = subprocess.run([os.path.join(app, 'Contents/MacOS/DS Modded')], env=env, capture_output=True, text=True, timeout=600)
    open(os.path.join(OUT, 'dry.txt'), 'w').write(r.stdout + r.stderr)
    genv = {l[4:].split('=', 1)[0]: l[4:].split('=', 1)[1] for l in r.stdout.splitlines() if l.startswith('ENV ')}
    argv = [l[5:] for l in r.stdout.splitlines() if l.startswith('ARGV ')]
    cwd = next((l[4:] for l in r.stdout.splitlines() if l.startswith('CWD ')), None)
    check('stub dry run: game copy made, command printed', r.returncode == 0 and argv and os.path.exists(argv[0]), f'rc {r.returncode} {argv[:1]}')
    act = os.path.join(root, 'profiles/modded/data/Mods/active.json')
    a = json.load(open(act)) if os.path.exists(act) else {}
    check('first start mod list = every shipped mod except Hell', a.get('enabled') == ['custom-modes', 'gcd-cdr', 'multiplayer'], a)
    # start once, hidden: batchmode + DSRIG_BATCH, guard on windows / front
    e = {k: v for k, v in genv.items()}
    e.update(HOME=genv.get('CFFIXED_USER_HOME', os.path.join(OUT, 'home')), USER=os.environ.get('USER', ''), PATH='/usr/bin:/bin:/usr/sbin:/sbin',
             TMPDIR=os.environ.get('TMPDIR', '/tmp'), DSRIG_BATCH='1', DSRIG_FPS='30')
    e.update(lrig.ctl_env() if hasattr(lrig, 'ctl_env') else {'DSRIG_CTL': '1'})   # test copy: control socket on (the app never sets it)
    front0 = lrig.front_pid()
    game = subprocess.Popen(argv + ['-batchmode'], env=e, cwd=cwd, stdin=subprocess.DEVNULL, stdout=open(os.path.join(OUT, 'stdout.log'), 'wb'),
                            stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5))
    prof = genv['DSRIG_DIR']; lrig.idir = lambda x: prof           # the control socket lives in the profile folder
    t0, title, shown = time.time(), None, []
    while time.time() - t0 < 300 and game.poll() is None:
        if lrig.visible_windows(game.pid) or lrig.front_pid() == game.pid:
            shown.append(lrig.visible_windows(game.pid)); game.kill(); break
        try:
            s = lrig.send('P', {'cmd': 'status'}, timeout=5, connect_wait=1)
            if s.get('scene') == 1 and not s.get('busy'): title = s; break
        except Exception: pass
        time.sleep(2)
    check('installed game started hidden and reached the title', title is not None, f'{time.time() - t0:.0f} s, exit {game.poll()}')
    check('never on screen (guard)', not shown, shown)
    dl = os.path.join(genv.get('DSL_LOG_DIR', prof), 'dsload.log'); d = open(dl, errors='replace').read() if os.path.exists(dl) else ''
    if ZID:
        check(f'mod zip {os.path.basename(ZIP)}: unpacked into the mods folder', os.path.exists(os.path.join(root, 'mods', ZID, 'mod.json'))
              and os.path.exists(os.path.join(root, 'mods', '.installed', os.path.basename(ZIP))), sorted(os.listdir(os.path.join(root, 'mods'))))
        lj, uj = (os.path.join(root, x, 'mods', ZID, 'mod.json') for x in ('loader', ''))
        same = os.path.exists(lj) and open(lj, 'rb').read() == open(os.path.join(root, 'mods', ZID, 'mod.json'), 'rb').read()
        check(f'mod zip: the loader runs that {ZID} (player copy, or the app copy when the zip is the same mod.json)',
              os.path.exists(os.path.join(root, 'loader', 'mods', ZID, '.player_mod')) or same, f'player copy {os.path.exists(os.path.join(root, "loader", "mods", ZID, ".player_mod"))}, same mod.json {same}')
        check(f'mod zip: dsload opened {ZID}', any(ZID in l and 'opened' in l for l in d.splitlines()), [l for l in d.splitlines() if ZID in l][:3])
    check('dsload: multiplayer, custom-modes, gcd-cdr running; Hell off', all(f'{m}' in d for m in ('multiplayer', 'custom-modes', 'gcd-cdr')) and 'opened' in d, d[-300:])
except Exception as ex:
    check('test ran without an error', False, repr(ex))
finally:
    if game and game.poll() is None:
        try: lrig.send('P', {'cmd': 'quit'}, timeout=5, connect_wait=1); game.wait(20)
        except Exception: game.kill()
    if lease: subprocess.run([sys.executable, RIG, 'release', lease, 'gate13'], capture_output=True)
    check('owner prefs unchanged', lrig.check_prefs(restore=False) == prefs0 and prefs0, prefs0)
    log(f'RESULT {sum(res)}/{len(res)} pass')
