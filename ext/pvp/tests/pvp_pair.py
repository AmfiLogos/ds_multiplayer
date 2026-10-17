#!/usr/bin/env python3
"""PvP M1 pair test (ext/pvp lane): two copies with the PvP dylib (ext/pvp/build/dsrig.dylib), lane "pvp".

  pvp_pair.py [--tag NAME] [--ticks 6000] [--keep]

Host H loads the dungeon save (rig.SAVE, plain stats), guest G joins. Both vote pause, so every setup command lands on
the same world tick. H claims 2 player units, G claims the other 2. Host sends PvP SETUP (slot 0 Blue, slot 1 Red) and
START. Checks, on both copies:
  1. status: match on, every PvP unit has the 2 enemy units in its hostile list (HostileEntityData), none of its teammate
  2. pvp_rel: GetRelation = Hostile (1) across teams, Ally (2) for teammates; IsHostileTo lists both ways
  3. a guest START (pvp_raw from G) is refused on both copies (host only)
  4. by themselves: H moves one Blue unit next to the Red units (a plain move, no attack order); then nothing; units
     enter combat (state 2) and HP drops on both teams
  5. with orders: each side sends an attack order on an enemy unit through the lockstep (ord_run_attack mp=1)
  6. a unit goes down (downed or dead)
  7. fingerprints (fp.log) equal on both copies for every common tick from START, at least 3000 ticks; no desync
Writes det_out-like logs to ext/pvp/out/<tag>/: result.json (outcome summary for repeat-run comparison), fp copies,
frames (screenshots) at START, mid-fight and end."""
import json, os, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig
LANE = f'pvp-{os.getpid()}'                 # unique per run: two runs of one test never share a pair

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('run_%H%M%S')
TICKS = int(A[A.index('--ticks') + 1]) if '--ticks' in A else 6000
FREEZE = 8939400                                             # rig.SAVE starts at world tick 8938508
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
    for i in range(240):                                     # other lanes hold pairs: wait for a free one (max 3 shared)
        p = rig.lease(LANE, os.getpid())
        if p:
            return p
        if i % 10 == 0:
            log('no free pair yet, waiting')
        time.sleep(30)
    raise RuntimeError('no free pair')


def fp_read(x, t_from):
    d = {}
    p = os.path.join(rig.idir(x), 'fp.log')
    for line in open(p):
        q = line.split()
        if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from:
            d[int(q[0])] = q[1]
    return d


def main():
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'dylib', rig.DYLIB, 'out', OUT)
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2, 'auto_resume': False},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'auto_resume': False, 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
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
        # freeze: a stamped host order stops the world at world tick FREEZE on both copies (same tick every run), so every
        # setup command below lands on that tick
        c(H, cmd='mp_pause', on=0); c(G, cmd='mp_pause', on=0)          # auto_resume is off: the join pause needs run votes
        log('freeze order', c(H, cmd='pvp_freeze', at=FREEZE))
        wait('both frozen at FREEZE', lambda: all((lambda q: q.get('frozen') and q.get('world_tick') == FREEZE)(c(x, cmd='pvp_status')) for x in (H, G)), timeout=300)
        t0 = st(H)['tick']; log('frozen at world tick', t0)
        fp_freeze = {x: c(x, cmd='fingerprint') for x in (H, G)}; log('fingerprint at freeze', fp_freeze)
        us = c(H, cmd='list_units')['units']
        players = sorted([u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u], key=lambda u: u['guid'])
        foes = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        log('player units', [(u['name'], u['cell'], round(u['hp'])) for u in players])
        log('closest monster to the party', min((d2(p, f) ** 0.5 for p in players for f in foes), default=None), 'monsters', len(foes))
        blue, red = players[:2], players[2:4]
        for u in blue:
            log('H claim', u['name'], c(H, cmd='mp_claim', guid=u['guid']))
        for u in red:
            log('G claim', u['name'], c(G, cmd='mp_claim', guid=u['guid']))
        log('setup', c(H, cmd='pvp_setup', teams='1,2'))
        time.sleep(1)
        log('guest START (must be refused)', c(G, cmd='pvp_raw', hex='0102'))
        time.sleep(1)
        log('start', c(H, cmd='pvp_start'))
        wait('match on both', lambda: c(H, cmd='pvp_status').get('active') and c(G, cmd='pvp_status').get('active'), timeout=60)
        ps = {x: c(x, cmd='pvp_status') for x in (H, G)}
        for x in (H, G):
            log(x, 'pvp_status', json.dumps(ps[x]))
        check('1 match on, both copies: same PvP hash, every unit lists its 2 enemies',
              all(ps[x]['active'] and ps[x]['ticks'] == 0 for x in (H, G)) and ps[H]['hash'] == ps[G]['hash'] and
              all(u['enemies_listed'] == 2 for x in (H, G) for u in ps[x]['units']) and len(ps[H]['units']) == 4,
              f"hash {ps[H]['hash']} / {ps[G]['hash']}")
        check('3 guest START refused on both (host only)', all(ps[x]['counts']['rejected'] == 1 for x in (H, G)) and ps[H]['matches'] == 1,
              f"rejected H {ps[H]['counts']['rejected']} G {ps[G]['counts']['rejected']}")
        rel = {}
        for x in (H, G):
            rel[x] = (c(x, cmd='pvp_rel', a=blue[0]['guid'], b=red[0]['guid']), c(x, cmd='pvp_rel', a=blue[0]['guid'], b=blue[1]['guid']),
                      c(x, cmd='pvp_rel', a=red[1]['guid'], b=blue[1]['guid']), c(x, cmd='pvp_rel', a=red[0]['guid'], b=red[1]['guid']))
            log(x, 'relations blue0-red0, blue0-blue1, red1-blue1, red0-red1:', rel[x])
        check('2 GetRelation Hostile across teams, Ally in a team; IsHostile + lists both ways',
              all(r[0]['relation'] == 1 and r[0]['is_hostile'] == 1 and r[0]['list_ab'] == 1 and r[0]['list_ba'] == 1 and
                  r[2]['relation'] == 1 and r[2]['list_ab'] == 1 and
                  r[1]['relation'] == 2 and r[1]['is_hostile'] == 0 and r[1]['list_ab'] == 0 and
                  r[3]['relation'] == 2 and r[3]['list_ab'] == 0 for r in rel.values()))
        c(H, cmd='screenshot', path=os.path.join(OUT, 'frame_H_start.png')); c(G, cmd='screenshot', path=os.path.join(OUT, 'frame_G_start.png'))
        # 4 + 5, all sent while paused (same world tick on every run): one plain move of a Blue unit beside the Red pair,
        # and one attack order per side through the lockstep (ord_run_attack mp=1). blue0 and red0 get no attack order.
        tgt = red[0]['cell']
        log('H plain move of', blue[0]['name'], c(H, cmd='mp_move', guid=blue[0]['guid'], x=tgt[0] + 1, y=tgt[1]))
        r5 = (c(H, cmd='ord_run_attack', unit=blue[1]['guid'], target=red[1]['guid'], x=red[1]['cell'][0], y=red[1]['cell'][1], mp=1),
              c(G, cmd='ord_run_attack', unit=red[1]['guid'], target=blue[1]['guid'], x=blue[1]['cell'][0], y=blue[1]['cell'][1], mp=1))
        log('attack orders H, G', r5)
        time.sleep(2)
        log('unfreeze', c(H, cmd='pvp_freeze', at=-1))
        t_start = t0
        hp0 = {u['guid']: u['hp'] for u in players}
        tgt_ok = lambda u, foes: u.get('target') in foes or u.get('task_target') in foes
        bl, rd = {u['guid'] for u in blue}, {u['guid'] for u in red}
        auto = None; t_wait = time.time()
        while time.time() - t_wait < 120:
            s = c(H, cmd='pvp_status'); us = {u['guid']: u for u in s['units']}
            lost = {g: hp0[g] - us[g].get('hp', hp0[g]) for g in us}
            if (any(lost[g] > 0 for g in bl) and any(lost[g] > 0 for g in rd) and us[red[0]['guid']].get('combat') == 2 and
                    tgt_ok(us[red[0]['guid']], bl) and tgt_ok(us[blue[0]['guid']], rd)):
                auto = (s['ticks'], lost); break
            time.sleep(1)
        log('auto fight:', auto)
        sH = c(H, cmd='pvp_status'); log(H, 'units', [(u['guid'][:8], u['team'], u.get('combat'), round(u.get('hp', 0), 1), u.get('task'), u.get('target', '')[:8], u.get('task_target', '')[:8]) for u in sH['units']])
        check('4 by themselves: red0 (no order at all) and blue0 (plain move only) fight an enemy, both teams lose HP', bool(auto),
              f'at match tick {auto[0]}' if auto else 'not within 120 s')
        u5 = {u['guid']: u for u in sH['units']}
        check('5 attack orders accepted through the lockstep; ordered units target the ordered enemy',
              all(r.get('rc') == 0 for r in r5) and tgt_ok(u5[blue[1]['guid']], {red[1]['guid']}) and tgt_ok(u5[red[1]['guid']], {blue[1]['guid']}),
              f"blue1 -> {u5[blue[1]['guid']].get('target')}/{u5[blue[1]['guid']].get('task_target')}, red1 -> {u5[red[1]['guid']].get('target')}/{u5[red[1]['guid']].get('task_target')}")
        c(H, cmd='screenshot', path=os.path.join(OUT, 'frame_H_fight.png')); c(G, cmd='screenshot', path=os.path.join(OUT, 'frame_G_fight.png'))
        # 6: a unit goes down; then 8: the guest resyncs mid-match (the host's join-point save carries the PvP block)
        down = None; resynced = None; t_run = time.time()
        while time.time() - t_run < 900:
            s = c(H, cmd='pvp_status')
            if not down:
                outs = [(u['guid'], u['out_tick']) for u in s['units'] if u.get('out_tick', -1) >= 0]
                if outs:
                    down = (s['ticks'], outs); log('unit down:', down)
                    c(H, cmd='screenshot', path=os.path.join(OUT, 'frame_H_down.png')); c(G, cmd='screenshot', path=os.path.join(OUT, 'frame_G_down.png'))
            if down and not resynced and s['ticks'] >= 2500:
                l0 = st(G)['mp'].get('loads', 0)
                for x in (H, G): shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_{"H" if x == H else "G"}_pre_resync.log'))
                log('G resync', c(G, cmd='mp_resync'))
                wait('G reloaded', lambda: (lambda q: q['mp'].get('loads', 0) > l0 and q.get('det', {}).get('locked'))(st(G)), timeout=600)
                time.sleep(2); c(H, cmd='mp_pause', on=0); c(G, cmd='mp_pause', on=0)      # auto_resume is off: vote run again
                wait('G running', lambda: not st(G)['mp'].get('paused'), timeout=120)
                resynced = c(G, cmd='pvp_status'); log('G after resync', resynced['active'], resynced['ticks'])
            if s['ticks'] >= TICKS and down and resynced:
                break
            time.sleep(3)
        check('6 a unit goes down', bool(down), str(down))
        c(H, cmd='mp_pause', on=1)
        wait('both paused at one tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a.get('mp', {}).get('paused') and b.get('mp', {}).get('paused'))(st(H), st(G)), timeout=120)
        fh, fg = fp_read(H, t_start), fp_read(G, t_start)
        com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
        mh, mg = st(H)['mp'], st(G)['mp']
        sH, sG = c(H, cmd='pvp_status'), c(G, cmd='pvp_status')
        check('8 resync mid-match: guest reloaded with the PvP state (match on, same PvP hash, same hostile lists as the host)',
              bool(resynced) and resynced['active'] and sG['active'] and sH['hash'] == sG['hash'] and
              [(u['guid'], u['hed'], u['enemies_listed'], u.get('out_tick')) for u in sH['units']] == [(u['guid'], u['hed'], u['enemies_listed'], u.get('out_tick')) for u in sG['units']],
              f"G loads {mg.get('loads')}, hash {sH['hash']} / {sG['hash']}, lists H {[u['enemies_listed'] for u in sH['units']]} G {[u['enemies_listed'] for u in sG['units']]}")
        check('7 fingerprints equal on both copies for 3000+ ticks, no desync',
              len(com) >= 3000 and first is None and not mh.get('desynced') and not mg.get('desynced') and mh.get('desyncs', 0) == 0 and mg.get('desyncs', 0) == 0 and sH['hash'] == sG['hash'],
              f'{len(com)} common ticks ({com[0] if com else None}..{com[-1] if com else None}), first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, pvp ticks {sH["ticks"]}/{sG["ticks"]}')
        for x in (H, G):
            shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_{"H" if x == H else "G"}.log'))
        c(H, cmd='screenshot', path=os.path.join(OUT, 'frame_H_end.png')); c(G, cmd='screenshot', path=os.path.join(OUT, 'frame_G_end.png'))
        end_units = {u['guid']: u for u in c(H, cmd='list_units')['units']}
        summary = {'t0': t0, 'fp_freeze': fp_freeze, 'auto': auto[0] if auto else None, 'down': down, 'end_tick': st(H)['tick'],
                   'out_ticks': {u['guid']: u.get('out_tick') for u in sH['units']},
                   'fp_at': {str(t): fh[t] for t in com[::500]}, 'last_fp': (com[-1], fh[com[-1]]) if com else None,
                   'units': {g: (end_units[g]['name'], round(end_units[g]['hp'], 2), end_units[g].get('downed'), end_units[g]['dead']) for g in hp0 if g in end_units},
                   'results': results}
        json.dump(summary, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        log('summary', json.dumps(summary)[:1500])
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, LANE)   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
