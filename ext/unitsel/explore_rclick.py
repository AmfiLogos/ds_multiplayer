#!/usr/bin/env python3
"""exploration: what does the host's right-click on a standing friend's unit (own unit selected) send?"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, players, select, mark, log_since
import b6_lookclick_pair as B
if not os.environ.get('US_DYLIB'):
    L.rig.DYLIB = os.path.join(L.WT, 'build', 'mouse', 'dsrig.dylib')


def body(X, Y):
    mine = L.setup(X, Y, window=True)
    for x in (X, Y): c(x, cmd='ui_teams', action='close')
    u = mine[Y][0]; w = mine[X][0]
    B.coop(X, action='camera', x=u['cell'][0] + 0.5, y=u['cell'][1] + 0.5, zoom=6); time.sleep(2)
    for k, dy in enumerate((0.3, 0.6, 0.9, 1.2)):
        mx, my = mark(X), mark(Y)
        select(X, w['guid']); time.sleep(0.5)
        s = B.coop(X); sx, sy = B.to_screen(s, u['cell'][0] + 0.5, u['cell'][1] + dy)
        c0 = c(X, cmd='ord_stats').get('captured')
        c(X, cmd='ui_teams', action='mouse', x=sx, y=sy, button=1); time.sleep(3)
        log(f'try {k} dy {dy}: host captured {c0} -> {c(X, cmd="ord_stats").get("captured")}, mouse_cell {B.coop(X).get("mouse_cell")} unit {u["cell"]}')
        log('host:\n' + log_since(X, mx)[-1200:]); log('guest:\n' + log_since(Y, my)[-1200:])
        log('guest unit', [(q['cell'], q.get('task')) for q in players(Y) if q['guid'] == u['guid']])
    L.results['explored'] = True


if __name__ == '__main__':
    sys.exit(L.run_main('explore_rclick', body, window=True))
