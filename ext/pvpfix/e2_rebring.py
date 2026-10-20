#!/usr/bin/env python3
"""E2 (owner v0.1.20: "bring units ... from inside a PvP world (there is no button to bring units once inside)"): in the
match preparation each player has a Bring Units button on the HUD; a new Bring there (here: the guest swaps to one other
unit, through the same send as the card window) makes the host set the match up again with the new roster: the brought
units come in, the match runs, 0 desyncs, and the world after the match is the one from before the FIRST start.
  e2_rebring.py [--dylib PATH] [--tag NAME]"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pv_lib import T, G_UNITS  # noqa: E402


def body(t):
    H, G = t.H, t.G
    t.prepare(); t.host_world(bo_down=1, secs_down=2); t.guest_join()
    tpl = sorted(u['guid'] for u in t.c(H, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead'])
    t.bring_both(); t.lobby_start()
    time.sleep(3)
    q = {x: t.c(x, cmd='mpm_pvp') for x in (H, G)}
    t.check('E2 Bring Units button on the HUD in the preparation (both copies)', all(q[x].get('ok') for x in (H, G)) and all(t.c(x, cmd='mpm_pvp', action='press_hud_bring').get('ok') is not False for x in (H, G)), q[H].get('hud_text'))
    m0 = t.c(H, cmd='pvp_status')['matches']
    t.log('G brings again (one unit)', t.c(G, cmd='pvp_units_bring', file=os.path.join(t.work[G], 'my_home.json'), guids=G_UNITS[0]))
    t.wait('match set up again with 3 units', lambda: (lambda s: s.get('active') and s.get('matches', 0) > m0 and len(s.get('units', [])) == 3)(t.c(H, cmd='pvp_status')), timeout=300, every=2)
    t.wait('guest in the new match', lambda: (lambda s: s.get('active') and len(s.get('units', [])) == 3)(t.c(G, cmd='pvp_status')), timeout=300, every=2)
    s = t.c(H, cmd='pvp_status')
    t.check('E2 the match runs again with the new roster (3 units: host 2, guest 1)', sum(1 for u in s['units'] if u['player'] == 1) == 1 and sum(1 for u in s['units'] if u['player'] == 0) == 2, [(u['player'], u['guid'][:8]) for u in s['units']])
    t.c(G, cmd='pvp_vote', op='propose'); t.wait('vote', lambda: t.c(H, cmd='pvp_status')['vote_open'], timeout=30, every=0.5); t.c(H, cmd='pvp_vote', op='agree')
    t.wait('fight', lambda: (lambda q2: q2.get('fight_start', 0) > 0 and not q2.get('placing'))(t.c(H, cmd='pvp_status')), timeout=90, every=1)
    seg = t.c(H, cmd='status')['tick']; segs = []
    t.wait('round ends (time)', lambda: t.c(H, cmd='pvp_status').get('ending') or not t.c(H, cmd='pvp_status').get('active'), timeout=200, every=1)
    segs.append(t.fp_compare(seg, settle=0.3)); t.log('segment', segs)
    t.wait('match over + world back', lambda: all((lambda q2: not q2.get('active') and q2.get('phase') == 0)(t.c(x, cmd='pvp_status')) for x in (H, G)), timeout=300, every=2)
    t.wait('both loaded', lambda: all((lambda q2: q2.get('scene') == 2 and not q2.get('busy'))(t.c(x, cmd='status')) for x in (H, G)), timeout=300, every=2)
    time.sleep(5)
    back = {x: sorted(u['guid'] for u in t.c(x, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead']) for x in (H, G)}
    ds = (t.c(H, cmd='status')['mp'].get('desyncs'), t.c(G, cmd='status')['mp'].get('desyncs'))
    t.check('E2 after the match: the world of before the first start (template units, no brought unit) on both', back[H] == back[G] == tpl, (len(back[H]), len(tpl)))
    t.check('E2 0 desyncs', segs[0][0] >= 200 and segs[0][1] is None and not ds[0] and not ds[1], (segs, ds))


if __name__ == '__main__':
    T('e2', sys.argv).run(body)
