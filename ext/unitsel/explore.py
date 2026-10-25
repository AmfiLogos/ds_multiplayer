#!/usr/bin/env python3
"""exploration (not a test): what happens today to a guest order while the host paused (B4), and to a Return scroll use (B5)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from us_lib import *  # noqa

SCROLL_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', '7012fd3d-525f-47bd-bfb5-2c35e53b7709']
SCROLL_SLOT = {'92be08ca-b7de-497a-a4ea-9aac4be0b603': 3, '7012fd3d-525f-47bd-bfb5-2c35e53b7709': 5}


def body(X, Y):
    mine = setup(X, Y, claim=False)
    us = players(X)
    log('players', [(u['name'], u['guid'], u['cell']) for u in us])
    sc = [u for u in us if u['guid'] in SCROLL_UNITS]
    other = [u for u in us if u['guid'] not in SCROLL_UNITS]
    gy = sc[:1] + other[:1]; gx = other[1:3]
    for x, lst in ((X, gx), (Y, gy)):
        for u in lst:
            log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
        c(x, cmd='ord_hover', n=0)
    time.sleep(4)
    # ---- B4: host pauses, guest orders its own unit
    log('host pause', c(X, cmd='mp_pause', on=1))
    wait('paused both', lambda: mp(X).get('paused') and mp(Y).get('paused'), timeout=60)
    time.sleep(2)
    u = gy[1]; cell = (u['cell'][0] + 3, u['cell'][1])
    my, mx = mark(Y), mark(X)
    o0 = (mp(X).get('orders'), mp(Y).get('orders')); s0 = c(Y, cmd='ord_stats')
    log('guest order while paused', order_move(Y, u['guid'], cell))
    time.sleep(6)
    log('orders applied before/after', o0, (mp(X).get('orders'), mp(Y).get('orders')))
    log('guest ord_stats before', s0); log('guest ord_stats after', c(Y, cmd='ord_stats'))
    log('guest log:\n' + log_since(Y, my)[-3000:])
    log('host log:\n' + log_since(X, mx)[-1500:])
    log('host resume', c(X, cmd='mp_pause', on=0))
    wait('running', lambda: not mp(X).get('paused') and not mp(Y).get('paused'), timeout=60)
    time.sleep(8)
    u2 = [q for q in players(X) if q['guid'] == u['guid']]
    log('unit after resume', u['cell'], '->', u2[0]['cell'] if u2 else None, 'target', cell)
    # ---- B5: guest uses a Return scroll
    if sc:
        s = sc[0]; slot = SCROLL_SLOT[s['guid']]
        my, mx = mark(Y), mark(X)
        log('select', select(Y, s['guid']))
        r = inject(Y, 'UseItemRequested', i32_16=1, guid_32=s['guid'], i32_52=slot, i32_48=-1)
        log('UseItemRequested', r)
        time.sleep(8)
        log('guest log:\n' + log_since(Y, my)[-3000:])
        log('host log:\n' + log_since(X, mx)[-2000:])
        log('status X', st(X).get('map'), 'Y', st(Y).get('map'))
        time.sleep(15)
        log('status X', st(X).get('map'), 'Y', st(Y).get('map'))
    results['explored'] = True


if __name__ == '__main__':
    sys.exit(run_main('explore', body))
