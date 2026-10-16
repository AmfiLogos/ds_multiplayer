#!/usr/bin/env python3
"""Windows copy W_BRG for the bridge test: C:\\DSW_BRG in bottle DSMP (lane baselib.dll + BepInEx be.788 + DsmpBridge
plugins), instance folder ext/bepinex_bridge/inst/W_BRG. Same as research/loadertest.noindex/bie_wrig.py.
usage: brg_wrig.py start|stop|cmd '<json>'  or  brg_wrig.py xplay <pair> <script> [args]"""
import os, sys, subprocess, runpy
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/win'))
import wrig
wrig.INST = os.path.expanduser('~/ds_multiplayer/ext/bepinex_bridge/inst/W_BRG')

def start():
    if subprocess.run(["pgrep", "-f", "wine"], capture_output=True).stdout and wrig.dsmp_pids():
        raise SystemExit("a DSMP Windows copy already runs (max one machine-wide)")
    if not subprocess.run(["pgrep", "-f", "dsmp_guard.py"], capture_output=True).stdout:
        subprocess.Popen(["python3", os.path.expanduser("~/ds_multiplayer/win/tools/dsmp_guard.py"), "7200"],
                         start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, DSRIG_DIR=wrig.winpath(wrig.INST), DSRIG_PORT=str(wrig.PORT), DSMP_GUARD_TAG="1",
               WINEDLLOVERRIDES="winhttp=n,b")
    env.pop("DSRIG_RANGES", None)
    args = ["nice", "-n", "19", f"{wrig.C}/wine", "--bottle", wrig.BOTTLE, "--no-wait", "--dll", "winhttp=n,b", "C:\\DSW_BRG\\DungeonSettlers.exe",
            "-batchmode", "-nographics", "-logFile", wrig.winpath(f"{wrig.INST}/player.log")]
    subprocess.run(args, env=env, stdout=open(f"{wrig.INST}/wine.out", "a"), stderr=subprocess.STDOUT)
    print("started W_BRG; DSMP processes:", wrig.dsmp_pids())
wrig.start = start

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'start': start()
    elif a[0] == 'stop': wrig.stop()
    elif a[0] == 'cmd': print(wrig.cmd(a[1]))
    elif a[0] == 'xplay':
        sys.argv = [os.path.expanduser('~/ds_multiplayer/win/xplay_run.py')] + a[1:]
        runpy.run_path(sys.argv[0], run_name='__main__')
