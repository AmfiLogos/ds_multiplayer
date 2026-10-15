#!/usr/bin/env python3
"""PvP M3 + M5 pair test (ext/pvp lane): rounds, round reset by snapshot reload, match end back to the world; pause rule.
(Helpers copied from pvp_arena.py.) Old M2 doc kept below for the shared setup:
 Lane "pvp", dylib ext/pvp/build/dsrig.dylib.

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
TAG = arg('--tag', time.strftime('rounds_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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
    log('pair', pair, 'out', OUT)
    global T_START; T_START = time.strftime('%H:%M:%S')
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    both = lambda f: all(f(x) for x in (H, G))
    ps = lambda x: c(x, cmd='pvp_status')
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
        us = c(H, cmd='list_units')['units']
        players = sorted([u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u], key=lambda u: u['guid'])
        blue, red = players[:2], players[2:4]
        for u in blue:
            c(H, cmd='mp_claim', guid=u['guid'])
        for u in red:
            c(G, cmd='mp_claim', guid=u['guid'])
        time.sleep(2)
        pre = {x: (st(x), c(x, cmd='pvp_arena_info'), {u['guid']: round(u['hp'], 1) for u in c(x, cmd='list_units')['units'] if u['faction'] == 1}) for x in (H, G)}
        log('before the match: floor', pre[H][0].get('floor'), 'monsters', pre[H][1]['monsters_alive'], 'player hp', pre[H][2])
        log('setup', c(H, cmd='pvp_setup', teams='1,2', best_of=3, round_secs=90, pause=1, plan_every=12, plan_len=6))
        time.sleep(1)
        log('arena', c(H, cmd='pvp_arena', region=1, floor=FLOOR, seed=SEED))
        wait('both on the arena floor', lambda: both(lambda x: (lambda s: s.get('floor') == FLOOR and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(st(x))), timeout=600)
        time.sleep(3)
        fx = arg('--fix-out', 1)
        log('fix_out', fx, c(H, cmd='pvp_cfg', fix_out=fx), c(G, cmd='pvp_cfg', fix_out=fx))
        log('start', c(H, cmd='pvp_start'))
        wait('match on both', lambda: both(lambda x: ps(x).get('active')), timeout=60)
        time.sleep(2)
        # P2: no player pause in a match
        log('G pause vote (must be dropped)', c(G, cmd='mp_pause', on=1)); time.sleep(3)
        check('P2 no player pause in the match (pause vote dropped)', not st(H)['mp'].get('paused') and not st(G)['mp'].get('paused'))
        c(G, cmd='mp_pause', on=0)
        # P1 planning pauses: the first one ended by Ready from both players, the next by the host clock
        p1 = None; p2 = None; rounds = []; seen_round = 1; t_all = time.time()
        while time.time() - t_all < 1500:
            sh = ps(H)
            if sh['planning'] and p1 is None:
                w0 = st(H)['tick']; time.sleep(1.5); w1 = st(H)['tick']; wg = st(G)['tick']
                o = c(H, cmd='ord_run_attack', unit=blue[0]['guid'], target=red[0]['guid'], x=0, y=0, mp=1)
                time.sleep(0.5)
                r = (c(H, cmd='pvp_ready'), c(G, cmd='pvp_ready'))
                t_r = time.time(); wait('planning ended by Ready', lambda: not ps(H)['planning'] and not ps(G)['planning'], timeout=20, every=0.3)
                p1 = {'frozen': w0 == w1, 'tick': w1, 'guest_tick': wg, 'order_rc': o.get('rc'), 'ready': r, 'resumed_after_s': round(time.time() - t_r, 1), 'plans': sh['plans']}
                log('planning pause 1', p1)
            elif sh['planning'] and p1 is not None and p2 is None and sh['plans'] > p1['plans']:
                t_p = time.time(); wait('planning ended by the host clock', lambda: not ps(H)['planning'], timeout=30, every=0.3)
                p2 = {'lasted_s': round(time.time() - t_p, 1), 'plans': sh['plans']}
                log('planning pause 2', p2)
            if sh['phase'] == 0:
                rounds.append({'resetting': 2}); log('match over seen'); break
            if sh['round'] > seen_round:
                wait('next round on both', lambda: both(lambda x: (lambda q: q['round'] == sh['round'] and not q['resetting'])(ps(x))), timeout=600, every=1)
                q = {x: ps(x) for x in (H, G)}
                rounds.append({'resetting': 1, 'round': seen_round, 'score': q[H]['score'], 'draws': q[H]['draws'],
                               'guest_same': (q[G]['round'], q[G]['score'], q[G]['draws']) == (q[H]['round'], q[H]['score'], q[H]['draws']),
                               'next': {x: (q[x]['round'], q[x]['ticks'], [u.get('out_tick') for u in q[x]['units']]) for x in (H, G)}})
                log('round reset', rounds[-1]); shot(H, f"H_round{q[H]['round']}_start"); shot(G, f"G_round{q[H]['round']}_start")
                seen_round = sh['round']
            time.sleep(1)
        check('P1 planning pause: world frozen on both, an order during it accepted, Ready from both ends it at once; the host clock ends the next one',
              bool(p1 and p1['frozen'] and p1['order_rc'] == 0 and p1['resumed_after_s'] < 3 and p2 and 4 <= p2['lasted_s'] <= 12), f'p1 {p1} p2 {p2}')
        r_ok = [r for r in rounds if r['resetting'] == 1]
        import re
        def ends(x):
            out = []
            for line in open(os.path.join(rig.idir(x), 'rig.log')):
                m = re.search(r'pvp: round (\d+) over at match tick (\d+): winner team (\d)', line)
                if m and line[:8] >= T_START:
                    out.append(tuple(int(v) for v in m.groups()))
            return out
        ends_h, ends_g = ends(H), ends(G); logs_same = ends_h == ends_g and len(ends_h) >= 2
        check('R1 round end on both copies; reload of the arena snapshot; next round starts clean on both (round+1, units back in, same PvP hash)',
              bool(r_ok) and all(r['guest_same'] and r['next'][H][0] == r['round'] + 1 and all(o == -1 for o in r['next'][H][2] + r['next'][G][2]) for r in r_ok)
              and logs_same, f'{len(r_ok)} round resets; round ends H {ends_h} G {ends_g}')
        # R2: match over -> world snapshot reloaded, PvP off, world as before the match
        m_over = bool(rounds) and rounds[-1]['resetting'] == 2
        if m_over:
            wait('PvP off and back in the world on both', lambda: both(lambda x: (lambda q, s: q['phase'] == 0 and s.get('floor') == pre[H][0].get('floor') and not s.get('busy') and s.get('det', {}).get('locked'))(ps(x), st(x))), timeout=600, every=2)
            time.sleep(3)
        post = {x: (st(x), c(x, cmd='pvp_arena_info'), {u['guid']: round(u['hp'], 1) for u in c(x, cmd='list_units')['units'] if u['faction'] == 1}, ps(x)) for x in (H, G)}
        log('after the match: floor', post[H][0].get('floor'), 'monsters', post[H][1]['monsters_alive'], 'player hp', post[H][2], 'score', post[H][3]['score'], 'winner', post[H][3]['match_winner'])
        hp_same = all(abs(post[x][2].get(g, -1) - pre[x][2][g]) <= max(2.0, 0.02 * pre[x][2][g]) for x in (H, G) for g in pre[x][2])
        check('R2 match over: every peer back in the world from the pre-match snapshot (same floor, monsters back, player HP as before, PvP off, same score on both)',
              bool(m_over) and all(post[x][3]['phase'] == 0 and post[x][0].get('floor') == pre[x][0].get('floor') and post[x][1]['monsters_alive'] == pre[x][1]['monsters_alive'] for x in (H, G))
              and hp_same and post[H][3]['score'] == post[G][3]['score'] and post[H][3]['match_winner'] == post[G][3]['match_winner'],
              f"rounds {[(r.get('round'), r.get('score'), r.get('draws')) for r in rounds]}, monsters {pre[H][1]['monsters_alive']} -> {post[H][1]['monsters_alive']}/{post[G][1]['monsters_alive']}, hp same {hp_same}")
        shot(H, 'H_world_after'); shot(G, 'G_world_after')
        time.sleep(30)                                       # play on in the world for 30 s
        c(H, cmd='pvp_freeze', at=st(H)['tick'] + 120)
        wait('both frozen', lambda: both(lambda x: ps(x).get('frozen')), timeout=120)
        mh, mg = st(H)['mp'], st(G)['mp']
        t_last = max(post[H][0]['tick'], post[G][0]['tick'])
        fh, fg = fp_read(H, t_last), fp_read(G, t_last)
        com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
        check('D no desync in the whole match (every reload included); fingerprints equal after the match; pause rules back',
              mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0 and first is None and len(com) > 600 and ps(H)['hash'] == ps(G)['hash'],
              f'desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, loads H {mh.get("loads")} G {mg.get("loads")}, {len(com)} common ticks after, first diff {first}')
        json.dump({'results': results, 'rounds': rounds, 'p1': p1, 'p2': p2, 'pre_hp': pre[H][2], 'post_hp': post[H][2]}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1, default=str)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, 'pvp')   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
