#!/usr/bin/env python3
"""Item 31 guard (owner 2026-11-02: "The bottom of the chat appears in a different location when I open the chat versus when I
close it ... the first line of the chat should be at the exact same height as ... when we don't press enter and also the size
of the font change which it shouldn't between both modes"). Template: mpmenu.c CH_FS block (one line style, one anchor).

ONE hidden loader test copy (background-only, small window, muted, notch fix off; never the owner's game, saves or screen).
  Phase sp: single player (no session: the open box is read only): the Moonlight test save, local chat lines (mp_api v3
            chat_local, as the Units Laboratory DPS log writes them).
  Phase mp: the same copy hosts that save alone on this run's own port (rig/rigports.py; never a fixed one), its lines go
            through the real send path (Enter, text, Enter); then Enter opens the box with its input line (typing).
Each phase, with a short newest line and with a newest line that wraps: the closed view (lines fade, no input line), the
open box, the closed view again. Fails when (the breaks this catches):
  - the newest line's text (its screen rect: bottom, top, left, right) moves more than 1 px between closed and open;
  - its font size differs between closed and open (point size, or more than 1 px on screen).
Usage: HM_BUILD=<kit: dsload.dylib (dev key) + mods/multiplayer> LD_ROOT=<ds_modloader checkout> python3 chat_line_test.py
       [--phase sp|mp|both]   CL_OUT = output folder; CL_X = copy name (default CLN); CL_LOCK = a mkdir lock;
       CL_SHOTS = 1: a frame of each state into CL_OUT (closed / open)."""
import json, os, shutil, sys, threading, time, traceback

LD = os.path.expanduser(os.environ.get('LD_ROOT', '~/ds_launcher'))
sys.path.insert(0, os.path.join(LD, 'loader', 'test'))
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, owner screen guard)
from hostmods_pair import lrig, log, check, c  # noqa: E402

X = os.environ.get('CL_X', 'CLN'); HP.H = HP.G = X
OUT = os.path.abspath(os.environ.get('CL_OUT') or os.path.join(os.path.dirname(HP.BUILD), 'chatline', time.strftime('%Y%m%d-%H%M%S')))
os.makedirs(OUT, exist_ok=True)
HP.SHOTS = OUT
LOCK = os.path.expanduser(os.environ.get('CL_LOCK', ''))
SHOTS = os.environ.get('CL_SHOTS') == '1'
PHASE = sys.argv[sys.argv.index('--phase') + 1] if '--phase' in sys.argv else 'both'
LONG = 'a longer line that wraps onto a second row at the width of the chat, so its text is two rows high in both views'
info = {}


def mstatus():
    return c(X, cmd='mpm_status').get('chat') or {}


def newest(ch, idle):
    rows = [r for r in ch.get('rows') or [] if bool(r.get('idle')) == idle]
    return max(rows, key=lambda r: r['k']) if rows else None


def tolerant(f):                                                 # a view that never shows = a failed check, not a stopped run
    def g(what):
        try:
            return f(what)
        except RuntimeError as e:
            log(what, repr(e)); return mstatus(), None
    return g


@tolerant
def closed_view(what):
    ch = HP.wait(f'{what}: the closed view shows the newest line', lambda: (lambda q: q if not q.get('box') and q.get('newest_alpha', 0) >= 0.99
                                                                              and newest(q, True) else None)(mstatus()), timeout=20, every=0.3)
    return ch, newest(ch or {}, True)


@tolerant
def open_view(what):
    c(X, cmd='mpm_chat', open=1)
    ch = HP.wait(f'{what}: the open box shows the newest line', lambda: (lambda q: q if q.get('box') and newest(q, False) else None)(mstatus()),
                 timeout=20, every=0.3)
    return ch, newest(ch or {}, False)


def close_box(what):                                            # Escape as a real key (the kit's Escape stack closes the box)
    c(X, cmd='mpm_key', key='escape', via='app')
    HP.wait(f'{what}: the box closed', lambda: not mstatus().get('box'), timeout=20, every=0.3)


def same(a, b, what, mode):
    """a = closed row, b = open row: one line, same place and size"""
    if not a or not b:
        check(f'{what}: the newest line shows closed and open ({mode})', False, {'closed': a, 'open': b}); return
    d = [round(b['rect'][i] - a['rect'][i], 1) for i in range(4)]
    check(f'{what}: the newest line ({a.get("text", "")[:24]!r}) sits at the same place closed and open ({mode}): bottom, top, left, right within 1 px',
          a['k'] == b['k'] and all(abs(v) <= 1 for v in d), {'k': (a['k'], b['k']), 'closed': a['rect'], 'open': b['rect'], 'delta_px': d})
    fa, fb = a.get('fs'), b.get('fs')
    check(f'{what}: the newest line has the same font size closed and open ({mode})',
          fa is not None and fb is not None and abs(fa - fb) < 0.01 and abs(a.get('fs_px', 0) - b.get('fs_px', 0)) <= 1,
          {'closed': (fa, a.get('fs_px')), 'open': (fb, b.get('fs_px'))})


def views(what, mode, shot_tag=None):
    ch0, a = closed_view(what)
    if shot_tag and SHOTS:
        HP.shot(X, f'{shot_tag}_closed.png')
    ch1, b = open_view(what)
    if shot_tag and SHOTS:
        HP.shot(X, f'{shot_tag}_open.png')
    info[f'{what}_{mode}'] = {'closed': a, 'open': b, 'box': ch1.get('box_rect') if ch1 else None, 'read': (ch1 or {}).get('read'),
                              'typing': (ch1 or {}).get('typing'), 'ro': (ch1 or {}).get('ro')}
    log(what, mode, json.dumps(info[f'{what}_{mode}']))
    same(a, b, what, mode)
    return ch1


def to_world(det):
    lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'chatline_world.json'); shutil.copy2(HP.SAVE, src)
    log('load', c(X, cmd='load_save', file=src, det=det))
    if det:
        HP.wait('host locked', lambda: HP.st(X).get('det', {}).get('locked'), timeout=900, every=2)
    HP.wait('the world', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy'))(HP.st(X)), timeout=900, every=2)
    time.sleep(5)


def start_copy(extra):
    HP.room_for(1)
    if os.getloadavg()[0] > 20:
        HP.wait('load under 20', lambda: os.getloadavg()[0] < 20, timeout=1200, every=15)
    HP.start(X, 'http://127.0.0.1:9', extra)


def phase_sp(extra):
    start_copy(extra)
    to_world(0)
    for i in range(6):
        c(X, cmd='mpm_chat', local=f'Local line {i + 1}: hit 123')
    ch = views('sp short', 'read only box', 'sp')
    check('sp: single player opens the read-only box (no input line)', bool(ch) and ch.get('ro') == 1 and ch.get('box') == 1, {k: (ch or {}).get(k) for k in ('ro', 'box', 'read')})
    close_box('sp short')
    c(X, cmd='mpm_chat', local=f'Local line 7: {LONG}')
    views('sp wrapped', 'read only box')
    close_box('sp wrapped')
    HP.stop(X)


def say(text):                                                  # Enter, the text, Enter: the real send path
    c(X, cmd='mpm_chat', open=1); time.sleep(0.6)
    c(X, cmd='mpm_chat', text=text); time.sleep(0.6)
    c(X, cmd='mpm_chat', enter=1); time.sleep(0.8)


def phase_mp(extra):
    I = lrig.idir(X)
    port = HP.rp.port_for([I], os.path.basename(sys.argv[0]), share=True)   # this run's own port (the one HP.start sets)
    json.dump({'role': 'host', 'port': port, 'name': 'Hosta', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800},
              open(os.path.join(I, 'mp.json'), 'w'))
    start_copy(extra)
    to_world(1)
    ok, lines = HP.realrig.rigports.players_guard(lrig.send, [X], 1, 'start')
    check('players: no extra player at the start', ok, '; '.join(lines))
    for i in range(4):
        say(f'host line {i + 1}')
    HP.wait('the sent lines came back', lambda: any('host line 4' in t for t in mstatus().get('last') or []), timeout=30, every=0.5)
    ch = views('mp short', 'typing box', 'mp')
    check('mp: in a session Enter opens the box with its input line, typing', bool(ch) and ch.get('ro') == 0 and ch.get('typing') == 1
          and (ch.get('sb') or {}).get('field') is not None, {k: (ch or {}).get(k) for k in ('ro', 'typing', 'read')})
    close_box('mp short')
    say(f'host line 5: {LONG}')
    HP.wait('the long line came back', lambda: any('host line 5' in t for t in mstatus().get('last') or []), timeout=30, every=0.5)
    views('mp wrapped', 'typing box')
    close_box('mp wrapped')
    ok, lines = HP.realrig.rigports.players_guard(lrig.send, [X], 1, 'end')
    check('players: no extra player at the end', ok, '; '.join(lines))


def main():
    res = {}; locked = False
    g = threading.Thread(target=HP.guard, daemon=True); g.start()
    try:
        if LOCK:
            t0 = time.time()
            while True:
                try:
                    os.mkdir(LOCK); locked = True; break
                except FileExistsError:
                    if time.time() - t0 > 3600:
                        raise SystemExit('copy lock busy for an hour')
                    time.sleep(20)
        HP.make_copy(X)
        HP.kit(X, {}, [], ['multiplayer'])
        HP.park(os.path.join(lrig.idir(X), 'mp_menu.json'))       # the chat at its default place and size
        HP.park(os.path.join(lrig.idir(X), 'data', 'Mods', 'ui_places.tsv'))
        crash = os.path.join(OUT, 'crash'); os.makedirs(crash, exist_ok=True); open(os.path.join(crash, 'answer'), 'w').write('no')
        extra = {'DSL_CRASHREP_TEST': crash, 'DSRIG_WRITE_DIRS': lrig.ctl_env()['DSRIG_WRITE_DIRS'] + ':' + OUT}
        if PHASE in ('sp', 'both'):
            phase_sp(extra)
        if PHASE in ('mp', 'both'):
            phase_mp(extra)
    except BaseException as e:
        log(traceback.format_exc())
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        try:
            HP.stop(X)
        except Exception as e:
            log('stop', repr(e))
        if locked:
            os.rmdir(LOCK)
        for f in ('dsload.log', 'player.log', 'rig.log'):
            p = os.path.join(lrig.idir(X), f)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f))
        res.update({'checks': HP.checks, 'info': info, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks)})
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
