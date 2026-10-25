#!/usr/bin/env python3
"""Escape hides the spectator's mini overview (owner 2026-10-25: "make sure Escape works to close our custom windows").

One leased rig pair (no loader: the overview's Escape is in the mod's own in-game Escape chain, ui_game.c escape_ours):
H hosts the demo save and starts, S joins as a spectator and shows the mini overview (O). On S, REAL Escape key events
(mpm_key: AppKit -> the game's own input) check: a viewer box that is open folds first (as before), then Escape hides the
overview as its X does ("Press O to show it again"), and the game did not act on that press (no Esc menu); the next
Escape opens the Esc menu. The old build (origin/main before dsesc) opens the Esc menu and leaves the overview.
  esc_mini_pair.py [--dylib build/dsrig.dylib] [--tag NAME]   Exit 0 = PASS. Out: build/esc_mini_out/<tag>/."""
import json, os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rig
A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(ROOT, 'build', 'dsrig.dylib')
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('mini_%H%M%S')
OUT = os.path.join(ROOT, 'build', 'esc_mini_out', TAG); os.makedirs(OUT, exist_ok=True)
LANE = f'dsesc-mini-{os.getpid()}'
LOG = open(os.path.join(OUT, 'test.log'), 'a'); checks = []


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a); print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    checks.append((name, bool(ok))); log(('PASS ' if ok else 'FAIL ') + name, '' if ok else info)


def buttons():
    t = open(os.path.join(ROOT, 'src', 'mpmenu.h')).read(); t = t[t.index('enum mpm_button {') + 17:]; t = t[:t.index('};')]
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S); v = -1; out = {}
    for w in [w.strip() for w in t.split(',') if w.strip()]:
        n, _, e = w.partition('='); v = int(e) if e.strip() else v + 1; out[n.strip()[4:]] = v
    return out


def main():
    if os.getloadavg()[0] > 20: log('FAIL not run: load over 20'); sys.exit(1)
    pair = None
    for _ in range(60):
        pair = rig.lease(LANE)
        if pair: break
        time.sleep(30)
    if not pair: log('FAIL not run: no free pair'); sys.exit(1)
    H, S = pair[0], pair[1]; B = buttons()
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
    esc = lambda: c(S, cmd='ui_escape')
    mini = lambda: c(S, cmd='ui_mini')

    def key_escape():
        r = c(S, cmd='mpm_key', key='escape'); time.sleep(1.5); return r

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    try:
        for x in (H, S):
            for f in ('mp.json', 'mp_menu.json', os.path.join('data', 'mp_ui.json')):
                p = os.path.join(rig.idir(x), f)
                if os.path.exists(p): os.replace(p, p + '.last')
        json.dump({'spectators': 1}, open(os.path.join(rig.idir(H), 'mp_menu.json'), 'w'))
        busy = [x for x in (H, S) if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
        if busy: raise RuntimeError(f'copies already running outside this lease: {busy}')
        log('pair', pair, 'dylib', rig.DYLIB)
        for x in (H, S): rig.start(x, window=True, fps=30)        # hidden windows (key events need a window, not -batchmode)
        for x in (H, S): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=900)
        time.sleep(3)
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True); src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        port = c(H, cmd='mpm_status').get('port')
        press(H, 'START'); time.sleep(3)
        press(S, 'MULTIPLAYER'); press(S, 'JOIN_ADDR')
        c(S, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5); c(S, cmd='mpm_press', button=B['WATCH'])
        wait('S watching', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('spectator') and s.get('mp', {}).get('loads', 0) >= 1
                                    and not s.get('mp', {}).get('needs_state'))(st(S)), timeout=900, every=1)
        time.sleep(5)
        if not mini().get('shown'): c(S, cmd='ui_mini', action='o'); time.sleep(1.5)
        m = mini(); e = esc()
        check('S is a spectator with the mini overview shown, no Esc menu', st(S).get('mp', {}).get('spectator') and m.get('shown') == 1 and e.get('esc_menu') == 0, (m.get('shown'), e))
        up = m.get('shown') == 1
        if e.get('viewer') and not e.get('viewer_folded'):          # the viewer box folds first (its Escape, kept)
            key_escape(); e = esc(); m = mini()
            check('a 1st Escape folds the viewer box (as before), the overview stays', e.get('viewer_folded') == 1 and m.get('shown') == 1 and e.get('esc_menu') == 0, (e, m.get('shown')))
        key_escape(); e = esc(); m = mini()
        check('Escape hides the mini overview (as its X)', up and m.get('shown') == 0 and m.get('setting') == 0, (m.get('shown'), m.get('setting')))
        check('the game did not act on that press (no Esc menu)', up and e.get('esc_menu') == 0, e)
        key_escape(); e = esc()
        check('the next Escape opens the Esc menu', up and e.get('esc_menu') == 1, e)
        key_escape(); e = esc()
        log('info: the Esc menu after one more Escape:', e.get('esc_menu'))
    except Exception as ex:
        check('no exception', False, repr(ex))
    finally:
        for x in (H, S):
            try: rig.stop(x)
            except Exception as ex: log('stop', x, ex)
        for x in (H, S):
            for f in ('rig.log',):
                p = os.path.join(rig.idir(x), f)
                if os.path.exists(p):
                    try: open(os.path.join(OUT, f'{x}_{f}'), 'w').write(open(p, errors='replace').read())
                    except Exception: pass
        log('release', rig.release(pair, LANE))
    fails = [n for n, ok in checks if not ok]
    log(f'RESULT esc_mini_pair: {len(checks) - len(fails)} PASS, {len(fails)} FAIL')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
