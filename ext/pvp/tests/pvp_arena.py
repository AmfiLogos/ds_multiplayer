#!/usr/bin/env python3
"""PvP M2 pair test (ext/pvp lane): arena floor. Lane "pvp", dylib ext/pvp/build/dsrig.dylib.

  pvp_arena.py [--tag NAME] [--floor 2] [--seed 424242] [--ticks 4000] [--keep]

Host H loads rig.SAVE (dungeon region 1 floor 0), guest G joins; H claims 2 units, G claims 2. Host: SETUP (slot 0 Blue,
slot 1 Red), ARENA (region 1, floor F, forced seed S). Checks on both copies:
  A1 both arrive on floor F with the same generated map (room hash), same PvP state
  A2 no living monster, no NPC on the arena; spawn categories were cut on both
  B1 START: same team ends on both; Red walks to the far portal, Blue holds the arrival point; the fight begins
     (placing ends) with the teams far apart, then every unit gets an attack order on the closest enemy
  B2 combat: both teams lose HP; a unit goes out
  C1 exit portal refused (a portal interaction order on the exit portal: no return screen, refusal counted)
  C2 no respawn / dungeon simulator: its Tick is cut, still 0 monsters at the end; stock game-over check skipped
  D  fingerprints equal for 3000+ ticks, no desync
Logs and frames: ext/pvp/out/<tag>/."""
import json, os, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('arena_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
OUT = os.path.join(ROOT, 'ext', 'pvp', 'out', TAG); os.makedirs(OUT, exist_ok=True)
results = {}
LOG = open(os.path.join(OUT, 'test.log'), 'a')


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def c(_inst, **k):
    return rig.send(_inst, k, timeout=120)


def st(x):
    for _ in range(10):
        s = c(x, cmd='status')
        if 'tick' in s:
            return s
        time.sleep(2)
    raise RuntimeError(f'{x}: no status')


def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def lease():
    for i in range(480):
        p = rig.lease('pvp', os.getpid())
        if p:
            return p
        if i % 10 == 0:
            log('no free pair yet, waiting')
        time.sleep(30)
    raise RuntimeError('no free pair')


def fp_read(x, t_from):
    d = {}
    for line in open(os.path.join(rig.idir(x), 'fp.log')):
        q = line.split()
        if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from:
            d[int(q[0])] = q[1]
    return d


def shot(x, name):
    c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{name}.png'))


def main():
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'floor', FLOOR, 'seed', SEED, 'out', OUT)
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    both = lambda f: all(f(x) for x in (H, G))
    try:
        rig.start(H, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy'), 'H loaded', timeout=900)   # a loaded Mac: the host can take minutes
        time.sleep(5)
        rig.start(G, fps=30)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
        wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        c(H, cmd='mp_pause', on=1); c(G, cmd='mp_pause', on=1)
        wait('both paused, same tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a['mp'].get('paused') and b['mp'].get('paused'))(st(H), st(G)), timeout=120)
        us = c(H, cmd='list_units')['units']
        players = sorted([u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u], key=lambda u: u['guid'])
        blue, red = players[:2], players[2:4]
        for u in blue:
            c(H, cmd='mp_claim', guid=u['guid'])
        for u in red:
            c(G, cmd='mp_claim', guid=u['guid'])
        log('setup', c(H, cmd='pvp_setup', teams='1,2'))
        time.sleep(1)
        log('arena', c(H, cmd='pvp_arena', region=1, floor=FLOOR, seed=SEED))
        time.sleep(1)
        c(H, cmd='mp_pause', on=0); c(G, cmd='mp_pause', on=0)
        # A1: both on the arena floor, same map
        wait('both on the arena floor, idle and locked', lambda: both(lambda x: (lambda s: s.get('floor') == FLOOR and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(st(x))), timeout=600)
        time.sleep(3)
        ai = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
        ps = {x: c(x, cmd='pvp_status') for x in (H, G)}
        for x in (H, G):
            log(x, 'arena_info', json.dumps(ai[x])[:900])
        check('A1 both on floor %d with the same generated map and PvP state' % FLOOR,
              ai[H]['room_hash'] == ai[G]['room_hash'] and ai[H]['rooms'] > 0 and ps[H]['hash'] == ps[G]['hash'] and ps[H]['arena'] == 1,
              f"room hash {ai[H]['room_hash']} / {ai[G]['room_hash']}, rooms {ai[H]['rooms']}, pvp hash {ps[H]['hash']} / {ps[G]['hash']}")
        check('A2 arena empty: no living monster, no NPC; spawns cut on both',
              all(ai[x]['monsters_alive'] == 0 and ai[x]['non_player_non_monster'] == 0 and ai[x]['cuts']['spawn'] > 0 for x in (H, G)),
              f"monsters {ai[H]['monsters_alive']}/{ai[G]['monsters_alive']}, others {ai[H]['non_player_non_monster']}/{ai[G]['non_player_non_monster']}, cuts {ai[H]['cuts']} / {ai[G]['cuts']}")
        shot(H, 'H_arena'); shot(G, 'G_arena')
        # B1: START
        c(H, cmd='mp_pause', on=1); c(G, cmd='mp_pause', on=1)
        wait('paused', lambda: (lambda a, b: a['tick'] == b['tick'] and a['mp'].get('paused') and b['mp'].get('paused'))(st(H), st(G)), timeout=120)
        t0 = st(H)['tick']
        log('start', c(H, cmd='pvp_start'))
        wait('match on both', lambda: both(lambda x: c(x, cmd='pvp_status').get('active')), timeout=60)
        ps = {x: c(x, cmd='pvp_status') for x in (H, G)}
        ai = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
        log('ends', ps[H]['ends'], ps[G]['ends'], 'portals', ai[H]['portals'])
        c(H, cmd='mp_pause', on=0); c(G, cmd='mp_pause', on=0)
        fs = wait('fight begins', lambda: (lambda s: s if s['fight_start'] else None)(c(H, cmd='pvp_status')), timeout=300)
        cells = {u['guid']: u['cell'] for u in c(H, cmd='list_units')['units'] if 'cell' in u}
        cen = lambda team: [sum(cells[u['guid']][k] for u in team) / len(team) for k in (0, 1)]
        cb, cr = cen(blue), cen(red)
        sep = ((cb[0] - cr[0]) ** 2 + (cb[1] - cr[1]) ** 2) ** 0.5
        log('fight starts at match tick', fs['fight_start'] - 1, 'blue centre', cb, 'red centre', cr, 'separation', round(sep, 1), 'red end', ps[H]['ends'][1])
        check('B1 START: same ends on both; teams at opposite ends when the fight begins (separation > 15 cells); kick orders sent',
              ps[H]['ends'] == ps[G]['ends'] and ps[H]['ends'][1] != [0, 0] and sep > 15 and c(H, cmd='pvp_arena_info')['cuts']['kick'] >= 4,
              f"ends {ps[H]['ends']}, separation {round(sep, 1)}, fight at match tick {fs['fight_start'] - 1}")
        shot(H, 'H_fight_start'); shot(G, 'G_fight_start')
        # C1: portal interaction refused (Blue unit at the exit portal)
        far = max(ai[H]['portals'], key=lambda p: (p[0] - ps[H]['ends'][0][0]) ** 2 + (p[1] - ps[H]['ends'][0][1]) ** 2) if ai[H]['portals'] else None
        if far:
            log('portal order', c(H, cmd='ord_run_attack', unit=blue[0]['guid'], target=far[2], x=far[0], y=far[1], type=29, mp=1))
        # B2: fight to an out unit; then run on
        hp0 = {u['guid']: u['hp'] for u in players}
        out = None; t_run = time.time(); lost_seen = None
        while time.time() - t_run < 900:
            s = c(H, cmd='pvp_status'); us_ = {u['guid']: u for u in s['units']}
            lost = {g: round(hp0[g] - us_[g].get('hp', hp0[g]), 1) for g in us_}
            if any(lost[u['guid']] > 0 for u in blue) and any(lost[u['guid']] > 0 for u in red):
                lost_seen = lost_seen or (s['ticks'], lost)
            outs = [(u['guid'][:8], u['out_tick']) for u in s['units'] if u.get('out_tick', -1) >= 0]
            if outs and not out:
                out = (s['ticks'], outs); log('unit out', out); shot(H, 'H_out'); shot(G, 'G_out')
            if s['ticks'] >= TICKS and out:
                break
            time.sleep(3)
        check('B2 combat on the arena: both teams lose HP, a unit goes out', bool(lost_seen and out), f'hp lost {lost_seen}, out {out}')
        c(H, cmd='mp_pause', on=1)
        wait('both paused at one tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a['mp'].get('paused') and b['mp'].get('paused'))(st(H), st(G)), timeout=120)
        ai = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
        res = {x: c(x, cmd='ui_result') for x in (H, G)}
        log('end arena_info', json.dumps(ai[H]['cuts']), json.dumps(ai[G]['cuts']), 'result screen', res)
        check('C1 exit portal refused on both (no return screen)', far is not None and all(ai[x]['cuts']['portal'] > 0 and not res[x].get('open') for x in (H, G)),
              f"portal refusals {ai[H]['cuts']['portal']}/{ai[G]['cuts']['portal']}")
        check('C2 no respawn: dungeon simulator cut, still 0 monsters; stock game-over check skipped',
              all(ai[x]['cuts']['dungeon_tick'] > 1000 and ai[x]['monsters_alive'] == 0 and ai[x]['cuts']['gameover'] > 0 for x in (H, G)),
              f"dungeon ticks cut {ai[H]['cuts']['dungeon_tick']}, monsters {ai[H]['monsters_alive']}, game over skipped {ai[H]['cuts']['gameover']}/{ai[G]['cuts']['gameover']}")
        fh, fg = fp_read(H, t0), fp_read(G, t0)
        com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
        mh, mg = st(H)['mp'], st(G)['mp']; sH, sG = c(H, cmd='pvp_status'), c(G, cmd='pvp_status')
        check('D fingerprints equal for 3000+ ticks, no desync, same PvP hash',
              len(com) >= 3000 and first is None and mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0 and sH['hash'] == sG['hash'],
              f'{len(com)} common ticks, first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, pvp hash {sH["hash"]}/{sG["hash"]}')
        shot(H, 'H_end'); shot(G, 'G_end')
        json.dump({'results': results, 'ends': sH['ends'], 'out': out, 'cuts': ai[H]['cuts'], 'units': sH['units']}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, 'pvp')   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
