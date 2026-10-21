#!/usr/bin/env python3
"""unitsel B5 + vote pause (owner 2026-10-20): a guest's Return scroll goes to a vote of every player; while the vote is
open the game is paused for everyone, also in "No pause" mode; refused = nobody moves; agreed = the scroll runs (both
peers back to the settlement). One hidden pair, mp.json allow_pause false, ElvenExpedition x10 demo save (dungeon).
Fails on the old code: the scroll ran at once (no vote box, no pause). Frames: ~/ds_unitsel_out/b5_*.png"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait, check, inject, select

SCROLL = '92be08ca-b7de-497a-a4ea-9aac4be0b603'   # Leo: a Return scroll in item slot 3 of this save
SH = os.path.expanduser('~/ds_unitsel_out'); os.makedirs(SH, exist_ok=True)


def vs(x):
    return c(x, cmd='ui_vote_state')


def ticks_still(xs, secs=5):
    a = [st(x)['tick'] for x in xs]; time.sleep(secs); b = [st(x)['tick'] for x in xs]
    return a == b, (a, b)


def use_scroll(Y):
    select(Y, SCROLL); time.sleep(1)
    return inject(Y, 'UseItemRequested', i32_16=1, guid_32=SCROLL, i32_52=3, i32_48=-1)


def body(X, Y):
    L.setup(X, Y, window=True, allow_pause=False, guest_claims=[SCROLL])
    t0 = st(X)['tick']
    # ---- 1. refused
    r = use_scroll(Y)
    log('scroll use (guest)', r)
    v = wait('vote box on the host', lambda: (lambda s: s if s.get('open') and s.get('prompt') else None)(vs(X)), timeout=60, every=1)
    check('the scroll did not run at once: vote open, kind Return scroll, proposer = guest, both still in the dungeon',
          v.get('kind') == 4 and v.get('proposer') == 1 and st(X).get('map') == 1 and st(Y).get('map') == 1, v)
    time.sleep(2)
    still, tk = ticks_still([X, Y])
    check('vote open: the game is paused for everyone (No pause mode)', still and vs(Y).get('vote_hold') == 1, f'ticks {tk}, guest hold {vs(Y)}')
    c(X, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, 'b5_host_vote_box.png'))
    c(Y, cmd='mpm_shot', how='backbuffer', path=os.path.join(SH, 'b5_guest_waiting.png'))
    log('host refuses', c(X, cmd='ui_map_vote', accept=0))
    wait('vote closed', lambda: not vs(X).get('open') and not vs(Y).get('open'), timeout=30, every=1)
    time.sleep(6)
    still, tk = ticks_still([X, Y], 4)
    check('refused: the game runs again and nobody left the dungeon', not still and st(X).get('map') == 1 and st(Y).get('map') == 1 and vs(Y).get('vote_hold') == 0, f'ticks {tk}')
    # ---- 2. agreed
    r = use_scroll(Y); log('scroll use again (guest)', r)
    wait('vote box on the host again', lambda: vs(X).get('open') and vs(X).get('prompt'), timeout=60, every=1)
    still, tk = ticks_still([X, Y], 4)
    check('second vote open: paused again', still, f'ticks {tk}')
    log('host agrees', c(X, cmd='ui_map_vote', accept=1))
    ok = wait('both back in the settlement', lambda: st(X).get('map') == 0 and st(Y).get('map') == 0 and not st(X).get('busy') and not st(Y).get('busy'), timeout=180, every=2)
    check('agreed: the Return scroll ran: both peers in the settlement', bool(ok))
    time.sleep(10)
    still, tk = ticks_still([X, Y], 4)
    check('after the return the game runs (no pause left behind)', not still, f'ticks {tk}')
    n, first = L.fp_equal_from(X, Y, t0)
    check('fingerprints equal every common tick, no desync flagged', n > 300 and first is None and not mp(X).get('desyncs') and not mp(Y).get('desyncs'),
          f'common {n}, first diff {first}, desyncs {mp(X).get("desyncs")} {mp(Y).get("desyncs")}')


if __name__ == '__main__':
    sys.exit(L.run_main('b5_scroll', body, window=True))
