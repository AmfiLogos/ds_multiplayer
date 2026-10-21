#!/usr/bin/env python3
"""unitsel B6 (owner 2026-10-21: "my clicks on my friend's units locked his control"): the host clicks and selects the
guest's units again and again (real mouse path: fake mouse in UnityEngine.Input, build -DTEAMS_MOUSE_TEST) while the
guest gives move orders; looking at a friend's unit must never send anything that changes or blocks his orders.
Every guest order must run, the host sends no order, 0 desyncs. One hidden pair, ElvenExpedition x10 demo save."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, check, order_move, players, select, mark, log_since
if not os.environ.get('US_DYLIB'):
    L.rig.DYLIB = os.path.join(L.WT, 'build', 'mouse', 'dsrig.dylib')


def coop(_i, **k):
    return c(_i, cmd='ui_coop', **k)


def cell_of(x, g):
    u = [q for q in players(x) if q['guid'] == g]
    return tuple(u[0]['cell']) if u else None


def to_screen(s, wx, wy):
    cx, cy, o = s['cam']; w, h = s['screen']; k = h / (2 * o)
    return w / 2 + (wx - cx) * k, h / 2 + (wy - cy) * k


RIGHT = {'on': False, 'own': None}


def host_clicks(X, g, n=3):
    for _ in range(n):
        if RIGHT['on']:                                       # phase 2: the host's own unit selected, right-click on the friend's unit
            select(X, RIGHT['own']); time.sleep(0.3)
        cl = cell_of(X, g)
        if not cl: return
        s = coop(X)
        sx, sy = to_screen(s, cl[0] + 0.5, cl[1] + 0.7)
        c(X, cmd='ui_teams', action='mouse', x=sx, y=sy, button=1 if RIGHT['on'] else 0); time.sleep(1.2)
        if not RIGHT['on']: select(X, g); time.sleep(0.5)


def body(X, Y):
    mine = L.setup(X, Y, window=True)
    for x in (X, Y): c(x, cmd='ui_teams', action='close')
    u = mine[Y][0]; home = tuple(u['cell'])
    for x in (X, Y): coop(x, action='camera', x=home[0] + 1.5, y=home[1] + 0.5, zoom=6)
    time.sleep(2)
    t0 = st(X)['tick']; mx = mark(X)
    cap0 = c(X, cmd='ord_stats').get('captured', 0)
    ok = 0; rounds = 12
    RIGHT['own'] = mine[X][0]['guid']
    for r in range(rounds):
        if r == 6:
            cap_left = c(X, cmd='ord_stats').get('captured', 0)
            check('left clicks / selecting a friend\'s unit send no order from the host', cap_left == cap0, f'host captured {cap0} -> {cap_left}')
            RIGHT['on'] = True; log('--- phase 2: host right-clicks the guest unit with its own unit selected')
        tgt = (home[0] + 3, home[1]) if r % 2 == 0 else home
        order_move(Y, u['guid'], tgt)                         # the guest orders his unit ...
        host_clicks(X, u['guid'])                             # ... while the host clicks / selects it
        try:
            wait('the guest unit arrives', lambda: cell_of(X, u['guid']) == tgt and cell_of(Y, u['guid']) == tgt, timeout=40, every=1); ok += 1
            log(f'round {r}: arrived at {tgt}')
        except RuntimeError:
            log(f'round {r}: NOT arrived: at {cell_of(X, u["guid"])} want {tgt}; guest unit task', [q.get('task') for q in players(X) if q['guid'] == u['guid']])
        host_clicks(X, u['guid'], 1)
    cap1 = c(X, cmd='ord_stats').get('captured', 0)
    lg = log_since(X, mx)
    log('host log lines with ord/order:\n' + '\n'.join(l for l in lg.splitlines() if 'ord' in l or 'order' in l)[-2500:])
    check(f"guest orders all run while the host clicks his unit ({ok}/{rounds})", ok == rounds)
    n, first = L.fp_equal_from(X, Y, t0)
    check('fingerprints equal, 0 desyncs', n > 300 and first is None and not mp(X).get('desyncs') and not mp(Y).get('desyncs'), f'common {n} first {first}')


if __name__ == '__main__':
    sys.exit(L.run_main('b6_lookclick', body, window=True))
