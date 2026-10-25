#!/usr/bin/env python3
"""The multiplayer chat box moves and resizes with the mouse, and keeps its place and size after a restart (owner 2026-10-25:
"drag it off if we decide, and allow to make it bigger as well with drag & drop on corner & bottom").

ONE hidden loader test copy (background-only, small window, muted, notch fix off; never the owner's game, saves or screen),
single player (no session): the Moonlight test save, local chat lines (mp_api v3 chat_local, as the Units Laboratory DPS log
writes them), the open box (Enter). The chat reads the mouse through Unity icalls, so the rig control channel's
`mpm_chat {drag, x0, y0, x1, y1, button}` plays a press, a move over some frames and a release through the chat's own drag
code (opens only in DSRIG_CTL=1 copies). Fails when (the breaks this catches):
  - a left drag on the title band does not move the box (or changes its size);
  - a drag on the bottom-right corner does not make it wider and taller (top-left corner fixed);
  - a drag on the bottom edge does not make it taller only;
  - the open box does not show more history lines when taller, or its lines do not follow its width;
  - it goes below 470 x 270, over 80 % of the screen width / 90 % of its height, or off the screen;
  - a right-click drag no longer moves it;
  - place or size are lost after the game restarts (mp_menu.json).
Usage: HM_BUILD=<kit: dsload.dylib (dev key, silent) + mods/multiplayer> LD_ROOT=<ds_modloader checkout> python3 chat_drag_test.py
       CD_OUT = output folder (results.json, shots, logs); CD_X = the copy's name (default CDR)."""
import json, os, shutil, sys, threading, time, traceback

LD = os.path.expanduser(os.environ.get('LD_ROOT', '~/ds_launcher'))
sys.path.insert(0, os.path.join(LD, 'loader', 'test'))
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, owner screen guard)
from hostmods_pair import lrig, log, check, c  # noqa: E402

X = os.environ.get('CD_X', 'CDR'); HP.H = HP.G = X
OUT = os.path.abspath(os.environ.get('CD_OUT') or os.path.join(os.path.dirname(HP.BUILD), 'chatdrag', time.strftime('%Y%m%d-%H%M%S')))
os.makedirs(OUT, exist_ok=True)
HP.SHOTS = OUT
W0, H0 = 470, 270


def mstatus():
    r = c(X, cmd='mpm_status')
    return r.get('chat') or {}


def drag(x0, y0, x1, y1, button=0, frames=10):
    a = c(X, cmd='mpm_chat', drag=1, x0=x0, y0=y0, x1=x1, y1=y1, button=button, frames=frames)
    if not a.get('ok'):
        raise RuntimeError(f'drag refused: {a}')
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(0.3)
        ch = mstatus()
        if not ch.get('fake') and not ch.get('drag'):
            time.sleep(0.4)                                       # the frame after the release shows the new place
            return mstatus()
    raise RuntimeError('the scripted drag did not finish')


def near(a, b, tol=1.5):
    return bool(a) and bool(b) and len(a) == len(b) and all(abs(p - q) <= tol for p, q in zip(a, b))


def size_is(ch, w, h):                                          # canvas units, printed rounded
    return near(ch.get('size'), [w, h], 1)


def box_rows(ch):
    return [r for r in ch.get('rows') or [] if not r.get('idle')]


def to_world():
    lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'chatdrag_world.json'); shutil.copy2(HP.SAVE, src)
    log('load', c(X, cmd='load_save', file=src, det=0))
    HP.wait('the world', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy'))(HP.st(X)), timeout=900, every=2)
    time.sleep(5)


def open_box(n_lines):
    for i in range(n_lines):
        txt = f'Test line {i + 1}: ' + ('a longer local line that wraps onto a second row at the default width of the box ' if i % 7 == 3 else 'hit 123')
        r = c(X, cmd='mpm_chat', local=txt)
        if r.get('rc') != 0:
            log('chat_local', i, r)
    c(X, cmd='mpm_chat', open=1)
    return HP.wait('the open chat box', lambda: (lambda ch: ch if ch.get('typing') and ch.get('box') else None)(mstatus()), timeout=60, every=0.5)


def start_copy(extra):
    HP.room_for(1)
    if os.getloadavg()[0] > 20:
        HP.wait('load under 20', lambda: os.getloadavg()[0] < 20, timeout=1200, every=15)
    HP.start(X, 'http://127.0.0.1:9', extra)


def run(extra):
    start_copy(extra)
    to_world()
    dl = HP.flog(X, 'dsload.log')
    check('the copy is silent (AudioListener held at 0)', 'AudioListener held at 0' in dl, [l for l in dl.splitlines() if 'AudioListener' in l][:2])
    ch = open_box(40)
    info['screen'] = ch.get('screen'); info['default'] = {k: ch.get(k) for k in ('rect', 'size', 'pos', 'line_w')}
    r, sc = ch['rect'], (ch['rect'][2] - ch['rect'][0]) / ch['size'][0]
    cw = 1080.0 * ch['screen'][0] / ch['screen'][1]                # canvas width (the canvas is 1080 high)
    log('open box', json.dumps(info['default']), 'px per unit', round(sc, 3), 'canvas width', round(cw))
    check('the open box starts at the default size 470 x 270', size_is(ch, W0, H0) and near([r[2] - r[0], r[3] - r[1]], [W0 * sc, H0 * sc], 2), ch['size'])
    rows0 = len(box_rows(ch))
    HP.shot(X, 'chat_1_default.png')

    # 1. left drag on the title band: moved by (+150, +100) units, same size
    ch1 = drag(r[0] + 150 * sc, r[3] - 12 * sc, r[0] + 300 * sc, r[3] + 88 * sc)
    r1 = ch1['rect']
    check('left drag on the title band moves the box (+150, +100 units), its size stays', near(r1, [r[0] + 150 * sc, r[1] + 100 * sc, r[2] + 150 * sc, r[3] + 100 * sc], 2)
          and size_is(ch1, W0, H0), json.dumps({'before': r, 'after': r1, 'size': ch1['size']}))

    # 2. corner drag: +200 wide, +150 tall, the top-left corner stays
    ch2 = drag(r1[2] - 3, r1[1] + 3, r1[2] - 3 + 200 * sc, r1[1] + 3 - 150 * sc)
    r2 = ch2['rect']
    check('corner drag makes the box wider and taller (670 x 420), its top-left corner stays', size_is(ch2, W0 + 200, H0 + 150)
          and near([r2[0], r2[3]], [r1[0], r1[3]], 1.5) and near([r2[2] - r2[0], r2[3] - r2[1]], [(W0 + 200) * sc, (H0 + 150) * sc], 2),
          json.dumps({'before': r1, 'after': r2, 'size': ch2['size']}))
    rows2 = box_rows(ch2)
    check('the taller box shows more history lines (40 in the history)', len(rows2) > rows0 + 4, (rows0, len(rows2)))
    lw = max((rw['rect'][2] - rw['rect'][0] for rw in rows2), default=0)
    check('its lines follow the width (box width - 24 = 646 units)', near(ch2.get('line_w'), [W0 + 200 - 60, W0 + 200 - 24], 1) and abs(lw - (W0 + 200 - 24) * sc) <= 2,
          json.dumps({'line_w': ch2.get('line_w'), 'widest_row_px': lw}))
    HP.shot(X, 'chat_2_corner.png')

    # 3. bottom edge drag: +60 tall only
    ch3 = drag((r2[0] + r2[2]) / 2, r2[1] + 2, (r2[0] + r2[2]) / 2 + 50 * sc, r2[1] + 2 - 60 * sc)
    r3 = ch3['rect']
    check('bottom edge drag makes the box taller only (+60), top and width stay', size_is(ch3, W0 + 200, H0 + 210)
          and near([r3[0], r3[2], r3[3]], [r2[0], r2[2], r2[3]], 1.5), json.dumps({'before': r2, 'after': r3, 'size': ch3['size']}))

    # 4. minimum: the corner pushed far up-left -> 470 x 270
    ch4 = drag(r3[2] - 3, r3[1] + 3, r3[2] - 3 - 2000 * sc, r3[1] + 3 + 2000 * sc)
    check('never under 470 x 270 (corner pushed far up-left)', size_is(ch4, W0, H0), ch4['size'])

    # 5. maximum: box to the top-left corner (title drag), then the corner far down-right -> 80 % x 90 %, on the screen
    r4 = ch4['rect']
    ch5 = drag(r4[0] + 150 * sc, r4[3] - 12 * sc, r4[0] + 150 * sc - 3000 * sc, r4[3] - 12 * sc + 3000 * sc)
    r5 = ch5['rect']
    ch6 = drag(r5[2] - 3, r5[1] + 3, r5[2] - 3 + 5000 * sc, r5[1] + 3 - 5000 * sc)
    r6, sw, sh = ch6['rect'], ch6['screen'][0], ch6['screen'][1]
    mw = round(0.8 * cw)
    check('never over 80 % of the screen width and 90 % of its height (972), and fully on the screen',
          abs(ch6['size'][0] - mw) <= 1 and abs(ch6['size'][1] - 972) <= 1 and r6[0] >= 0 and r6[1] >= 0 and r6[2] <= sw and r6[3] <= sh,
          json.dumps({'size': ch6['size'], 'max_w': mw, 'rect': r6, 'screen': [sw, sh]}))
    HP.shot(X, 'chat_3_max.png')

    # 6. right-click drag still moves it (anywhere on the open box); then a middle size + place for the restart
    ch7 = drag((r6[0] + r6[2]) / 2, (r6[1] + r6[3]) / 2, (r6[0] + r6[2]) / 2 + 80 * sc, (r6[1] + r6[3]) / 2 - 40 * sc, button=1)
    r7 = ch7['rect']
    check('right-click drag on the box still moves it', near(r7, [r6[0] + 80 * sc, r6[1] - 40 * sc, r6[2] + 80 * sc, r6[3] - 40 * sc], 2) and ch7['size'] == ch6['size'],
          json.dumps({'before': r6, 'after': r7}))
    ch8 = drag(r7[2] - 3, r7[1] + 3, r7[2] - 3 - (ch7['size'][0] - 760) * sc, r7[1] + 3 + (ch7['size'][1] - 520) * sc)
    check('corner drag back to 760 x 520', size_is(ch8, 760, 520), ch8['size'])
    HP.shot(X, 'chat_4_resized.png')
    mm = json.load(open(os.path.join(lrig.idir(X), 'mp_menu.json')))
    check('place and size saved in mp_menu.json at the release', [mm.get('chat_w'), mm.get('chat_h')] == ch8['size']
          and near([mm.get('chat_x'), mm.get('chat_y')], ch8['pos'], 0.6), json.dumps({k: mm.get(k) for k in ('chat_x', 'chat_y', 'chat_w', 'chat_h')}))
    before = {k: ch8.get(k) for k in ('rect', 'size', 'pos')}
    info['before_restart'] = before

    # 7. restart: the same place and size
    HP.stop(X)
    for f in ('dsload.log', 'player.log', 'rig.log'):
        p = os.path.join(lrig.idir(X), f)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(OUT, 'run1_' + f))
    start_copy(extra)
    to_world()
    ch9 = open_box(3)
    info['after_restart'] = {k: ch9.get(k) for k in ('rect', 'size', 'pos')}
    check('after a restart the box opens at the same place and size', ch9['size'] == before['size'] and near(ch9['pos'], before['pos'], 0.6)
          and near(ch9['rect'], before['rect'], 1.5), json.dumps({'before': before, 'after': info['after_restart']}))
    HP.shot(X, 'chat_5_after_restart.png')


info = {}
LOCK = os.path.expanduser(os.environ.get('CD_LOCK', ''))       # one hidden copy at a time for a lane: mkdir lock


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
        HP.park(os.path.join(lrig.idir(X), 'mp_menu.json'))       # the chat starts at its default place and size
        crash = os.path.join(OUT, 'crash'); os.makedirs(crash, exist_ok=True); open(os.path.join(crash, 'answer'), 'w').write('no')
        wd = lrig.ctl_env()['DSRIG_WRITE_DIRS'] + ':' + OUT          # the shots go to OUT
        run({'DSL_CRASHREP_TEST': crash, 'DSRIG_WRITE_DIRS': wd})
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
        for f in ('dsload.log', 'player.log', 'rig.log', 'mp_menu.json'):
            p = os.path.join(lrig.idir(X), f)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f))
        res.update({'checks': HP.checks, 'info': info, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks)})
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
