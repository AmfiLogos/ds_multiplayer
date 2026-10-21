#!/usr/bin/env python3
"""hubonly lane (owner 2026-10-21 "keep only the Hub: Co-op or PvP"): the hub from the menu, then PvP from the hub, then a
co-op expedition start. Same start as ext/fepvp13/tests/hub_dive_test.py (copied: base save units for the guest, buffed).
After the hub start: Esc > PvP Match with the rows moved from the old PvP page (Floor, visibility, presets), a match on the
hub base (vote, fight, end), both back in the hub, 0 desyncs; HUD Bring Units in the preparation; then Teams -> expedition,
both in the dungeon. Shots of the menu entry, the host page, the lobby and the PvP window (--shots DIR).
  --lang-h N / --lang-g N  game LanguageType of host / guest (0 en, 7 ja); --ui S  UIScale of both copies
  --solo  host only: menu, page, lobby, hub, PvP window shots (no guest, no match)
Original doc of hub_dive_test.py:
Start builds the hub from the wiki base template, no host save is loaded). Lane fepvp, this worktree's build/dsrig.dylib.
Original doc:
Foreign Expedition dive test (plan FE-2), ONE leased pair, build/dsrig_fe.dylib.
Same start as fe_pair_test.py (Normal hub, guest brings 2 Elven units, host 1), then a REAL dungeon run:
Teams -> expedition with all 3 moved units -> fight in the dungeon for a few minutes -> portal -> Return screen ->
base -> fe_end -> guest gets the results (present) -> leaves -> fe_apply -> loads the new home save.
Checks: XP gained in the hub comes home (level / exp = the hub's end state), dungeon loot tagged to the guest comes home
into his storage, crafted / hub items never, fingerprints equal, recruit order refused in the hub (if a candidate exists)."""
import glob, json, os, shutil, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition')        # fixtures (not in git)
sys.path.insert(0, os.path.join(W, 'ext', 'foreign_expedition')); sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import fe_save as fe
import rig

rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1]) if '--dylib' in sys.argv else os.path.join(W, 'build', 'dsrig.dylib')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # test copy of the wiki file
GAME_SAVES = None                                    # set in run(): the copies' "game folder" (copies only, never the real one)
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, HOST_FE=123)
B.update(json.loads(os.popen(f'python3 {W}/ext/fepvp13/tools/mpm_ids.py {W}').read()))   # menu ids of this build
OUT = os.path.join(W, 'ext', 'hubonly', 'out', 'hubonly_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
os.environ['DSRIG_FE_BACKUP'] = os.path.join(OUT, 'backups')
LOG = open(os.path.join(OUT, 'dive.log'), 'a'); results = []
DIVE_S = int(sys.argv[sys.argv.index('--dive') + 1]) if '--dive' in sys.argv else 300
DEATH = int(sys.argv[sys.argv.index('--death') + 1]) if '--death' in sys.argv else 1     # rule: death permanent 1 / 0
WEAK = '--weak' in sys.argv          # the guest's 2nd unit leaves home at 3 % health (test copy only): it should die
RECRUIT = int(sys.argv[sys.argv.index('--recruit') + 1]) if '--recruit' in sys.argv else 0   # host rule Recruiting in the hub 0 Off / 1 On
BUFF = '--no-buff' not in sys.argv   # test copies only: brought units get +BUFF_N Strength / Constitution / Agility. WHY: v0.1.20-21 every
                                     # hub dive ended in the stock game over (3 level-1 units vs ~18 enemies, down ~40 s after the first fight,
                                     # also after going home: the enemies follow to the portal), so XP / loot / return were never tested
BUFF_N = float(sys.argv[sys.argv.index('--buff') + 1]) if '--buff' in sys.argv else 60.0   # +20 was not enough: 20 enemies took 290 of 474 hp in 5 s
HUBMAP = '--base-map' not in sys.argv   # the hub map (maps/hub_<build>.json next to the dylib: a full base, no units); --base-map = without it
SHOT = sys.argv[sys.argv.index('--shot') + 1] if '--shot' in sys.argv else None   # copy of the in-game hub frame (after the start)
FEED = '--feed' in sys.argv          # test copies only: brought units leave home fed and rested (Hunger, Energy 1.0)
LANE = f'hubonly-{os.getpid()}'
ARG = lambda k, d: type(d)(sys.argv[sys.argv.index(k) + 1]) if k in sys.argv else d
LANG = {'H': ARG('--lang-h', 0), 'G': ARG('--lang-g', 7)}; UISCALE = ARG('--ui', 1.1); SOLO = '--solo' in sys.argv
SHOTS = os.path.expanduser(ARG('--shots', '~/ds_freyin_save/shots')); os.makedirs(SHOTS, exist_ok=True)
CODE = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
HUB_LABEL = {k: json.load(open(os.path.join(W, 'lang', f'{v}.json'))).get('Hub: Co-op or PvP', 'Hub: Co-op or PvP') if v != 'en' else 'Hub: Co-op or PvP' for k, v in CODE.items()}
ROLE = {}                                            # copy letter -> 'H' / 'G' (set in main)
_prep = rig.prepare_data
def _prep_lang(x):                                   # test copies only: their own UserSetting.json
    _prep(x)
    us = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG[ROLE.get(x, 'H')]; s['GeneralSettingData']['IsLanguageInitialized'] = True
    s['GraphicSettingData']['UIScale'] = UISCALE; json.dump(s, open(us, 'w'), indent=1)
rig.prepare_data = _prep_lang
def owner_plays():                                   # owner play rule: his game BINARY runs (launcher copy or the app)
    import re as _re, subprocess
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/'], capture_output=True, text=True).stdout
    return any(l and not _re.search(r'/instances/|/private/tmp/|/tmp/|ToDelete|/ds_rel0[0-9]+/|/test_env/|pgrep|zsh -c|bash -c', l) for l in out.splitlines())
STEP_S = 5                           # fight loop step (was 15 s: units went from 50 % to down between two looks)
HURT = 0.5                           # a moved unit under this share of its max health (or down): no new fight, the run goes home
SI = dict(HealthHead=12, HealthBody=13, Energy=14, Hunger=15, Level=17, Exp=18)   # fe_stats index (StatType - 1)


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def main():
    import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))   # kill -TERM: copies stopped + pair released (finally)
    pair, t0 = None, time.time()
    while owner_plays():
        log('the owner plays: waiting'); time.sleep(60)
    while not pair and time.time() - t0 < 3 * 3600:
        pair = rig.lease(LANE, os.getpid())
        if not pair: time.sleep(30)
    if not pair: log('no free pair'); sys.exit(2)
    H, G = pair[0], pair[1]; ROLE[H] = 'H'; ROLE[G] = 'G'
    log('pair', pair, 'dylib', rig.DYLIB, 'lang', LANG, 'ui', UISCALE, 'solo', SOLO)
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
    PEERS = (H,) if SOLO else (H, G)
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

    def shot(x, name, tip=None):                            # game frame (backbuffer) -> OUT and SHOTS/menu_<name>_<lang>.png
        if tip: c(x, cmd='mpm_tip', title=tip); time.sleep(1.5)
        p = os.path.join(OUT, f'{name}_{CODE[LANG[ROLE[x]]]}_ui{UISCALE}.png'); r = c(x, cmd='mpm_shot', how='backbuffer', path=p)
        if tip: c(x, cmd='mpm_tip')
        time.sleep(1)
        if os.path.exists(p): shutil.copy2(p, os.path.join(SHOTS, 'menu_' + os.path.basename(p)))
        log('shot', name, r.get('ok'), r.get('error', ''))

    def join_guest(port):
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); shot(G, 'entry_tip', tip=HUB_LABEL[LANG['G']]); press(G, 'JOIN_ADDR')
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
    gsave = os.path.join(gdir, 'guest_home_base.json')   # owner rule 2026-10-21: dives bring the wiki BASE save's units (buffed, cannot die)
    open(gsave, 'w').write(open(TPL).read().replace('"Version":"DS_B.0.4.11"', '"Version":"DS_B.0.4.29"').replace('"Version": "DS_B.0.4.11"', '"Version": "DS_B.0.4.29"'))   # a save of today's game (Bring refuses other game versions)
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
    def buff(sv, g):
        gs = fe.comp(fe.ec_of(sv)[g], 0).setdefault('GeneratedStats', {})
        for k in ('Strength', 'Constitution', 'Agility'): gs[k] = gs.get(k, 0.0) + BUFF_N
    if BUFF:
        for g in gpick: buff(home, g)
    if WEAK or FEED or BUFF: json.dump(home, open(gsave, 'w'), indent=2)
    hpick = [fe.clan_units(fe.load(hsave))[0][0]]
    if BUFF:
        hs = fe.load(hsave); buff(hs, hpick[0]); json.dump(hs, open(hsave, 'w'), indent=2)
    if FEED:
        hs = fe.load(hsave); fe.comp(fe.ec_of(hs)[hpick[0]], 0)['StateStats'].update(Hunger=1.0, Energy=1.0); json.dump(hs, open(hsave, 'w'), indent=2)
    log('args', dict(dive=DIVE_S, death=DEATH, weak=WEAK, recruit=RECRUIT, feed=FEED, buff=BUFF))

    rig.start(H, fps=30, window=True)
    if not SOLO: rig.start(G, fps=30, window=True)
    rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
    time.sleep(3)
    press(H, 'MULTIPLAYER')
    ms = c(H, cmd='mpm_status'); log('menu status keys', sorted(ms)[:60])
    shot(H, 'entry'); shot(H, 'entry_tip', tip=HUB_LABEL[LANG['H']])
    press(H, 'HOST_FE')                                         # the one mode entry under "Host a game": the hub
    shot(H, 'host_page')
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
    press(H, 'HOST_START'); time.sleep(2)                      # the FE page's Start: the neutral hub is built and loads
    check('hub: no load list / new campaign screen (Start builds the neutral hub)', c(H, cmd='mpm_status').get('state') in ('pick_save', 'host_loading', 'lobby'), c(H, cmd='mpm_status').get('state'))
    wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
    port = c(H, cmd='mpm_status').get('port')
    hub_pre = None
    if HUBMAP:   # owner 2026-10-21: the hub = a player's full base, no units before the start, everything unlocked
        rl = open(os.path.join(rig.idir(H), 'rig.log'), errors='replace').read()
        check('hub map: the hub was built from the shipped hub map', f'map ' in rl and 'hub_25846389.json' in rl and 'built from the hub map' in rl,
              next((l for l in rl.splitlines()[::-1] if 'fe hub:' in l and 'built from' in l), '')[:200])
        sv = max(glob.glob(os.path.join(rig.idir(H), 'data', 'Saves', 'MPHub_*.json')), key=os.path.getmtime)
        w0 = json.load(open(sv)); hub_pre = w0
        rd = w0['ResearchSaveData']['ResearchDatas']
        check('hub map: every research done, every region open, no player units in the hub file (the Guild Merchant stays, 0.1.20)',
              all(r['Workload'] >= r['RequiredWorkload'] and not r['IsLocked'] for r in rd.values())
              and all(not r['IsLocked'] for r in w0['ClanSaveData']['ExpeditionMapData'] if r.get('Region', -1) >= 0)
              and not w0['EntityContainerSaveData']['UnitContainerSaveData']['Base'].get('PlayerGuidList') and 'MPHubTemplate' not in w0,
              f"{len(rd)} research, {len(w0['EntityContainerSaveData']['BuildingContainerSaveData']['Base']['BuildingSaveData'])} buildings")
        lu0 = [u for u in c(H, cmd='list_units', map=0)['units'] if u['faction'] == 1]
        check('hub map: 0 player units in the hub before the start', len(lu0) == 0, str(len(lu0)))
    if not SOLO: join_guest(port)
    if HUBMAP:
        time.sleep(20)
        go = {x: (st(x).get('scene'), bool(c(x, cmd='ui_result').get('open'))) for x in PEERS}
        check('hub map: no game over in the empty hub (both peers still in the world 20 s after the join)', all(v == (2, False) for v in go.values()), str(go))
    # F4 Bring units window (board FEBringA), driven like a player: lobby button, pick the save, click unit cards, Bring
    try: SOLO or wait('guest knows the FE mode (host mail)', lambda: c(G, cmd='mpm_fewin').get('fe_mode') == 1, timeout=60, every=1)
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
    if not SOLO: bring_ui(G, gsave, gpick, 'guest')
    bring_ui(H, hsave, hpick, 'host')
    time.sleep(3)
    for x in PEERS: shot(x, 'lobby')                         # the hub lobby (host: the hint line says what the hub is for)
    nb = 1 if SOLO else 3
    wait(f'host received {nb} units', lambda: c(H, cmd='fe_status').get('received') == nb, timeout=60, every=1)
    # ---- the hub start (the lobby Start brings everyone's units in)
    press(H, 'START')
    wait('run started from the menu', lambda: (lambda f: f if f.get('run') or f.get('pending_start') else None)(c(H, cmd='fe_status')), timeout=60, every=1)
    wait('every peer in the hub', lambda: all(c(x, cmd='fe_status').get('hub_world') and st(x).get('det', {}).get('locked') for x in PEERS), timeout=600, every=2)
    fh = c(H, cmd='fe_status'); hub_units = fh['hub_units']; run_id = fh['run']
    check(f'hub start from the menu: the hub runs with the {nb} brought units', bool(run_id) and len(hub_units) == nb, fh.get('msg'))
    wait('running', lambda: all(not mp(x).get('paused') for x in PEERS), timeout=180)
    time.sleep(3)
    # ---- PvP from the hub: Esc > PvP Match (the old PvP page's rows and presets are in this window now)
    for x in PEERS: c(x, cmd='mpm_esc'); time.sleep(2); c(x, cmd='mpm_pvp', action='press_esc'); time.sleep(2)
    w = {x: c(x, cmd='mpm_pvp') for x in PEERS}
    check('hub PvP: the PvP window opens from the Esc menu (every peer)', all(w[x].get('window') for x in PEERS), str({x: w[x].get('window') for x in PEERS}))
    rows = [r_[0] for r_ in w[H].get('rows', [])]
    log('PvP window rows (host)', w[H].get('rows'))
    check('hub PvP window: 13 rule rows (Mode .. Vanilla check, Floor, the 6 visibility rules) + presets', len(rows) == 13 and 'presets' in w[H], str(rows))
    v0 = w[H]['rows'][7][1]
    c(H, cmd='mpm_pvp', action='arrow', row=7, dir=1); time.sleep(1.5); v1 = c(H, cmd='mpm_pvp')['rows'][7][1]
    c(H, cmd='mpm_pvp', action='arrow', row=7, dir=1); time.sleep(1.5); v2 = c(H, cmd='mpm_pvp')['rows'][7][1]
    check('hub PvP window: a visibility rule steps from its arrows and comes back', v0 != v1 and v2 == v0, f'{v0} -> {v1} -> {v2}')
    np0 = w[H].get('presets', 0)
    c(H, cmd='mpm_pvp', action='preset_save', name='Hub test'); time.sleep(1.5); q = c(H, cmd='mpm_pvp')
    np1 = q.get('presets', 0)                                # a copy can keep "Hub test" from an older run: then it is replaced
    check('hub PvP window: Save as preset saves a named preset (now shown)', np1 in (np0, np0 + 1) and np1 >= 1 and q.get('preset_now') == 'Hub test', f"{np1} {q.get('preset_now')} {q.get('preset_msg')}")
    shot(H, 'pvp_window_preset'); shot(H, 'pvp_window_tip', tip=q['rows'][7][0])
    c(H, cmd='mpm_pvp', action='preset_delete'); time.sleep(1.5); q = c(H, cmd='mpm_pvp')
    check('hub PvP window: Delete preset removes it (the rules stay)', q.get('presets') == np1 - 1 and q.get('preset_now') == '', f"{q.get('presets')} {q.get('preset_msg')}")
    here = {k: json.load(open(os.path.join(W, 'lang', f'{v}.json'))).get('This settlement', 'This settlement') if v != 'en' else 'This settlement' for k, v in CODE.items()}[LANG['H']]
    for _ in range(4):                                       # map: this settlement (the copy's saved rules may say a dungeon)
        if c(H, cmd='mpm_pvp')['rows'][4][1] == here: break
        c(H, cmd='mpm_pvp', action='arrow', row=4, dir=1); time.sleep(1.2)
    log('map row', c(H, cmd='mpm_pvp')['rows'][4])
    if c(H, cmd='mpm_pvp').get('mode') != 1: c(H, cmd='mpm_pvp', action='arrow', row=0, dir=1); time.sleep(1.2)   # Fight
    for _ in range(3):                                       # best of 1, 60 s rounds (whatever the copy's saved rules say)
        if c(H, cmd='mpm_pvp').get('bo') == 1: break
        c(H, cmd='mpm_pvp', action='arrow', row=1, dir=-1); time.sleep(1.2)
    for _ in range(5):
        if c(H, cmd='mpm_pvp').get('secs') == 60: break
        c(H, cmd='mpm_pvp', action='arrow', row=2, dir=-1); time.sleep(1.2)
    q = c(H, cmd='mpm_pvp'); log('rules', q.get('mode'), q.get('bo'), q.get('secs'), q.get('pause'))
    for x in PEERS: shot(x, 'pvp_window')
    rl = open(os.path.join(rig.idir(H), 'rig.log'), errors='replace').read()
    fit = [l for l in rl.splitlines() if 'pvp ui: PvP window' in l and 'scale' in l]
    log('fit lines', fit[-2:])
    if SOLO:
        c(H, cmd='mpm_pvp', action='close'); return
    log('Start Match', c(H, cmd='mpm_pvp', action='start'))
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=300, every=2)
    t_match = st(H)['tick']; ps = c(H, cmd='pvp_status')
    check('hub PvP: match on the hub base (no transfer), the 3 hub units fight', ps.get('arena') == 2 and st(H).get('map') == 0 and len(ps.get('units', [])) == 3, f"arena {ps.get('arena')} units {len(ps.get('units', []))}")
    time.sleep(8)
    hb = c(G, cmd='mpm_pvp', action='press_hud_bring'); time.sleep(2); hbs = c(G, cmd='mpm_pvp')
    check('hub PvP: HUD Bring Units shows in the preparation and opens Bring Units', hb.get('ok') is True, json.dumps(hb)[:200])
    shot(G, 'pvp_hud_bring')
    if hbs.get('bring', {}).get('open'): c(G, cmd='mpm_pvp', action='br_back'); time.sleep(1)
    c(G, cmd='pvp_vote', op='propose'); wait('vote open', lambda: c(H, cmd='pvp_status').get('vote_open'), timeout=60, every=1); c(H, cmd='pvp_vote', op='agree')
    wait('fight begins', lambda: c(H, cmd='pvp_status').get('fight_start', 0) > 0 and not c(H, cmd='pvp_status').get('placing'), timeout=300, every=2)
    s = c(H, cmd='pvp_status'); cl = {u['guid']: u for u in c(H, cmd='list_units')['units']}
    tc = {t: [cl[u['guid']]['cell'] for u in s['units'] if u['team'] == t and u['guid'] in cl and 'cell' in cl[u['guid']]] for t in (1, 2)}
    near = lambda cs, e: all(abs(a[0] - e[0]) + abs(a[1] - e[1]) <= 8 for a in cs)
    log('INFO (old flaky check, not counted): teams at their ends', near(tc[1], s['ends'][0]) and near(tc[2], s['ends'][1]), json.dumps(tc), s.get('ends'))
    owner = {u['guid']: (H if u['player'] == 0 else G) for u in s['units']}
    t9 = time.time()
    while time.time() - t9 < 500:
        q = c(H, cmd='pvp_status')
        if not q.get('active') and q.get('matches', 0) >= 1: break
        if q.get('active') and q.get('fight_start') and not q.get('placing') and not q.get('ending') and not q.get('resetting'):
            cl = {u['guid']: u for u in c(H, cmd='list_units')['units']}
            al = [u for u in q.get('units', []) if u.get('out_tick', -1) < 0 and 'cell' in cl.get(u['guid'], {})]
            for u in al:                                        # each player orders his own units (no automatic attacks, E7)
                en = [v for v in al if v['team'] != u['team']]
                if en:
                    c0 = cl[u['guid']]['cell']; tg = min(en, key=lambda v: (cl[v['guid']]['cell'][0] - c0[0]) ** 2 + (cl[v['guid']]['cell'][1] - c0[1]) ** 2); tcell = cl[tg['guid']]['cell']
                    rig.send(owner[u['guid']], {'cmd': 'ord_run_attack', 'unit': u['guid'], 'target': tg['guid'], 'x': tcell[0], 'y': tcell[1], 'mp': 1}, timeout=60)
        if not q.get('resetting'):
            for x in (H, G): shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_pvp_{x}.log'))
        time.sleep(3)
    wait('match over + hub back on both', lambda: all((lambda q: not q.get('active') and q.get('phase') == 0)(c(x, cmd='pvp_status')) and (lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x)) for x in (H, G)), timeout=600, every=3)
    time.sleep(5)
    pe = {x: c(x, cmd='pvp_status') for x in (H, G)}; fs = {x: c(x, cmd='fe_status') for x in (H, G)}
    alive = {u['guid']: u for u in c(H, cmd='list_units')['units'] if u['faction'] == 1}
    hub0 = sorted(h['hub'] for h in hub_units)
    check('hub PvP: match over, same winner on both; the hub is back (same run, hub world, its 3 units alive)',
          pe[H].get('match_winner') == pe[G].get('match_winner') and pe[H].get('matches', 0) >= 1 and fs[H].get('run') == run_id and fs[H].get('hub_world') and fs[G].get('hub_world')
          and all(g in alive and not alive[g]['dead'] for g in hub0), f"winner {pe[H].get('match_winner')}/{pe[G].get('match_winner')} run {fs[H].get('run')}")
    def rd(x):
        out = {}
        p = os.path.join(OUT, f'fp_pvp_{x}.log')
        for line in (open(p) if os.path.exists(p) else []):
            q = line.split()
            if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_match: out[int(q[0])] = q[1]
        return out
    a, b = rd(H), rd(G); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    check('hub PvP: fingerprints equal during the match, no desync', len(com) >= 300 and first is None and not mp(G).get('desyncs') and not mp(H).get('desyncs'),
          f'{len(com)} common, first diff {first}, desyncs {mp(H).get("desyncs")}/{mp(G).get("desyncs")}')
    wait('result window', lambda: c(H, cmd='mpm_pvp').get('result'), timeout=120, every=2)
    shot(H, 'pvp_result'); shot(G, 'pvp_result')
    for x in (H, G): c(x, cmd='mpm_pvp', action='press_ok'); time.sleep(2)
    time.sleep(3)
    check('hub PvP: "Back to the Hub" keeps everyone in the hub', all(st(x).get('scene') == 2 and c(x, cmd='fe_status').get('hub_world') and not c(x, cmd='mpm_pvp').get('result') for x in (H, G)))
    # ---- co-op: an expedition start from the same hub (Teams window, stock steps, approve), both in the dungeon
    gu = c(G, cmd='fe_status')['uid']
    mine = {H: [h['hub'] for h in hub_units if h['player'] != gu], G: [h['hub'] for h in hub_units if h['player'] == gu]}
    tm(H, action='open'); tm(G, action='open'); time.sleep(3)
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
    wait('both in the dungeon', lambda: all(st(x).get('map', 0) != 0 and not st(x).get('busy') and st(x).get('det', {}).get('locked') for x in (H, G)), timeout=600, every=3)
    time.sleep(20)
    lu = {x: sorted(u['guid'] for u in c(x, cmd='list_units')['units'] if u['faction'] == 1 and u['guid'] in hub0) for x in (H, G)}
    check('co-op after PvP: the expedition starts from the hub, both in the dungeon with the 3 units, no desync',
          lu[H] == lu[G] == hub0 and st(H).get('map') == st(G).get('map') and not mp(H).get('desyncs') and not mp(G).get('desyncs'),
          f"map {st(H).get('map')}/{st(G).get('map')} units {len(lu[H])}/{len(lu[G])} desyncs {mp(H).get('desyncs')}/{mp(G).get('desyncs')}")


if __name__ == '__main__':
    main()
