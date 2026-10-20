#!/usr/bin/env python3
"""unitsel B4 (owner feedback v0.1.20): while one player paused, the others still give orders to their own units;
the orders are taken at once (stamped for the paused steps, same on every peer) and run when the game resumes, like
single-player. 2 min of play with pauses by the host and orders by the guest: 0 desyncs, equal fingerprints.
One hidden pair (windows: the guest's frames ~/ds_unitsel_out/b4_*.png), ElvenExpedition x10 demo save."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, check, order_move, players

SH = os.path.expanduser('~/ds_unitsel_out'); os.makedirs(SH, exist_ok=True)


def cell_of(x, g):
    u = [q for q in players(x) if q['guid'] == g]
    return tuple(u[0]['cell']) if u else None


def body(X, Y):
    mine = L.setup(X, Y, window=True)
    c(Y, cmd='ui_teams', action='close')
    t0 = st(X)['tick']; T0 = time.time()
    rounds = 0; ok_all = True
    while time.time() - T0 < 120 or rounds < 3:
        u = mine[Y][rounds % 2]
        start = cell_of(X, u['guid'])
        tgt = (start[0] + (3 if rounds % 2 == 0 else -3), start[1])
        log('round', rounds, 'host pauses', c(X, cmd='mp_pause', on=1))
        wait('paused on both', lambda: mp(X).get('paused') and mp(Y).get('paused'), timeout=60, every=1)
        time.sleep(2)
        tk = st(Y)['tick']; o0 = (mp(X).get('orders', 0), mp(Y).get('orders', 0))
        sent = order_move(Y, u['guid'], tgt)
        got = wait('the paused order is taken on both', lambda: mp(X).get('orders', 0) > o0[0] and mp(Y).get('orders', 0) > o0[1], timeout=30, every=1)
        time.sleep(3)
        still = st(Y)['tick'] == tk and cell_of(X, u['guid']) == start
        if rounds == 0:
            c(Y, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, 'b4_guest_order_while_paused.png'))
        log('host resumes', c(X, cmd='mp_pause', on=0))
        arrived = wait('the unit walks there after the resume', lambda: cell_of(X, u['guid']) == tgt and cell_of(Y, u['guid']) == tgt, timeout=60, every=1)
        ok = bool(sent and got and still and arrived)
        ok_all &= ok
        log(f'round {rounds}: sent {sent} taken {bool(got)} nothing moved while paused {still} arrived {bool(arrived)}')
        rounds += 1
        time.sleep(8)                                          # some play between the pauses
    check(f'B4: {rounds} rounds: a guest order given while the host paused is taken at once and runs on resume', ok_all)
    n, first = L.fp_equal_from(X, Y, t0)
    check('2 min with pauses + orders: fingerprints equal every common tick, 0 desyncs', n > 3000 and first is None and not mp(X).get('desyncs') and not mp(Y).get('desyncs'),
          f'common {n} first diff {first} desyncs {mp(X).get("desyncs")} {mp(Y).get("desyncs")}')


if __name__ == '__main__':
    sys.exit(L.run_main('b4_pause', body, window=True))
