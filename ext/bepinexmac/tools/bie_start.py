#!/usr/bin/env python3
"""Start one rig copy (batchmode, guarded) with BepInEx 6 IL2CPP arm64 (Doorstop) [+ dsrig.dylib].
usage: bie_start.py X [--rig] [--inst DIR] [--dsload DYLIB]   (DIR default ext/bepinexmac/inst.noindex/bie)"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

x = sys.argv[1]
B = os.path.expanduser('~/ds_multiplayer/ext/bepinexmac/inst.noindex/bie')
if '--inst' in sys.argv:
    B = os.path.abspath(sys.argv[sys.argv.index('--inst') + 1])
if rig.alive(rig.read_pid(x)):
    sys.exit(f'{x} already running')
rig.prepare_data(x)
rig.fix_mp_port(x)
if os.path.exists(rig.sockpath(x)):
    os.unlink(rig.sockpath(x))
home = os.path.join(rig.idir(x), 'home')
inject = [os.path.join(B, 'libdoorstop.dylib')]
if '--dsload' in sys.argv:                       # launcher order: dsload first, then Doorstop
    inject.insert(0, os.path.abspath(sys.argv[sys.argv.index('--dsload') + 1]))
if '--rig' in sys.argv:
    inject.append(rig.DYLIB)
    inject.append(os.path.expanduser('~/ds_multiplayer/research/loadertest.noindex/envrestore.dylib'))
env = {'HOME': home, 'CFFIXED_USER_HOME': home, 'USER': os.environ.get('USER', ''),
       'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin',
       'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': rig.idir(x), 'DSRIG_FPS': '30',
       'DSRIG_BATCH': '1', 'DYLD_INSERT_LIBRARIES': ':'.join(inject),
       'DOORSTOP_ENABLED': '1', 'DOORSTOP_TARGET_ASSEMBLY': os.path.join(B, 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'),
       'DOORSTOP_CLR_RUNTIME_CORECLR_PATH': os.path.join(B, 'dotnet/libcoreclr.dylib'),
       'DOORSTOP_CLR_CORLIB_DIR': os.path.join(B, 'dotnet'), 'DOORSTOP_IGNORE_DISABLED_ENV': '0'}
env.update({k: v for k, v in os.environ.items() if k.startswith(('DSRIG_', 'BEPINEX_', 'DOTNET_', 'COREHOST_', 'DSL_')) and k not in env})
env['DSML_INSERT'] = env['DYLD_INSERT_LIBRARIES']
args = [os.path.join(rig.idir(x), rig.EXE_REL), '-logFile', os.path.join(rig.idir(x), 'player.log'),
        '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540', '-batchmode']
spec = {'args': args, 'env': env, 'front_before': rig.front_pid() or 0, 'bg': True, 'show': False}
open(os.path.join(rig.idir(x), 'launch.json'), 'w').write(json.dumps(spec))
open(rig.pidfile(x), 'w').write('')
g = subprocess.Popen([sys.executable, os.path.abspath(rig.__file__), '_guard', x], stdin=subprocess.DEVNULL,
                     stdout=open(os.path.join(rig.idir(x), 'guard.log'), 'ab'), stderr=subprocess.STDOUT,
                     start_new_session=True)
for _ in range(100):
    if rig.alive(rig.read_pid(x)):
        break
    time.sleep(0.1)
print(f'{x}: pid {rig.read_pid(x)} guard {g.pid}')
