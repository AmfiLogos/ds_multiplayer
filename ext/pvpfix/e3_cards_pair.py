#!/usr/bin/env python3
"""E1 + E3 in the real card window (loader copies with the widget kit; hidden, muted, windowed; never the owner's game):
host WITH Units Laboratory, guest WITHOUT it.
 host: PvP page -> PvP world + lobby -> lobby Bring Units = the card window: the save list holds the regular world from
       DSML_GAME_SAVES (E1); the Saves / My Units switch is there (E3); My Units shows its cards; tick a My Units unit;
       Build a unit opens the Unit Builder; Save there -> the new unit is listed and ticked; Bring -> the host has 2 units
 guest: joins; its card window has no My Units switch; brings 2 save units
 match: Start -> vote -> fight with the host's My Units units -> 0 desyncs (guest has no Units Laboratory).
Run from the loader worktree's loader folder:  HM_BUILD=<loader build with this branch> BC_MP=<this checkout> python3 <this file>
Frames: <out>/*.png (out printed at the start)."""
import json, os, shutil, sys, time, zipfile
LT = os.path.expanduser(os.environ.get('LD_TEST', '~/ds_ld_pvpfix/loader/test'))
sys.path.insert(0, LT)
import bringcards_pair as BC  # noqa: E402  (helpers: driver, clicks, menu presses, lease/kits through hostmods_pair)
from bringcards_pair import HP, lrig, realrig, c, st, wait, check, log  # noqa: E402
H, G = BC.H, BC.G
OUT = BC.OUT
LIB = os.path.expanduser('~/ds_cm_pvpfix/units/test/data/launcher_library.json')
REG = 'RegularWorld__AutoSave_0123456789abcdef0123456789abcdef_0'


def cm_pkg():
    src = os.path.join(HP.BUILD, 'mods', 'custom-modes'); zp = os.path.join(OUT, 'pkg', 'custom-modes.zip')
    os.makedirs(os.path.dirname(zp), exist_ok=True)
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as zf:
        for root, _, files in os.walk(src):
            for f in files:
                p = os.path.join(root, f); zf.write(p, os.path.join('custom-modes', os.path.relpath(p, src)))
    return zp


def prepare(pk):
    env = {}
    for x in (H, G):
        withul = x == H
        HP.kit(x, pk, [BC.DRV] + (['custom-modes'] if withul else []), ['multiplayer'] + (['custom-modes'] if withul else []) + [BC.DRV])
        I = lrig.idir(x)
        for sub in ('data/fe', 'data/mp_owners', 'mp.json'):
            HP.park(os.path.join(I, sub))
        hs = os.path.join(I, 'data', 'Saves', 'home_Elven.json'); shutil.copy2(os.path.join(BC.FIX, 'home_Elven.json'), hs)
        cache = os.path.join(I, 'data', 'Mods', 'multiplayer', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
        shutil.copy2(BC.TPL, os.path.join(cache, os.path.basename(BC.TPL)))
        gs = os.path.join(OUT, f'game_saves_{x}'); os.makedirs(gs, exist_ok=True)
        shutil.copy2(os.path.join(BC.FIX, 'home_Elven.json'), os.path.join(gs, REG + '.json')); tt = time.time() + 86400; os.utime(os.path.join(gs, REG + '.json'), (tt, tt))
        d = BC.drv_dir(x); os.makedirs(os.path.join(d, 'cap'), exist_ok=True)
        env[x] = {'DSML_GAME_SAVES': gs, 'DSL_UNITS_TEST': d}
        if withul:
            lib = os.path.join(OUT, 'library.json'); dd = json.load(open(LIB)); dd['units'] = dd['units'][:3]; json.dump(dd, open(lib, 'w'))
            env[x]['DSMP_UNITS_LIB'] = lib
    return env


def lib_units():
    return json.load(open(os.path.join(OUT, 'library.json')))['units']


def run(port):
    BC.to_title(H)
    BC.press(H, 'MULTIPLAYER'); BC.press(H, 'HOST_C1'); time.sleep(1)
    c(H, cmd='mpm_pvp', action='page_arrow', row=1, dir=-1); c(H, cmd='mpm_pvp', action='page_arrow', row=2, dir=-1); c(H, cmd='mpm_pvp', action='page_arrow', row=2, dir=-1)
    BC.press(H, 'HOST_START')
    wait('PvP world + lobby', lambda: (lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'))(st(H))
         and c(H, cmd='mpm_status').get('state') == 'lobby', timeout=900, every=2)
    port = c(H, cmd='mpm_status').get('port') or port
    s = BC.open_cards(H, 'e3_host')
    check('E1 card window: the regular world from DSML_GAME_SAVES is in the save list', REG + '.json' in s.get('save_files', []), s.get('save_files'))
    check('E3 card window with Units Laboratory: the Saves / My Units switch', s.get('ul') == 1, s)
    BC.click(H, 'bring:src_lib', 2.0); s = BC.bst(H); HP.shot(H, 'e3_myunits.png')
    check('E3 My Units: its units as cards', s.get('src') == 1 and s.get('lib_units') == 3 and s.get('cards', 0) >= 1 and s.get('card_sprites', 0) >= 1, s)
    BC.click(H, 'bring:tick0', 1.2)
    n_lib = len(lib_units())
    BC.click(H, 'bring:left0', 2.0)                                       # Build a unit
    b = BC.req(H, 'state'); log('builder state', json.dumps(b)[:400])
    time.sleep(8)                                                          # the builder loads its data
    BC.req(H, 'field_set', target='bld:name', text='Built In Bring')
    BC.click(H, 'bld:save', 2.0)
    try:
        wait('the built unit in My Units', lambda: len(lib_units()) == n_lib + 1, timeout=30, every=1)
    except RuntimeError:
        pass
    time.sleep(2); s = BC.bst(H); HP.shot(H, 'e3_built.png')
    built = next((u for u in lib_units() if u.get('name') == 'Built In Bring'), None)
    check('E3 Build a unit: the Unit Builder saved a unit, the window lists it and ticked it', built is not None and s.get('picked') == 2 and s.get('lib_units') == n_lib + 1, (s, built and built.get('id')))
    BC.click(H, 'bring:bring', 2.0)
    u = wait('host has 2 units', lambda: (lambda q: q if q.get('received') == 2 else None)(c(H, cmd='pvp_units_status')), timeout=60, every=1)
    check('E3 Bring sends the My Units unit and the built one (full entries)', sorted(n for n, _ in u.get('names', [])) != [] and u.get('received') == 2, u)
    BC.to_title(G); BC.join(port)
    s = BC.open_cards(G, 'e3_guest')
    check('E3 guest WITHOUT Units Laboratory: no My Units switch (saves only)', s.get('ul') == 0 and s.get('open') == 1, s)
    ok_g, sg = BC.pick(G, 2, 'e3_guest')
    wait('host has 4 units', lambda: c(H, cmd='pvp_units_status').get('received') == 4, timeout=90, every=1)
    c(H, cmd='pvp_mods', ask=1)
    try:
        wait('guest mod report', lambda: len(c(H, cmd='pvp_mods').get('reports', [])) >= 1, timeout=60)
    except RuntimeError:
        pass
    BC.press(H, 'START')
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=600, every=2)
    ps = c(H, cmd='pvp_status'); hu = [x['guid'] for x in ps['units'] if x['player'] == 0]
    lg = {x['guid']: x for x in c(G, cmd='list_units')['units']}
    names = sorted(lg[g2]['name'] for g2 in hu if g2 in lg)
    check('E3 the guest has the host\'s My Units + built units', len(hu) == 2 and 'Built In Bring' in names, names)
    c(G, cmd='pvp_vote', op='propose'); wait('vote', lambda: c(H, cmd='pvp_status')['vote_open'], timeout=30, every=0.5); c(H, cmd='pvp_vote', op='agree')
    wait('fight', lambda: (lambda q: q.get('fight_start', 0) > 0 and not q.get('placing'))(c(H, cmd='pvp_status')), timeout=90, every=1)
    t0 = st(H)['tick']
    owner = {x['guid']: (H if x['player'] == 0 else G) for x in ps['units']}
    for x in (H, G): HP.shot(x, f'e3_fight_{x}.png')
    seg = None; tend = time.time() + 240
    while time.time() < tend:
        q = c(H, cmd='pvp_status')
        if q.get('ending'):
            a, b2 = HP.fp_map(H, t0), HP.fp_map(G, t0); com = sorted(set(a) & set(b2)); seg = (len(com), next((t for t in com if a[t] != b2[t]), None)); break
        alive = [x for x in q['units'] if x.get('out_tick', -1) < 0]
        cells = {x['guid']: x for x in c(H, cmd='list_units')['units']}
        for x in alive:
            en = [y for y in alive if y['team'] != x['team'] and 'cell' in cells.get(y['guid'], {})]
            if en and 'cell' in cells.get(x['guid'], {}):
                tg = en[0]; tc = cells[tg['guid']]['cell']
                c(owner[x['guid']], cmd='ord_run_attack', unit=x['guid'], target=tg['guid'], x=tc[0], y=tc[1], mp=1)
        time.sleep(4)
    mh, mg = st(H)['mp'], st(G)['mp']
    check('E3 match with Units Laboratory units, guest without it: 0 desyncs', seg and seg[0] >= 200 and seg[1] is None and not mh.get('desyncs') and not mg.get('desyncs'), (seg, mh.get('desyncs'), mg.get('desyncs')))


def main():
    import subprocess
    while subprocess.run(['pgrep', '-f', 'DSModLauncher/games/'], capture_output=True).returncode == 0 or subprocess.run(['pgrep', '-f', 'Dungeon Settlers Modded.app'], capture_output=True).returncode == 0:
        log('owner is playing: waiting 60 s'); time.sleep(60)                 # owner play rule
    T0 = time.time(); pair = realrig.lease('e3cards', os.getpid())
    while not pair and time.time() - T0 < 1800:
        time.sleep(15); pair = realrig.lease('e3cards', os.getpid())
    if not pair: raise SystemExit('no free pair')
    port = realrig.PAIR_PORTS[pair]; log('lease', pair, 'out', OUT, 'build', HP.BUILD)
    import threading; g = threading.Thread(target=HP.guard, daemon=True); g.start()
    res = {}
    try:
        for x in (H, G): HP.make_copy(x)
        pk = BC.driver_pkg(); pk['custom-modes'] = cm_pkg()
        env = prepare(pk)
        HP.room_for(2)
        for x in (H, G): HP.start(x, 'http://127.0.0.1:9', env[x])
        run(port)
    except BaseException as e:
        import traceback; log(traceback.format_exc()); check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        for x in (G, H):
            try: HP.stop(x)
            except Exception as e: log('stop', x, repr(e))
            for f in ('dsload.log', 'rig.log', 'player.log', 'fp.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        realrig.release(pair)
        res = {'checks': HP.checks, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks)}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", OUT)


if __name__ == '__main__':
    main()
