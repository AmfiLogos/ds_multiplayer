#!/usr/bin/env python3
"""E7 (owner v0.1.20 PvP session): a player's own orders must win in PvP.
 a) G = hold position (BehaviourOption WaitForOrder, toggled through the game's own ToggleBehaviourOption event as the
    input path sends it): that unit must not walk off to chase / attack by itself when the fight begins.
 b) an attack order on enemy X stays on X while another enemy Y attacks the unit (the attack order is not overwritten).
Fails on v0.1.20: the PvP fight kicks gave every unit an automatic attack order on the closest enemy at the fight start
and every 2 s to idle units, over hold position and over the player's own orders.
  e7_orders.py [--dylib PATH] [--tag NAME]"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pv_lib import T  # noqa: E402


def body(t):
    H, G = t.H, t.G
    t.prepare(); t.host_world(); t.guest_join(); t.bring_both(); t.lobby_start()
    s, cells = t.units(H)
    blue = [u for u in s['units'] if u['team'] == 1]; red = [u for u in s['units'] if u['team'] == 2]
    b0, b1 = blue[0]['guid'], blue[1]['guid']
    owner = {u['guid']: (H if u['player'] == 0 else G) for u in s['units']}
    # a) hold position on b0 (from its owner's copy, the game's event; captured and sent like a key press)
    r = t.c(owner[b0], cmd='ord_inject', ev='ToggleBehaviourOption', **{'ent@16': b0, 'i32@24': 2, 'u8@28': 1, 'u8@29': 1})
    t.log('hold position on', b0[:8], r)
    t.wait('hold option on the unit (both copies)', lambda: all((next(u for u in t.c(x, cmd='pvp_status')['units'] if u['guid'] == b0).get('options', 2) & 2) for x in (H, G)), timeout=30, every=1)
    t.log('Start fight (host)', t.c(H, cmd='pvp_fight'))                   # v0.1.20: unknown command, the fight began on arrival
    t.wait('fight begins', lambda: (lambda q: q.get('fight_start', 0) > 0 and not q.get('placing'))(t.c(H, cmd='pvp_status')), timeout=300, every=1)
    s, cells = t.units(H); c0 = cells[b0].get('cell'); t.log('fight began; hold unit at', c0)
    # b) b1 attacks red[1]; red[0] keeps whatever it does (fights b1 or b0)
    x = red[1]['guid']; xc = cells[x].get('cell', [0, 0])
    t.log('b1 attack order', t.c(owner[b1], cmd='ord_run_attack', unit=b1, target=x, x=xc[0], y=xc[1], mp=1))
    held, kept, trace = True, True, []
    for i in range(16):
        time.sleep(0.5)
        s, cells = t.units(H)
        u0 = next(u for u in s['units'] if u['guid'] == b0); u1 = next(u for u in s['units'] if u['guid'] == b1)
        c = cells[b0].get('cell'); moved = c and c0 and abs(c[0] - c0[0]) + abs(c[1] - c0[1]) > 1
        tgt = u0.get('target', '00000000-0000-0000-0000-000000000000'); att = tgt[:8] != '00000000'
        trace.append((i, c, u0.get('task'), u0.get('combat'), tgt[:8], u1.get('target', '')[:8], u1.get('task_target', '')[:8], u1.get('task')))
        if moved: held = False
        if i >= 2 and u1.get('out_tick', -1) < 0 and next(q for q in s['units'] if q['guid'] == x).get('out_tick', -1) < 0 \
                and u1.get('target', '')[:8] not in ('', x[:8]) and u1.get('task_target', '')[:8] != x[:8]: kept = False
    t.log('trace (i, hold cell, task, combat, hold target, b1 target, b1 task target, b1 task)', trace)
    t.check('a hold position (G): the unit stays where it is when the fight begins (no automatic chase)', held, f'start {c0}')
    t.check('b attack order kept: b1 stays on its target while another enemy fights it', kept)
    n, first, ds = t.fp_compare(t.t_match)
    t.check('c no desync (fingerprints equal)', n >= 300 and first is None and not ds[0] and not ds[1], (n, first, ds))


if __name__ == '__main__':
    T('e7', sys.argv).run(body)
