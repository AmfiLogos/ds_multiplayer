#!/usr/bin/env python3
"""E3 (owner 2026-10-20): a host WITH Units Laboratory brings a My Units unit and a freshly built one (a unit saved to My
Units while the game runs, as the Unit Builder saves it) into PvP; the guest has NO Units Laboratory, sees the same units
(same stats on both copies) and fights them; 0 desyncs. The card window's source switch / Build button need the loader's
widget kit (not in a rig copy): this proves the data path the window uses (pvp_bring_picks with My Units picks).
  e3_lib_pair.py [--dylib PATH] [--tag NAME]"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pv_lib import T, rig, G_UNITS  # noqa: E402
LIB = os.path.expanduser('~/ds_cm_pvpfix/units/test/data/launcher_library.json')


def body(t):
    H, G = t.H, t.G
    lib = os.path.join(t.out, 'library.json'); d = json.load(open(LIB)); d['units'] = d['units'][:3]; json.dump(d, open(lib, 'w'))
    t.prepare(env={H: {'DSRIG_CUSTOM': os.path.join(t.out, 'custom-modes.dylib'), 'DSMP_UNITS_LIB': lib}, G: {'DSRIG_CUSTOM': '0'}})
    t.host_world(bo_down=1, secs_down=2)
    t.guest_join()
    # a unit saved to My Units while the game runs (what the Unit Builder's Save writes)
    d = json.load(open(lib)); fresh = json.loads(json.dumps(d['units'][1])); fresh['id'] = 'f0e1d2c3b4a59687'; fresh['name'] = 'Fresh Build'
    fresh['build']['name'] = 'Fresh Build'; d['units'].append(fresh); tmp = lib + '.tmp'; json.dump(d, open(tmp, 'w')); os.replace(tmp, lib)
    ids = [d['units'][0]['id'], fresh['id']]
    r = t.c(H, cmd='pvp_bring_lib', ids=','.join(ids)); t.log('H brings My Units', r)
    t.check('E3 host with Units Laboratory brings a My Units unit and a freshly built one', r.get('ok') and r.get('ul') == 1, r)
    r2 = t.c(G, cmd='pvp_bring_lib', ids=ids[0]); t.log('G (no Units Laboratory) My Units', r2)
    t.check('E3 guest without Units Laboratory: no My Units source (refused, nothing sent)', not r2.get('ok') and r2.get('ul') == 0, r2)
    t.log('G bring', t.c(G, cmd='pvp_units_bring', file=os.path.join(t.work[G], 'my_home.json'), guids=','.join(G_UNITS)))
    t.wait('host has 4 units', lambda: t.c(H, cmd='pvp_units_status')['received'] == 4, timeout=90, every=1)
    t.c(H, cmd='pvp_mods', ask=1); t.wait('guest mod report', lambda: len(t.c(H, cmd='pvp_mods').get('reports', [])) >= 1, timeout=60, every=1)
    t.lobby_start()
    s = t.c(H, cmd='pvp_status'); units = s['units']
    host_units = [u['guid'] for u in units if u['player'] == 0]
    lu = {x: {u['guid']: u for u in t.c(x, cmd='list_units')['units']} for x in (H, G)}
    names = sorted(lu[G][g].get('name', '?') for g in host_units if g in lu[G])
    same = all(g in lu[G] and g in lu[H] and all(lu[H][g].get(k) == lu[G][g].get(k) for k in ('name', 'key', 'max_hp')) for g in host_units)
    t.log('host units on G', names, 'H view', [lu[H][g].get('name') for g in host_units if g in lu[H]])
    t.check('E3 the guest (no Units Laboratory) has the host\'s My Units units, same data on both copies', len(host_units) == 2 and same and 'Fresh Build' in names, (names, same))
    for x in (H, G): t.shot(x, 'e3_units')
    owner = {u['guid']: (H if u['player'] == 0 else G) for u in units}
    t.c(G, cmd='pvp_vote', op='propose'); t.wait('vote', lambda: t.c(H, cmd='pvp_status')['vote_open'], timeout=30, every=0.5); t.c(H, cmd='pvp_vote', op='agree')
    t.wait('fight', lambda: (lambda q: q.get('fight_start', 0) > 0 and not q.get('placing'))(t.c(H, cmd='pvp_status')), timeout=90, every=1)
    seg = t.c(H, cmd='status')['tick']; segs = []
    t_end = time.time() + 300
    while time.time() < t_end:
        s = t.c(H, cmd='pvp_status')
        if s.get('ending') and not segs: segs.append(t.fp_compare(seg, settle=0.3)); t.log('segment', segs[-1])
        if not s.get('active') and s.get('matches', 0) >= 1: break
        if s.get('fight_start') and not s.get('placing') and not s.get('ending'):
            s2, c2 = t.units(H)
            alive = [u for u in s2['units'] if u.get('out_tick', -1) < 0 and u['guid'] in c2 and 'cell' in c2[u['guid']]]
            for u in alive:
                en = [v for v in alive if v['team'] != u['team']]
                if not en: continue
                c0 = c2[u['guid']]['cell']; tg = min(en, key=lambda v: (c2[v['guid']]['cell'][0] - c0[0]) ** 2 + (c2[v['guid']]['cell'][1] - c0[1]) ** 2); tc = c2[tg['guid']]['cell']
                t.c(owner[u['guid']], cmd='ord_run_attack', unit=u['guid'], target=tg['guid'], x=tc[0], y=tc[1], mp=1)
        time.sleep(4)
    ds = (t.c(H, cmd='status')['mp'].get('desyncs'), t.c(G, cmd='status')['mp'].get('desyncs'))
    t.check('E3 match with the My Units units: 0 desyncs (fingerprints equal, counters 0)', segs and segs[0][0] >= 200 and segs[0][1] is None and not ds[0] and not ds[1], (segs, ds))


if __name__ == '__main__':
    T('e3', sys.argv).run(body)
