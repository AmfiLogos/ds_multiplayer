#!/usr/bin/env python3
"""ML core on Mac without the loader's kind "melon" code: test copy Q (lrig, hidden, staged off-screen), dsload + multiplayer,
our MelonLoader build injected after dsload (MELONLOADER_BASEDIR, MELONLOADER_MOD_DIRS = HelloMelon folder).
usage: ml_core_test.py <out_dir> [fresh]   fresh = new ML base folder (interop generated again, minutes)"""
import json, os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher_melon/loader/test'))
import l1_test as t
X = 'Q'; t.X = X; t.I = t.r.idir(X); t.SDIR = os.path.join(t.I, 'softrestart'); t.MODS = os.path.join(t.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1]); os.makedirs(OUT, exist_ok=True); t.OUT = OUT
M = os.path.expanduser('~/ds_multiplayer/ext/melon')
BASE = os.path.join(t.I, 'melon_base')
MODDIR = os.path.join(t.I, 'melon_mods', 'hello-melon')
APP = os.path.join(t.I, 'DungeonSettlers.app')


def mlog():
    try:
        return open(os.path.join(BASE, 'MelonLoader', 'Latest.log'), errors='replace').read()
    except OSError:
        return ''


def main():
    if 'fresh' in sys.argv[2:] and os.path.exists(BASE):
        os.makedirs(os.path.join(t.I, 'old'), exist_ok=True); os.replace(BASE, os.path.join(t.I, 'old', 'melon_base.%d' % time.time()))
    if not os.path.exists(BASE):
        subprocess.run(['cp', '-c', '-R', os.path.join(M, 'inst.noindex', 'ml'), BASE], check=True)
    os.makedirs(MODDIR, exist_ok=True)
    shutil.copy2(os.path.join(M, 'plugin/HelloMelon/bin/Release/net6.0/HelloMelon.dll'), os.path.join(MODDIR, 'HelloMelon.dll'))
    lease = None
    env = {'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents/Frameworks/dsload.dylib') + ':' + os.path.join(BASE, 'MelonLoader.Bootstrap.dylib'),
           'MELONLOADER_BASEDIR': BASE, 'MELONLOADER_MOD_DIRS': os.path.dirname(MODDIR) + '/hello-melon'}
    if 'hotbar' in sys.argv[2:]:                    # the Nexus mod Extended Hotbar (ML build) instead of HelloMelon
        hd = os.path.join(t.I, 'melon_mods', 'extended-hotbar')
        if not os.path.exists(hd):
            subprocess.run(['cp', '-c', '-R', os.path.expanduser('~/ds_multiplayer/research/loadertest.noindex/hotbar_ml/Mods'), hd], check=True)
        env['MELONLOADER_MOD_DIRS'] = hd
    if 'probe' in sys.argv[2:]:                     # + IcallProbe (native call of a stripped icall through MelonLoader's injector)
        pd = os.path.join(t.I, 'melon_mods', 'icall-probe'); os.makedirs(pd, exist_ok=True)
        shutil.copy2(os.path.join(M, 'plugin/IcallProbe/bin/Release/net6.0/IcallProbe.dll'), os.path.join(pd, 'IcallProbe.dll'))
        env['MELONLOADER_MOD_DIRS'] += ':' + pd
    import threading
    g = threading.Thread(target=t.guard, daemon=True); g.start()
    summary = {}
    try:
        t_l = time.time()                              # the copy counts as a pair: wait for a lease (max 4 machine-wide)
        while not lease and time.time() - t_l < 1200:
            r = subprocess.run(['python3', os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'lease', 'melon', '--pid', str(os.getpid())],
                               capture_output=True, text=True)
            lease = r.stdout.strip().split()[-1:] if r.returncode == 0 and r.stdout.strip() else None
            if not lease: time.sleep(15)
        t.log('lease', lease)
        if not lease: raise RuntimeError('no pair lease in 20 min')
        t0 = time.time()
        a = t.start_a(('multiplayer',), env)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title', 540)
        summary['title_s'] = round(time.time() - t0, 1)
        time.sleep(15)
        L = t.dlog(); ML = mlog()
        if 'hotbar' in sys.argv[2:]:
            time.sleep(30)
            alive = [p for p in t.game_pids()]
            f1 = t.status().get('frames', 0); time.sleep(3); f2 = t.status().get('frames', 0)
            ML = mlog(); hb = [l for l in ML.splitlines() if 'Extended Hotbar' in l or 'DungeonSettlers10Slots' in l or 'Hotbar' in l]
            open(os.path.join(OUT, 'hotbar_lines.txt'), 'w').write('\n'.join(hb))
            t.check('Extended Hotbar loaded by MelonLoader', any('Extended Hotbar v' in l for l in hb), hb[:3])
            t.check('game stable 45 s after the title (frames move)', alive and f2 > f1 > 0, (alive, f1, f2))
            raise SystemExit
        t.check('dsload: multiplayer hooks MainFlow.Update', 'MainFlow.Update' in L)
        t.check('ML: support module loaded', 'No Support Module Loaded' not in ML and 'Support Module' in ML, [l for l in ML.splitlines() if 'Support Module' in l][:3])
        t.check('HelloMelon loaded', 'HELLOMELON load' in ML)
        t.check('HelloMelon Harmony patched (2)', ML.count('HELLOMELON PATCHED') == 2, [l for l in ML.splitlines() if 'PATCH' in l][:4])
        t.check('HelloMelon Harmony postfix fires', 'HELLOMELON PATCH-HIT' in ML)
        t.check('HelloMelon injected MonoBehaviour Update fires', 'HELLOMELON TICK' in ML)
        f1 = t.status().get('frames', 0); time.sleep(3); f2 = t.status().get('frames', 0)
        t.check('dsload hook fires (multiplayer frame counter moves)', f2 > f1 > 0, (f1, f2))
        t.check('no ML errors', '[ERROR]' not in ML, [l for l in ML.splitlines() if 'ERROR' in l][:5])
        t.shot('A_title')
    except SystemExit:
        pass
    except Exception as e:
        t.check('run finished', False, e)
    finally:
        t.stop_guard.set()
        for p in t.game_pids():
            try:
                t.r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception:
                pass
            time.sleep(3)
            if t.alive(p):
                os.kill(p, 9)
        if lease:
            subprocess.run(['python3', os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'release', lease[0], 'melon'])
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(t.I, f)): shutil.copy2(os.path.join(t.I, f), os.path.join(OUT, f))
        if os.path.exists(os.path.join(BASE, 'MelonLoader', 'Latest.log')): shutil.copy2(os.path.join(BASE, 'MelonLoader', 'Latest.log'), os.path.join(OUT, 'Latest.log'))
        summary.update({'checks': t.checks, 'violations': t.violations})
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        t.log(f"{sum(c['ok'] for c in t.checks)}/{len(t.checks)} checks, violations {len(t.violations)}", summary.get('title_s'))


if __name__ == '__main__':
    main()
