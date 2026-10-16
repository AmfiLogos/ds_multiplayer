#!/usr/bin/env python3
"""PvP M3b part 2 pair test (ext/pvp lane): units from DS wiki builder team links, rebuilt in the mod (pvp_build.c).
Each player puts a 2-unit team link on his clipboard (test copies: <copy>/clipboard.txt), opens Esc > PvP Match >
Bring Units... and presses Paste Wiki Link; the host presses Start Match. Then, in the arena before the fight, every
stat of every built unit is read from the game (unit_stats = StatusReader, what the stat panel shows) on both copies and
compared with the builder's own sheet for the same build (tools/make_build_cases.js).
  W1 each copy: the window says the units were brought
  W2 the 4 wiki units come in at the settlement on both copies, named as in the links
  W3 every stat of every unit is the same on both copies (read at the settlement: the builder's sheet gets the host's
     difficulty affecter as extra adjustments; state stats such as Mood are left out)
  W4 Start Match: the match runs with them (2 per team)
  D  no desync
The builder-vs-game table goes to out/<tag>/stat_diff.txt (the difference list; not a pass/fail).
  pvp_wiki_units.py [--tag NAME] [--seed 5] [--keep]"""
import json, os, shutil, subprocess, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig
sys.path.insert(0, os.path.join(ROOT, 'research', 'ux_shots'))
import ux

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('wiki_%H%M%S')); SEED = arg('--seed', 5)
OUT = os.path.join(ROOT, 'ext', 'pvp', 'out', TAG); os.makedirs(OUT, exist_ok=True)
results = {}
LOG = open(os.path.join(OUT, 'test.log'), 'a')
FIX = os.path.join(ROOT, 'ext', 'foreign_expedition', 'fixtures')
HOST_SAVE = os.path.join(FIX, 'hub_Moonlight_mp.json')


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


def ushot(x, name):
    c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'ui_{name}.png'))


def main():
    teams = [json.loads(l) for l in subprocess.run(['node', os.path.join(ROOT, 'ext', 'pvp', 'tools', 'make_build_cases.js'), '4', str(SEED), '--team', '2'] + (['--edge'] if '--edge' in A else []),
                                                   capture_output=True, text=True, check=True).stdout.splitlines()]
    json.dump(teams, open(os.path.join(OUT, 'teams.json'), 'w'), indent=1)
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'out', OUT, 'links', [t['link'] for t in teams])
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    ps = lambda x: c(x, cmd='pvp_status')
    pv = lambda x, **k: c(x, cmd='mpm_pvp', **k)
    us_all = lambda x: [u for u in c(x, cmd='list_units')['units'] if u['faction'] == 1]
    link_of = {H: teams[0], G: teams[1]}
    try:
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
        for x in (H, G):
            open(os.path.join(rig.idir(x), 'clipboard.txt'), 'w').write('https://dungeonsettlers.wiki/builder/?team=' + link_of[x]['link'])
            c(x, cmd='mpm_esc'); time.sleep(2); pv(x, action='press_esc'); time.sleep(2)
            pv(x, action='bring'); time.sleep(2)
            pv(x, action='br_wiki'); time.sleep(3)
            w = pv(x); log(x, 'bring status', w['bring']['status'])
            ushot(x, 'bring_wiki')
            check(f'W1 {x}: the Bring window says the wiki units were brought', 'Brought 2 unit' in w['bring']['status'], w['bring']['status'])
            pv(x, action='br_back'); time.sleep(1)
        wait('host has 4 units', lambda: (lambda q: q if q['received'] == 4 else None)(c(H, cmd='pvp_units_status')), timeout=60, every=1)
        # the units come in at the settlement first (pvp_units_start, as Start Match does); stats are read there, where the
        # only world effect is the host's difficulty affecter (the arena region adds its environment, e.g. AgilityWeaken)
        log('units start', c(H, cmd='pvp_units_start'))
        want = sorted(u['build']['name'] for t in teams[:2] for u in t['units'])
        wait('the wiki units at the settlement on both', lambda: all(sorted(u['name'] for u in us_all(x) if not u['dead']) == want and st(x).get('map') == 0
                                                                     and not st(x).get('busy') for x in (H, G)), timeout=600, every=3)
        time.sleep(5)
        check('W2 the 4 wiki units are in the world on both copies, named as in the links', True, str(want))
        diff_keys = sorted(set(h['Key'] for cs in json.load(open(HOST_SAVE))['EntityComponentSaveData']['EntityComponents'].values()
                               for cc in cs if cc['Type'] == 3 for h in cc['Data'].get('AffecterHolders', []) if 'PlayerDifficulty' in h['Key']))
        aff = {r['Key']: r for r in json.load(open(os.path.expanduser('~/dungeonsettlersdb/data/AffecterTable.json')))}
        extra = [[a.rsplit('_', 1)[0], float(a.rsplit('_', 1)[1])] for k in diff_keys for a in (aff.get(k, {}).get('AdjustStatsData') or []) if a != 'NULL']
        log('world difficulty', diff_keys, extra)
        byname = {u['build']['name']: u for t in teams[:2] for u in t['units']}
        names_order = sorted(byname)
        # world effect per unit: Lycans carry AFFECTER_NocturnalDayTime by day (the test world is at day)
        noct = [[a.rsplit('_', 1)[0], float(a.rsplit('_', 1)[1])] for a in (aff['AFFECTER_NocturnalDayTime'].get('AdjustStatsData') or []) if a != 'NULL']
        for n in names_order:
            if byname[n]['build']['race'] == 'AFFECTER_Lycan': byname[n]['build']['_worldAdj'] = noct
        sheets = json.loads(subprocess.run(['node', os.path.join(ROOT, 'ext', 'pvp', 'tools', 'builder_sheet.js'), json.dumps(extra)],
                                           input=json.dumps([byname[n]['build'] for n in names_order]), capture_output=True, text=True, check=True).stdout)
        sheet = dict(zip(names_order, sheets))
        STATE = {'Mood', 'Energy', 'Hunger', 'Stress', 'HealthHead', 'HealthBody', 'HealthTotal', 'Barrier', 'Exp', 'Weight'}
        rows, same = [], True
        for u in us_all(H):
            if u['name'] not in byname: continue
            sh = c(H, cmd='unit_stats', guid=u['guid'])['stats']; sg = c(G, cmd='unit_stats', guid=u['guid'])['stats']
            dk = [k for k in sh if k not in STATE and abs(sh[k] - sg.get(k, 1e9)) > 1e-3]
            if dk: same = False; log('stats differ between copies for', u['name'], [(k, sh[k], sg.get(k)) for k in dk])
            log('state stats (not compared, they move with time)', u['name'], [(k, sh.get(k), sg.get(k)) for k in sorted(STATE) if k in sh and sh.get(k) != sg.get(k)])
            b = byname[u['name']]['build']
            for k, v in sorted(sheet[u['name']].items()):
                if k in sh and k not in STATE and isinstance(v, (int, float)):
                    rows.append((u['name'], k, round(v, 3), round(sh[k], 3), round(sh[k] - v, 3)))
            ex = byname[u['name']]['expect']
            for k, v in (('MainSkillPoint', ex['spMain']), ('SubSkillPoint', ex['spSub']), ('Level', b['lvl'])):
                rows.append((u['name'], k, round(v, 3), round(sh[k], 3), round(sh[k] - v, 3)))
            rows.append((u['name'], '_build', json.dumps({k: b[k] for k in ('race', 'bg', 'lvl', 'roll', 'offer', 'inds', 'insc', 'insctrees', 'eq', 'tags', 'learned', 'manual', 'elix', 'majorBase') if k in b}), '', ''))
        check('W3 every stat of every wiki unit is the same on both copies (state stats aside)', same and rows)
        with open(os.path.join(OUT, 'stat_diff.txt'), 'w') as f:
            f.write('unit\tstat\tbuilder\tgame\tgame-builder\n')
            for r in rows: f.write('\t'.join(str(x) for x in r) + '\n')
        nd = sum(1 for r in rows if r[1] != '_build' and abs(r[4]) > 0.05)
        log('stat rows', len(rows), 'differ by more than 0.05:', nd, '-> stat_diff.txt')
        ushot(H, 'settlement_wiki_units')
        # folded conditional passives (builder model: cond, not a milestone): their stat at rest, to watch in the fight
        pg = json.loads(subprocess.run(['node', '-e', "const c=require(process.argv[1]).load();process.stdout.write(JSON.stringify(c.D.passiveGrants))",
                                        os.path.join(ROOT, 'ext', 'pvp', 'tools', 'wiki_codec_probe.js')], capture_output=True, text=True, check=True).stdout)
        watch = []                                           # (guid, name, skill, stat, value at rest)
        for u in us_all(H):
            b = byname.get(u['name'], {}).get('build')
            for sk in (b or {}).get('learned', []):
                g = pg.get(sk)
                if g and g.get('cond') and not g.get('thr'):
                    stn = g['adj'][0][0]; watch.append((u['guid'], u['name'], sk, stn, c(H, cmd='unit_stats', guid=u['guid'])['stats'].get(stn)))
        log('folded passives to watch in the fight', watch)
        c(H, cmd='mpm_esc'); time.sleep(2); pv(H, action='press_esc'); time.sleep(2)
        pv(H, action='arrow', row=1, dir=-1); time.sleep(0.5)
        log('Start Match', pv(H, action='press_start'))
        s = wait('match on both', lambda: (lambda a, b: a if a['active'] and b['active'] else None)(ps(H), ps(G)), timeout=900, every=3)
        time.sleep(3)
        names = sorted(u['name'] for u in us_all(H) if not u['dead'])
        log('units in the match', names, [(u['team'], u['player']) for u in s['units']])
        if watch:
            wait('the fight begins', lambda: ps(H).get('fighting') or ps(H).get('fight_start'), timeout=300, every=1)
            peak = {(w[0], x): w[4] for w in watch for x in (H, G)}; reads = dict.fromkeys(peak, 0)
            t0 = time.time()
            while time.time() - t0 < 25:                       # First Assault lasts 15 s from the start of combat
                for w in watch:
                    for x in (H, G):
                        try: v = c(x, cmd='unit_stats', guid=w[0])['stats'].get(w[3])
                        except Exception: v = None
                        if v is not None and w[4] is not None: peak[(w[0], x)] = max(peak[(w[0], x)], v); reads[(w[0], x)] += 1
                time.sleep(1)
            skipped = {x: c(x, cmd='pvp_arena_info')['cuts'].get('passive_skipped') for x in (H, G)}
            log('in-fight peaks', [(w[1], w[2], w[3], w[4], peak[(w[0], H)], peak[(w[0], G)]) for w in watch], 'skipped', skipped, 'reads', list(reads.values()))
            check('W5 a folded passive does not add again in the fight (peak = value at rest on both copies; live grant skipped)',
                  all(reads.values()) and all(abs(peak[(w[0], x)] - w[4]) < 1e-3 for w in watch for x in (H, G)) and all(skipped.values()), f'{skipped} reads {list(reads.values())}')
        check('W4 the match runs with the 4 wiki units (2 per team)', names == want and sorted(u['team'] for u in s['units']) == [1, 1, 2, 2], str(names))
        ushot(H, 'arena_wiki_units')
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
