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
sys.path.insert(0, os.path.join(ROOT, 'research', 'ux_shots'))
import ux                                     # hidden window at a real resolution; frames from the back buffer

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('own_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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




def ushot(x, name):
    c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'ui_{name}.png'))



FIX = os.path.join(ROOT, 'ext', 'foreign_expedition', 'fixtures')
HOST_SAVE = os.path.join(FIX, 'hub_Moonlight_mp.json')     # the host's world: the Moonlight settlement (1 unit)
HOME = os.path.join(FIX, 'home_Elven.json')                # each player's own save (8 units at home)
H_UNITS = ['477ad25a-7c22-43f5-be28-db055895297e', '137a4264-054d-4b21-81f2-a69d349a3bd0']   # Ivan, Silvarion
G_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', 'acd20405-56c9-4360-80f6-b4c84e1a9ba4']   # Leo, Leonis


def main():
    """M3c own units: host and guest each bring 2 units from a save ON THEIR OWN COPY (fixtures copied into each instance),
    the host starts: the world becomes the settlement with the brought units (owned by who brought them), then a
    best-of-1 Fight on the arena, then the world before the units came back (with its own unit and owner table)."""
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'out', OUT)
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    both = lambda f: all(f(x) for x in (H, G))
    ps = lambda x: c(x, cmd='pvp_status')
    us_base = lambda x: [u for u in c(x, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead']]
    try:
        ux.LEASED = pair; ux.start(H, 1512, 982)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500)
        work = {}
        for x in (H, G):
            work[x] = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work[x], exist_ok=True)
            shutil.copy2(HOME, os.path.join(work[x], 'my_home.json'))
        src = os.path.join(work[H], 'host_world.json'); shutil.copy2(HOST_SAVE, src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy'), 'H loaded', timeout=900)   # a loaded Mac: the host can take minutes
        time.sleep(5)
        ux.start(G, 1512, 982)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
        wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        before = {x: sorted(u['guid'] for u in us_base(x)) for x in (H, G)}
        own_before = {g: c(H, cmd='mp_owner', guid=g).get('owner') for g in before[H]}
        log('world units before', before[H], 'owners', own_before, 'map', st(H).get('map'))
        log('H bring', c(H, cmd='pvp_units_bring', file=os.path.join(work[H], 'my_home.json'), guids=','.join(H_UNITS)))
        log('G bring', c(G, cmd='pvp_units_bring', file=os.path.join(work[G], 'my_home.json'), guids=','.join(G_UNITS)))
        r = wait('host has 4 units', lambda: (lambda q: q if q['received'] == 4 else None)(c(H, cmd='pvp_units_status')), timeout=60, every=1)
        log('received', r)
        l0 = st(G)['mp'].get('loads', 0)
        log('units start', c(H, cmd='pvp_units_start'))
        wait('guest reloaded the new world', lambda: (lambda s: s['mp'].get('loads', 0) > l0 and s.get('det', {}).get('locked') and not s.get('busy'))(st(G)), timeout=600)
        time.sleep(5)
        after = {x: us_base(x) for x in (H, G)}
        names = {x: sorted(u['name'] for u in after[x]) for x in (H, G)}
        owners = {u['name']: c(H, cmd='mp_owner', guid=u['guid']).get('owner') for u in after[H]}
        log('units now', names, 'owners', owners, 'status', c(H, cmd='pvp_units_status'))
        ushot(H, 'H_units_in'); ushot(G, 'G_units_in')
        check('O1 the world has the 4 brought units on both copies (and not the old one); each owned by who brought it',
              names[H] == names[G] == sorted(['Ivan', 'Silvarion', 'Leo', 'Leonis']) and owners.get('Ivan') == 0 and owners.get('Silvarion') == 0 and owners.get('Leo') == 1 and owners.get('Leonis') == 1,
              f'names {names[H]} / {names[G]}, owners {owners}')
        # the match: SETUP, arena, START, best of 1
        log('setup', c(H, cmd='pvp_setup', teams='1,2', best_of=1, round_secs=120, pause=0))
        time.sleep(1)
        log('arena', c(H, cmd='pvp_arena', region=1, floor=FLOOR, seed=SEED))
        wait('both on the arena floor', lambda: both(lambda x: (lambda s: s.get('floor') == FLOOR and s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(st(x))), timeout=600)
        time.sleep(3)
        log('start', c(H, cmd='pvp_start'))
        s = wait('match on', lambda: (lambda q: q if q['active'] else None)(ps(H)), timeout=60)
        check('O2 the PvP table holds the brought units, 2 per team', len(s['units']) == 4 and sorted(u['team'] for u in s['units']) == [1, 1, 2, 2], str([(u['team'], u['player']) for u in s['units']]))
        time.sleep(10); ushot(H, 'H_fight'); ushot(G, 'G_fight')
        wait('match over and the old world back', lambda: both(lambda x: ps(x)['phase'] == 0 and ps(x)['matches'] >= 1 and st(x).get('map') == 0 and not st(x).get('busy') and st(x).get('det', {}).get('locked')), timeout=900, every=3)
        time.sleep(8)
        back = {x: sorted(u['guid'] for u in us_base(x)) for x in (H, G)}
        own_back = {g: c(H, cmd='mp_owner', guid=g).get('owner') for g in back[H]}
        log('world units after', back, 'owners', own_back, 'status', c(H, cmd='pvp_units_status'))
        ushot(H, 'H_world_back')
        check('O3 match over: the world before the units came is back on both (its own unit, none of the brought ones), owner table as before',
              back[H] == back[G] == before[H] and own_back == own_before, f'{back[H]} vs {before[H]}, owners {own_back} vs {own_before}')
        mh, mg = st(H)['mp'], st(G)['mp']
        check('D no desync', mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0, f"loads G {mg.get('loads')}")
        json.dump({'results': results}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, LANE)   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
