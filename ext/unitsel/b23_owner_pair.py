#!/usr/bin/env python3
"""unitsel B3 + B2 (owner feedback v0.1.20), one hidden pair, base save MoonlightBrigade.
 B3: a guest who arrives with no unit gets the Teams window with "Pick your units" (once); the host does not.
 B2: a unit that changes owner (host moves it in Teams) leaves the selection of the player who no longer may order it;
     a friend's unit you only look at stays selected.
Both checks fail on the old code (no window; the lost unit stays selected)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, check, select, players

BASE = os.path.join(L.rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')


def tm(x, **k):
    return c(x, cmd='ui_teams', **k)


def sel(x):
    return c(x, cmd='ord_stats').get('sel', [])


def owner(x, g):
    return c(x, cmd='ui_owner', guid=g).get('owner')


def body(X, Y):
    L.setup(X, Y, save=BASE, claim=False)
    t0 = st(X)['tick']
    try:
        a = wait('guest: Teams opened by itself', lambda: (lambda s: s if s.get('open') else None)(tm(Y)), timeout=40, every=1)
    except RuntimeError:
        a = None
    check('B3: guest with no unit: the Teams window opens with "Pick your units"', bool(a) and 'Pick your units' in (a or {}).get('hint', ''), (a or {}).get('hint'))
    check('B3: the host (it orders free units) gets no window', not tm(X).get('open'))
    tm(Y, action='close'); time.sleep(6)
    check('B3: once: closed, it does not open again', not tm(Y).get('open'))
    us = players(X)
    for x, lst in ((X, us[:2]), (Y, us[2:4])):
        for u in lst:
            log(x, 'claim', u['name'], c(x, cmd='mp_claim', guid=u['guid']))
    time.sleep(4)
    u0, u1, u2 = us[0]['guid'], us[1]['guid'], us[2]['guid']
    # B2a: the host frees the guest's selected unit
    select(Y, u2); time.sleep(2)
    check('B2 setup: the guest has his unit selected', u2 in sel(Y), sel(Y))
    tm(X, action='open'); time.sleep(2)
    tm(X, action='admin', on=1); time.sleep(3)                # the host moves another player's unit only in Admin mode
    log('host header Free', tm(X, action='header', player=-1).get('target')); r = tm(X, action='click', guid=u2, button=0)
    log('host click u2: rc', r.get('rc'), 'target', r.get('target'), 'units', [(w.get('guid', '')[:8], w.get('clickable'), w.get('col')) for w in r.get('widgets', [])][:8])
    wait('u2 free on both', lambda: owner(X, u2) == -1 and owner(Y, u2) == -1, timeout=30, every=1)
    time.sleep(3)
    check('B2: the guest lost the unit (now free): it left his selection', u2 not in sel(Y), sel(Y))
    tm(X, action='admin', on=0); time.sleep(3)
    # B2b: the host gives its own selected unit to the guest
    tm(X, action='close'); select(X, u0); time.sleep(2)
    check('B2 setup: the host has its unit selected', u0 in sel(X), sel(X))
    tm(X, action='open'); time.sleep(2)
    log('host header guest', tm(X, action='header', player=1).get('target')); r = tm(X, action='click', guid=u0, button=0)
    log('host click u0: rc', r.get('rc'), 'target', r.get('target'))
    wait('u0 owned by the guest on both', lambda: owner(X, u0) == 1 and owner(Y, u0) == 1, timeout=30, every=1)
    time.sleep(3)
    check("B2: the host gave its unit away: it left the host's selection", u0 not in sel(X), sel(X))
    tm(X, action='close')
    # a friend's unit only looked at stays selected
    select(Y, u1); time.sleep(4)
    check("B2: a friend's unit the guest clicks to look at stays selected (no owner change)", u1 in sel(Y), sel(Y))
    n, first = L.fp_equal_from(X, Y, t0)
    check('fingerprints equal, no desync', n > 100 and first is None and not mp(X).get('desyncs'), f'common {n} first diff {first}')


if __name__ == '__main__':
    sys.exit(L.run_main('b23_owner', body))
