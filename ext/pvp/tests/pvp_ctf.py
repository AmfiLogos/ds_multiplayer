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
LANE = f'pvp-{os.getpid()}'                 # unique per run: two runs of one test never share a pair

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('ctf_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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
        p = rig.lease(LANE, os.getpid())
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
    """M6 CTF: best of 1 capture. Fight by the kicks until one team is fully out (or 90 s), then the side with units left
    sends them to the enemy flag (take), then home (capture). Checks: homes + banners on both, take / capture events at the
    same match ticks on both, match over -> world back, no desync."""
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'out', OUT)
    t_start = time.strftime('%H:%M:%S')
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
        pre_floor = st(H).get('floor'); pre_mon = c(H, cmd='pvp_arena_info')['monsters_alive']
        log('setup', c(H, cmd='pvp_setup', teams='1,2', mode=2, best_of=1, round_secs=600, pause=0))
        time.sleep(1)
        log('arena', c(H, cmd='pvp_arena', region=1, floor=FLOOR, seed=SEED))
        wait('both on the arena floor', lambda: both(lambda x: (lambda s: s.get('floor') == FLOOR and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(st(x))), timeout=600)
        time.sleep(3)
        log('start', c(H, cmd='pvp_start'))
        wait('fight on both', lambda: both(lambda x: (lambda q: q['fight_start'] > 0 and q['ctf']['homes'][0] != [0, 0])(ps(x))), timeout=300)
        time.sleep(2)
        q = {x: ps(x) for x in (H, G)}
        log('ctf', q[H]['ctf'], q[G]['ctf'])
        check('C1 CTF homes the same on both, flags home, banner stands placed on both',
              q[H]['ctf']['homes'] == q[G]['ctf']['homes'] and all(q[x]['ctf']['banners'] == 2 and [f[0] for f in q[x]['ctf']['flags']] == [0, 0] for x in (H, G)),
              f"homes {q[H]['ctf']['homes']}, banners {q[H]['ctf']['banners']}/{q[G]['ctf']['banners']}")
        shot(H, 'H_ctf_start'); shot(G, 'G_ctf_start')
        # fight until one team has no unit in (or 90 s); elimination does not end a CTF round
        t0 = time.time(); side = None
        while time.time() - t0 < 240:
            s = ps(H); ins = {t: [u for u in s['units'] if u['team'] == t and u.get('out_tick', -1) < 0] for t in (1, 2)}
            if not ins[1] or not ins[2] or time.time() - t0 > 90:
                side = 1 if len(ins[1]) >= len(ins[2]) else 2; break
            time.sleep(2)
        s = ps(H)
        check('C2 elimination does not end a CTF round', not s['resetting'] and s['phase'] == 2, f"units in {[(u['team'], u.get('out_tick')) for u in s['units']]}")
        runners = [u for u in s['units'] if u['team'] == side and u.get('out_tick', -1) < 0]
        who = {1: H, 2: G}[side]; enemy_home = s['ctf']['homes'][2 - side]; own_home = s['ctf']['homes'][side - 1]
        log('team', side, 'goes for the flag with', [u['guid'][:8] for u in runners], 'enemy home', enemy_home)
        taken = None; cap = None; t1 = time.time(); last_order = 0
        while time.time() - t1 < 400:
            s = ps(H); f = s['ctf']['flags'][2 - side]
            runners = [u for u in s['units'] if u['team'] == side and u.get('out_tick', -1) < 0]
            if s['phase'] == 0 or s['resetting']:
                cap = s; break
            if time.time() - last_order > 6:
                tgt = own_home if f[0] == 1 else (f[2], f[3])
                for u in runners:
                    c(who, cmd='mp_move', guid=u['guid'], x=tgt[0], y=tgt[1])
                last_order = time.time()
            if f[0] == 1 and not taken:
                taken = (s['ticks'], f); log('flag taken', taken); shot(H, 'H_flag_taken'); last_order = 0
            time.sleep(1)
        def evs(x):
            import re
            out = []
            for line in open(os.path.join(rig.idir(x), 'rig.log')):
                m = re.search(r'pvp: ctf (\w+) flag of team (\d) at match tick (\d+)', line)
                if m and line[:8] >= t_start:
                    out.append((m.group(1), int(m.group(2)), int(m.group(3))))
            return out
        eh, eg = evs(H), evs(G)
        log('events H', eh); log('events G', eg)
        check('C3 flag taken and captured; the same CTF events at the same match ticks on both copies',
              bool(taken) and any(e[0] == 'taken' for e in eh) and any(e[0] == 'CAPTURED' for e in eh) and eh == eg, f'{len(eh)} events')
        wait('PvP off and back in the world on both', lambda: both(lambda x: (lambda q2, s2: q2['phase'] == 0 and s2.get('floor') == pre_floor and not s2.get('busy') and s2.get('det', {}).get('locked'))(ps(x), st(x))), timeout=600, every=2)
        time.sleep(3)
        post = {x: (ps(x), c(x, cmd='pvp_arena_info')) for x in (H, G)}
        check('C4 capture won the match (best of 1); back in the world on both (floor, monsters as before); same score',
              all(post[x][0]['score'][side - 1] == 1 and post[x][0]['match_winner'] == side and post[x][1]['monsters_alive'] == pre_mon for x in (H, G)),
              f"score {post[H][0]['score']}/{post[G][0]['score']}, winner {post[H][0]['match_winner']}, monsters {pre_mon} -> {post[H][1]['monsters_alive']}")
        mh, mg = st(H)['mp'], st(G)['mp']
        check('D no desync', mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0, f"desyncs {mh.get('desyncs')}/{mg.get('desyncs')}, loads G {mg.get('loads')}")
        json.dump({'results': results, 'events': eh, 'taken': taken}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1, default=str)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, LANE)   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
