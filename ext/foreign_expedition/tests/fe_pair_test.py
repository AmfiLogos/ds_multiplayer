#!/usr/bin/env python3
"""Foreign Expedition pair test (F3 + F5 + F6), ONE leased pair, build/dsrig_fe.dylib.
  host  = Moonlight base at NORMAL difficulty (fixtures/hub_Moonlight_normal.json), hosted through the menu
  guest = joins by address, brings 2 units from Elven Expedition (VeryHard, 8 units) by mail
  host  brings 1 unit of its own (Moonlight MP save, VeryHard)
  fe_start -> both peers load the hub (new campaign, fe5ea000 prefix), owners = bringers, gear Own storage;
  stats of the moved units on both peers (Normal vs VeryHard difficulty holders); fingerprints equal;
  guest LEAVES; host fe_end -> results kept; guest REJOINS -> results resent + acknowledged;
  guest leaves, fe_apply: backup of the whole Saves folder first, new save, second apply refused, load it, stats again.
Writes: out/pair_<time>/ + the copies' instance folders + backups/<time>/ (env DSRIG_FE_BACKUP)."""
import glob, json, os, shutil, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT); sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import fe_save as fe
import rig

rig.DYLIB = os.path.join(ROOT, 'build', 'dsrig_fe.dylib')
os.environ['DSRIG_FE_BACKUP'] = os.path.join(ROOT, 'backups')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15)
OUT = os.path.join(ROOT, 'out', 'pair_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
LOG = open(os.path.join(OUT, 'pair.log'), 'a'); results = []
STAT_NAMES = json.load(open(os.path.join(ROOT, 'tests', 'stat_names.json')))


def log(*a):
    s = ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def main():
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < 3 * 3600:
        # lead 2026-10-14: the release-blocking desync hunt (menuteams lane) has the first pair; lease only while it holds one
        # lead 2026-10-14 12:00: the release test has 2 pairs; fe and pvpbuild share the third (never while pvp holds one)
        held = rig._with_leases(lambda d: [l['lane'] for l in d.values()])
        if not any('pvp' in l for l in held):
            pair = rig.lease('fe_pair', os.getpid())
        if not pair: time.sleep(30)
    if not pair: log('no free pair'); sys.exit(2)
    H, G = pair[0], pair[1]
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        run(H, G)
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc())
        results.append(('no exception', False))
    finally:
        for x in (H, G): rig.stop(x)
        for x in (H, G):                                        # lead: every peer's logs kept with the run
            for f in ('player.log', 'rig.log'):
                src = os.path.join(rig.idir(x), f)
                if os.path.exists(src): shutil.copy2(src, os.path.join(OUT, f'{x}_{f}'))
        ex = {x: [l.strip()[:300] for l in open(os.path.join(OUT, f'{x}_player.log'), errors='replace') if 'Exception' in l]
              for x in (H, G) if os.path.exists(os.path.join(OUT, f'{x}_player.log'))}
        log('player.log exceptions', {x: len(v) for x, v in ex.items()}); [log('  ', x, l) for x, v in ex.items() for l in v[:5]]
        log('released', rig.release(pair))
        n = sum(1 for _, ok in results if not ok)
        log(f'{len(results) - n}/{len(results)} PASS' + (f', {n} FAIL' if n else ''), '| out:', OUT)


def run(H, G):
    c = lambda x, **k: rig.send(x, k, timeout=180)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t1 = time.time()
        while time.time() - t1 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def named(v): return {(STAT_NAMES[i] if i < len(STAT_NAMES) else f'stat{i + 1}'): x for i, x in enumerate(v)}

    def stats(x, g):
        r = c(x, cmd='fe_stats', guid=g)
        return named(r['stats']) if r.get('ok') else None

    def fp_equal_from(t_from):
        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and int(p[0]) > t_from: out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(G); com = sorted(set(a) & set(b))
        return len(com), next((t for t in com if a[t] != b[t]), None)

    def join_guest(port):
        if not rig.alive(rig.read_pid(G)): rig.start(G, fps=30, window=True)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
        fed = os.path.join(rig.idir(x), 'data', 'fe')                    # a fresh FE state per run (moved aside, kept)
        if os.path.exists(fed): os.replace(fed, fed + '_' + time.strftime('%Y%m%d-%H%M%S'))
    # inputs: copies only
    gsave = os.path.join(OUT, 'guest_home_Elven.json'); shutil.copy2(os.path.join(ROOT, 'fixtures', 'home_Elven.json'), gsave)
    hsave = os.path.join(OUT, 'host_home_Moonlight.json'); shutil.copy2(os.path.join(ROOT, 'fixtures', 'hub_Moonlight_mp.json'), hsave)
    base = os.path.join(OUT, 'hub_base_normal.json'); shutil.copy2(os.path.join(ROOT, 'fixtures', 'hub_Moonlight_normal.json'), base)
    home = fe.load(gsave)
    units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
    gpick = [units[0][0], units[1][0]]
    hpick = [fe.clan_units(fe.load(hsave))[0][0]]
    # the guest's home save also sits in the guest's own Saves folder (that is where apply writes the new save)
    gsd = os.path.join(rig.idir(G), 'data', 'Saves'); os.makedirs(gsd, exist_ok=True)
    gstem = home['CampaignSaveHeader']['SaveName']
    if not os.path.exists(os.path.join(gsd, gstem + '.json')): shutil.copy2(gsave, os.path.join(gsd, gstem + '.json'))

    rig.start(H, fps=30, window=True); rig.start(G, fps=30, window=True)
    rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
    # baseline: the same 2 units in their HOME world (VeryHard), measured by F2 on this build (fe_stats, game paused)
    f2 = sorted(glob.glob(os.path.join(ROOT, 'out', 'f2_*', 'stats_home.json')))[-1]
    s_home = {g: named(list(v.values())) for g, v in json.load(open(f2)).items()}   # F2 keys: 62 names + statN; same order
    check('baseline: home stats of the 2 units (F2 run)', all(s_home.get(g) for g in gpick), f2)
    rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
    time.sleep(3)
    press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
    c(H, cmd='load_save', file=base, det=0)
    wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
    port = c(H, cmd='mpm_status').get('port')
    join_guest(port)
    log('host fe_status', c(H, cmd='fe_status'))

    # F3: units travel by mail
    r = c(G, cmd='fe_bring', file=gsave, guids=','.join(gpick)); log('guest bring', r)
    r2 = c(H, cmd='fe_bring', file=hsave, guids=','.join(hpick)); log('host bring', r2)
    s = wait('host received 3 units', lambda: (lambda f: f if f.get('received') == 3 else None)(c(H, cmd='fe_status')), timeout=60, every=1)
    check('F3: guest units reached the host by mail (sender stamped), host units kept', s.get('received') == 3, s.get('msg'))

    # F5: start
    tick0 = st(H).get('tick', 0)
    REC = 1 if '--recruit' in sys.argv else 0                  # host option "Recruiting in the hub" (default Off)
    r = c(H, cmd='fe_start', death=1, xp=1, items=1, recruit=REC); log('fe_start', r)
    run_id = r.get('run')
    wait('both peers in the hub', lambda: c(H, cmd='fe_status').get('hub_world') and c(G, cmd='fe_status').get('hub_world')
         and (lambda s: s.get('det', {}).get('locked'))(st(H)) and (lambda s: s.get('det', {}).get('locked'))(st(G)), timeout=600, every=2)
    fh = c(H, cmd='fe_status'); log('host fe_status', fh)
    hub_units = fh.get('hub_units') or []
    check('F5: hub built at the join point and loaded on both peers', fh.get('run') == run_id and len(hub_units) == 3, fh.get('msg'))
    press(H, 'START')                                                   # lobby closes (normal path)
    wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
    lu = {u['guid']: u for u in c(H, cmd='list_units')['units']}
    check('F5: the 3 moved units are in the world, alive', all(h['hub'] in lu and not lu[h['hub']]['dead'] for h in hub_units),
          str([(lu.get(h['hub'], {}).get('name'), lu.get(h['hub'], {}).get('max_hp')) for h in hub_units]))
    gu = c(G, cmd='fe_status').get('uid')
    own_ok = True
    for h in hub_units:
        oh, og = c(H, cmd='mp_owner', guid=h['hub']).get('owner'), c(G, cmd='mp_owner', guid=h['hub']).get('owner')
        want_guest = h['player'] == gu
        own_ok &= oh == og == (1 if want_guest else 0)
        log('owner', h['hub'][:8], 'host view', oh, 'guest view', og, 'brought by guest' if want_guest else 'brought by host')
    check('F5: owners = the players who brought them, same on both peers', own_ok)
    # stats in the hub on both peers + the difficulty move
    diffs = {}
    for h in hub_units:
        if h['player'] != gu: continue
        a, b = stats(H, h['hub']), stats(G, h['hub'])
        live = ('Energy', 'Hunger', 'Stress', 'HealthHead', 'HealthBody', 'HealthTotal', 'Mood', 'Exp')   # change with time
        dd = {k: (a[k], b[k]) for k in a if k not in live and abs(a[k] - b[k]) > 1e-3} if a and b else 'missing'
        check(f'F5: stats of {h["hub"][:8]} equal on host and guest (live needs excluded)', dd == {}, json.dumps(dd)[:400])
        base_s = s_home[h['home']]
        diffs[h['home']] = {k: (base_s[k], b[k]) for k in b if abs(base_s[k] - b[k]) > 1e-3}
        log(f'STATS home(VeryHard) -> hub(Normal) {h["home"][:8]}:', json.dumps(diffs[h['home']]))
    json.dump(diffs, open(os.path.join(OUT, 'stats_home_vs_hub.json'), 'w'), indent=1)
    # recruiting in the hub: Off = window never opens + order dropped silently; On = works, recruit owned by the recruiter
    for x in (H, G): log(x, 'fe_status recruit_allowed', c(x, cmd='fe_status').get('recruit_allowed'))
    mk = {x: os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) for x in (H, G)}
    pop0 = len([u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1])
    cell = next(u['cell'] for u in c(G, cmd='list_units')['units'] if u['faction'] == 1 and 'cell' in u)
    log('guest recruit order', c(G, cmd='ord_inject', ev='CandidateRecruitRequested', **{'i32@16': 0, 'i32@20': 0, 'i32@24': cell[0] + 1, 'i32@28': cell[1], 'i32@32': 1}))
    w = c(G, cmd='ui_popup', type=8); time.sleep(8)
    logs = {x: open(os.path.join(rig.idir(x), 'rig.log'), 'rb').read()[mk[x]:] for x in (H, G)}
    pop1 = len([u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1])
    if REC:
        new = [u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1 and u['guid'] not in {h['hub'] for h in hub_units}]
        own = [c(H, cmd='mp_owner', guid=u['guid']).get('owner') for u in new]
        check('Recruiting On: the recruit order runs on both peers, the recruit belongs to the recruiter',
              all(b'allowed' in logs[x] for x in (H, G)) and pop1 == pop0 + 1 and 1 in own, f'units {pop0}->{pop1}, owners {own}')
    else:
        check('Recruiting Off: window not opened, order dropped silently on both peers, no new unit',
              w.get('active') == 0 and all(b'refused: Foreign Expedition hub' in logs[x] for x in (H, G)) and pop1 == pop0, f'popup {w}, units {pop0}->{pop1}')
    time.sleep(40)
    n, first = fp_equal_from(tick0)
    check('F5: fingerprints equal in the hub', n >= 60 and first is None and not mp(G).get('desyncs'), f'{n} common ticks after the start, first diff {first}')
    c(H, cmd='ui_capture', path=os.path.join(OUT, 'hub_host.png')); c(G, cmd='ui_capture', path=os.path.join(OUT, 'hub_guest.png'))

    # F6: guest leaves, host ends, guest rejoins -> results resent
    c(G, cmd='mp_quit', mode='title'); wait('guest on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=120)
    time.sleep(3)
    c(H, cmd='mp_left_choice', keep=1); time.sleep(2)
    r = c(H, cmd='fe_end'); log('fe_end', r)
    check('F6: host ended the run with the guest away (2 results made; the guest\'s waits on the host)', r.get('ok') and r.get('results') == 2
          and not os.path.exists(os.path.join(rig.idir(G), 'data', 'fe', 'results', f"{r.get('run')}.json")), str(r))
    join_guest(port)
    gres = os.path.join(rig.idir(G), 'data', 'fe', 'results', f'{run_id}.json')
    wait('results delivered after the rejoin', lambda: os.path.exists(gres), timeout=120, every=2)
    dl = glob.glob(os.path.join(rig.idir(H), 'data', 'fe', 'runs', run_id, 'results', '*.delivered'))
    time.sleep(3)
    dl = glob.glob(os.path.join(rig.idir(H), 'data', 'fe', 'runs', run_id, 'results', '*.delivered'))
    check('F6: results resent on the rejoin, acknowledged (host marked delivered)', os.path.exists(gres) and len(dl) >= 1, f'{len(dl)} delivered')
    c(G, cmd='mp_quit', mode='title'); wait('guest on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=120)

    # apply (guest, on the title): backup FIRST, new save, second apply refused
    before = sorted(os.listdir(gsd))
    gsave_bytes = open(gsave, 'rb').read()
    r = c(G, cmd='fe_apply', run=run_id, target=gsave); log('fe_apply', r)
    bk = r.get('backup', '')
    check('F6: backup of the whole Saves folder made before the apply', bk and os.path.isdir(bk) and sorted(os.listdir(bk)) == sorted(f for f in before if os.path.isfile(os.path.join(gsd, f))), f'{bk}: {len(os.listdir(bk)) if bk and os.path.isdir(bk) else 0} files')
    check('F6: apply wrote a NEW save, the original untouched', r.get('ok') and os.path.exists(r.get('file', '')) and
          open(gsave, 'rb').read() == gsave_bytes, str(r.get('report')))
    r2 = c(G, cmd='fe_apply', run=run_id, target=gsave); log('second apply', r2)
    check('F6: second apply refused', not r2.get('ok') and 'Already' in r2.get('error', ''), r2.get('error'))
    if not r.get('ok'): return
    new = fe.load(r['file'])
    check('F6: new save: difficulty holders back to the home ones', all(
        sorted(h['Key'] for h in fe.comp(fe.ec_of(new)[g], 3)['AffecterHolders'] if fe.DIFF_RE.match(h['Key'])) == fe.difficulty_keys(home) for g in gpick))
    # load the new home save: stats equal to the home baseline again
    c(G, cmd='load_save', file=r['file'], det=0)
    wait('new home loaded', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('map') == 0 and s.get('tick', -1) > 0)(st(G)), timeout=400)
    c(G, cmd='set_speed', speed=0); time.sleep(2)
    s_new = {g: stats(G, g) for g in gpick}
    json.dump(s_new, open(os.path.join(OUT, 'stats_new_home.json'), 'w'), indent=1)
    for g in gpick:
        d = {k: (s_home[g][k], s_new[g][k]) for k in s_new[g] if abs(s_home[g][k] - s_new[g][k]) > 1e-3} if s_new[g] else 'missing'
        log(f'STATS home -> after apply {g[:8]}:', json.dumps(d))
        bad = [k for k in (d if isinstance(d, dict) else {}) if k not in ('Energy', 'Hunger', 'Stress', 'HealthHead', 'HealthBody', 'HealthTotal', 'Mood', 'VisionRange', 'EnergyRegeneration')]
        check(f'F6: {g[:8]} back home: combat stats and attributes = before the trip (no XP gained in this short run)', s_new[g] and not bad, str(bad))
    c(G, cmd='ui_capture', path=os.path.join(OUT, 'new_home_guest.png'))
    errs = {x: sum(1 for l in open(os.path.join(rig.idir(x), 'player.log'), errors='replace') if 'Exception' in l) for x in (H, G)}
    log('player.log exceptions', errs)


if __name__ == '__main__':
    main()
