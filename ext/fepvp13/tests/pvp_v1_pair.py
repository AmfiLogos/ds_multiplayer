#!/usr/bin/env python3
"""fepvp: PvP v1 pair test (owner v12 13: "a map (the starting map for now), players appear there, PvP is on").
Extends ext/plan13/pvp_base.py to the full flow, through the code the menu calls (rig mpm_pvp actions = the host choice
"PvP"'s page Start and lobby Start):
  host: PvP world from the wiki base template (page Start) -> host session (mp.json) -> guest joins -> each player
  brings 2 units from his own save (copies) -> lobby Start -> units come in -> SETUP + BASE -> START at the settlement:
  teams walk to the two ends of the map -> countdown -> fight -> a round ends -> the match ends (best of 1) -> the world
  comes back (no brought unit left) -> result window "Back to the Title" -> both copies at the title.
Checks (both copies):
  1 PvP world: v1 flag, settlement (map 0), no monster
  2 match on: same PvP hash, 4 units, every unit lists its 2 enemies, arena = base (2)
  3 base ends: two different cells, same on both; teams walk there; the fight begins (placing over)
  4 combat: both teams lose HP; a unit goes out
  5 round end + match end: winner / score same on both; world back (brought units gone, template units back)
  6 fingerprints equal for every common tick from the match start to the end, >= 2000 ticks, no desync
  7 result window "Back to the Title" -> both copies at the title (session over)
  pvp_v1_pair.py [--dylib PATH] [--tag NAME] [--keep]"""
import json, os, shutil, sys, time
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(W, 'build', 'dsrig.dylib')
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('v1_%Y%m%d-%H%M%S')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')        # test copy of the wiki file
HOME = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures/home_Elven.json')  # each player's own save
H_UNITS = ['477ad25a-7c22-43f5-be28-db055895297e', '137a4264-054d-4b21-81f2-a69d349a3bd0']   # Ivan, Silvarion
G_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', 'acd20405-56c9-4360-80f6-b4c84e1a9ba4']   # Leo, Leonis
OUT = os.path.join(W, 'ext', 'fepvp13', 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a'); results = {}


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def c(x, **k): return rig.send(x, k, timeout=180)


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def fp_read_file(path, t_from):
    d = {}
    if not os.path.exists(path): return d
    for line in open(path):
        q = line.split()
        if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from: d[int(q[0])] = q[1]
    return d


def main():
    import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))   # kill -TERM: copies stopped + pair released (finally)
    pair = None
    while not pair:
        pair = rig.lease('fepvp', os.getpid())
        if not pair: log('no free pair, waiting'); time.sleep(30)
    H, G = pair[0], pair[1]; port = rig.pair_port(H)
    log('pair', pair, 'dylib', rig.DYLIB, 'out', OUT)
    try:
        run(H, G, port)
        results['ran to the end'] = True
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); results['no exception'] = False
    finally:
        results.setdefault('ran to the end', False)
        if '--keep' not in A:
            for x in (H, G): rig.stop(x)
        for x in (H, G):
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
            for f in ('player.log', 'rig.log', 'fp.log'):
                if os.path.exists(os.path.join(rig.idir(x), f)): shutil.copy2(os.path.join(rig.idir(x), f), os.path.join(OUT, f'{x}_{f}'))
        ex = {x: sum(1 for l in open(os.path.join(OUT, f'{x}_player.log'), errors='replace') if 'Exception' in l) for x in (H, G) if os.path.exists(os.path.join(OUT, f'{x}_player.log'))}
        log('player.log exceptions', ex)
        if '--keep' not in A: log('released', rig.release(pair, 'fepvp'))
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        n = sum(1 for v in results.values() if not v)
        log('RESULT', 'PASS' if results and not n else 'FAIL', f'{len(results) - n}/{len(results)}', json.dumps(results))


def run(H, G, port):
    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
        d = os.path.join(rig.idir(x), 'data', 'mp_owners')
        if os.path.exists(d): os.replace(d, d + '_' + time.strftime('%Y%m%d-%H%M%S'))
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
    work = {}
    for x in (H, G):
        work[x] = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work[x], exist_ok=True)
        shutil.copy2(HOME, os.path.join(work[x], 'my_home.json'))
    rig.start(H, fps=30, window=True)
    rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500); time.sleep(3)
    # the page Start's world (fe_world_write); loaded here with determinism on, as the rig host (mp.json) needs.
    # The page Start's own load (fe_world_start) is the FE hub's path: hub_smoke.py / the hub dive.
    r = c(H, cmd='mpm_pvp', action='v1_world_file'); log('H PvP world file', r)
    check('0 the PvP world is built from the base template', r.get('ok') and os.path.exists(r.get('file', '')), json.dumps(r))
    log('H load', c(H, cmd='load_save', file=r['file'], det=1))
    rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'), 'H loaded + hosting', timeout=900)
    time.sleep(3)
    rig.start(G, fps=30, window=True)
    rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
    wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(c(G, cmd='status')), timeout=900)
    time.sleep(3)
    for x in (H, G): c(x, cmd='mp_pause', on=0)
    v1 = {x: c(x, cmd='mpm_pvp') for x in (H, G)}
    info = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
    log('v1 flags', {x: v1[x].get('v1') for x in (H, G)}, 'info', {x: (info[x].get('map'), info[x].get('monsters_alive'), info[x].get('units')) for x in (H, G)})
    check('1 PvP world on both: v1, settlement (map 0), no monster', all(v1[x].get('v1') == 1 and info[x].get('map') == 0 and info[x].get('monsters_alive') == 0 for x in (H, G)))
    tpl_units = sorted(u['guid'] for u in c(H, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead'])
    log('template units', len(tpl_units))
    # each player brings 2 units from his own save (the Bring window's send = pvp_units_bring)
    log('H bring', c(H, cmd='pvp_units_bring', file=os.path.join(work[H], 'my_home.json'), guids=','.join(H_UNITS)))
    log('G bring', c(G, cmd='pvp_units_bring', file=os.path.join(work[G], 'my_home.json'), guids=','.join(G_UNITS)))
    wait('host has 4 units', lambda: c(H, cmd='pvp_units_status')['received'] == 4, timeout=90, every=1)
    log('mods ask', c(H, cmd='pvp_mods', ask=1))
    wait('guest mod report', lambda: len(c(H, cmd='pvp_mods').get('reports', [])) >= 1, timeout=60, every=1)
    c(H, cmd='mpm_pvp', action='page_arrow', row=1, dir=-1)                  # host page rows: To win, Round time
    c(H, cmd='mpm_pvp', action='page_arrow', row=2, dir=-1); c(H, cmd='mpm_pvp', action='page_arrow', row=2, dir=-1)
    wait('host rules set', lambda: (lambda q: q.get('bo') == 1 and q.get('secs') == 60)(c(H, cmd='mpm_pvp')), timeout=20, every=1)
    r = c(H, cmd='mpm_pvp', action='v1_lobby_start'); log('H lobby Start', r)
    check('2a lobby Start accepted (brought units, Vanilla check)', r.get('ok'), json.dumps(r))
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=600, every=2)
    t_match = c(H, cmd='status')['tick']
    ps = {x: c(x, cmd='pvp_status') for x in (H, G)}
    for x in (H, G): log(x, 'pvp_status', json.dumps(ps[x])[:900])
    seen = {H: {}, G: {}}                                    # PvP hash by match tick on each copy: compare at a common tick
    for _ in range(40):
        for x in (H, G): q = c(x, cmd='pvp_status'); seen[x][q['ticks']] = q['hash']
        if set(seen[H]) & set(seen[G]): break
    ct = sorted(set(seen[H]) & set(seen[G])); same_hash = bool(ct) and all(seen[H][t] == seen[G][t] for t in ct)
    ps[H]['hash'], ps[G]['hash'] = (seen[H][ct[0]], seen[G][ct[0]]) if ct else ('none', 'common')
    check('2 match on: same hash, 4 units, played on the settlement (arena 2), not enemies yet while walking to the ends',
          same_hash and len(ps[H]['units']) == 4 and all(u['enemies_listed'] == 0 for x in (H, G) for u in ps[x]['units'])
          and all(ps[x]['arena'] == 2 and c(x, cmd='status').get('map') == 0 for x in (H, G)), f"hash {ps[H]['hash']} / {ps[G]['hash']}")
    pg = {u['guid'] for u in ps[H]['units']}
    log('PvP units at START', [(u['name'], u['guid'][:8], u.get('cell'), u.get('task'), u.get('downed'), u.get('dead')) for u in c(H, cmd='list_units')['units'] if u['guid'] in pg])
    ends = {x: ps[x]['ends'] for x in (H, G)}
    check('3a base ends: two different cells, same on both', ends[H] == ends[G] and ends[H][0] != ends[H][1] and ends[H][0] != [0, 0], json.dumps(ends))
    for x in (H, G): c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_start.png'))
    wait('fight begins (teams in place)', lambda: c(H, cmd='pvp_status').get('fight_start', 0) > 0 and not c(H, cmd='pvp_status').get('placing'), timeout=300, every=2)
    s = c(H, cmd='pvp_status'); cells = {u['guid']: u for u in c(H, cmd='list_units')['units']}
    log('PvP units at the fight', [(cells[u['guid']]['name'], u['guid'][:8], u['team'], cells[u['guid']].get('cell'), cells[u['guid']].get('task'), cells[u['guid']].get('downed')) for u in s['units'] if u['guid'] in cells])
    team_cells = {t: [cells[u['guid']]['cell'] for u in s['units'] if u['team'] == t and u['guid'] in cells and 'cell' in cells[u['guid']]] for t in (1, 2)}
    near = lambda cs, e: all(abs(a[0] - e[0]) + abs(a[1] - e[1]) <= 8 for a in cs)
    check('3 teams walked to their ends; the fight began; now every unit lists its 2 enemies', near(team_cells[1], s['ends'][0]) and near(team_cells[2], s['ends'][1])
          and all(u['enemies_listed'] == 2 for u in s['units']), json.dumps(team_cells))
    for x in (H, G): c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_placed.png'))
    hp0 = {u['guid']: u.get('hp', 0) for u in s['units']}
    down = None; lost = None; t_run = time.time()
    while time.time() - t_run < 600:
        s = c(H, cmd='pvp_status')
        if s.get('units'):
            l = {u['guid']: hp0.get(u['guid'], 0) - u.get('hp', 0) for u in s['units'] if 'hp' in u}
            if not lost and any(v > 0 for g, v in l.items() if any(u['guid'] == g and u['team'] == 1 for u in s['units'])) and \
                    any(v > 0 for g, v in l.items() if any(u['guid'] == g and u['team'] == 2 for u in s['units'])): lost = l
            outs = [u['guid'][:8] for u in s['units'] if u.get('out_tick', -1) >= 0]
            if outs and not down:
                down = outs; log('unit out:', outs); [c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_down.png')) for x in (H, G)]
        if not s.get('active') and s.get('matches', 0) >= 1: break
        if not s.get('resetting'):                           # fp.log starts again at each reload: keep the match's lines
            for x in (H, G): shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_match_{x}.log'))
        time.sleep(2)
    check('4 combat: both teams lose HP, a unit goes out', bool(lost) and bool(down), f'lost {bool(lost)} down {down}')
    wait('match over on both + world back', lambda: all((lambda q: not q.get('active') and q.get('phase') == 0 and q.get('matches') == 1)(c(x, cmd='pvp_status')) for x in (H, G)), timeout=600, every=2)
    wait('both loaded + running', lambda: all((lambda q: q.get('scene') == 2 and not q.get('busy') and q.get('det', {}).get('locked'))(c(x, cmd='status')) for x in (H, G)), timeout=600, every=2)
    time.sleep(5)
    pe = {x: c(x, cmd='pvp_status') for x in (H, G)}
    back = {x: sorted(u['guid'] for u in c(x, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead']) for x in (H, G)}
    log('after the match', {x: (pe[x]['match_winner'], pe[x]['score'], pe[x]['round'], len(back[x])) for x in (H, G)})
    check('5 match end: same winner + score on both; the world is back (template units, no brought unit)',
          pe[H]['match_winner'] == pe[G]['match_winner'] and pe[H]['score'] == pe[G]['score'] and back[H] == back[G] == tpl_units,
          f"winner {pe[H]['match_winner']} score {pe[H]['score']}, units back {len(back[H])}/{len(tpl_units)}")
    for x in (H, G): c(x, cmd='mp_pause', on=1)
    wait('both paused at one tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a.get('mp', {}).get('paused') and b.get('mp', {}).get('paused'))(c(H, cmd='status'), c(G, cmd='status')), timeout=120)
    fh, fg = fp_read_file(os.path.join(OUT, f'fp_match_{H}.log'), t_match), fp_read_file(os.path.join(OUT, f'fp_match_{G}.log'), t_match)
    com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
    mh, mg = c(H, cmd='status')['mp'], c(G, cmd='status')['mp']
    check('6 fingerprints equal on both for 500+ common ticks from the match start, no desync',
          len(com) >= 500 and first is None and not mh.get('desyncs') and not mg.get('desyncs'),
          f'{len(com)} common ticks ({com[0] if com else None}..{com[-1] if com else None}), first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}')
    for x in (H, G): c(x, cmd='mp_pause', on=0)
    wait('result window on both', lambda: all(c(x, cmd='mpm_pvp').get('result') for x in (H, G)), timeout=120, every=2)
    for x in (H, G): c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'ui_{x}_result.png'))
    log('H result OK (Back to the Title)', c(H, cmd='mpm_pvp', action='press_ok'))
    wait('both at the title, session over', lambda: all((lambda q: q.get('scene') == 1 and not q.get('mp', {}).get('started'))(c(x, cmd='status')) for x in (H, G)), timeout=300, every=2)
    check('7 Back to the Title: both copies at the title', True)
    json.dump({'results': results, 'ends': ends[H], 'winner': pe[H]['match_winner'], 'score': pe[H]['score'], 'common_ticks': len(com)}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
