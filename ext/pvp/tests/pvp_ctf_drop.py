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
TAG = arg('--tag', time.strftime('ctfdrop_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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



def seq_in(xs, want):                    # want appears in order, back to back
    return any(xs[i:i + len(want)] == want for i in range(len(xs)))


def main():
    """CTF in the game: banner stands at both homes (frames), a carried flag (frame), drop (the carrier goes down) and return
    (a teammate of the flag touches it). Automatic attack orders off (pvp_cfg kicks 0) so the script decides who fights.
    Hidden windows 1512x982, frames from the back buffer."""
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
        ux.LEASED = pair; ux.start(H, 1512, 982)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy'), 'H loaded', timeout=900)   # a loaded Mac: the host can take minutes
        time.sleep(5)
        ux.start(G, 1512, 982)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
        wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        us = c(H, cmd='list_units')['units']
        players = sorted([u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u], key=lambda u: u['guid'])
        for u in players[:2]:
            c(H, cmd='mp_claim', guid=u['guid'])
        for u in players[2:4]:
            c(G, cmd='mp_claim', guid=u['guid'])
        time.sleep(2)
        log('setup', c(H, cmd='pvp_setup', teams='1,2', mode=2, best_of=1, round_secs=900, pause=0))
        time.sleep(1)
        log('arena', c(H, cmd='pvp_arena', region=1, floor=FLOOR, seed=SEED))
        wait('both on the arena floor', lambda: both(lambda x: (lambda s: s.get('floor') == FLOOR and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(st(x))), timeout=600)
        time.sleep(3)
        for x in (H, G):
            log(x, 'cfg', c(x, cmd='pvp_cfg', kicks=0))
        log('start', c(H, cmd='pvp_start'))
        s = wait('fight + homes', lambda: (lambda q: q if q['fight_start'] > 0 and q['ctf']['homes'][0] != [0, 0] else None)(ps(H)), timeout=300)
        g_banners0 = ps(G)['ctf']['banners']; homes = s['ctf']['homes']; log('homes', homes, 'banners', s['ctf']['banners'], 'banner cells', c(H, cmd='pvp_arena_info')['banner_cells'])
        for k, hm in (('blue', homes[0]), ('red', homes[1])):
            for x in (H, G):
                c(x, cmd='pvp_cfg', cam_x=hm[0] + 0.5, cam_y=hm[1] + 0.5, cam_size=7)
            time.sleep(1.5); ushot(H, f'H_home_{k}')
        blue = [u for u in s['units'] if u['team'] == 1]; red = [u for u in s['units'] if u['team'] == 2]
        mid = [(homes[0][0] + homes[1][0]) // 2, (homes[0][1] + homes[1][1]) // 2]
        cr = blue[0]                                                  # the runner (carrier)
        # Red waits in the middle; the runner walks to Red's flag and takes it
        for u in red:
            c(G, cmd='mp_move', guid=u['guid'], x=mid[0], y=mid[1])
        c(H, cmd='mp_move', guid=blue[1]['guid'], x=homes[0][0], y=homes[0][1])
        taken = None; t0 = time.time()
        while time.time() - t0 < 180 and not taken:
            q = ps(H); f = q['ctf']['flags'][1]
            if f[0] == 1:
                taken = (q['ticks'], f); break
            c(H, cmd='mp_move', guid=cr['guid'], x=homes[1][0], y=homes[1][1]); time.sleep(4)
        log('taken', taken)
        if taken:
            for x in (H, G):
                c(x, cmd='pvp_cfg', cam_x=taken[1][2] + 0.5, cam_y=taken[1][3] + 0.5, cam_size=7)
            time.sleep(1.5); ushot(H, 'H_carried'); ushot(G, 'G_carried')
        # the carrier walks into Red; both Red units attack it until it is out -> the flag drops
        dropped = None; t1 = time.time(); marker_ok = carrier_down = False; mk2 = {}
        while taken and time.time() - t1 < 480:
            q = ps(H); f = q['ctf']['flags'][1]
            if f[0] == 2:
                dropped = (q['ticks'], f); break
            if f[0] == 0:
                break
            cu = [u for u in q['units'] if u['guid'] == cr['guid']][0]
            c(H, cmd='mp_move', guid=cr['guid'], x=mid[0], y=mid[1])
            for u in red:
                c(G, cmd='ord_run_attack', unit=u['guid'], target=cr['guid'], x=f[2], y=f[3], mp=1)
            time.sleep(5)
        log('dropped', dropped)
        returned = None
        if dropped:
            for x in (H, G):
                c(x, cmd='pvp_cfg', cam_x=dropped[1][2] + 0.5, cam_y=dropped[1][3] + 0.5, cam_size=7)
            time.sleep(1.5); ushot(H, 'H_dropped')
            mk = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
            cu = {u['guid']: u for u in c(H, cmd='list_units')['units']}
            marker_ok = all(mk[x]['cuts']['markers'] == 1 and [dropped[1][2], dropped[1][3], 0] in mk[x]['banner_cells'] for x in (H, G))
            carrier_down = not cu[cr['guid']]['dead']
            log('marker', mk[H]['banner_cells'], mk[H]['cuts'], 'carrier dead', cu[cr['guid']]['dead'], 'downed', cu[cr['guid']].get('downed'))
            t2 = time.time()
            # the flag cell holds the marker stand (a building) and the down carrier: walk to a free cell beside it
            nb = [(1, 0), (0, 1), (-1, 0), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)]; k = 0
            while time.time() - t2 < 120:
                q = ps(H); f = q['ctf']['flags'][1]
                if f[0] == 0:
                    returned = (q['ticks'], f); break
                for u in [u for u in q['units'] if u['team'] == 2 and u.get('out_tick', -1) < 0]:
                    c(G, cmd='mp_move', guid=u['guid'], x=f[2] + nb[k % 8][0], y=f[3] + nb[k % 8][1])
                k += 1; time.sleep(4)
        log('returned', returned)
        if returned:
            time.sleep(2)
            mk2 = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
            log('after return', mk2[H]['banner_cells'], mk2[H]['cuts'])
            for x in (H, G):
                c(x, cmd='pvp_cfg', cam_x=dropped[1][2] + 0.5, cam_y=dropped[1][3] + 0.5, cam_size=7)
            time.sleep(1.5); ushot(H, 'H_returned')
        import re
        def evs(x):
            out = []
            for line in open(os.path.join(rig.idir(x), 'rig.log')):
                m = re.search(r'pvp: ctf (\w+) flag of team (\d) at match tick (\d+)', line)
                if m and line[:8] >= t_start:
                    out.append((m.group(1), int(m.group(2)), int(m.group(3))))
            return out
        eh, eg = evs(H), evs(G)
        log('events H', eh); log('events G', eg)
        check('F1 banner stands at both homes on both copies (frames ui_H_home_*)', s['ctf']['banners'] == 2 and g_banners0 == 2)
        check('F2 flag taken; the carrier shows it (frame ui_*_carried)', bool(taken))
        check('F3 drop: the carrier went down and the flag lies there', bool(dropped))
        check('F4 return: a Red unit touched its dropped flag, it went home', bool(returned))
        check('F6 dropped flag: a banner marker on its ground cell on both copies; gone after the return', bool(dropped) and marker_ok and bool(returned) and all(mk2[x]['cuts']['markers'] == 0 for x in (H, G)))
        check('F7 the carrier went down, not dead', bool(dropped) and carrier_down)
        check('F5 the same CTF events at the same match ticks on both copies', eh == eg and seq_in([e[0] for e in eh], ['taken', 'dropped', 'returned']), str(eh))   # an earlier round may hold a capture first (v1_ctfdrop 2026-10-15)
        mh, mg = st(H)['mp'], st(G)['mp']
        check('D no desync', mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0)
        json.dump({'results': results, 'events': eh, 'homes': homes}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1, default=str)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, LANE)   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
