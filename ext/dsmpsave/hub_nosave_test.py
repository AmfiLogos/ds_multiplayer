#!/usr/bin/env python3
"""dsmpsave lane: a Foreign Expedition hub session leaves no files behind. A hub is a throwaway world: it cannot be
loaded or hosted again, so a save, a Multiplayer tag, an owners file, a remembered-rules line or a "host" Recent worlds
row of it only clutters the load list and the menus (the old build left all of these on the owner's Mac).
ONE leased pair, started only while the 1-minute load is under 15 (no wait for the owner's game: lead's rule).
Flow (start copied from ext/hubonly/hub_coop_pvp.py, without the PvP part): the host starts the hub from the menu
(Hub: Co-op or PvP page, Start), the guest joins by address, both bring units (guest: 2 units of the wiki base save,
host: 1), the lobby Start, a co-op expedition start from the hub (Teams, stock steps, approve), both in the dungeon
(map change = the stock host autosave on the old build); the host sends mp_save once (CampaignSaveLoader.Save, the
manual-save path); then both go back to the title with the stock system menu Quit (guest first, host last); both
copies quit. Checks, per copy:
  1. no new or changed file in data/Saves (any name; snapshot taken on the title before the session)
  2. mp_tags.txt has no line whose guid starts with fe5e (hub template fe5eb..., started hub fe5ea00...)
  3. data/mp_owners has no file starting with fe5e
  4. mp_worlds.txt has no line starting with fe5e
  5. host only: mp_history.json has no row with "how":"host" whose key starts with fe5e
plus one setup check (the whole flow ran), so a run that never reached the dungeon can never pass, and two player checks
(rigports.players_guard: no player but these two copies in the session, after the join and before the quits).
Before the run, old hub entries (fe5e) of earlier runs are taken out of those files and folders (originals in
<out>/aside/<copy>/), so the checks see this session only.
Usage: hub_nosave_test.py [--dylib PATH] [--src DIR]
  --dylib  default: this worktree's build/dsrig.dylib; hub_25846389.json must sit next to it
  --src    the tree whose src/mpmenu.h gives the menu button ids (default: this worktree)
Both copies start without Units Laboratory (DSRIG_CUSTOM=0, like the gate's `noul`), unless DSRIG_CUSTOM is set.
Output: PASS <name> / FAIL <name>  [info] per check, then RESULT PASS|FAIL <n> PASS, <m> FAIL.
Logs: ext/dsmpsave/out/hubnosave_<time>/ (run.log, each copy's player.log and this run's part of rig.log)."""
import json, os, shutil, signal, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(W, 'ext', 'foreign_expedition')); sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import fe_save as fe
import rig
import rigports

ARG = lambda k, d: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d
rig.DYLIB = os.path.abspath(os.path.expanduser(ARG('--dylib', os.path.join(W, 'build', 'dsrig.dylib'))))
SRC = os.path.abspath(os.path.expanduser(ARG('--src', W)))
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki base save (guest units)
HUBMAP = os.path.join(os.path.dirname(rig.DYLIB), 'hub_25846389.json')
HUB = 'fe5e'                                          # campaign guid prefix of every hub world (fe_hub.c / fe_game.c)
LOAD_MAX, LOAD_WAIT_S, LEASE_WAIT_S = 15.0, 20 * 60, 20 * 60
LANE = 'dsmpsave-nosave'
BUFF_N = 60.0                                         # test copies only: brought units +60 Str / Con / Agi (hub dives go down without it)
OUT = os.path.join(HERE, 'out', 'hubnosave_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
os.environ['DSRIG_FE_BACKUP'] = os.path.join(OUT, 'backups')
os.environ.setdefault('DSRIG_CUSTOM', '0')        # no Units Laboratory (custom-modes.dylib next to the dylib): ~/ds_multiplayer/build
                                                  # keeps an Oct 17 one whose own old hub code built the hub and refused the lobby Start
                                                  # (runs 1-2). Nothing of a session needs it (rig/ul_side.py); the gate runs hub tests the same way
LOG = open(os.path.join(OUT, 'run.log'), 'a'); results = []


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def say(s):                                           # check / RESULT lines: no time stamp (gate regexes)
    print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); say(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info and not ok else ''))
    if info and ok: log('  info:', info)


def result_line(extra=''):
    n = sum(1 for _, ok in results if not ok)
    say(f'RESULT {"PASS" if results and not n else "FAIL"} {len(results) - n} PASS, {n} FAIL' + (f'  ({extra})' if extra else ''))


# ---------------------------------------------------------------- files of one copy
is_hub = lambda g: g.strip().lower().startswith(HUB)


def saves_snap(x):
    """{relative path: (size, mtime)} of every file under data/Saves"""
    d = os.path.join(rig.idir(x), 'data', 'Saves'); out = {}
    for r, _, fs in os.walk(d):
        for f in fs:
            p = os.path.join(r, f)
            try:
                s = os.stat(p); out[os.path.relpath(p, d)] = (s.st_size, s.st_mtime)
            except FileNotFoundError:
                pass
    return out


def lines(x, name):
    p = os.path.join(rig.idir(x), name)
    return open(p, errors='replace').read().splitlines() if os.path.exists(p) else []


def hub_tags(x):                                      # mp_tags.txt: "<guid>\t<names>"
    return [l.split('\t', 1)[0] for l in lines(x, 'mp_tags.txt') if l.strip() and is_hub(l.split('\t', 1)[0])]


def hub_worlds(x):                                    # mp_worlds.txt: "<guid> <pause> <speedup> ..."
    return [l.split()[0] for l in lines(x, 'mp_worlds.txt') if l.split() and is_hub(l.split()[0])]


def hub_owners(x):                                    # data/mp_owners/<guid>_<tick>.bin
    d = os.path.join(rig.idir(x), 'data', 'mp_owners')
    return sorted(f for f in os.listdir(d) if is_hub(f)) if os.path.isdir(d) else []


def hist_rows(x):                                     # mp_history.json: a JSON array, one row per line
    out = []
    for l in lines(x, 'mp_history.json'):
        l = l.strip().rstrip(',')
        if l.startswith('{'):
            try: out.append((l, json.loads(l)))
            except ValueError: pass
    return out


def hub_host_rows(x):
    return [r.get('key', '') for _, r in hist_rows(x) if r.get('how') == 'host' and is_hub(r.get('key', ''))]


def clean_copy(x):
    """take earlier runs' hub entries out (originals kept in OUT/aside/<x>/) so the checks see this session only"""
    root = rig.idir(x); aside = os.path.join(OUT, 'aside', x); os.makedirs(aside, exist_ok=True); n = {}
    for name, hub in (('mp_tags.txt', lambda l: l.strip() and is_hub(l.split('\t', 1)[0])),
                      ('mp_worlds.txt', lambda l: l.split() and is_hub(l.split()[0]))):
        p = os.path.join(root, name)
        if not os.path.exists(p): continue
        ls = open(p, errors='replace').readlines(); kept = [l for l in ls if not hub(l)]
        if len(kept) != len(ls):
            shutil.copy2(p, os.path.join(aside, name))
            open(p + '.tmp', 'w').writelines(kept); os.replace(p + '.tmp', p)
        n[name] = len(ls) - len(kept)
    p = os.path.join(root, 'mp_history.json')
    if os.path.exists(p):
        rows = hist_rows(x); kept = [l for l, r in rows if not is_hub(r.get('key', ''))]
        if len(kept) != len(rows):
            shutil.copy2(p, os.path.join(aside, 'mp_history.json'))
            open(p + '.tmp', 'w').write('[\n' + ',\n'.join(kept) + ('\n' if kept else '') + ']\n'); os.replace(p + '.tmp', p)
        n['mp_history.json'] = len(rows) - len(kept)
    own = hub_owners(x)
    if own:
        os.makedirs(os.path.join(aside, 'mp_owners'), exist_ok=True)
        for f in own: shutil.move(os.path.join(root, 'data', 'mp_owners', f), os.path.join(aside, 'mp_owners', f))
    n['mp_owners'] = len(own)
    fe_d = os.path.join(root, 'data', 'fe')                    # an earlier run's Foreign Expedition state (hub tests move it too)
    if os.path.exists(fe_d): shutil.move(fe_d, os.path.join(aside, 'fe'))
    log(x, 'old hub entries moved aside', n)


# ---------------------------------------------------------------- the session
def run(H, G, S0, flow):
    B = json.loads(subprocess.run([sys.executable, os.path.join(W, 'ext', 'fepvp13', 'tools', 'mpm_ids.py'), SRC],
                                  capture_output=True, text=True, check=True).stdout)      # menu button ids of this build's source
    c = lambda x, **k: rig.send(x, k, timeout=180)
    st = lambda x: c(x, cmd='status')
    tm = lambda x, **k: c(x, cmd='ui_teams', **k)
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
    on_title = lambda s: s.get('scene') == 1 and not s.get('busy')

    def wait(what, pred, timeout=600, every=2):
        t1 = time.time()
        while time.time() - t1 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    # test worlds for Bring Units: copies only, in this run's folder (the Bring window's "Game folder")
    gdir = os.path.join(OUT, 'game_saves'); os.makedirs(gdir); os.environ['DSRIG_FE_GAME_SAVES'] = gdir
    gsave = os.path.join(gdir, 'guest_home_base.json')
    open(gsave, 'w').write(open(TPL).read().replace('"Version":"DS_B.0.4.11"', '"Version":"DS_B.0.4.29"')
                           .replace('"Version": "DS_B.0.4.11"', '"Version": "DS_B.0.4.29"'))   # Bring refuses other game versions
    hsave = os.path.join(gdir, 'host_home_base.json')       # the wiki base save too (never a copy of the owner's worlds), its own world id
    open(hsave, 'w').write(open(gsave).read().replace(json.load(open(gsave))['CampaignSaveHeader']['CampaignGuid'], '7e57a0b1-0000-4000-8000-00000000d5a5'))
    cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))

    def buff(sv, g):
        gs = fe.comp(fe.ec_of(sv)[g], 0).setdefault('GeneratedStats', {})
        for k in ('Strength', 'Constitution', 'Agility'): gs[k] = gs.get(k, 0.0) + BUFF_N
    home = fe.load(gsave)
    units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
    gpick = [units[0][0], units[1][0]]
    for g in gpick: buff(home, g)
    json.dump(home, open(gsave, 'w'), indent=2)
    hs = fe.load(hsave); hpick = [units[2][0]]; buff(hs, hpick[0]); json.dump(hs, open(hsave, 'w'), indent=2)   # a unit the guest does not bring

    rig.start(H, fps=30, window=True); rig.start(G, fps=30, window=True)
    rig.wait_for(H, on_title, 'host title', timeout=1500); rig.wait_for(G, on_title, 'guest title', timeout=1500)
    time.sleep(3)
    for x in (H, G):                                          # the "before" state: both on the title, no session yet
        S0[x] = saves_snap(x)
        log(x, 'snapshot: data/Saves', len(S0[x]), 'files | hub entries now: tags', hub_tags(x), 'owners', hub_owners(x),
            'worlds', hub_worlds(x), 'history host rows', hub_host_rows(x))

    # host: Multiplayer > Hub: Co-op or PvP > rules > Start (the neutral hub is built and loads)
    press(H, 'MULTIPLAYER'); time.sleep(2); press(H, 'HOST_FE'); time.sleep(1); press(H, 'SETTINGS'); time.sleep(1)
    want = dict(fe=1, fe_death=1, fe_xp=1, fe_loot=1, fe_recruit=0)
    for k, btn in (('fe_death', 'FED_NEXT'), ('fe_xp', 'FEX_NEXT'), ('fe_loot', 'FEL_NEXT'), ('fe_recruit', 'FER_NEXT')):
        if c(H, cmd='mpm_status')['settings'].get(k) != want[k]: press(H, btn)
    log('host page rules', {k: c(H, cmd='mpm_status')['settings'].get(k) for k in want})
    press(H, 'FE_DONE'); time.sleep(1)
    press(H, 'HOST_START'); time.sleep(2)
    wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
    port = c(H, cmd='mpm_status').get('port')
    rl = open(os.path.join(rig.idir(H), 'rig.log'), 'rb').read()[flow['rl0'][H]:].decode(errors='replace')
    log('host hub start:', next((l for l in rl.splitlines()[::-1] if 'fe hub:' in l and 'built from' in l), '(no "built from" line)')[:200], '| port', port)

    # guest: Multiplayer > Join by address
    time.sleep(2)
    press(G, 'MULTIPLAYER'); time.sleep(2); press(G, 'JOIN_ADDR')
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
    wait('guest loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
    wait('guest knows the hub mode', lambda: c(G, cmd='mpm_fewin').get('fe_mode') == 1, timeout=60, every=1)
    pok, pls = rigports.players_guard(rig.send, [H, G], 2, 'start')   # another run's copy in this session = EXTRA PLAYER
    check('players: no extra player at the start', pok, '; '.join(pls))

    def bring_ui(x, path, guids):                             # the lobby's Bring Units window, driven like a player
        wait(f'{x} lobby state', lambda: c(x, cmd='mpm_status').get('state') in ('lobby', 'guest_lobby'), timeout=90, every=1)
        for _ in range(5):
            c(x, cmd='mpm_click', button=B['FE_BRINGWIN']); time.sleep(2)
            if c(x, cmd='mpm_fewin').get('mode') == 1: break
        w = c(x, cmd='mpm_fewin')
        idx = next(i for i, s_ in enumerate(w['saves']) if s_['path'] == path)
        for _ in range(idx // 6): c(x, cmd='mpm_click', button=B['FE_SAVEPG']); time.sleep(0.5)
        c(x, cmd='mpm_click', button=B['FE_SAVE0'] + idx % 6); time.sleep(1.5)
        us = [u['guid'] for u in c(x, cmd='mpm_fewin')['units']]
        for g in guids:
            c(x, cmd='mpm_click', button=B['FE_UNIT0'] + us.index(g) % 8); time.sleep(0.7)
        w = c(x, cmd='mpm_fewin')
        if w.get('picked') != len(guids): raise RuntimeError(f'{x}: Bring window picked {w.get("picked")} of {len(guids)} ({w.get("err")})')
        c(x, cmd='mpm_click', button=B['FE_OK']); time.sleep(2)
        log(x, 'brought', len(guids), 'unit(s):', c(x, cmd='fe_status').get('msg'))
    bring_ui(G, gsave, gpick)
    bring_ui(H, hsave, hpick)
    wait('host received 3 units', lambda: c(H, cmd='fe_status').get('received') == 3, timeout=60, every=1)

    # the lobby Start: every peer in the hub. The lobby sees brought units a few seconds after fe_status counts them
    # (run 1: Start 2 s after the host's Bring said "Nobody brought units yet"): wait, and press again like a player
    time.sleep(8)
    for k in range(3):
        press(H, 'START')
        try:
            wait('hub run started', lambda: (lambda f: f if f.get('run') or f.get('pending_start') else None)(c(H, cmd='fe_status')), timeout=20, every=1)
            break
        except RuntimeError:
            log('lobby Start: no run yet, try', k + 1)
            if k == 2: raise
    wait('every peer in the hub', lambda: all(c(x, cmd='fe_status').get('hub_world') and st(x).get('det', {}).get('locked') for x in (H, G)), timeout=600)
    fh = c(H, cmd='fe_status'); hub_units = fh['hub_units']
    wait('running', lambda: all(not st(x).get('mp', {}).get('paused') for x in (H, G)), timeout=180)
    time.sleep(3)
    log('in the hub: run', fh.get('run'), '|', len(hub_units), 'hub units | host status map', st(H).get('map'))

    # co-op expedition start from the hub (Teams window, stock steps, the guest approves): both in the dungeon
    gu = c(G, cmd='fe_status')['uid']
    mine = {H: [h['hub'] for h in hub_units if h['player'] != gu], G: [h['hub'] for h in hub_units if h['player'] == gu]}
    tm(H, action='open'); tm(G, action='open'); time.sleep(3)
    for x in (H, G):
        for g in mine[x]: tm(x, action='click', guid=g, button=1); time.sleep(1)
    tm(H, action='close'); tm(G, action='close'); time.sleep(2)
    tm(H, action='open'); time.sleep(1); log('teams go', tm(H, action='go'))
    wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(H)), timeout=60)
    tm(H, action='stock_select', units=','.join(mine[H] + mine[G])); time.sleep(1); tm(H, action='stock_next')
    wait('proposer stock step', lambda: tm(H)['stock'].get('state') == 1, timeout=60)
    tm(H, action='stock_accept')
    wait('guest receiver', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(G)), timeout=90)
    if tm(G).get('stock', {}).get('open'):
        tm(G, action='stock_next'); wait('guest summary', lambda: tm(G).get('prop', {}).get('shown'), timeout=60)
    tm(G, action='approve')
    wait('both in the dungeon', lambda: all(st(x).get('map', 0) != 0 and not st(x).get('busy') and st(x).get('det', {}).get('locked') for x in (H, G)), timeout=600, every=3)
    flow['dungeon'] = True
    log('both in the dungeon: map', st(H).get('map'), '/', st(G).get('map'))
    time.sleep(15)

    # a normal host save (the manual-save path), then both back to the title (stock system menu Quit; host last)
    log('host mp_save', c(H, cmd='mp_save', name='hub_nosave_test'))
    time.sleep(10)
    pok, pls = rigports.players_guard(rig.send, [H, G], 2, 'end')     # the session ends with the quits below
    check('players: no extra player at the end', pok, '; '.join(pls))
    for x, who in ((G, 'guest'), (H, 'host')):
        c(x, cmd='mpm_esc'); time.sleep(2)                    # the system menu must exist for its own Quit (the save-on-exit path)
        r = c(x, cmd='mp_quit', mode='stock'); how = 'stock'
        if not r.get('ok'):
            c(x, cmd='mp_quit', mode='title'); how = 'event (no system menu)'
        log(who, 'quit to title:', how, r)
        rig.wait_for(x, on_title, f'{who} on the title', timeout=180)
    time.sleep(5)
    flow['title'] = True


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))   # kill -TERM: copies stopped + pair released (finally)
    log('dylib', rig.DYLIB, time.strftime('(%Y-%m-%d %H:%M)', time.localtime(os.path.getmtime(rig.DYLIB))) if os.path.exists(rig.DYLIB) else '(missing)',
        '| menu ids from', SRC, '| out', OUT)
    if not os.path.exists(rig.DYLIB) or not os.path.exists(HUBMAP):
        result_line(f'setup: {"dylib" if not os.path.exists(rig.DYLIB) else "hub map " + os.path.basename(HUBMAP) + " next to the dylib"} missing'); sys.exit(2)
    if subprocess.run(['git', '-C', SRC, 'diff', '--quiet', 'HEAD', '--', 'src/mpmenu.h']).returncode == 1:
        log('note: src/mpmenu.h has uncommitted changes in', SRC, '(menu ids follow the working file)')
    t0 = time.time()                                          # lead's rule: one pair while the owner plays, only under load 15
    while os.getloadavg()[0] >= LOAD_MAX:
        if time.time() - t0 >= LOAD_WAIT_S:
            result_line(f'setup: 1-minute load stayed at {LOAD_MAX:.0f} or more for {LOAD_WAIT_S // 60} min'); sys.exit(2)
        log(f'1-minute load {os.getloadavg()[0]:.1f} (limit {LOAD_MAX:.0f}): waiting 60 s'); time.sleep(60)
    log(f'1-minute load {os.getloadavg()[0]:.1f}: go')
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < LEASE_WAIT_S:
        pair = rig.lease(LANE, os.getpid())
        if not pair: log('no free pair: waiting 30 s'); time.sleep(30)
    if not pair:
        result_line('setup: no free pair'); sys.exit(2)
    H, G = pair[0], pair[1]
    log('pair', pair, '(host', H, 'guest', G + ')')
    S0, flow, moved = {}, {'dungeon': False, 'title': False, 'rl0': {}}, []
    try:
        inst = os.path.realpath(rig.INST)
        for x in (H, G):                                      # the copies' data is under the rig instances, never the owner's
            sp = os.path.realpath(os.path.join(rig.idir(x), 'data', 'Saves'))
            ok = sp.startswith(inst + os.sep) and 'DSModLauncher' not in sp and 'CanOpener' not in sp
            log(x, 'data/Saves =', sp, '| under the rig instances:', ok)
            if not ok: raise SystemExit(f'{x}: data/Saves is not under the rig instances')
        for x in (H, G):
            p = os.path.join(rig.idir(x), 'mp.json')         # a test session file would start a session at boot
            if os.path.exists(p): os.replace(p, p + '.last'); moved.append(x)
            flow['rl0'][x] = os.path.getsize(os.path.join(rig.idir(x), 'rig.log')) if os.path.exists(os.path.join(rig.idir(x), 'rig.log')) else 0
            clean_copy(x)
        run(H, G, S0, flow)
    except Exception as e:
        log('ERROR', repr(e)[:300]); LOG.write(traceback.format_exc()); LOG.flush()
        flow['err'] = repr(e)[:160]
    finally:
        for x in (H, G): rig.stop(x)                          # app quit too: anything written at exit counts
        time.sleep(2)
        if S0:
            info = '' if flow['title'] else flow.get('err', 'stopped early') + (' (after the dungeon)' if flow['dungeon'] else ' (before the dungeon)')
            check('setup: hub from the menu, guest joined, both in the dungeon, host mp_save, both back on the title', flow['title'], info)
            for x, who in ((H, 'host'), (G, 'guest')):
                s1, s0 = saves_snap(x), S0[x]
                new = sorted(f for f in s1 if f not in s0); chg = sorted(f for f in s1 if f in s0 and s1[f] != s0[f])
                log(who, 'data/Saves new:', new, '| changed:', chg)
                check(f'{who}: no new or changed file in data/Saves', not new and not chg,
                      ' '.join(([f'new {new[:6]}'] if new else []) + ([f'changed {chg[:6]}'] if chg else [])))
                t = hub_tags(x); check(f'{who}: mp_tags.txt has no hub world line (fe5e)', not t, ', '.join(t[:4]))
                o = hub_owners(x); check(f'{who}: data/mp_owners has no hub world file (fe5e)', not o, f'{len(o)}: ' + ', '.join(o[:3]))
                wl = hub_worlds(x); check(f'{who}: mp_worlds.txt has no hub world line (fe5e)', not wl, ', '.join(wl[:4]))
                if who == 'host':
                    hr = hub_host_rows(x); check('host: mp_history.json has no "host" row for a hub world (fe5e)', not hr, ', '.join(hr[:4]))
        for x in moved:                                       # other tests read mp.json (port, role): put it back
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
        for x in (H, G):                                      # logs of this run: player.log (new each start), rig.log from the run start
            src = os.path.join(rig.idir(x), 'player.log')
            if os.path.exists(src): shutil.copy2(src, os.path.join(OUT, f'{x}_player.log'))
            src = os.path.join(rig.idir(x), 'rig.log')
            if os.path.exists(src):
                off = flow['rl0'].get(x, 0)
                with open(src, 'rb') as f, open(os.path.join(OUT, f'{x}_rig.log'), 'wb') as o:
                    f.seek(off if off <= os.path.getsize(src) else 0); shutil.copyfileobj(f, o)
        log('released', rig.release(pair, LANE), '| out:', OUT)
        result_line('' if S0 else 'setup: ' + flow.get('err', 'no snapshot'))


if __name__ == '__main__':
    main()
