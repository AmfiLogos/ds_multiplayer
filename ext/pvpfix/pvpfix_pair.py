#!/usr/bin/env python3
"""pvpfix lane: one full PvP v1 match on a hidden Mac pair (owner feedback on v0.1.20, PLAN_v021.md section E), checks:
  E1  Bring lists the regular saves (DSML_GAME_SAVES) next to the modded ones (text list; the card window uses the same list)
  E4  fight vote: a proposal pauses the game; Not yet closes it; nobody starts alone; all agree -> countdown; Cancel stops it
  E5  each player sees only his team's vision: enemy units hidden outside it; no opponent camera view / ping
  E5b/E7b a click on an enemy unit gives the monster group (3001), never my player-unit group; an order on an enemy unit
      sent from my copy changes nothing on it
  E6  random team ends: reachable, 12..30 cells apart (or the farthest pair), same on both copies
  E7  hold position (G) honoured when the fight begins; no automatic attack orders
  E8  HUD shows the preparation / vote / countdown ("Fight in N")
  E9  round result screen while the world stands still, then the next round / the match end
  and 0 desyncs (fingerprints of every common tick per world segment, desync counters).
  pvpfix_pair.py [--dylib PATH] [--tag NAME] [--ctf]      (--ctf: Capture the Flag, best of 1 (one round), a capture tried)
Frames (game back buffer, UI included): <out>/*.png"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pv_lib import T, rig, HOME  # noqa: E402

CTF = '--ctf' in sys.argv
ZERO = '00000000-0000-0000-0000-000000000000'


def body(t):
    H, G = t.H, t.G
    gs = os.path.join(t.out, 'game_saves_H'); os.makedirs(gs, exist_ok=True)
    REG = 'RegularWorld__AutoSave_0123456789abcdef0123456789abcdef_0'
    shutil.copy2(HOME, os.path.join(gs, REG + '.json'))
    t.prepare(game_saves={H: gs})
    t.host_world(bo_down=(1 if CTF else 0), secs_down=2)                  # Fight: best of 3, 60 s; CTF: 1 capture (1 round), 60 s
    # E1: the lobby's Bring Units (text list in the rig copy: no widget kit) lists the regular save
    r = t.c(H, cmd='mpm_pvp', action='lobby_bring'); time.sleep(1.5); r = t.c(H, cmd='mpm_pvp')
    t.check('E1 Bring lists the regular save from the game folder (DSML_GAME_SAVES)', REG in r.get('bring', {}).get('saves', []), r.get('bring', {}).get('saves'))
    t.c(H, cmd='mpm_pvp', action='br_back'); time.sleep(1)
    if CTF: t.c(H, cmd='mpm_pvp', action='page_arrow', row=0, dir=1)       # host page row 0: Mode -> Capture the Flag (lobby rules = page rules)
    t.guest_join(); t.bring_both(); t.lobby_start()
    s = t.c(H, cmd='pvp_status')
    t.log('match on: mode', s['mode'], 'rules', s['rules'], 'ends', s['ends'], 'kicks', s.get('kicks'))
    units = s['units']; owner = {u['guid']: (H if u['player'] == 0 else G) for u in units}; team = {u['guid']: u['team'] for u in units}
    # E6: ends same on both, reachable distance
    e = {x: t.c(x, cmd='pvp_status')['ends'] for x in (H, G)}
    d = ((e[H][0][0] - e[H][1][0]) ** 2 + (e[H][0][1] - e[H][1][1]) ** 2) ** 0.5
    t.check('E6 random team ends: same on both copies, two cells 12..30 apart', e[H] == e[G] and e[H][0] != e[H][1] and 12 <= d <= 30.5, (e, round(d, 1)))
    # E5: each copy hides the enemy units it cannot see; fog per team; no opponent view
    f = {}
    for _ in range(25):                                                     # the teams walk apart: until each copy hides an enemy (max ~50 s)
        f = {x: t.c(x, cmd='mpm_fog') for x in (H, G)}
        if any(f[x].get('enemy_hidden', 0) >= 1 for x in (H, G)): break
        time.sleep(2)
    t.log('fog', f)
    for x in (H, G): t.shot(x, 'fog_world', ui=False)
    t.check('E5 per-team fog: each copy has its team, per-team fog on, every enemy unit shown exactly when its tile is in the team\'s vision',
            all(f[x].get('pvp_team') in (1, 2) and f[x].get('pvp_team') != f[H if x == G else G].get('pvp_team') and f[x].get('filtering') and f[x].get('eov') for x in (H, G))
            and all(len(f[x].get('enemies', [])) == 2 and all(e[0] == e[1] for e in f[x]['enemies']) for x in (H, G)), f)
    t.log('E5 enemies hidden now (outside the team vision):', {x: f[x].get('enemy_hidden') for x in (H, G)})
    # E7: hold position on one Blue unit (its owner's copy, the game's own event)
    hold = next(u['guid'] for u in units if u['team'] == 1)
    t.log('hold on', hold[:8], t.c(owner[hold], cmd='ord_inject', ev='ToggleBehaviourOption', **{'ent@16': hold, 'i32@24': 2, 'u8@28': 1, 'u8@29': 1}))
    t.wait('hold option on both copies', lambda: all(next(u for u in t.c(x, cmd='pvp_status')['units'] if u['guid'] == hold).get('options', 0) & 2 for x in (H, G)), timeout=30, every=1)
    # placing: walk to the ends (first 10 s)
    time.sleep(14)
    q = {x: t.c(x, cmd='mpm_pvp') for x in (H, G)}
    for x in (H, G): t.shot(x, 'prep')
    t.check('E8 preparation shown: HUD "Preparation", Propose fight button on both, no countdown', all('Preparation' in q[x].get('hud_text', '') and q[x].get('fight_btn') for x in (H, G))
            and not t.c(H, cmd='pvp_status')['countdown_at'], {x: (q[x].get('hud_text'), q[x].get('fight_btn')) for x in (H, G)})
    # E5b / E7b: a click on an enemy unit = the monster group; my own unit = player group
    mine_h = next(u['guid'] for u in units if owner[u['guid']] == H); enemy_h = next(u['guid'] for u in units if owner[u['guid']] == G)
    sg = {'enemy': t.c(H, cmd='pvp_selgroup', guid=enemy_h).get('group'), 'mine': t.c(H, cmd='pvp_selgroup', guid=mine_h).get('group')}
    t.check('E5b/E7b click on an enemy unit: monster group 3001 (inspect view, no selection for orders); my unit 3003', sg == {'enemy': 3001, 'mine': 3003}, sg)
    # E4 vote: G proposes -> paused; H not yet -> closed, no countdown
    t.log('G propose', t.c(G, cmd='pvp_vote', op='propose'))
    t.wait('vote open on both', lambda: all(t.c(x, cmd='pvp_status')['vote_open'] for x in (H, G)), timeout=30, every=0.5)
    t0 = t.c(H, cmd='status')['tick']; time.sleep(2.5); t1 = t.c(H, cmd='status')['tick']
    qv = {x: t.c(x, cmd='mpm_pvp') for x in (H, G)}
    for x in (H, G): t.shot(x, 'vote')
    t.check('E4 a proposal pauses the game and asks the other player (H: box with Agree / Not yet; G: waiting)',
            t1 == t0 and qv[H].get('proposal_box') and qv[G].get('proposal_box') and 'proposed' in qv[H].get('hud_text', ''), (t0, t1, {x: (qv[x].get('proposal_box'), qv[x].get('hud_text')) for x in (H, G)}))
    t.log('H not yet', t.c(H, cmd='mpm_pvp', action='press_prop_no'))
    t.wait('vote closed', lambda: not t.c(H, cmd='pvp_status')['vote_open'], timeout=30, every=0.5)
    time.sleep(2)
    t.check('E4 Not yet closes it: no countdown, the game runs again', not t.c(H, cmd='pvp_status')['countdown_at'] and t.c(H, cmd='status')['tick'] > t1)
    t.check('E4 nobody starts alone: the host has no direct start (a Start command from the host alone does nothing)',
            t.c(H, cmd='pvp_vote', op='agree') is not None and not t.c(H, cmd='pvp_status')['countdown_at'])
    # all agree -> countdown; cancel; again -> fight
    t.c(H, cmd='mpm_pvp', action='press_fight'); t.wait('vote open (H proposed)', lambda: t.c(G, cmd='pvp_status')['vote_open'], timeout=30, every=0.5)
    t.c(G, cmd='mpm_pvp', action='press_prop_yes')
    t.wait('countdown on both', lambda: all(t.c(x, cmd='pvp_status')['countdown_at'] for x in (H, G)), timeout=30, every=0.5)
    time.sleep(1.5); qc = {x: t.c(x, cmd='mpm_pvp') for x in (H, G)}
    for x in (H, G): t.shot(x, 'countdown')
    t.check('E4/E8 all agreed: the countdown runs, HUD "Fight in N" on both', all('Fight in' in qc[x].get('hud_text', '') for x in (H, G)), {x: qc[x].get('hud_text') for x in (H, G)})
    t.c(G, cmd='mpm_pvp', action='press_fight')                              # the button is Cancel now
    t.wait('countdown cancelled', lambda: not t.c(H, cmd='pvp_status')['countdown_at'], timeout=30, every=0.5)
    time.sleep(12)
    t.check('E4 Cancel stops the countdown: no fight without a new agreement', not t.c(H, cmd='pvp_status')['fight_start'])
    t.c(G, cmd='pvp_vote', op='propose'); t.wait('vote open', lambda: t.c(H, cmd='pvp_status')['vote_open'], timeout=30, every=0.5)
    t.c(H, cmd='pvp_vote', op='agree')
    hc = None
    def began():
        nonlocal hc
        s2, cells = t.units(H)
        if hc is None and s2.get('countdown_at'): hc = cells[hold].get('cell')
        return s2.get('fight_start', 0) > 0 and not s2.get('placing')
    t.wait('fight begins', began, timeout=60, every=0.5)
    s, cells = t.units(H); hc = hc or cells[hold].get('cell')
    t.log('fight began; hold unit at', hc)
    # fight: each player orders his units (not the hold unit) onto the closest enemy, like right-clicks, every 4 s
    segs = []; seg_from = t.c(H, cmd='status')['tick']; rounds_seen = {}; screen_ok = []; t_end = time.time() + 900
    hold_ok, cap_try = True, 0
    def order_all():
        s2, cells2 = t.units(H)
        alive = [u for u in s2['units'] if u.get('out_tick', -1) < 0 and u['guid'] in cells2 and 'cell' in cells2[u['guid']]]
        for u in alive:
            if u['guid'] == hold: continue
            en = [v for v in alive if v['team'] != u['team']]
            if not en: continue
            c0 = cells2[u['guid']]['cell']; tgt = min(en, key=lambda v: (cells2[v['guid']]['cell'][0] - c0[0]) ** 2 + (cells2[v['guid']]['cell'][1] - c0[1]) ** 2)
            tc = cells2[tgt['guid']]['cell']
            t.c(owner[u['guid']], cmd='ord_run_attack', unit=u['guid'], target=tgt['guid'], x=tc[0], y=tc[1], mp=1)
    first_order = True; done = set(); last_order = 0
    while time.time() < t_end:
        s = t.c(H, cmd='pvp_status')
        if not s.get('active') and s.get('matches', 0) >= 1: break
        if s.get('ending') and s['round'] not in rounds_seen:
            rounds_seen[s['round']] = s['ending']
            a0 = t.c(H, cmd='status')['tick']; time.sleep(0.8)
            sc = {x: t.c(x, cmd='mpm_pvp') for x in (H, G)}; a1 = t.c(H, cmd='status')['tick']
            n, first, ds = t.fp_compare(seg_from, settle=0); segs.append((n, first, ds)); t.log('segment fingerprints', n, first, ds)   # the world stands still: fp.log is complete
            for x in (H, G): t.shot(x, f'round{s["round"]}_screen')
            screen_ok.append(all(sc[x].get('round_screen') for x in (H, G)) and a0 == a1)
            t.log('round', s['round'], 'ending', s['ending'], 'score', s['score'], 'screen', {x: sc[x].get('round_screen') for x in (H, G)}, 'frozen', a0 == a1)
        if s.get('resetting') or s.get('ending'):
            time.sleep(1); continue
        if s.get('fight_start') and not s.get('placing'):
            if first_order:
                s3, c3 = t.units(H); hc2 = c3[hold].get('cell')
                if hc and hc2 and abs(hc2[0] - hc[0]) + abs(hc2[1] - hc[1]) > 1: hold_ok = False
                first_order = False
                for x in (H, G): t.shot(x, 'fight')
                if s['round'] == 1 and 'E7b' not in done:                  # E7b: my clicks / orders on an enemy unit change nothing on it
                    done.add('E7b')
                    s4, c4 = t.units(H)
                    alive = [u for u in s4['units'] if u.get('out_tick', -1) < 0 and u['guid'] in c4 and 'cell' in c4[u['guid']]]
                    X = next((u for u in alive if owner[u['guid']] == G), None); Y = next((u for u in alive if owner[u['guid']] == H and u['guid'] != hold), None)
                    if X and Y:
                        yc = c4[Y['guid']]['cell']; t.c(G, cmd='ord_run_attack', unit=X['guid'], target=Y['guid'], x=yc[0], y=yc[1], mp=1)
                        t.c(G, cmd='ord_inject', ev='ToggleBehaviourOption', **{'ent@16': X['guid'], 'i32@24': 2, 'u8@28': 1, 'u8@29': 1})   # its owner: hold ON
                        time.sleep(1.5)
                        before = {x: next(u for u in t.c(x, cmd='pvp_status')['units'] if u['guid'] == X['guid']) for x in (H, G)}
                        xc = c4[X['guid']]['cell']
                        t.c(H, cmd='ord_run_attack', unit=Y['guid'], target=X['guid'], x=xc[0], y=xc[1], type=21, mp=1)   # a non-attack interaction aimed at the enemy
                        t.c(H, cmd='ord_run_attack', unit=X['guid'], target=Y['guid'], x=yc[0], y=yc[1], mp=1)            # an order for the enemy unit itself
                        t.c(H, cmd='ord_inject', ev='ToggleBehaviourOption', **{'ent@16': X['guid'], 'i32@24': 2, 'u8@28': 1, 'u8@29': 0})   # me: hold OFF on the enemy
                        time.sleep(2)
                        after = {x: next(u for u in t.c(x, cmd='pvp_status')['units'] if u['guid'] == X['guid']) for x in (H, G)}
                        keep = all((after[x].get('options', 0) & 2) and (before[x].get('options', 0) & 2) and (after[x].get('target') == before[x].get('target') or after[x].get('out_tick', -1) >= 0
                                   or before[x].get('target') == ZERO) for x in (H, G))
                        t.check('E7b my orders on an enemy unit change nothing on it (its hold position stays on, its target stays; both copies)', keep,
                                {x: ((before[x].get('target', '')[:8], before[x].get('options')), (after[x].get('target', '')[:8], after[x].get('options'))) for x in (H, G)})
            if s['round'] not in rounds_seen and time.time() - last_order >= 4:
                order_all(); last_order = time.time()
        elif s.get('placing') and s.get('round', 1) > 1:
            seg_from = t.c(H, cmd='status')['tick']                          # a new world segment after the reload
            first_order = True; hc = None
        time.sleep(1)
    t.check('E7 hold position: the held unit did not walk off when the fight began (no automatic attack orders)', hold_ok and s.get('kicks', 0) == 0)
    t.check('E9 round result screen on both copies while the world stands still, for every ended round', screen_ok and all(screen_ok), (rounds_seen, screen_ok))
    t.wait('match over on both + world back', lambda: all((lambda q2: not q2.get('active') and q2.get('phase') == 0)(t.c(x, cmd='pvp_status')) for x in (H, G)), timeout=600, every=2)
    t.wait('both loaded', lambda: all((lambda q2: q2.get('scene') == 2 and not q2.get('busy'))(t.c(x, cmd='status')) for x in (H, G)), timeout=600, every=2)
    pe = {x: t.c(x, cmd='pvp_status') for x in (H, G)}
    ds = (t.c(H, cmd='status')['mp'].get('desyncs'), t.c(G, cmd='status')['mp'].get('desyncs'))
    t.check('match end: same winner + score on both', pe[H]['match_winner'] == pe[G]['match_winner'] and pe[H]['score'] == pe[G]['score'], {x: (pe[x]['match_winner'], pe[x]['score']) for x in (H, G)})
    t.check('0 desyncs: fingerprints equal in every round segment, desync counters 0', segs and all(n >= 200 and first is None for n, first, _ in segs) and not ds[0] and not ds[1], (segs, ds))
    t.wait('result window', lambda: all(t.c(x, cmd='mpm_pvp').get('result') for x in (H, G)), timeout=120, every=2)
    for x in (H, G): t.shot(x, 'result')
    json.dump({'results': t.results, 'rounds': rounds_seen, 'segs': segs}, open(os.path.join(t.out, 'result.json'), 'w'), indent=1)


if __name__ == '__main__':
    T('pvpfix_ctf' if CTF else 'pvpfix', sys.argv).run(body)
