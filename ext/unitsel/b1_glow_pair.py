#!/usr/bin/env python3
"""unitsel B1 (owner 2026-10-20 late): a unit another player has selected glows on your screen in the colour of that
player's name (the game's unit outline) with a small name tag; the selection travels outside the sim (dsnet P_SEL).
One hidden pair (windows for the frames), ElvenExpedition x10 demo save. Frames: ~/ds_unitsel_out/b1_*.png"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, check, select

SH = os.path.expanduser('~/ds_unitsel_out'); os.makedirs(SH, exist_ok=True)


def rgb(h):
    h = h.lstrip('#'); return [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]


def tag_of(s, guid):
    return next((t for t in s.get('tags', []) if t['unit'] == guid), None)


def glow_ok(t):
    return t and t['tag'] == 1 and t['glow'] == 1 and all(abs(a - b) < 0.02 for a, b in zip(t['glow_rgb'], rgb(t['want'])))


def body(X, Y):
    mine = L.setup(X, Y, window=True)
    t0 = st(X)['tick']
    u, w = mine[Y][0], mine[X][0]
    for x in (X, Y):
        c(x, cmd='ui_coop', action='camera', x=u['cell'][0] + 0.5, y=u['cell'][1] + 0.5, zoom=5)
    log('guest selects', u['name'], select(Y, u['guid']))
    t = wait('host sees the guest selection', lambda: (lambda s: glow_ok(tag_of(s, u['guid'])) and s)(c(X, cmd='ui_selshow')), timeout=20, every=1)
    check("host screen: the guest's selected unit glows in the guest's colour, name tag above", bool(t), t)
    time.sleep(1); c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, 'b1_host_sees_guest_selection.png'))
    log('host selects', w['name'], select(X, w['guid']))
    t = wait('guest sees the host selection', lambda: (lambda s: glow_ok(tag_of(s, w['guid'])) and s)(c(Y, cmd='ui_selshow')), timeout=20, every=1)
    check("guest screen: the host's selected unit glows in the host's colour, name tag above", bool(t), t)
    time.sleep(1); c(Y, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, 'b1_guest_sees_host_selection.png'))
    u2 = mine[Y][1]
    log('guest selects another unit', u2['name'], select(Y, u2['guid']))
    s = wait('host: glow moved', lambda: (lambda s: tag_of(s, u2['guid']) and not tag_of(s, u['guid']) and s)(c(X, cmd='ui_selshow')), timeout=20, every=1)
    check('the glow follows the new selection (old unit no longer tagged)', bool(s), s)
    n, first = L.fp_equal_from(X, Y, t0)
    check('selection is outside the sim: fingerprints equal, no desync', n > 100 and first is None and not mp(X).get('desyncs'), f'common {n} first diff {first}')


if __name__ == '__main__':
    sys.exit(L.run_main('b1_glow', body, window=True))
