#!/usr/bin/env python3
"""The multiplayer chat's "-" / "+" stay under the mouse (owner 2026-11-01: "when clicking on the - + to make an interface
smaller or bigger, the - + should stay in the same place so i can click multiple time"). The chat places itself, so the
kit hands it the move (ds_modloader kit panel_size_shift, mp_esc.c mpesc_size_shift, mpmenu.c).
ONE hidden loader test copy (ds_modloader loader/test/esc_test.py start: loader + multiplayer + the test module uikit-demo,
whose fake mouse clicks the kit's buttons; never the owner's game or screen), single player world, local chat lines, the
read-only box open. "+" 3 times then "-" 3 times, each at ONE screen point with the mouse held there. Fails when a click
misses (the size does not change by 10 %) or the clicked button's centre moves 2 px or more, or the new place is not
kept in mp_menu.json. Starts from a FRESH chat place (the copy's mp_menu.json moved aside: a copy that kept the chat at
the screen edge made the clamp move the button, as designed). EDGE: the chat dragged past the left edge, then "+": the
whole box stays on the screen (the screen wins over the anchor).
Usage: DSL_TEST_COPY=<copy> MP_PIN=<ds_multiplayer commit> python3 chat_anchor_test.py [out] [--no-build]
       (run with the ds_modloader worktree's loader/test on PYTHONPATH: LD_TEST=<that folder>)"""
import json, os, signal, sys, threading, time
sys.path.insert(0, os.path.expanduser(os.environ.get('LD_TEST', '~/ds_launcher/loader/test')))
os.environ.setdefault('DSL_TEST_COPY', 'ANC')
import esc_test as E  # noqa: E402
log, check, ui, ctl, wait_for = E.log, E.check, E.ui, E.ctl, E.wait_for


def chat(): return (ctl({'cmd': 'mpm_status'}, 5) or {}).get('chat') or {}
def part(r, name):
    w = (r[2] - r[0]) * 22 / 46.0
    return [r[0], r[1], r[0] + w, r[3]] if name == 'minus' else [r[2] - w, r[1], r[2], r[3]]
def centre(r): return ((r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0)


def cases():
    for i in range(30): ctl({'cmd': 'mpm_chat', 'local': f'Test line {i + 1}: hit 123'}, 5)
    ctl({'cmd': 'mpm_chat', 'open': 1}, 5)
    ch = wait_for(lambda: (lambda c: c if c.get('box') else None)(chat()), 30, 0.5) or {}
    check('setup: the chat box is open', bool(ch.get('box')), {k: ch.get(k) for k in ('box', 'read', 'ro')})
    b = (ch.get('box_rect') or [0, 0, 0, 0, 0])[1:]
    ui('api', op='move_xy', x=(b[0] + b[2]) / 2, y=(b[1] + b[3]) / 2); time.sleep(0.8)
    ch = wait_for(lambda: (lambda c: c if c.get('zoom_btns') else None)(chat()), 10, 0.3) or {}
    check('setup: with the mouse on the box, the chat shows "-" / "+"', bool(ch.get('zoom_btns')), ch.get('zoom_btns'))
    if not ch.get('zoom_btns'): return
    z = ch.get('zoom', 0)
    for name, d in (('plus', 0.1), ('minus', -0.1)):
        bx, by = centre(part(ch['zoom_btns'], name)); seen = []; ok = False
        for n in range(3):
            ui('api', op='click_xy', x=bx, y=by); time.sleep(1.5); ch = chat(); z2 = ch.get('zoom', 0)
            q = ch.get('zoom_btns'); cx, cy = centre(part(q, name)) if q else (-999, -999)
            seen.append((round(z2, 2), round(cx - bx, 1), round(cy - by, 1)))
            ok = abs(z2 - z - d) < 0.001 and abs(cx - bx) < 2 and abs(cy - by) < 2
            z = z2
            if not ok: break
        check(f'chat: "{"+" if d > 0 else "-"}" clicked 3 times at one point: each click sizes the chat 10 % and the button stays under the mouse (< 2 px)', ok and len(seen) == 3, ((bx, by), seen))
    time.sleep(1)
    try: kept = json.load(open(os.path.join(E.I, 'mp_menu.json')))
    except Exception as e: kept = {'error': repr(e)}
    xy = ch.get('chat_xy') or [None, None]
    check('chat: the place after the clicks is kept (mp_menu.json)', abs(kept.get('chat_x', 1e9) - xy[0]) <= 1 and abs(kept.get('chat_y', 1e9) - xy[1]) <= 1, (kept.get('chat_x'), kept.get('chat_y'), xy))


def edge_case():
    ch = chat(); b = (ch.get('box_rect') or [0, 0, 0, 0, 0])[1:]
    if not ch.get('box') or b[2] <= b[0]: check('edge: the chat box is open', False, b); return
    cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
    j = ctl({'cmd': 'mpm_chat', 'drag': 1, 'x0': cx, 'y0': cy, 'x1': cx - b[2] - 200, 'y1': cy, 'button': 1, 'frames': 20}, 5) or {}
    time.sleep(2.5); ch = chat(); b1 = (ch.get('box_rect') or [0, 0, 0, 0, 0])[1:]
    ui('api', op='move_xy', x=(b1[0] + b1[2]) / 2, y=(b1[1] + b1[3]) / 2); time.sleep(0.8)
    ch = wait_for(lambda: (lambda c: c if c.get('zoom_btns') else None)(chat()), 10, 0.3) or {}
    if not ch.get('zoom_btns'): check('edge: "-" / "+" show at the left edge', False, (j, b1)); return
    z = ch.get('zoom', 0); bx, by = centre(part(ch['zoom_btns'], 'plus'))
    ui('api', op='click_xy', x=bx, y=by); time.sleep(1.5); ch = chat(); b2 = (ch.get('box_rect') or [0, 0, 0, 0, 0])[1:]
    sw = ((E.st().get('w') or {}).get('screen') or [0, 0])[0]
    check('edge: chat dragged past the left edge, then "+": 10 % bigger and the whole box on the screen (x >= 0)',
          j.get('ok') and abs(ch.get('zoom', 0) - z - 0.1) < 0.001 and -1 <= b1[0] <= 8 and b2[0] >= -1 and (not sw or b2[2] <= sw + 1),
          (j, b, b1, b2, z, ch.get('zoom'), sw))


def main():
    out = os.path.abspath([a for a in sys.argv[1:] if not a.startswith('--')][0]) if [a for a in sys.argv[1:] if not a.startswith('--')] else E.OUT
    if E.game_pids(): raise SystemExit(f'copy {E.X} already running')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    if '--no-build' not in sys.argv: E.build()
    E.install()
    mm = os.path.join(E.I, 'mp_menu.json')                           # a fresh chat place (the kept one may be at a screen edge)
    if os.path.exists(mm): os.makedirs(os.path.join(out, 'old'), exist_ok=True); os.replace(mm, os.path.join(out, 'old', 'mp_menu.%d.json' % time.time()))
    g = threading.Thread(target=E.guard, daemon=True); g.start()
    try:
        E.start()
        wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 240, 1)
        wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(E.st()), 60, 1)
        ctl({'cmd': 'load_save', 'name': E.SAVE}, 30)
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') else None)(ctl({'cmd': 'status'}, 5)), 180, 1)
        check('world loaded', bool(t), t)
        time.sleep(8)
        cases()
        edge_case()
        ui('capture', name='chat_anchor')
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for p in E.game_pids(): os.kill(p, signal.SIGKILL)
        E.stop.set()
    check('copy never on screen / frontmost', not E.violations, E.violations[:3])
    fails = [c for c in E.checks if not c['ok']]
    os.makedirs(out, exist_ok=True); json.dump({'checks': E.checks}, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    log(f'RESULT chat_anchor_test: {len(E.checks) - len(fails)} PASS, {len(fails)} FAIL')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
