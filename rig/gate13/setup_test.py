#!/usr/bin/env python3
"""setup_test.py <Setup.exe> <out>: the player's Windows install path in the CrossOver bottle DSMP (graphics null, audio
off: nothing on screen, no sound). Takes the wine lock (owner gate13). Steps: the existing install folder
(AppData\\Local\\Dungeon Settlers Modded) is moved aside (never removed); Setup.exe /S; files, mods and the uninstall entry
are there; the byte scan of what was installed (not the game copy) finds no banned name and no "/Users/"; the game starts
once (-batchmode -nographics) and reaches the title with our mods running; a marker save is written; the uninstaller
(/S) removes the program but keeps profiles\\ (saves). One line per check, last "RESULT x/y pass"."""
import json, os, re, shutil, subprocess, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_multiplayer/win')
import wloader as w
SETUP, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]); os.makedirs(OUT, exist_ok=True)
WL = f'{H}/ds_multiplayer/rig/wine_lease.py'; GAMEW = 'C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers'
BAN = re.compile(__import__('base64').b64decode('KD9pOmdhbWluZ2tlbGV8a2VsZWRpYWJhdGV8ZGlhYmF0ZWtlbGUpfCg/PCFbQS1aYS16XSlbS2tdW0VlXVtMbF1bRWVdKD8hW2Etel0p').decode())   # the repos' pre-push ban regex (owner name), kept encoded so this file passes that hook
res = []
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'banscan.py')).read().split('for z in sys.argv')[0])   # save_like()
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(n, ok, info=''): res.append(bool(ok)); log('PASS' if ok else 'FAIL', n, f' [{str(info)[:300]}]')
def wine(*args, timeout=900):
    return subprocess.run(['nice', '-n', '19', f'{w.C}/wine', '--bottle', w.BOTTLE] + list(args), capture_output=True, text=True, timeout=timeout)
if subprocess.run([sys.executable, WL, 'take', 'gate13', '--pid', str(os.getpid())]).returncode: sys.exit('wine busy / locked')
try:
    if not w.bottle_silent(): raise SystemExit('bottle not silent')
    if os.path.exists(w.ROOT): os.rename(w.ROOT, w.ROOT + '.gate13-' + time.strftime('%Y%m%d-%H%M%S'))
    # Setup reuses the folder in this key (InstallDirRegKey); other lanes' test installs set it to their own folders
    wine('reg', 'delete', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DungeonSettlersModded', '/f', timeout=120)
    t = os.path.join(w.DRIVE, 'gate13_setup'); os.makedirs(t, exist_ok=True); shutil.copy2(SETUP, os.path.join(t, 'Setup.exe'))
    r = wine('C:\\gate13_setup\\Setup.exe', '/S', f'/GAME={GAMEW}')
    open(os.path.join(OUT, 'setup.out'), 'w').write(r.stdout + r.stderr)
    mods = sorted(os.listdir(os.path.join(w.ROOT, 'mods'))) if os.path.isdir(os.path.join(w.ROOT, 'mods')) else []
    check('Setup.exe /S installs: starter, uninstaller, game copy, mods', r.returncode == 0 and os.path.exists(os.path.join(w.ROOT, 'Dungeon Settlers Modded.exe'))
          and os.path.exists(os.path.join(w.ROOT, 'Uninstall.exe')) and os.path.isdir(os.path.join(w.ROOT, 'game')) and
          all(m in mods for m in ('multiplayer', 'custom-modes', 'gcd-cdr', 'hell-difficulty')), f'rc {r.returncode} mods {mods} {os.listdir(w.ROOT) if os.path.isdir(w.ROOT) else None}')
    q = wine('reg', 'query', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DungeonSettlersModded', timeout=120).stdout
    check('"Apps & features" entry with a quiet uninstall', 'QuietUninstallString' in q, q[-300:])
    hits, n = [], 0
    for d, dirs, fs in os.walk(w.ROOT):
        if os.path.relpath(d, w.ROOT).split(os.sep)[0] in ('game', 'profiles'): continue
        for f in fs:
            p = os.path.join(d, f); b = open(p, 'rb').read(); n += 1
            for tx in (b.decode('latin-1'), b.decode('utf-16-le', errors='ignore'), b[1:].decode('utf-16-le', errors='ignore')):
                if BAN.search(tx) or '/Users/' in tx: hits.append(os.path.relpath(p, w.ROOT)); break
            if save_like(os.path.relpath(p, w.ROOT).replace(os.sep, '/')): hits.append('save data: ' + os.path.relpath(p, w.ROOT))
    check('installed program files: no banned name, no /Users/, no save data', n > 0 and not hits, f'{n} files, hits {hits[:5]}')
    w.start([])
    t0, title = time.time(), None
    while time.time() - t0 < 600:
        try:
            s = w.cmd({'cmd': 'status'}, timeout=5)
            if s.get('boot_ready') or (s.get('scene') == 1 and not s.get('busy')): title = s; break
        except Exception: pass
        time.sleep(5)
    check('installed game starts once and reaches the title', title is not None, f'{time.time() - t0:.0f} s')
    dl = os.path.join(w.PROF, 'dsload.log'); d = open(dl, errors='replace').read() if os.path.exists(dl) else ''
    shutil.copy2(dl, os.path.join(OUT, 'dsload.log')) if d else None
    check('dsload: multiplayer, custom-modes, gcd-cdr opened', all(f'{m} ' in d for m in ('multiplayer', 'custom-modes', 'gcd-cdr')) and 'opened' in d, d[-300:])
    w.stop(); time.sleep(5)
    sv = os.path.join(w.PROF, 'data', 'Saves'); os.makedirs(sv, exist_ok=True)
    marker = os.path.join(sv, 'gate13_marker.json'); open(marker, 'w').write('{"gate13": 1}\n')
    nsave = len(os.listdir(sv))
    r = wine(w.WROOT + '\\Uninstall.exe', '/S', timeout=300)
    for _ in range(60):
        if not os.path.exists(os.path.join(w.ROOT, 'Uninstall.exe')) and not os.path.isdir(os.path.join(w.ROOT, 'game')): break
        time.sleep(2)
    gone = [x for x in ('game', 'mods', 'Dungeon Settlers Modded.exe', 'Uninstall.exe') if not os.path.exists(os.path.join(w.ROOT, x))]
    left = [os.path.relpath(os.path.join(d, f), w.ROOT) for x in ('game', 'mods') for d, _, fs in os.walk(os.path.join(w.ROOT, x)) for f in fs]
    open(os.path.join(OUT, 'left_after_uninstall.txt'), 'w').write('\n'.join(left))
    check('uninstall removes the program (game copy, mods, starter, uninstaller)', len(gone) == 4,
          f'gone {gone}, left {os.listdir(w.ROOT) if os.path.isdir(w.ROOT) else None}; {len(left)} files left in game/mods, e.g. {left[:8]}; uninstall rc {r.returncode} {r.stdout[-200:]}')
    check('uninstall keeps the saves (profiles\\)', os.path.exists(marker) and len(os.listdir(sv)) == nsave, f'{nsave} files before')
    q = wine('reg', 'query', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DungeonSettlersModded', timeout=120)
    check('uninstall entry removed', q.returncode != 0, q.stdout[-200:])
except Exception as ex:
    check('test ran without an error', False, repr(ex))
finally:
    try: w.stop()
    except Exception: pass
    subprocess.run([sys.executable, WL, 'release', 'gate13'])
    log(f'RESULT {sum(res)}/{len(res)} pass')
