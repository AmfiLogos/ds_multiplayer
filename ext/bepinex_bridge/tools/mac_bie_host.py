#!/usr/bin/env python3
"""Start a leased rig copy X the way the l1-core launcher starts a world with BepInEx on Mac: dsload.dylib (l1-core loader,
built in l1/loader from the bepinex-bridge commit) + Doorstop (bepinex-mac 0.1.1 package in l1/bie/0.1.1), mods folder
l1/loader/build/mods (multiplayer.dylib, opened RTLD_LOCAL by the loader), BEPINEX_PLUGIN_DIRS = mods/dsmp-bridge +
build/demo_plugin. Hidden batchmode copy with the rig guard (same as ext/bepinexmac/tools/bie_start.py).
import: start(x) ; CLI: mac_bie_host.py X"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
L = os.path.expanduser('~/ds_multiplayer/ext/bepinex_bridge')
B = os.path.join(L, 'l1/bie/0.1.1'); LD = os.path.join(L, 'l1/loader/build')

def start(x, fps=30):
    if rig.alive(rig.read_pid(x)): raise SystemExit(f'{x} already running')
    rig.prepare_data(x); rig.fix_mp_port(x)
    if os.path.exists(rig.sockpath(x)): os.unlink(rig.sockpath(x))
    home = os.path.join(rig.idir(x), 'home')
    dsload = os.path.join(LD, 'dsload.dylib')
    env = {'HOME': home, 'CFFIXED_USER_HOME': home, 'USER': os.environ.get('USER', ''), 'LOGNAME': os.environ.get('LOGNAME', ''),
           'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSRIG_DIR': rig.idir(x), 'DSRIG_FPS': str(fps), **(rig.ctl_env() if hasattr(rig, 'ctl_env') else {'DSRIG_CTL': '1'}),
           'DSRIG_BATCH': '1', 'DYLD_INSERT_LIBRARIES': dsload + ':' + os.path.join(B, 'libdoorstop.dylib'),
           'DSL_MODS_DIR': os.path.join(LD, 'mods'), 'DSL_PROFILE': os.path.join(L, 'l1/profile.json'), 'DSL_NO_ALERT': '1',
           'DSML_DSLOAD': dsload, 'DSML_BEPINEX_DIR': B,
           'DOORSTOP_ENABLED': '1', 'DOORSTOP_TARGET_ASSEMBLY': os.path.join(B, 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'),
           'DOORSTOP_CLR_RUNTIME_CORECLR_PATH': os.path.join(B, 'dotnet/libcoreclr.dylib'), 'DOORSTOP_CLR_CORLIB_DIR': os.path.join(B, 'dotnet'),
           'DOORSTOP_IGNORE_DISABLED_ENV': '0',
           'BEPINEX_PLUGIN_DIRS': os.path.join(L, 'mods/dsmp-bridge') + ':' + os.path.join(L, 'build/demo_plugin')}
    env.update({k: v for k, v in os.environ.items() if k.startswith(('DSRIG_',)) and k not in env})
    args = [os.path.join(rig.idir(x), rig.EXE_REL), '-logFile', os.path.join(rig.idir(x), 'player.log'),
            '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540', '-batchmode']
    spec = {'args': args, 'env': env, 'front_before': rig.front_pid() or 0, 'bg': True, 'show': False}
    open(os.path.join(rig.idir(x), 'launch.json'), 'w').write(json.dumps(spec))
    open(rig.pidfile(x), 'w').write('')
    g = subprocess.Popen([sys.executable, os.path.abspath(rig.__file__), '_guard', x], stdin=subprocess.DEVNULL,
                         stdout=open(os.path.join(rig.idir(x), 'guard.log'), 'ab'), stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(100):
        if rig.alive(rig.read_pid(x)): break
        time.sleep(0.1)
    print(f'{x}: BepInEx + dsload host pid {rig.read_pid(x)} guard {g.pid}', flush=True)
    return rig.read_pid(x)

if __name__ == '__main__':
    start(sys.argv[1])
