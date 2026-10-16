#!/usr/bin/env python3
"""fepvp13: Foreign Expedition dive in the NEUTRAL HUB (copy of ext/foreign_expedition/tests/fe_dive_test.py; the host page
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
OUT = os.path.join(W, 'ext', 'fepvp13', 'out', 'hubdive_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
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
    # recruiting in the hub (host option): Off = the recruitment window never opens and a replayed recruit order is refused
    # on every peer; On = allowed like a normal multiplayer world
    fa = {x: c(x, cmd='fe_status').get('recruit_allowed') for x in (H, G)}
    check(f'hub: recruit_allowed = {RECRUIT} on both peers (host option travels in the campaign guid)', all(v == RECRUIT for v in fa.values()), str(fa))
    mk = {x: os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) for x in (H, G)}
    cell = next(u['cell'] for u in c(G, cmd='list_units')['units'] if u['guid'] in mine[G] and 'cell' in u)
    log('recruit inject', c(G, cmd='ord_inject', ev='CandidateRecruitRequested', **{'i32@16': 0, 'i32@20': 0, 'i32@24': cell[0] + 1, 'i32@28': cell[1], 'i32@32': 1}))
    time.sleep(6)
    tail = {x: open(os.path.join(rig.idir(x), 'rig.log'), 'rb').read()[mk[x]:].decode('utf-8', 'replace') for x in (H, G)}   # byte offsets
    for x in (H, G): [log('  ', x, l[:200]) for l in tail[x].splitlines() if 'recruit' in l.lower() or 'ord:' in l][:12]
    if RECRUIT == 0:
        seen = {x: 'refused: Foreign Expedition hub' in tail[x] for x in (H, G)}
        check('hub Off: a recruit order is refused on both peers (same replayed decision)', all(seen.values()), str(seen))
    else:
        seen = {x: 'refused: Foreign Expedition hub' in tail[x] for x in (H, G)}
        check('hub On: a recruit order is NOT refused by the Foreign Expedition rule', not any(seen.values()), str(seen))
    finds = {sub: c(G, cmd='ui_find', sub=sub, all=1) for sub in ('recruit', 'Recruit', 'tavern', 'hire', 'Population')}   # where the game offers recruiting
    json.dump(finds, open(os.path.join(OUT, 'ui_find_recruit.json'), 'w'), indent=1)
    r = c(G, cmd='ui_popup', type=8); time.sleep(2)
    c(G, cmd='ui_capture', path=os.path.join(OUT, f'hub_guest_recruit_popup_{RECRUIT}.png'))
    if RECRUIT == 0: check('hub Off: the recruitment window is not offered (popup 8 never opens)', r.get('active') == 0, str(r))
    else: check('hub On: the recruitment window opens', r.get('active') == 1, str(r))
    c(G, cmd='ui_escape'); time.sleep(1)
    gl = {x: open(os.path.join(rig.idir(x), 'rig.log'), 'rb').read()[mk0[x]:].decode('utf-8', 'replace') for x in (H, G)}
    hid = {x: 'Guild Contact button hidden' in gl[x] for x in (H, G)}
    check(f"hub: Guild Contact HUD button {'hidden' if RECRUIT == 0 else 'shown'} on both peers", all(v == (RECRUIT == 0) for v in hid.values()), str(hid))
    c(G, cmd='ui_capture', path=os.path.join(OUT, 'hub_guest_hud.png'))
    s_hub0 = {g: c(H, cmd='fe_stats', guid=g)['stats'] for g in mine[G]}   # index 18 Level, 19 Exp (1-based StatType -> [17], [18])
    log('hub start level/exp', {g[:8]: (s[17], s[18]) for g, s in s_hub0.items()})

    # expedition with the 3 moved units (Teams window, stock steps, approve)
    tm(H, action='open'); tm(G, action='open'); time.sleep(3)
    # the hub units are already owned by their bringers: a left click would RELEASE them; right click marks them for the run
    for g in mine[H]: tm(H, action='click', guid=g, button=1); time.sleep(1)
    for g in mine[G]: tm(G, action='click', guid=g, button=1); time.sleep(1)
    tm(H, action='close'); tm(G, action='close'); time.sleep(2)
    tm(H, action='open'); time.sleep(1); log('go', tm(H, action='go'))
    wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(H)), timeout=60, every=2)
    tm(H, action='stock_select', units=','.join(mine[H] + mine[G])); time.sleep(1); tm(H, action='stock_next')
    wait('proposer stock step', lambda: tm(H)['stock'].get('state') == 1, timeout=60, every=2)
    tm(H, action='stock_accept')
    wait('guest receiver', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(G)), timeout=90, every=2)
    if tm(G).get('stock', {}).get('open'):
        tm(G, action='stock_next'); wait('guest summary', lambda: tm(G).get('prop', {}).get('shown'), timeout=60, every=2)
    tm(G, action='approve')
    wait('both in the dungeon', lambda: all(st(x).get('map', 0) != 0 and not st(x).get('busy') for x in (H, G)), timeout=600, every=3)
    check('dive: both peers in the dungeon with the moved units', True, f"map {st(H).get('map')}")
    c(H, cmd='ui_capture', path=os.path.join(OUT, 'dungeon_host.png'))
    t_end = time.time() + DIVE_S; picked = set(); exp0 = [s_hub0[g][18] for g in mine[G]]
    while time.time() < t_end:
        time.sleep(15)
        dmap = st(G).get('map', 1)
        if dmap == 0 or c(H, cmd='ui_result').get('open'): break
        f = c(H, cmd='fe_status')
        alive = {u['guid']: u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1}
        up = [g for g in mine[G] if g in alive and not alive[g]['dead'] and not alive[g].get('downed')]
        items = [i for i in c(G, cmd='ui_items', map=dmap).get('items', []) if i['guid'] not in picked and i.get('owner', -1) < 0]
        us = c(H, cmd='list_units')['units']
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        for x in (H, G):                                        # each side sends its own idle units to the nearest enemy
            lv = live(x)
            for g in mine[x]:
                u = lv.get(g)
                if u and enemies and u.get('task') == 0:
                    e = min(enemies, key=lambda e: d2(u, e)); log('fight:', x, g[:8], '->', e['cell'], order_move(x, g, e['cell']))
        if up and items:                                        # the guest's unit picks up a dungeon item (PickUpItemRequested, mp order)
            it = items[0]; picked.add(it['guid'])
            log('pickup', it, c(G, cmd='ui_story', action='pickup', unit=up[0], item=it['guid']))
        fs = {g: c(H, cmd='fe_stats', guid=g).get('stats') or [0] * 20 for g in mine[H] + mine[G]}
        exp = [fs[g][SI['Exp']] for g in mine[G]]
        log('dungeon: loot tags', f.get('loot'), '| items on map', len(items), '| moved units', [(g[:8], alive.get(g, {}).get('hp'), alive.get(g, {}).get('dead'),
            alive.get(g, {}).get('task'), 'hunger %.2f energy %.2f' % (fs[g][SI['Hunger']], fs[g][SI['Energy']])) for g in mine[H] + mine[G]],
            '| guest exp', exp, '| enemies', sum(1 for u in c(H, cmd='list_units')['units'] if u['faction'] not in (1,) and not u['dead']))
        if not WEAK and f.get('loot', 0) > 0 and any(e > e0 for e, e0 in zip(exp, exp0)): log('XP and loot: going home'); break
    c(H, cmd='ui_capture', path=os.path.join(OUT, 'dungeon_host_end.png'))
    # back to the base: portal -> Return screen -> Return -> confirm
    if st(G).get('map', 1) != 0:
        bl = c(G, cmd='ui_buildings', map=st(G).get('map', 1), subtype=2); log('portals', bl)
        game_over = bool(c(H, cmd='ui_result').get('open'))      # every unit down: the stock game over opened the return screen
        alive_end = [] if game_over else [g for x in (G, H) for g in mine[x] if g in live(x)]
        if game_over: log('game over in the dungeon (every unit down): no portal walk')
        pc = (int(bl['buildings'][0]['x']), int(bl['buildings'][0]['y'])) if bl.get('buildings') else None
        if pc and not game_over:                                # every live unit gathers at the portal first (units away from it stay behind)
            for x in (G, H):
                for g in mine[x]:
                    if g in live(x): log(x, 'to portal', g[:8], order_move(x, g, pc))
            t9 = time.time()
            while time.time() - t9 < 180:
                time.sleep(10)
                allu = {u['guid']: u for x in (G, H) for u in c(x, cmd='list_units')['units']}
                far = [(g[:8], allu.get(g, {}).get('cell')) for x in (G, H) for g in mine[x]
                       if g in allu and not allu[g]['dead'] and (not allu[g].get('cell') or abs(allu[g]['cell'][0] - pc[0]) + abs(allu[g]['cell'][1] - pc[1]) > 3)]
                log('gathering at the portal', pc, 'still away', far)
                if not far: break
                for x in (G, H):
                    for g, u in live(x).items():
                        if g in mine[x] and u.get('task') == 0 and abs(u['cell'][0] - pc[0]) + abs(u['cell'][1] - pc[1]) > 3: order_move(x, g, pc)
            c(H, cmd='ui_capture', path=os.path.join(OUT, 'dungeon_at_portal_host.png'))
        # ONE live unit uses the portal (two at the same step open nothing); sometimes the use opens nothing (unit busy in
        # a fight?): try again every 20 s with the next unit
        tries = [(x, g) for x in (H, G) for g in mine[x]]
        for k in range(0 if game_over else 12):
            x, g = tries[k % len(tries)]
            if g not in live(x) or not pc: continue
            log(x, 'portal use', g[:8], c(x, cmd='ui_interact', unit=g, building=bl['buildings'][0]['guid'], map=st(x).get('map', 1)))
            t8 = time.time()
            while time.time() - t8 < 20 and not c(H, cmd='ui_result').get('open'): time.sleep(2)
            if c(H, cmd='ui_result').get('open'): break
            if k == 0: c(H, cmd='ui_capture', path=os.path.join(OUT, 'portal_use_nothing_host.png'))
    if st(H).get('map') != 0:
        rs = wait('return screen on both', lambda: (lambda a, b: (a, b) if a.get('open') and b.get('open') else None)(c(H, cmd='ui_result'), c(G, cmd='ui_result')), timeout=900, every=3)
        P = H if rs[0]['chooser'] == 0 else G
        c(P, cmd='ui_result', action='return'); time.sleep(3)
        log('return screen', {k: rs[0].get(k) for k in rs[0] if k not in ('ok',)})
        c(P, cmd='ui_capture', path=os.path.join(OUT, 'return_screen.png'))
        for k in range(4):
            log('confirm', k + 1, c(P, cmd='ui_texts').get('texts', '')[:0] if False else '', c(P, cmd='ui_result', action='confirm')); time.sleep(4)
            if not c(P, cmd='ui_result')['open']: break
    wait('both on the base map', lambda: all(st(x).get('map') == 0 and not st(x).get('busy') for x in (H, G)), timeout=900, every=3)
    time.sleep(10)
    lu = {u['guid']: u for u in c(H, cmd='list_units', map=0)['units']}
    log('base after return: moved units', [(g[:8], g in lu, lu.get(g, {}).get('dead'), lu.get(g, {}).get('hp')) for g in mine[H] + mine[G]], '| clan units on base', sum(1 for u in lu.values() if u['faction'] == 1))
    c(H, cmd='ui_capture', path=os.path.join(OUT, 'base_after_return_host.png'))
    if 'alive_end' in dir() or True:
        ae = locals().get('alive_end', [])
        if not locals().get('game_over'):
            check('return: every moved unit alive at the portal is on the base', ae and all(g in lu and not lu[g]['dead'] for g in ae), f'{len(ae)} alive at the end')
    n, first = fp_equal_from(tick0)
    check('dive: fingerprints equal over the whole run', n >= 100 and first is None and not mp(G).get('desyncs'), f'{n} common, first diff {first}')
    s_hub1 = {g: c(H, cmd='fe_stats', guid=g).get('stats') for g in mine[G]}
    gained = {g: (s_hub0[g][17], s_hub0[g][18], s[17] if s else None, s[18] if s else None) for g, s in s_hub1.items()}
    log('hub level/exp before -> after the dive', {g[:8]: v for g, v in gained.items()})
    if not WEAK: check('dive: the guest\'s units gained XP in the hub', any(v[3] is not None and (v[3] > v[1] or v[2] > v[0]) for v in gained.values()), str(gained))

    # end + results (guest present) + apply
    r = c(H, cmd='fe_end'); log('fe_end', r)
    gres = os.path.join(rig.idir(G), 'data', 'fe', 'results', f'{run_id}.json')
    wait('guest results', lambda: os.path.exists(gres), timeout=120, every=2)
    res = json.load(open(gres))
    hub_end = fe.load(os.path.join(rig.idir(H), 'data', 'fe', 'runs', run_id, 'hub_end.json'))
    loot = json.load(open(os.path.join(rig.idir(H), 'data', 'fe', 'runs', run_id, 'loot.json'))) if os.path.exists(os.path.join(rig.idir(H), 'data', 'fe', 'runs', run_id, 'loot.json')) else {}
    gl = [k for k, v in loot.items() if v.get('player_uid') == gu]
    log('loot tagged in the dungeon: all', len(loot), 'guest', len(gl), '| result items', len(res['items']), '| unit status', [(u['home_guid'][:8], u['status'], u.get('level_after')) for u in res['units']])
    check('results: items = the guest\'s dungeon loot that still exists (no hub / crafted items)', set(res['items']) <= set(gl), f"{len(res['items'])} of {len(gl)}")
    # F6 results window (board FEResultsA): opens by itself on the guest over the Esc menu; pick the target; Apply
    w = wait('results window open on the guest', lambda: (lambda w_: w_ if w_.get('mode') == 2 else None)(c(G, cmd='mpm_fewin')), timeout=120, every=2)
    c(G, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'results_window_guest.png'))
    for _ in range(w.get('ntarget', 0)):
        if c(G, cmd='mpm_fewin').get('target') == gsave: break
        c(G, cmd='mpm_click', button=B['FE_TGT_NEXT']); time.sleep(0.7)
    log('target', c(G, cmd='mpm_fewin').get('target'))
    c(G, cmd='mpm_click', button=B['FE_OK']); time.sleep(4)
    w = c(G, cmd='mpm_fewin'); log('after Apply', {k: w.get(k) for k in ('applied', 'conflict', 'err', 'msg')})
    c(G, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'results_applied_guest.png'))
    claims = json.load(open(os.path.join(rig.idir(G), 'data', 'fe', 'claims.json')))['claims'].get(run_id, {})
    r = {'ok': bool(w.get('applied')), 'file': claims.get('file', ''), 'backup': max(glob.glob(os.path.join(OUT, 'backups', '*')) or [''], key=lambda p: os.path.getmtime(p) if p else 0), 'report': w.get('msg')}
    check('F6 results window: Apply wrote the new save (window says Brought home)', r['ok'] and os.path.exists(r['file']), str(r))
    c(G, cmd='mpm_click', button=B['FE_OK']); time.sleep(1)
    c(G, cmd='mp_quit', mode='title'); wait('guest on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=120)
    # title: Multiplayer > Expedition results lists it as brought home; opening it shows "Already Brought Home"
    time.sleep(5); c(G, cmd='mpm_press', button=B['MULTIPLAYER']); time.sleep(2)
    c(G, cmd='mpm_click', button=B['FE_RESLIST']); time.sleep(2)
    c(G, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'results_list_title.png'))
    c(G, cmd='mpm_click', button=B['FE_RES0']); time.sleep(1.5)
    w = c(G, cmd='mpm_fewin'); c(G, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'results_already_applied.png'))
    check('F6 results list on the title: applied run refused with "Already Brought Home"', 'Already Brought Home' in (w.get('msg') or ''), (w.get('msg') or '')[:80])
    c(G, cmd='mpm_click', button=B['FE_CANCEL']); c(G, cmd='mpm_click', button=B['FE_CANCEL'])
    check('apply: backup first + new save', r.get('ok') and os.path.isdir(r.get('backup', '')), str(r.get('report')))
    if not r.get('ok'): return
    new = fe.load(r['file'])
    for u in res['units']:
        H_ = u['home_guid']
        if u['status'] != 'alive' and DEATH:
            check(f'{H_[:8]} died in the hub, death permanent: unit and its gear removed at home', fe.side_of(new, H_) is None
                  and not any(g in fe.ec_of(new) for g in fe.item_guids_of(fe.ec_of(home)[H_]))); continue
        if u['status'] != 'alive':
            snap = u.get('closure_end')
            check(f'{H_[:8]} died in the hub, death NOT permanent: home unit = its last multiplayer save state',
                  snap is not None and fe.side_of(new, H_) is not None and
                  fe.comp(fe.ec_of(new)[H_], 0)['ProgressionStats'] == fe.comp(snap['unit']['components'], 0)['ProgressionStats'],
                  f"from_snapshot {u.get('from_snapshot')}, level {u.get('level_after')}"); continue
        pe = fe.comp(fe.ec_of(new)[H_], 0)['ProgressionStats']; pe_hub = fe.comp(fe.ec_of(hub_end)[u['hub_guid']], 0)['ProgressionStats']
        check(f'{H_[:8]} home: level / exp = the hub end state', pe['Level'] == pe_hub['Level'] and pe['Exp'] == pe_hub['Exp'],
              f"home before L{fe.comp(fe.ec_of(home)[H_], 0)['ProgressionStats']['Level']} exp {fe.comp(fe.ec_of(home)[H_], 0)['ProgressionStats']['Exp']} -> L{pe['Level']} exp {pe['Exp']}")
    ec_new, ec_home = fe.ec_of(new), fe.ec_of(home)
    added = [g for g in ec_new if g not in ec_home]
    check('apply: returned loot items exist in the new home save', len(added) >= len(res['items']), f"{len(added)} new entities, {len(res['items'])} result items")
    c(G, cmd='load_save', file=r['file'], det=0)
    wait('new home loaded', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('map') == 0 and s.get('tick', -1) > 0)(st(G)), timeout=400)
    c(G, cmd='set_speed', speed=0); time.sleep(2)
    for u in res['units']:
        if u['status'] != 'alive' and DEATH: continue
        s = c(G, cmd='fe_stats', guid=u['home_guid']).get('stats')
        log(f"new home in game {u['home_guid'][:8]}: level {s[17] if s else None} exp {s[18] if s else None} maxhp {s[21] if s else None}")
        check(f"{u['home_guid'][:8]}: the game shows the hub level at home", s and s[17] == u.get('level_after'), str(u.get('level_after')))
    c(G, cmd='ui_capture', path=os.path.join(OUT, 'new_home_guest.png'))


if __name__ == '__main__':
    main()
