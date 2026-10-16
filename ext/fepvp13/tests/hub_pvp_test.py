#!/usr/bin/env python3
"""fepvp13: PvP FROM THE HUB (same start as hub_dive_test.py, then Esc > PvP Match with the hub's units, match end, hub back).
fepvp13: Foreign Expedition dive in the NEUTRAL HUB (copy of ext/foreign_expedition/tests/fe_dive_test.py; the host page
Start builds the hub from the wiki base template, no host save is loaded). Lane fepvp, this worktree's build/dsrig.dylib.
Original doc:
Foreign Expedition dive test (plan FE-2), ONE leased pair, build/dsrig_fe.dylib.
Same start as fe_pair_test.py (Normal hub, guest brings 2 Elven units, host 1), then a REAL dungeon run:
Teams -> expedition with all 3 moved units -> fight in the dungeon for a few minutes -> portal -> Return screen ->
base -> fe_end -> guest gets the results (present) -> leaves -> fe_apply -> loads the new home save.
Checks: XP gained in the hub comes home (level / exp = the hub's end state), dungeon loot tagged to the guest comes home
into his storage, crafted / hub items never, fingerprints equal, recruit order refused in the hub (if a candidate exists)."""
import glob, json, os, shutil, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
ROOT = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition')        # fixtures (not in git)
sys.path.insert(0, os.path.join(W, 'ext', 'foreign_expedition')); sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import fe_save as fe
import rig

rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1]) if '--dylib' in sys.argv else os.path.join(W, 'build', 'dsrig.dylib')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # test copy of the wiki file
GAME_SAVES = None                                    # set in run(): the copies' "game folder" (copies only, never the real one)
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, HOST_FE=123)
B.update(json.loads(os.popen(f'python3 {W}/ext/fepvp13/tools/mpm_ids.py {W}').read()))   # menu ids of this build
OUT = os.path.join(W, 'ext', 'fepvp13', 'out', 'hubpvp_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
os.environ['DSRIG_FE_BACKUP'] = os.path.join(OUT, 'backups')
LOG = open(os.path.join(OUT, 'dive.log'), 'a'); results = []
DIVE_S = int(sys.argv[sys.argv.index('--dive') + 1]) if '--dive' in sys.argv else 300
DEATH = int(sys.argv[sys.argv.index('--death') + 1]) if '--death' in sys.argv else 1     # rule: death permanent 1 / 0
WEAK = '--weak' in sys.argv          # the guest's 2nd unit leaves home at 3 % health (test copy only): it should die
RECRUIT = int(sys.argv[sys.argv.index('--recruit') + 1]) if '--recruit' in sys.argv else 0   # host rule Recruiting in the hub 0 Off / 1 On
FEED = '--feed' in sys.argv          # test copies only: brought units leave home fed and rested (Hunger, Energy 1.0)
LANE = 'fepvp'
SI = dict(HealthHead=12, HealthBody=13, Energy=14, Hunger=15, Level=17, Exp=18)   # fe_stats index (StatType - 1)


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def main():
    import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))   # kill -TERM: copies stopped + pair released (finally)
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < 3 * 3600:
        pair = rig.lease(LANE, os.getpid())
        if not pair: time.sleep(30)
    if not pair: log('no free pair'); sys.exit(2)
    H, G = pair[0], pair[1]
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        run(H, G)
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); results.append(('no exception', False))
    finally:
        for x in (H, G): rig.stop(x)
        for x in (H, G):                                        # other tests read mp.json (port, role): put it back
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
        for x in (H, G):                                        # lead: every peer's logs kept with the run
            for f in ('player.log', 'rig.log'):
                src = os.path.join(rig.idir(x), f)
                if os.path.exists(src): shutil.copy2(src, os.path.join(OUT, f'{x}_{f}'))
        ex = {x: [l.strip()[:300] for l in open(os.path.join(OUT, f'{x}_player.log'), errors='replace') if 'Exception' in l]
              for x in (H, G) if os.path.exists(os.path.join(OUT, f'{x}_player.log'))}
        log('player.log exceptions', {x: len(v) for x, v in ex.items()}); [log('  ', x, l) for x, v in ex.items() for l in v[:5]]
        log('released', rig.release(pair, LANE))
        n = sum(1 for _, ok in results if not ok)
        log(f'{len(results) - n}/{len(results)} PASS' + (f', {n} FAIL' if n else ''), '| out:', OUT)


def run(H, G):
    c = lambda x, **k: rig.send(x, k, timeout=180)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    tm = lambda x, **k: c(x, cmd='ui_teams', **k)
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

    def wait(what, pred, timeout=600, every=2):
        t1 = time.time()
        while time.time() - t1 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def fp_equal_from(t_from):
        def rd(x):
            out = {}
            for line in open(os.path.join(rig.idir(x), 'fp.log')):
                p = line.split()
                if len(p) >= 5 and int(p[0]) > t_from: out[int(p[0])] = p[1]
            return out
        a, b = rd(H), rd(G); com = sorted(set(a) & set(b))
        return len(com), next((t for t in com if a[t] != b[t]), None)

    def order_move(x, guid, cell):                          # player input path (src/ui_test/result_pair.py): select + click a cell
        c(x, cmd='ord_hover', n=0)
        r1 = c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
        r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
        return bool(r1.get('ok') and r2.get('ok'))

    def live(x):
        return {u['guid']: u for u in c(x, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead'] and not u.get('downed') and 'cell' in u}

    def join_guest(port):
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR')
        c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)

    for x in (H, G):
        p = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
        for sub in ('fe', 'mp_owners'):
            d = os.path.join(rig.idir(x), 'data', sub)
            if os.path.exists(d): os.replace(d, d + '_' + time.strftime('%Y%m%d-%H%M%S'))
    gdir = os.path.join(OUT, 'game_saves'); os.makedirs(gdir)       # DSRIG_FE_GAME_SAVES: what the Bring window lists as "Game folder"
    os.environ['DSRIG_FE_GAME_SAVES'] = gdir
    gsave = os.path.join(gdir, 'guest_home_Elven.json'); shutil.copy2(os.path.join(ROOT, 'fixtures', 'home_Elven.json'), gsave)
    hsave = os.path.join(gdir, 'host_home_Moonlight.json'); shutil.copy2(os.path.join(ROOT, 'fixtures', 'hub_Moonlight_mp.json'), hsave)
    cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))          # the wiki cache (the game downloads it once)
    home = fe.load(gsave)
    units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
    gpick = [units[0][0], units[1][0]]
    if WEAK:
        ss = fe.comp(fe.ec_of(home)[gpick[1]], 0)['StateStats']; ss['HealthHead'] = ss['HealthBody'] = 0.03
    if FEED:
        for g in gpick: fe.comp(fe.ec_of(home)[g], 0)['StateStats'].update(Hunger=1.0, Energy=1.0)
    if WEAK or FEED: json.dump(home, open(gsave, 'w'), indent=2)
    hpick = [fe.clan_units(fe.load(hsave))[0][0]]
    if FEED:
        hs = fe.load(hsave); fe.comp(fe.ec_of(hs)[hpick[0]], 0)['StateStats'].update(Hunger=1.0, Energy=1.0); json.dump(hs, open(hsave, 'w'), indent=2)
    log('args', dict(dive=DIVE_S, death=DEATH, weak=WEAK, recruit=RECRUIT, feed=FEED))

    rig.start(H, fps=30, window=True); rig.start(G, fps=30, window=True)
    rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
    time.sleep(3)
    press(H, 'MULTIPLAYER'); press(H, 'HOST_FE')                # uifix11: Foreign Expedition is chosen under "Host a game"
    # F4 host page (board FESettingsA): World -> Foreign Expedition, its Rules panel, a tooltip; frames for the owner
    want = dict(fe=1, fe_death=DEATH, fe_xp=1, fe_loot=1, fe_recruit=RECRUIT)   # the menu remembers the last values (mp_menu.json)
    press(H, 'SETTINGS'); time.sleep(1)
    for k, btn in (('fe_death', 'FED_NEXT'), ('fe_xp', 'FEX_NEXT'), ('fe_loot', 'FEL_NEXT'), ('fe_recruit', 'FER_NEXT')):
        if c(H, cmd='mpm_status')['settings'].get(k) != want[k]: press(H, btn)
    got = {k: c(H, cmd='mpm_status')['settings'].get(k) for k in want}
    check('F4 host page: Foreign Expedition rules set through the page', got == want, str(got))
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'host_page_fe_rules.png'))
    c(H, cmd='mpm_tip', title='Recruiting'); time.sleep(1.5)
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'host_page_fe_tip_recruiting.png')); c(H, cmd='mpm_tip')
    ms = c(H, cmd='mpm_status'); log('menu after World + Rules', {k: ms.get(k) for k in ('state', 'set', 'settings_open') if k in ms})
    press(H, 'FE_DONE'); time.sleep(1)
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'host_page_fe_summary.png'))
    press(H, 'LOAD_SAVE'); time.sleep(2)                       # the FE page's Start: the neutral hub is built and loads
    check('hub: no load list / new campaign screen (Start builds the neutral hub)', c(H, cmd='mpm_status').get('state') in ('pick_save', 'host_loading', 'lobby'), c(H, cmd='mpm_status').get('state'))
    wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
    port = c(H, cmd='mpm_status').get('port')
    join_guest(port)
    # F4 Bring units window (board FEBringA), driven like a player: lobby button, pick the save, click unit cards, Bring
    try: wait('guest knows the FE mode (host mail)', lambda: c(G, cmd='mpm_fewin').get('fe_mode') == 1, timeout=60, every=1)
    except RuntimeError:
        for x in (H, G): log(x, 'fewin', json.dumps(c(x, cmd='mpm_fewin'))[:400], '| fe_status', json.dumps(c(x, cmd='fe_status'))[:400], '| menu', c(x, cmd='mpm_status').get('state'))
        raise
    def bring_ui(x, path, guids, tag):
        wait(f'{x} lobby state', lambda: c(x, cmd='mpm_status').get('state') in ('lobby', 'guest_lobby'), timeout=90, every=1)
        for _ in range(5):                                      # the lobby button acts only once the lobby window is up
            c(x, cmd='mpm_click', button=B['FE_BRINGWIN']); time.sleep(2)
            if c(x, cmd='mpm_fewin').get('mode') == 1: break
        w = c(x, cmd='mpm_fewin'); log(x, 'bring window', {k: w.get(k) for k in ('mode', 'sel_save', 'version')}, [(s_['clan'], s_['ok'], s_['version']) for s_ in w.get('saves', [])])
        idx = next(i for i, s_ in enumerate(w['saves']) if s_['path'] == path)
        for _ in range(idx // 6): c(x, cmd='mpm_click', button=B['FE_SAVEPG']); time.sleep(0.5)
        c(x, cmd='mpm_click', button=B['FE_SAVE0'] + idx % 6); time.sleep(1.5)
        w = c(x, cmd='mpm_fewin'); us = [u['guid'] for u in w['units']]
        for g in guids:
            k = us.index(g); c(x, cmd='mpm_click', button=B['FE_UNIT0'] + k % 8); time.sleep(0.7)
        c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'bring_window_{tag}.png'))
        c(x, cmd='mpm_tip', title='Bring this unit'); time.sleep(1.2); c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'bring_tip_{tag}.png')); c(x, cmd='mpm_tip')
        w = c(x, cmd='mpm_fewin'); check(f'F4 Bring window ({tag}): save picked, {len(guids)} unit(s) checked', w.get('picked') == len(guids), w.get('err'))
        c(x, cmd='mpm_click', button=B['FE_OK']); time.sleep(2)
        log(x, 'after Bring', c(x, cmd='mpm_fewin').get('mode'), c(x, cmd='fe_status').get('msg'))
    bring_ui(G, gsave, gpick, 'guest'); bring_ui(H, hsave, hpick, 'host')
    time.sleep(3); c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'lobby_units_brought_host.png'))
    c(G, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'lobby_units_brought_guest.png'))
    wait('host received 3 units', lambda: c(H, cmd='fe_status').get('received') == 3, timeout=60, every=1)
    check('F4: units brought through the window reached the host (3)', True)
    mk0 = {x: os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) for x in (H, G)}
    press(H, 'START')                                           # FE lobby Start = the hub build (mpmenu MPA_FE_START)
    r = wait('run started from the menu', lambda: (lambda f: f if f.get('run') or f.get('pending_start') else None)(c(H, cmd='fe_status')), timeout=60, every=1)
    log('Start from the menu ->', r.get('msg')); run_id = None
    wait('both peers in the hub', lambda: c(H, cmd='fe_status').get('hub_world') and c(G, cmd='fe_status').get('hub_world')
         and st(H).get('det', {}).get('locked') and st(G).get('det', {}).get('locked'), timeout=600, every=2)
    fh = c(H, cmd='fe_status'); hub_units = fh['hub_units']; run_id = fh['run']
    check('F4: Start in the Foreign Expedition lobby built the hub with the host page rules', bool(run_id) and len(hub_units) == 3, fh.get('msg'))
    its = {x: c(x, cmd='ui_items', map=0).get('items', []) for x in (H, G)}
    ht = {x: sum(i.get('amount', 1) for i in its[x] if i.get('key') == 'ITEM_Hardtack') for x in (H, G)}
    check('hub: the storage stock is in the hub on both peers (Hardtack 100+ on the ground)', all(v >= 100 for v in ht.values()) and len(its[H]) == len(its[G]), f'{ht}, items {len(its[H])}/{len(its[G])}')
    gu = c(G, cmd='fe_status')['uid']
    mine = {H: [h['hub'] for h in hub_units if h['player'] != gu], G: [h['hub'] for h in hub_units if h['player'] == gu]}
    own = {h['hub'][:8]: (c(H, cmd='mp_owner', guid=h['hub']).get('owner'), c(G, cmd='mp_owner', guid=h['hub']).get('owner'), 'guest' if h['player'] == gu else 'host') for h in hub_units}
    check('F5 (menu Start): owners = the players who brought them, same on both peers',
          all(a == b and a == (1 if who == 'guest' else 0) for a, b, who in own.values()), str(own))
    wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
    tick0 = st(H)['tick']
    tick0 = st(H)['tick']
    # ---- PvP from the hub (Esc > PvP Match; teams = the units each player owns in the hub; played on this settlement)
    hub0 = sorted(h['hub'] for h in hub_units)
    for x in (H, G): c(x, cmd='mpm_esc'); time.sleep(2); c(x, cmd='mpm_pvp', action='press_esc'); time.sleep(2)   # real path: Esc menu > PvP Match
    w = {x: c(x, cmd='mpm_pvp') for x in (H, G)}
    check('hub PvP: the PvP window opens from the Esc menu on both', all(w[x].get('window') for x in (H, G)), str({x: w[x].get('window') for x in (H, G)}))
    c(H, cmd='mpm_pvp', action='arrow', row=1, dir=-1); time.sleep(1.5)            # best of 1
    c(H, cmd='mpm_pvp', action='arrow', row=2, dir=-1); time.sleep(1.5); c(H, cmd='mpm_pvp', action='arrow', row=2, dir=-1); time.sleep(1.5)   # 60 s
    log('mods ask', c(H, cmd='pvp_mods', ask=1).get('reports')); time.sleep(3)
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'hub_pvp_window.png'))
    log('Start Match', c(H, cmd='mpm_pvp', action='start'))
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=300, every=2)
    t_match = st(H)['tick']; ps = c(H, cmd='pvp_status')
    check('hub PvP: match on the settlement (no transfer), the hub units fight', ps.get('arena') == 2 and st(H).get('map') == 0 and len(ps.get('units', [])) == 3, f"arena {ps.get('arena')} units {len(ps.get('units', []))}")
    t9 = time.time()
    while time.time() - t9 < 400:
        q = c(H, cmd='pvp_status')
        if not q.get('active') and q.get('matches', 0) >= 1: break
        if not q.get('resetting'):
            for x in (H, G): shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_pvp_{x}.log'))
        time.sleep(2)
    wait('match over + world back on both', lambda: all((lambda q: not q.get('active') and q.get('phase') == 0)(c(x, cmd='pvp_status')) and (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x)) for x in (H, G)), timeout=600, every=3)
    time.sleep(5)
    pe = {x: c(x, cmd='pvp_status') for x in (H, G)}
    fs = {x: c(x, cmd='fe_status') for x in (H, G)}
    back = sorted(h['hub'] for h in fs[H].get('hub_units', []))
    alive = {u['guid']: u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1}
    log('after the hub match', {x: (pe[x].get('match_winner'), pe[x].get('score')) for x in (H, G)}, 'run', fs[H].get('run'), 'hub world', fs[H].get('hub_world'))
    check('hub PvP: match over, same winner on both; the hub is back (same run, hub world, its 3 units alive)',
          pe[H].get('match_winner') == pe[G].get('match_winner') and fs[H].get('run') == run_id and fs[H].get('hub_world') and fs[G].get('hub_world')
          and all(g in alive and not alive[g]['dead'] for g in hub0), f"run {fs[H].get('run')} units {[ (g[:8], g in alive) for g in hub0]}")
    def rd(x):
        out = {}
        p = os.path.join(OUT, f'fp_pvp_{x}.log')
        for line in (open(p) if os.path.exists(p) else []):
            q = line.split()
            if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_match: out[int(q[0])] = q[1]
        return out
    a, b = rd(H), rd(G); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    check('hub PvP: fingerprints equal during the match, no desync', len(com) >= 300 and first is None and not mp(G).get('desyncs'), f'{len(com)} common, first diff {first}, desyncs {mp(G).get("desyncs")}')
    w = wait('result window', lambda: c(H, cmd='mpm_pvp').get('result') and c(H, cmd='mpm_pvp'), timeout=120, every=2)
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'hub_pvp_result.png'))
    c(H, cmd='mpm_pvp', action='press_ok'); time.sleep(3)
    check('hub PvP: result "Back to the World" keeps everyone in the hub', st(H).get('scene') == 2 and st(G).get('scene') == 2 and c(H, cmd='fe_status').get('hub_world'))


if __name__ == '__main__':
    main()
