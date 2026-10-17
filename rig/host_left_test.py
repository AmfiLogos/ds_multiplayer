#!/usr/bin/env python3
"""Owner rule (2026-10-13): when the host leaves, every guest is kicked out at once ("The host left the game",
back to the title), with no save left on the guest. TCP (LAN) path, owner's menu flow, three ways the host goes:
  1. the host process is killed (SIGKILL by PID)
  2. the host presses Esc > "Leave multiplayer" (mpm_press LEAVE, the clone's real click path)
  3. the network is cut silently: the guest joins through a local TCP proxy that is then frozen (no FIN, no RST)
Each: time from the event to the guest's title screen (want <= ~3 s), the ended message, and the guest's Saves folder.
Leases a free pair (lane "mpglue"). Log on stdout."""
import json, os, signal, socket, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

B = dict(MULTIPLAYER=1, HOST=2, BACK=3, LOAD_SAVE=7, LEAVE=12, JOIN_ADDR=14, CONNECT=15)
results = {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


class Proxy:
    """127.0.0.1:port -> host port. freeze(): stop moving bytes, keep both sockets open (a silent cut)."""
    def __init__(self, up):
        self.up, self.frozen, self.socks = up, False, []
        self.ls = socket.socket(); self.ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.ls.bind(('127.0.0.1', 0)); self.ls.listen(4); self.port = self.ls.getsockname()[1]
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        while True:
            a, _ = self.ls.accept()
            b = socket.create_connection(('127.0.0.1', self.up))
            self.socks += [a, b]
            for s, d in ((a, b), (b, a)):
                threading.Thread(target=self.pump, args=(s, d), daemon=True).start()

    def pump(self, s, d):
        try:
            while True:
                x = s.recv(65536)
                if not x:
                    break
                while self.frozen:
                    time.sleep(1)                     # hold the bytes forever: nothing reaches the other side
                d.sendall(x)
        except OSError:
            pass


def snap(x):
    d = os.path.join(rig.idir(x), 'data', 'Saves')
    return {f: (os.path.getsize(os.path.join(d, f)), os.path.getmtime(os.path.join(d, f))) for f in os.listdir(d)} if os.path.isdir(d) else {}


def main():
    pair = rig.lease(f'mpglue-{os.getpid()}'); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r:
                return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p):
            os.replace(p, p + '.last')
    log('pair', pair)
    from demo import make_demo_save
    work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)

    def session(proxy=None):
        """host from the menu with a save, guest joins by address; returns once both run"""
        if not rig.alive(rig.read_pid(H)):
            rig.start(H, fps=30)
        if not rig.alive(rig.read_pid(G)):
            rig.start(G, fps=30)
        for x in (H, G):
            rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        px = Proxy(port) if proxy else None
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{px.port if px else port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        wait('both running', lambda: not st(H)['mp'].get('paused') and not st(G)['mp'].get('paused'), timeout=180)
        time.sleep(3)
        return px

    def measure(name, act):
        s0 = snap(G)
        t0 = time.time(); act()
        t_end = None
        while time.time() - t0 < 30:
            try:
                s = st(G)
            except Exception:
                s = {}
            if s.get('scene') == 1 and not s.get('busy'):
                t_end = time.time() - t0; break
            time.sleep(0.2)
        time.sleep(1)
        ms = c(G, cmd='mpm_status')                            # the menu takes the ended message (and acks mp's copy)
        new = sorted(f for f, v in snap(G).items() if s0.get(f) != v)
        check(f'{name}: guest on the title within 3 s', t_end is not None and t_end <= 3.5,
              f'{t_end if t_end is None else round(t_end, 2)} s; guest status scene {st(G).get("scene")}')
        check(f'{name}: guest told "The host left the game"', 'host left' in (ms.get('msg') or '').lower(), f"menu state {ms.get('state')} msg {ms.get('msg')!r} visible {ms.get('visible')}")
        check(f'{name}: no save written on the guest', not new, f'new/changed {new}')

    try:
        session()
        measure('host killed', lambda: (log('kill host pid', rig.read_pid(H)), os.kill(rig.read_pid(H), signal.SIGKILL)))
        time.sleep(3)
        session()
        measure('host leaves (Esc > Leave multiplayer)', lambda: press(H, 'LEAVE'))
        rig.stop(H); time.sleep(2)
        px = session(proxy=True)
        def cut():
            px.frozen = True; log('proxy frozen (silent cut)')
        measure('network cut (silent)', cut)
    except Exception as e:
        check('flow completed', False, repr(e))
    finally:
        rig.stop(H); rig.stop(G); rig.release(pair)
        log('RESULTS', json.dumps(results)); print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
