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
TAG = arg('--tag', time.strftime('ui_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
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


def main():
    """M4 UI: Esc entry, PvP window (host + guest), Start from the window (auto START on arrival), countdown + messages,
    HUD (boss bar copy), planning Ready button, result window. Frames: ui_*.png (overlay canvases)."""
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
    pv = lambda x, **k: c(x, cmd='mpm_pvp', **k)
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
        for x in (H, G):
            c(x, cmd='mpm_esc')
        time.sleep(3)
        e = {x: pv(x) for x in (H, G)}
        log('esc entry', e)
        ushot(H, 'H_esc'); 
        check('U1 Esc menu has "PvP Match" on both', all(e[x]['esc_entry'] for x in (H, G)))
        r = {x: pv(x, action='press_esc') for x in (H, G)}; time.sleep(2)
        w = {x: pv(x) for x in (H, G)}
        log('window', w)
        ushot(H, 'H_window'); ushot(G, 'G_window_guest')
        check('U2 PvP window opens from the Esc entry (real click path) on host and guest', all(w[x]['window'] for x in (H, G)))
        # host rules: best of 1, 1:00 rounds, planning pause on
        pv(H, action='arrow', row=1, dir=-1); time.sleep(0.5)
        pv(H, action='arrow', row=2, dir=-1); time.sleep(0.5); pv(H, action='arrow', row=2, dir=-1); time.sleep(0.5)
        pv(H, action='arrow', row=3, dir=1); time.sleep(1)
        w2 = pv(H); log('rules', w2)
        c(H, cmd='mpm_tip', title='Round time'); time.sleep(1.5); t1 = pv(H); c(H, cmd='mpm_tip', title='')
        ushot(H, 'H_window_rules')
        check('U3 rules arrows work; the game-format tooltip shows on a rules row', w2['bo'] == 1 and w2['secs'] == 60 and w2['pause'] == 1 and t1['tip'] == 'Round time', f"bo {w2['bo']} secs {w2['secs']} pause {w2['pause']} tip '{t1['tip']}'")
        pv(G, action='press_close'); time.sleep(1)
        log('start', pv(H, action='press_start'))
        wait('match on both', lambda: both(lambda x: ps(x)['active']), timeout=600, every=2)
        q = {x: pv(x) for x in (H, G)}
        time.sleep(3); ushot(H, 'H_hud_placing'); ushot(G, 'G_hud_placing')
        check('U4 Start Match from the window: arena + START by itself; HUD (boss bar copy) on both', all(ps(x)['active'] for x in (H, G)) and all(pv(x)['hud'] for x in (H, G)))
        # planning pause: Ready button on both; press it
        sp = wait('a planning pause', lambda: (lambda s: s if s['planning'] else None)(ps(H)), timeout=300, every=0.5)
        time.sleep(0.5)
        rv = {x: pv(x) for x in (H, G)}
        ushot(H, 'H_planning'); ushot(G, 'G_planning')
        rr = {x: pv(x, action='press_ready') for x in (H, G)}; log('ready', rr)
        t_r = time.time(); wait('planning over', lambda: not ps(H)['planning'], timeout=20, every=0.3)
        check('U5 planning pause: Ready button shown on both; pressing it ends the pause', all(rv[x]['ready'] for x in (H, G)) and time.time() - t_r < 4, f"ready shown {[rv[x]['ready'] for x in (H, G)]}")
        time.sleep(2); ushot(H, 'H_fight'); ushot(G, 'G_fight')
        wait('match over and back in the world', lambda: both(lambda x: ps(x)['phase'] == 0 and ps(x)['matches'] == 1 and st(x).get('floor') == 0 and not st(x).get('busy')), timeout=900, every=3)
        res = wait('result window on both', lambda: (lambda a, b: (a, b) if a['result'] and b['result'] else None)(pv(H), pv(G)), timeout=120, every=1)
        time.sleep(1); ushot(H, 'H_result'); ushot(G, 'G_result')
        check('U6 result window on both after the world came back', bool(res))
        ok = {x: pv(x, action='press_ok') for x in (H, G)}; time.sleep(2)
        check('U7 Back to the World closes it', not pv(H)['result'] and not pv(G)['result'])
        import re
        def msgs(x):
            out = []
            for line in open(os.path.join(rig.idir(x), 'rig.log')):
                m = re.search(r'pvp: message "(.*)"', line)
                if m and line[:8] >= t_start:
                    out.append(m.group(1))
            return out
        mh, mg = msgs(H), msgs(G)
        log('messages H', mh); log('messages G', mg)
        check('U8 the same centred messages on both (countdown, fight, round / match end)', mh == mg and any(m == '3' for m in mh) and any('wins the match' in m or 'draw' in m.lower() for m in mh), f'{len(mh)} messages')
        mh2, mg2 = st(H)['mp'], st(G)['mp']
        check('D no desync', mh2.get('desyncs', 0) == 0 and mg2.get('desyncs', 0) == 0)
        json.dump({'results': results, 'messages': mh}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, LANE)   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
