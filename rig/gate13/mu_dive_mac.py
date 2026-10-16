#!/usr/bin/env python3
"""gate13 v13 gate (PLAN_V13 G4, My Units in a hub dive), Mac, two LOADER test copies (dsload + multiplayer +
custom-modes + test-only units-driver), hidden + muted, one rig pair lease. TEST My Units library (env DSMP_UNITS_LIB,
a copy of custom/units/test/data/launcher_library.json in the run folder); never the owner's library / saves / prefs.

  host U: Multiplayer > PvP > Start -> PvP lobby -> lobby "Bring Units" -> the new window opens (check 2) -> Cancel ->
          title. Multiplayer > Foreign Expedition (Death Off) > Start -> neutral hub lobby.
  guest W: joins by address -> lobby "Bring Units" -> the new window (check 1) -> From a save: 1 unit of its save ->
          My Units: 1 unit of the test library -> Bring. Host brings 1 unit of its own save the same way.
  host lobby Start -> hub -> Teams -> expedition with the 3 units -> fight (check 3) -> portal -> Return -> fe_end ->
  guest results window -> Apply to its save (check 4: the save unit = hub end level / exp; the My Units unit is not in
  the new save, report says it stays out) -> check 5 test library byte-identical -> check 6 owner's library + prefs.

  python3 ~/ds_mp_gate13/rig/gate13/mu_dive_mac.py [--dive SECONDS] [--host U] [--guest W]
Prints "PASS name ..." / "FAIL name ..." per check, last line "RESULT x/y pass". Out: ext/gate13/out/mudive/mac_<time>/."""
import glob, json, os, shutil, signal, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mu_lcopy as M
WT = M.WT
sys.path.insert(0, os.path.join(WT, 'ext', 'foreign_expedition'))
import fe_save as fe

A = sys.argv
arg = lambda k, d: A[A.index(k) + 1] if k in A else d
HX, GX = arg('--host', 'U'), arg('--guest', 'W')
DIVE_S = int(arg('--dive', '420'))
LANE = 'gate13_mu'
OUT = os.path.join(WT, 'ext', 'gate13', 'out', 'mudive', 'mac_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
FIX = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
LIBFIX = os.path.join(WT, 'custom/units/test/data/launcher_library.json')
BTN = json.loads(os.popen(f'python3 {WT}/ext/fepvp13/tools/mpm_ids.py {WT}').read())
SI = dict(HealthHead=12, HealthBody=13, Energy=14, Hunger=15, Level=17, Exp=18)
LOGF = open(os.path.join(OUT, 'test.log'), 'a'); results = []


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOGF.write(s + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok)))
    s = ('PASS ' if ok else 'FAIL ') + name + (f'  [{str(info)[:400]}]' if info != '' else '')
    print(s, flush=True); LOGF.write(time.strftime('%H:%M:%S ') + s + '\n'); LOGF.flush()


def wait(what, pred, timeout=600, every=2):
    t1 = time.time()
    while time.time() - t1 < timeout:
        try:
            v = pred()
        except (ConnectionError, OSError, json.JSONDecodeError) as e:
            v = None; log('  (wait', what, 'retry:', repr(e)[:80], ')')
        if v: return v
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def feed(path):
    """test copy only: every clan unit leaves home fed and rested (as hub_dive_test --feed)"""
    s = fe.load(path)
    for g, *_ in fe.clan_units(s):
        fe.comp(fe.ec_of(s)[g], 0)['StateStats'].update(Hunger=1.0, Energy=1.0)
    json.dump(s, open(path, 'w'), indent=2)
    t = time.time() + 86400; os.utime(path, (t, t))           # newest = row 0 of "From a save"
    return s


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    import lrig
    owner0 = M.fstate(M.OWNER_LIB); prefs0 = lrig.check_prefs(restore=False)
    log('owner library before', owner0, '| owner prefs identical to the backup:', prefs0)
    m = M.modules(OUT); log('modules:', m['how']); log('dylib sha', m['mp_sha'], m['cm_sha'], m['drv_sha'])
    lib = os.path.join(OUT, 'lib', 'library.json'); os.makedirs(os.path.dirname(lib)); shutil.copyfile(LIBFIX, lib)
    lib0 = M.fstate(lib)
    gs = {}
    for x, f in ((HX, 'hub_Moonlight_mp.json'), (GX, 'home_Elven.json')):
        d = os.path.join(OUT, f'game_saves_{x}'); os.makedirs(d); shutil.copy2(os.path.join(FIX, f), os.path.join(d, f))
        gs[x] = os.path.join(d, f)
    home = feed(gs[GX]); feed(gs[HX])
    common = {'DSMP_UNITS_LIB': lib, 'DSMP_CLIP_PB': 'dsmp-gate13-mu-test', 'DSRIG_FE_BACKUP': os.path.join(OUT, 'backups')}
    H = M.Copy(HX, OUT, m, dict(common, DSRIG_FE_GAME_SAVES=os.path.dirname(gs[HX])))
    G = M.Copy(GX, OUT, m, dict(common, DSRIG_FE_GAME_SAVES=os.path.dirname(gs[GX])))
    for c in (H, G):
        c.install()
        for sub in ('fe', 'mp_owners'):
            d = os.path.join(c.I, 'data', sub)
            if os.path.exists(d): os.replace(d, d + '_' + time.strftime('%Y%m%d-%H%M%S'))
        p = os.path.join(c.I, 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
    cache = os.path.join(H.I, 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
    pair = M.lease(LANE, os.getpid()); log('lease', pair, '| host', HX, 'guest', GX, '| out', OUT)
    try:
        run(H, G, gs, home, lib)
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); check('test ran to the end without an error', False, repr(e))
    finally:
        for c in (H, G): c.stop()
        log('released', M.release(pair, LANE))
        ex = {}
        for c in (H, G):
            p = os.path.join(OUT, f'{c.x}_player.log')
            if os.path.exists(p): ex[c.x] = [l.strip()[:200] for l in open(p, errors='replace') if 'Exception' in l]
        log('player.log exceptions', {k: len(v) for k, v in ex.items()}); [log('  ', k, l) for k, v in ex.items() for l in v[:4]]
        check('5 test My Units library file byte-identical before / after', M.fstate(lib) == lib0, f'{lib0} -> {M.fstate(lib)}')
        o1 = M.fstate(M.OWNER_LIB); p1 = lrig.check_prefs(restore=False)
        check("6 owner's real My Units library + prefs unchanged", o1 == owner0 and p1 == prefs0 and p1,
              f'library {owner0} -> {o1}; prefs identical before {prefs0} after {p1}')
        check('no copy on screen (guard)', not H.violations and not G.violations, H.violations + G.violations)
        n = sum(1 for _, ok in results if ok)
        print(f'RESULT {n}/{len(results)} pass', flush=True); LOGF.write(f'RESULT {n}/{len(results)} pass | out {OUT}\n')


def run(H, G, gs, home, lib):
    st = lambda c: c.rig('status')
    mp = lambda c: st(c).get('mp', {})
    press = lambda c, n: (c.rig('mpm_press', button=BTN[n]), time.sleep(1.2))[0]
    bst = lambda c: c.rig('mu_bring_state')

    def open_bring(c, tag):
        """the lobby's Bring Units button (real path: OnPressButton of the lobby window button) -> the new window"""
        wait(f'{c.x} lobby', lambda: c.rig('mpm_status').get('state') in ('lobby', 'guest_lobby'), timeout=120, every=1)
        via = None
        for _ in range(8):
            via = c.rig('mpm_press', button=BTN['FE_BRINGWIN']); time.sleep(2)
            if bst(c).get('open') == 1: break
        s = bst(c); c.cap(f'bring_{tag}')
        old = c.rig('mpm_fewin').get('mode')
        log(c.x, tag, 'press', via, '| state', s, '| old FE window mode', old)
        return s.get('open') == 1, s, via

    def kit_at_title(c):
        """loader fix 3666546 (2026-10-16 03:07): the kit takes its templates at the first title by itself. A dsload built
        before it takes them only when a kit window opens at the title, so a window opened first in a world fails ("kit not
        ready"). Check 0 records which dsload this is; when it is the old one, a Bring window is opened and closed once at the
        title (rig mu_bring_open / mu_bring_close) so checks 1-4 can still run."""
        lg = lambda: open(os.path.join(c.I, 'dsload.log'), errors='replace').read()
        own = 'ui: templates from the title' in lg()
        check(f'0 loader kit ready in a world without a title window first ({c.x}: templates taken at the title by itself)', own,
              f"dsload.dylib {time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(M.DSLOAD)))}; a dsload before loader commit 3666546 needs a kit window at the title")
        if not own:
            a = c.rig('mu_bring_open', max=1, sources=3); time.sleep(1.5); b = c.rig('mu_bring_close'); time.sleep(1)
            log(c.x, 'warm-up at the title: mu_bring_open', a, 'close', b, '| templates now', 'ui: templates from the title' in lg())

    H.start()
    wait('host title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(H)), timeout=900, every=2)
    time.sleep(8); kit_at_title(H)
    # ---- check 2: PvP lobby
    press(H, 'MULTIPLAYER'); press(H, 'HOST_C1'); time.sleep(1)
    log('host page', H.rig('mpm_status').get('state'))
    press(H, 'HOST_START')
    wait('PvP world + lobby', lambda: (lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'))(st(H))
         and H.rig('mpm_status').get('state') == 'lobby', timeout=900, every=2)
    ok, s, via = open_bring(H, 'pvp_lobby')
    check('2 Bring Units window opens in the PvP lobby (lobby button)', ok and s.get('tab') in (1, 2) and s.get('max', 0) >= 1, f'{s} via {via}')
    H.click('bring:cancel', wait=1.5)
    log('after Cancel', bst(H))
    H.rig('mp_quit', mode='title')
    wait('host back on the title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(H)), timeout=300, every=2)
    time.sleep(6)
    # ---- Foreign Expedition neutral hub, Death Off
    G.start()
    press(H, 'MULTIPLAYER'); press(H, 'HOST_FE')
    want = dict(fe=1, fe_death=0, fe_xp=1, fe_loot=1, fe_recruit=0)
    press(H, 'SETTINGS'); time.sleep(1)
    for k, b in (('fe_death', 'FED_NEXT'), ('fe_xp', 'FEX_NEXT'), ('fe_loot', 'FEL_NEXT'), ('fe_recruit', 'FER_NEXT')):
        if H.rig('mpm_status')['settings'].get(k) != want[k]: press(H, b)
    got = {k: H.rig('mpm_status')['settings'].get(k) for k in want}
    log('host page FE rules', got)
    if got != want: check('host page: Foreign Expedition, Death Off', False, got); return
    press(H, 'FE_DONE'); time.sleep(1); press(H, 'HOST_START'); time.sleep(2)
    wait('hub + host session', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)), timeout=900)
    port = H.rig('mpm_status').get('port'); log('host port', port)
    wait('guest title', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(G)), timeout=900, every=2)
    time.sleep(6); kit_at_title(G)
    press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR'); G.rig('mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
    wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
    wait('guest knows the FE mode', lambda: G.rig('mpm_fewin').get('fe_mode') == 1, timeout=90, every=1)
    # ---- check 1: hub lobby (guest, then host); picks through real (fake-mouse) clicks
    ok_g, s, via = open_bring(G, 'hub_lobby_guest')
    G.click('bring:tab_save', wait=1.2); G.click('bring:left0', wait=1.5); sg = bst(G)
    G.click('bring:tick0', wait=1.0)
    G.click('bring:tab_lib', wait=1.5); G.click('bring:tick0', wait=1.0); s2 = bst(G); G.cap('bring_guest_picked')
    log('guest picks', s2, '| save tab', sg)
    G.click('bring:bring', wait=2.0)
    ok_h, sh_, via_h = open_bring(H, 'hub_lobby_host')
    H.click('bring:tab_save', wait=1.2); H.click('bring:left0', wait=1.5); H.click('bring:tick0', wait=1.0); s3 = bst(H)
    H.click('bring:bring', wait=2.0)
    check('1 Bring Units window opens in the hub lobby (guest and host, lobby button, tabs From a save + My Units)',
          ok_g and ok_h and s.get('tab') in (1, 2), f'guest {s} | host {sh_}')
    gexp = {}
    for f in glob.glob(os.path.join(G.I, 'data', 'fe', 'exports', '*.json')):
        x = json.load(open(f)); gexp[x['manifest']['export_id']] = x
    save_e = [x for x in gexp.values() if x.get('source_file') == gs[G.x]]
    lib_e = [x for x in gexp.values() if x['manifest'].get('source') == 'library' or x['manifest'].get('library_id')]
    log('guest exports', [(k[:8], x['manifest'].get('unit_guid', '')[:8], x['manifest'].get('source'), x.get('source_file', '')[-30:]) for k, x in gexp.items()])
    rcv = wait('host received 3 units', lambda: (lambda f: f if f.get('received') == 3 else None)(H.rig('fe_status')), timeout=90, every=1)
    check('3a guest brought 1 save unit + 1 My Units unit through the window (host has 3 with its own)',
          s2.get('picked') == 2 and len(save_e) == 1 and len(lib_e) == 1 and s3.get('picked') == 1,
          f"guest picked {s2.get('picked')}, exports save {len(save_e)} library {len(lib_e)}, host picked {s3.get('picked')}, received {rcv.get('received')}")
    if not (save_e and lib_e): return
    save_home = save_e[0]['manifest']['unit_guid']; lib_home = lib_e[0]['manifest']['unit_guid']
    lib_eid = lib_e[0]['manifest']['export_id']
    H.cap('lobby_brought_host')
    # ---- the hub
    press(H, 'START')
    wait('both peers in the hub', lambda: H.rig('fe_status').get('hub_world') and G.rig('fe_status').get('hub_world')
         and st(H).get('det', {}).get('locked') and st(G).get('det', {}).get('locked'), timeout=600, every=2)
    fh = H.rig('fe_status'); hub_units = fh['hub_units']; run_id = fh['run']
    gu = G.rig('fe_status')['uid']
    mine = {H.x: [h['hub'] for h in hub_units if h['player'] != gu], G.x: [h['hub'] for h in hub_units if h['player'] == gu]}
    hub_of = {h['home']: h['hub'] for h in hub_units}
    log('hub units', hub_units)
    lib_hub = hub_of.get(lib_home); save_hub = hub_of.get(save_home)
    check('3b hub built with 3 units, the My Units unit among them', bool(run_id) and len(hub_units) == 3 and lib_hub and save_hub, f'{len(hub_units)} units, run {run_id}')
    wait('both running', lambda: not mp(H).get('paused') and not mp(G).get('paused'), timeout=180)
    tick0 = st(H)['tick']
    s0 = {g: (H.rig('fe_stats', guid=g).get('stats') or [0] * 20) for g in mine[H.x] + mine[G.x]}
    log('hub start level/exp/hp', {g[:8]: (v[SI['Level']], v[SI['Exp']], v[SI['HealthBody']]) for g, v in s0.items()})
    tm = lambda c, **k: c.rig('ui_teams', **k)
    r0 = tm(H, action='open'); tm(G, action='open'); time.sleep(3)
    if r0.get('error') == 'unknown cmd':
        why = ("PRODUCT BUG (Mac loader): dsload opens modules RTLD_NOW|RTLD_LOCAL (loader/start/mac/dsl_start_mac.c:51) and the "
               "multiplayer module finds its own ui_teams_* with dlsym(RTLD_DEFAULT) (src/ui_game.c teams_lookup, src/mp_core.c "
               "DSNET_EXPEDITION_START): NULL from a RTLD_LOCAL image, so the Teams screen / expedition start code is not reached; "
               "rig ui_teams -> " + json.dumps(r0))
        check('3c the dive plays (Death Off) with the save unit + the My Units unit', False, why)
        check("4 after the dive: save unit's result applied, My Units unit's result NOT in the save", False, 'not reached: the dive could not start (3c)')
        return
    for g in mine[H.x]: tm(H, action='click', guid=g, button=1); time.sleep(1)
    for g in mine[G.x]: tm(G, action='click', guid=g, button=1); time.sleep(1)
    tm(H, action='close'); tm(G, action='close'); time.sleep(2)
    tm(H, action='open'); time.sleep(1); log('go', tm(H, action='go'))
    wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(H)), timeout=60)
    tm(H, action='stock_select', units=','.join(mine[H.x] + mine[G.x])); time.sleep(1); tm(H, action='stock_next')
    wait('proposer stock step', lambda: tm(H)['stock'].get('state') == 1, timeout=60)
    tm(H, action='stock_accept')
    wait('guest receiver', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(G)), timeout=90)
    if tm(G).get('stock', {}).get('open'):
        tm(G, action='stock_next'); wait('guest summary', lambda: tm(G).get('prop', {}).get('shown'), timeout=60)
    tm(G, action='approve')
    wait('both in the dungeon', lambda: all(st(c).get('map', 0) != 0 and not st(c).get('busy') for c in (H, G)), timeout=600, every=3)
    t_dg = st(H)['tick']; log('in the dungeon, map', st(H).get('map'), 'tick', t_dg)

    def order_move(c, guid, cell):
        c.rig('ord_hover', n=0)
        r1 = c.rig('ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
        r2 = c.rig('ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
        return bool(r1.get('ok') and r2.get('ok'))

    def live(c):
        return {u['guid']: u for u in c.rig('list_units')['units'] if u['faction'] == 1 and not u['dead'] and not u.get('downed') and 'cell' in u}

    t_end = time.time() + DIVE_S; t_min = time.time() + 90; hits = {}
    while time.time() < t_end:
        time.sleep(15)
        if st(G).get('map', 1) == 0 or H.rig('ui_result').get('open'): break
        us = H.rig('list_units')['units']
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        for c in (H, G):
            lv = live(c)
            for g in mine[c.x]:
                u = lv.get(g)
                if u and enemies and u.get('task') == 0:
                    e = min(enemies, key=lambda e: d2(u, e)); order_move(c, g, e['cell'])
        fs = {g: H.rig('fe_stats', guid=g).get('stats') or [0] * 20 for g in mine[H.x] + mine[G.x]}
        for g in fs:
            hits[g] = (fs[g][SI['Exp']] > s0[g][SI['Exp']] or fs[g][SI['Level']] > s0[g][SI['Level']] or fs[g][SI['HealthBody']] < s0[g][SI['HealthBody']]
                       or fs[g][SI['HealthHead']] < s0[g][SI['HealthHead']])
        log('dungeon tick', st(H)['tick'], '| enemies', len(enemies), '| exp/hp', {g[:8]: (fs[g][SI['Exp']], round(fs[g][SI['HealthBody']], 2)) for g in fs},
            '| fought', {g[:8]: v for g, v in hits.items()})
        if time.time() > t_min and hits.get(lib_hub) and hits.get(save_hub) and any(fs[g][SI['Exp']] > s0[g][SI['Exp']] for g in (lib_hub, save_hub)):
            log('both guest units fought, XP gained: going home'); break
    t_fight = st(H)['tick']
    H.cap('dungeon_host')
    check('3c the dive plays (Death Off): ticks advance in the dungeon, the save unit and the My Units unit fight (XP or HP changed)',
          t_fight - t_dg > 300 and hits.get(lib_hub) and hits.get(save_hub), f'ticks {t_dg} -> {t_fight}, fought {dict((g[:8], v) for g, v in hits.items())}')
    # ---- back to the base: portal -> Return screen -> Return -> confirm (hub_dive_test path)
    if st(G).get('map', 1) != 0:
        bl = G.rig('ui_buildings', map=st(G).get('map', 1), subtype=2); game_over = bool(H.rig('ui_result').get('open'))
        pc = (int(bl['buildings'][0]['x']), int(bl['buildings'][0]['y'])) if bl.get('buildings') else None
        log('portal', pc, 'game over', game_over)
        if pc and not game_over:
            for c in (G, H):
                for g in mine[c.x]:
                    if g in live(c): order_move(c, g, pc)
            t9 = time.time()
            while time.time() - t9 < 180:
                time.sleep(10)
                allu = {u['guid']: u for c in (G, H) for u in c.rig('list_units')['units']}
                far = [g for c in (G, H) for g in mine[c.x] if g in allu and not allu[g]['dead'] and (not allu[g].get('cell') or abs(allu[g]['cell'][0] - pc[0]) + abs(allu[g]['cell'][1] - pc[1]) > 3)]
                log('gathering at the portal, still away', [g[:8] for g in far])
                if not far: break
                for c in (G, H):
                    for g, u in live(c).items():
                        if g in mine[c.x] and u.get('task') == 0 and abs(u['cell'][0] - pc[0]) + abs(u['cell'][1] - pc[1]) > 3: order_move(c, g, pc)
            tries = [(c, g) for c in (H, G) for g in mine[c.x]]
            for k in range(12):
                c, g = tries[k % len(tries)]
                if g not in live(c): continue
                log(c.x, 'portal use', g[:8], c.rig('ui_interact', unit=g, building=bl['buildings'][0]['guid'], map=st(c).get('map', 1)))
                t8 = time.time()
                while time.time() - t8 < 20 and not H.rig('ui_result').get('open'): time.sleep(2)
                if H.rig('ui_result').get('open'): break
    if st(H).get('map') != 0:
        rs = wait('return screen on both', lambda: (lambda a, b: (a, b) if a.get('open') and b.get('open') else None)(H.rig('ui_result'), G.rig('ui_result')), timeout=900, every=3)
        P = H if rs[0]['chooser'] == 0 else G
        P.rig('ui_result', action='return'); time.sleep(3)
        for k in range(4):
            P.rig('ui_result', action='confirm'); time.sleep(4)
            if not P.rig('ui_result')['open']: break
    wait('both on the base map', lambda: all(st(c).get('map') == 0 and not st(c).get('busy') for c in (H, G)), timeout=900, every=3)
    time.sleep(8)
    s_end = {g: H.rig('fe_stats', guid=g).get('stats') for g in (save_hub, lib_hub)}
    log('hub end level/exp', {g[:8]: (v[SI['Level']], v[SI['Exp']]) if v else None for g, v in s_end.items()})
    # ---- end + results + apply (guest)
    log('fe_end', H.rig('fe_end'))
    gres = os.path.join(G.I, 'data', 'fe', 'results', f'{run_id}.json')
    wait('guest results', lambda: os.path.exists(gres), timeout=120)
    res = json.load(open(gres)); shutil.copy2(gres, os.path.join(OUT, 'guest_results.json'))
    hub_end = fe.load(os.path.join(H.I, 'data', 'fe', 'runs', run_id, 'hub_end.json'))
    log('results units', [(u['home_guid'][:8], u.get('hub_guid', '')[:8], u['export_id'][:8], u['status'], u.get('level_after')) for u in res['units']])
    w = wait('results window on the guest', lambda: (lambda w_: w_ if w_.get('mode') == 2 else None)(G.rig('mpm_fewin')), timeout=120)
    for _ in range(w.get('ntarget', 0)):
        if G.rig('mpm_fewin').get('target') == gs[G.x]: break
        G.rig('mpm_click', button=BTN['FE_TGT_NEXT']); time.sleep(0.7)
    log('target', G.rig('mpm_fewin').get('target'))
    G.rig('mpm_click', button=BTN['FE_OK']); time.sleep(4)
    w = G.rig('mpm_fewin'); log('after Apply', {k: w.get(k) for k in ('applied', 'conflict', 'err', 'msg')})
    G.cap('results_applied_guest')
    claims = json.load(open(os.path.join(G.I, 'data', 'fe', 'claims.json')))['claims'].get(run_id, {})
    new_p = claims.get('file', '')
    if not (w.get('applied') and new_p and os.path.exists(new_p)):
        check('4 results applied to the save (window Apply)', False, f"applied {w.get('applied')} file {new_p} msg {w.get('msg')}"); return
    shutil.copy2(new_p, os.path.join(OUT, 'guest_new_home.json'))
    new = fe.load(new_p)
    ru = {u['home_guid']: u for u in res['units']}
    pe = fe.comp(fe.ec_of(new)[save_home], 0)['ProgressionStats'] if save_home in fe.ec_of(new) else {}
    # a unit that fell in the dive is not in hub_end's entity list: its end state is the results file's level/exp_after
    rs = ru.get(save_home, {})
    ph = fe.comp(fe.ec_of(hub_end)[save_hub], 0)['ProgressionStats'] if save_hub in fe.ec_of(hub_end) \
        else {'Level': rs.get('level_after'), 'Exp': rs.get('exp_after')}
    p0 = fe.comp(fe.ec_of(home)[save_home], 0)['ProgressionStats']
    save_ok = rs.get('status') in ('alive', 'dead') and bool(pe) and pe.get('Level') == ph['Level'] and pe.get('Exp') == ph['Exp'] and \
        (ph['Exp'] != p0['Exp'] or ph['Level'] != p0['Level'])
    ec_new, ec_home = fe.ec_of(new), fe.ec_of(home)
    lib_in = [g for g in (lib_home, lib_hub) if g in ec_new or fe.side_of(new, g) is not None]
    nh, nn = len(fe.clan_units(home)), len(fe.clan_units(new))
    rep = ' | '.join(w.get('msg') or '' for _ in [0])
    lib_ok = lib_home in ru and not lib_in and nn == nh
    check('4 after the dive: the save unit\'s result applied (level / exp = hub end), the My Units unit\'s result is NOT in the save',
          save_ok and lib_ok,
          f"save unit {save_home[:8]} ({rs.get('status')}, Death Off): home L{p0['Level']} exp {p0['Exp']} -> hub end L{ph['Level']} exp {ph['Exp']} -> new save L{pe.get('Level')} exp {pe.get('Exp')}; "
          f"My Units unit {lib_home[:8]} (hub {lib_hub[:8]}) in results {lib_home in ru}, in the new save {bool(lib_in)}; clan units {nh} -> {nn}; window: {rep[:200]}")


if __name__ == '__main__':
    main()
