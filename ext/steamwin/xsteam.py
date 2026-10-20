#!/usr/bin/env python3
"""Lane steamwin: live Steam cross-play, Mac (Steam copy M, the Mac's signed-in account) <-> Windows VM (test account).

  xsteam.py mac_host                 Mac hosts a fresh-guid copy of the public base save over Steam; prints the lobby
  xsteam.py mac_friends [open|row N] the Mac's in-game picker (state / open / click row N = InviteUserToLobby)
  xsteam.py mac <json>               one control command to copy M
  xsteam.py vm_start                 VM game through the REAL starter (Steam on), title
  xsteam.py vm <json>                one control command to the VM game
  xsteam.py vm_accept <lobby> <host> the VM's Steam opens steam://joinlobby/2798330/<lobby>/<host>: Steam hands the running
                                     game GameLobbyJoinRequested (333), the same callback as an accepted chat invite
  xsteam.py play <seconds> [vm_host] host presses Start (if in the lobby), both run; samples desyncs + ticks on both
  xsteam.py logs <dir>
Env: source macenv.sh first (copy M). Never prints Steam ids or names into files that get committed.
"""
import json, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
WB = os.path.join(HERE, '..', '..', 'win', 'winbot')
sys.path.insert(0, WB)
import winrig as W                                                  # noqa: E402
import win_checklist as WC                                          # noqa: E402

MACSH = os.path.join(WB, 'mac', 'mac.sh')
I = os.path.expanduser('~/ds_mp_steamwin_ldr/loader/test_env/instances/M')


def mac(obj, timeout=60):
    sys.path.insert(0, os.path.join(WB, '..', '..', 'rig'))
    import ctl_token, socket
    s = socket.socket(socket.AF_UNIX); s.settimeout(timeout); s.connect(os.path.join(I, 'ctl.sock'))
    s.sendall(ctl_token.token_line(I) + json.dumps(obj).encode() + b'\n')
    buf = b''
    while not buf.endswith(b'\n'):
        d = s.recv(1 << 20)
        if not d:
            break
        buf += d
    s.close()
    return json.loads(buf)


def macsh(*a, timeout=600):
    r = subprocess.run([MACSH, *a], capture_output=True, text=True, timeout=timeout)
    o = r.stdout
    try:
        return json.loads(o[o.index('{'):])
    except ValueError:
        return {'raw': (o + r.stderr)[-800:]}


def wait(what, pred, timeout, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(every)
    raise SystemExit(f'timeout {timeout} s: {what}')


def out(o):
    print(json.dumps(o, indent=1), flush=True)


def no_box(f):
    """a stock Yes/No popup on a host (Lock Consecration) holds the game's event queue: the host is busy, runs no step,
    never reaches the join point, and the guest leaves after 90 s with no save (win_checklist.Mac.answer_boxes): No"""
    m = f({'cmd': 'mpm_status'})
    if (m.get('box') or {}).get('on'):
        f({'cmd': 'mpm_box', 'action': 'cancel'})
        return True


def mac_host():
    f = WC.fresh_world(os.path.join(WC.SAVES, 'base_25846389.json'), 'Steam')
    r = macsh('host', f)
    t0 = time.time()                                                # the popup shows a few seconds after the lobby opens
    while time.time() - t0 < 15 and not no_box(mac):
        time.sleep(1)
    st = wait('Mac Steam lobby', lambda: (lambda s: s if s.get('lobby') not in (None, '0') else None)(mac({'cmd': 'mp_steam_state'})), 60)
    out({'host': {k: r.get(k) for k in ('host', 'port', 'mpm_state', 'tick')}, 'steam': {k: st.get(k) for k in ('state', 'steam', 'host', 'lobby')}})


def vm():
    w = W.Win(); w.up(); w.starter = True
    return w


def main():
    a = sys.argv[1:]
    if a[0] == 'mac_host':
        mac_host()
    elif a[0] == 'mac_friends':
        k = {'cmd': 'mpm_friends', 'action': a[1] if len(a) > 1 else 'state'}
        if len(a) > 2:
            k['i'] = int(a[2])
        out(mac(k))
    elif a[0] == 'mac':
        out(mac(json.loads(a[1])))
    elif a[0] == 'vm_start':
        w = vm(); w.prepare(lang=0, active=WC.BASE_MODS, settings={}, consent='no'); out({'title_s': w.start()})
    elif a[0] == 'vm':
        out(vm().cmd(json.loads(a[1])))
    elif a[0] == 'vm_accept':
        w = vm()
        steam = r'C:\Program Files (x86)\Steam\steam.exe'
        out(w.a('run', exe=steam, args=f'steam://joinlobby/2798330/{a[1]}/{a[2]}', wait_ms=0))
    elif a[0] == 'play':
        secs = int(a[1]); vm_host = len(a) > 2 and a[2] == 'vm_host'
        w = vm()
        host = (lambda o: w.cmd(o)) if vm_host else mac
        guest = mac if vm_host else (lambda o: w.cmd(o))
        if host({'cmd': 'mpm_status'}).get('state', '').endswith('lobby'):
            host({'cmd': 'mpm_press', 'button': W.B['START']})
        t0 = time.time(); samples = []
        while time.time() - t0 < secs:
            s = {}
            for side, f in (('host', host), ('guest', guest)):
                no_box(f)
                q = f({'cmd': 'status'}); m = q.get('mp') or {}
                s[side] = {'tick': q.get('tick'), 'map': q.get('map'), 'locked': (q.get('det') or {}).get('locked'),
                           'desyncs': m.get('desyncs'), 'peers': m.get('peers'), 'started': m.get('started')}
            samples.append(s); print(time.strftime('%H:%M:%S'), json.dumps(s), flush=True)
            time.sleep(10)
        out({'first': samples[0], 'last': samples[-1], 'n': len(samples)})
    elif a[0] == 'vm_install':                                    # this lane's whole mods_all/win set; the installed one moved aside
        w = vm(); w.kill_all()
        src = os.path.expanduser('~/ds_mp_steamwin_ldr/loader/build/mods_all/win')
        md = W.ROOT + '\\mods'; keep = W.WORK + '\\to_delete\\steamwin_release_mods'
        if w.exists(keep):
            raise SystemExit('a moved-aside release set is already there: run vm_restore first')
        w.a('move', src=md, dst=keep, mkdirs=True)
        n = 0
        for root, _, fs in os.walk(src):
            for f in fs:
                rel = os.path.relpath(os.path.join(root, f), src).replace('/', '\\')
                w.put(os.path.join(root, f), md + '\\' + rel); n += 1
        for f in w.a('ls', path=keep)['entries']:                  # the loader's own files in mods (profile.json ...) stay
            if not f['dir'] and not w.exists(md + '\\' + f['name']):
                d, _ = w.read(keep + '\\' + f['name']); w.write(md + '\\' + f['name'], d)
        out({'installed_files': n})
    elif a[0] == 'vm_restore':
        w = vm(); w.kill_all()
        keep = W.WORK + r'\to_delete\steamwin_release_mods'
        if not w.exists(keep):
            raise SystemExit('nothing moved aside')
        w.a('move', src=W.ROOT + r'\mods', dst=W.WORK + r'\to_delete\steamwin_mods_' + time.strftime('%Y%m%d-%H%M%S'), mkdirs=True)
        w.a('move', src=keep, dst=W.ROOT + r'\mods', mkdirs=True)
        out({'restored': True})
    elif a[0] == 'vm_steam_check':                                  # standalone check, game closed (args: extra env, e.g. STEAM_CHECK_SELF=1)
        w = vm(); w.kill_all()
        exe = W.WORK + r'\steamwin\steam_check.exe'
        w.put(os.path.join(HERE, '..', '..', 'win', 'build', 'steam_check.exe'), exe)
        r = w.a('run', exe=w.expand(exe), args='', cwd=w.expand(W.WORK + r'\steamwin'), env=a[1:], wait_ms=90000, capture=True)
        print(r.get('out', r)); out({k: r.get(k) for k in ('exit', 'timeout')})
    elif a[0] == 'logs':
        os.makedirs(a[1], exist_ok=True)
        vm().logs(os.path.join(a[1], 'vm'))
        out(macsh('logs', os.path.join(a[1], 'mac')))


if __name__ == '__main__':
    main()
