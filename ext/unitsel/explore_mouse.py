#!/usr/bin/env python3
"""exploration (not a test): the guest's REAL mouse path (fake mouse fed into UnityEngine.Input, build -DTEAMS_MOUSE_TEST)
while the session runs and while the host paused: does a left-click select + right-click move become an order?"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, mark, log_since, players
L.rig.DYLIB = os.path.join(L.WT, 'build', 'mouse', 'dsrig.dylib')
SH = os.path.expanduser('~/ds_unitsel_out/explore'); os.makedirs(SH, exist_ok=True)


def coop(_i, **k):
    return c(_i, cmd='ui_coop', **k)


def to_screen(s, wx, wy):
    cx, cy, o = s['cam']; w, h = s['screen']
    k = h / (2 * o)
    return w / 2 + (wx - cx) * k, h / 2 + (wy - cy) * k


def click(_i, sx, sy, button):
    r = c(_i, cmd='ui_teams', action='mouse', x=sx, y=sy, button=button)
    time.sleep(2.5)
    return r


def attempt(X, Y, u, label):
    cell = [u['cell'][0], u['cell'][1]]
    coop(Y, action='camera', x=cell[0] + 0.5, y=cell[1] + 0.5); time.sleep(2)
    s = coop(Y)
    log(label, 'coop state cam', s.get('cam'), 'screen', s.get('screen'), 'speed', st(Y).get('mp', {}).get('speed'))
    m = mark(Y); o0 = mp(Y).get('orders', 0); s0 = c(Y, cmd='ord_stats')
    ux, uy = to_screen(s, cell[0] + 0.5, cell[1] + 0.5)
    click(Y, ux, uy, 0)
    log(label, 'after left click: selection', c(Y, cmd='ord_stats').get('sel'), 'mouse_cell', coop(Y).get('mouse_cell'))
    tx, ty = to_screen(s, cell[0] + 3.5, cell[1] + 0.5)
    click(Y, tx, ty, 1)
    time.sleep(0.5); c(Y, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, label + '_1s.png'))
    time.sleep(3); c(Y, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, label + '_4s.png'))
    s1 = c(Y, cmd='ord_stats')
    log(label, 'right click at', (tx, ty), 'mouse_cell', coop(Y).get('mouse_cell'), 'captured', s0.get('captured'), '->', s1.get('captured'),
        'orders', o0, '->', mp(Y).get('orders', 0))
    log(label, 'guest log:\n' + log_since(Y, m)[-2500:])
    return (cell[0] + 3, cell[1])


def body(X, Y):
    mine = L.setup(X, Y, window=True)
    c(Y, cmd='ui_teams', action='close'); time.sleep(1)
    u = mine[Y][0]
    tgt = attempt(X, Y, u, 'RUNNING')
    time.sleep(6)
    u = [q for q in players(X) if q['guid'] == u['guid']][0]
    log('unit now', u['cell'], 'target', tgt)
    log('host pause', c(X, cmd='mp_pause', on=1))
    wait('paused', lambda: mp(Y).get('paused'), timeout=60); time.sleep(3)
    tgt = attempt(X, Y, u, 'PAUSED')
    log('host resume', c(X, cmd='mp_pause', on=0)); time.sleep(10)
    u2 = [q for q in players(X) if q['guid'] == u['guid']][0]
    log('unit after resume', u['cell'], '->', u2['cell'], 'target', tgt)
    L.results['explored'] = True


if __name__ == '__main__':
    sys.exit(L.run_main('explore_mouse', body, window=True))
