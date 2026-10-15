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
sys.path.insert(0, os.path.join(ROOT, 'research', 'ux_shots'))
import ux                                     # hidden window at a real resolution; frames from the back buffer

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('bring_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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




def ushot(x, name):
    c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'ui_{name}.png'))



FIX = os.path.join(ROOT, 'ext', 'foreign_expedition', 'fixtures')
HOST_SAVE = os.path.join(FIX, 'hub_Moonlight_mp.json')     # the host's world: the Moonlight settlement (1 unit)
HOME = os.path.join(FIX, 'home_Elven.json')                # each player's own save (8 units at home)
H_UNITS = ['477ad25a-7c22-43f5-be28-db055895297e', '137a4264-054d-4b21-81f2-a69d349a3bd0']   # Ivan, Silvarion
G_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', 'acd20405-56c9-4360-80f6-b4c84e1a9ba4']   # Leo, Leonis



def main():
    """Usable PvP with own units, through the windows: the guest claims the world's own unit first; each player opens
    Esc > PvP Match > Bring Units..., picks his save and 2 units, Bring; the host presses Start Match. Checks: the windows,
    the brought units in the match, story events held, units go down and never die, the old world back with its unit
    owned by the guest again, no desync. Hidden windows 1512x982, frames from the back buffer."""
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
    pv = lambda x, **k: c(x, cmd='mpm_pvp', **k)
    us_all = lambda x: [u for u in c(x, cmd='list_units')['units'] if u['faction'] == 1]
    try:
        for x in (H, G):                                     # "your saves": the Elven save in each copy's own Saves folder
            d = os.path.join(rig.idir(x), 'data', 'Saves'); os.makedirs(d, exist_ok=True)
            shutil.copy2(HOME, os.path.join(d, 'Elven_home.json')); os.utime(os.path.join(d, 'Elven_home.json'))
        ux.LEASED = pair; ux.start(H, 1512, 982)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'host_world.json'); shutil.copy2(HOST_SAVE, src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy'), 'H loaded', timeout=900)   # a loaded Mac: the host can take minutes
        time.sleep(5)
        ux.start(G, 1512, 982)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
        wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        old = [u['guid'] for u in us_all(H) if not u['dead']]
        log('G claims the world unit', old, [c(G, cmd='mp_claim', guid=g) for g in old]); time.sleep(2)
        own_before = {g: c(H, cmd='mp_owner', guid=g).get('owner') for g in old}
        log('owners before', own_before)
        picks = {H: ('Ivan', 'Silvarion'), G: ('Leo', 'Leonis')}
        for x in (H, G):
            c(x, cmd='mpm_esc'); time.sleep(2); pv(x, action='press_esc'); time.sleep(2)
            pv(x, action='bring'); time.sleep(2)
            w = pv(x); saves = w['bring']['saves']; i = saves.index('Elven_home') if 'Elven_home' in saves else -1
            log(x, 'saves', saves)
            pv(x, action='br_save', i=i); time.sleep(3)
            w = pv(x); units = w['bring']['units']; log(x, 'units', [(u[0], u[2]) for u in units])
            for k, u in enumerate(units):
                if u[0] in picks[x]:
                    pv(x, action='br_unit', i=k); time.sleep(0.5)
            c(x, cmd='mpm_tip', title='Unit'); time.sleep(1.2); tip = pv(x)['tip']; c(x, cmd='mpm_tip', title='')
            ushot(x, 'bring_window')
            pv(x, action='br_send'); time.sleep(3)
            w = pv(x); log(x, 'bring status', w['bring']['status'], 'tip', tip)
            check(f'B1 {x}: Bring Units window lists the save and its units, 2 picked and brought', i >= 0 and 'Brought' in w['bring']['status'] and tip == 'Unit', w['bring']['status'])
            pv(x, action='br_back'); time.sleep(1)
        r = wait('host has 4 units', lambda: (lambda q: q if q['received'] == 4 else None)(c(H, cmd='pvp_units_status')), timeout=60, every=1)
        time.sleep(3)
        cols = {}
        for x in (H, G):                                     # the PvP window (open after Back) lists the units that will fight
            cols[x] = sorted(pv(x).get('columns', [])); ushot(x, 'pvp_window_brought')
        log('team columns', cols)
        check('B6 the PvP window lists the brought units (the ones that fight) on both copies, not the world unit',
              all(cols[x] == sorted(picks[H] + picks[G]) for x in (H, G)), str(cols))
        pv(H, action='arrow', row=1, dir=-1); time.sleep(0.5)                                  # best of 1 (3:00 rounds)
        log('Start Match', pv(H, action='press_start'))
        s = wait('match on both', lambda: (lambda a, b: a if a['active'] and b['active'] else None)(ps(H), ps(G)), timeout=900, every=3)
        names = sorted(u['name'] for u in us_all(H) if not u['dead'])
        log('units in the match', names, [(u['team'], u['player']) for u in s['units']])
        check('B2 the match runs with the 4 brought units (2 per team)', names == sorted(['Ivan', 'Silvarion', 'Leo', 'Leonis']) and sorted(u['team'] for u in s['units']) == [1, 1, 2, 2], str(names))
        ai = c(H, cmd='pvp_arena_info')
        check('B3 story events held during PvP (hold on)', ai['hold'] == 1 and c(G, cmd='pvp_arena_info')['hold'] == 1, json.dumps(ai['cuts']))
        # down-not-die is checked here when a unit goes out before the match ends (also in pvp_ctf_drop.py F7)
        outs = []; t_o = time.time()
        while time.time() - t_o < 600:
            q = ps(H)
            if q['phase'] == 0: break
            outs = [u['guid'] for u in q['units'] if u.get('out_tick', -1) >= 0]
            if outs: break
            time.sleep(0.5)
        if outs:                                             # read at once: with best of 1 the match may end seconds later
            lu = {u['guid']: u for u in c(H, cmd='list_units')['units']}
            ushot(H, 'downed')
            check('B4 out units are down, not dead (no item drop)', all(g in lu and not lu[g]['dead'] and lu[g].get('downed') for g in outs),
                  f"{[(lu[g]['name'], lu[g]['dead'], lu[g].get('downed')) if g in lu else (g, 'not on the map') for g in outs]}")
        else:
            check('B4 out units are down, not dead (no item drop)', False, 'no unit went out within 10 minutes')
        wait('match over and the old world back', lambda: both(lambda x: ps(x)['phase'] == 0 and ps(x)['matches'] >= 1 and st(x).get('map') == 0 and not st(x).get('busy') and st(x).get('det', {}).get('locked')), timeout=900, every=3)
        time.sleep(8)
        back = sorted(u['guid'] for u in us_all(H) if not u['dead'])
        own_back = {g: c(H, cmd='mp_owner', guid=g).get('owner') for g in back}
        log('after', back, own_back)
        ushot(H, 'world_back'); ushot(G, 'world_back_G')
        check('B5 old world back on both, its unit owned by the guest again (owner table restored), hold off',
              back == sorted(old) == sorted(u['guid'] for u in us_all(G) if not u['dead']) and own_back == own_before and own_before.get(old[0]) == 1
              and c(H, cmd='pvp_arena_info')['hold'] == 0, f'{back} owners {own_back} (before {own_before})')
        mh, mg = st(H)['mp'], st(G)['mp']
        check('D no desync', mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0, f"loads G {mg.get('loads')}")
        json.dump({'results': results}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, 'pvp')   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
