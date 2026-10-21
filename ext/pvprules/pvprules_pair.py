#!/usr/bin/env python3
"""Lane pvprules proof (owner 2026-10-20): PvP Rules box (visibility rules, map, floor, presets), presets across a restart,
a PvP match on a dungeon floor / a boss room through the real host-choice flow, the same map on both peers, no desync.

  pvprules_pair.py --phase rules  [--tag T]                 host only: set rules + presets in the box, restart, all back
  pvprules_pair.py --phase match  --map 2 --floor 2 [--tag T]   pair: PvP world -> lobby -> Bring -> Start -> arena floor
      --map: 1 random, 2 dungeon 1, 3 dungeon 2; --floor: 0 random, 1..4, 5 boss room
Checks print PASS / FAIL; the last line is RESULT. Frames + logs: ext/pvprules/out/<tag>/.
Owner play rule: waits while the owner's game runs (launcher copy or the Modded app)."""
import json, os, shutil, subprocess, sys, time
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
PHASE = arg('--phase', 'rules'); MAP = arg('--map', 2); FLOOR = arg('--floor', 2)
LANE = f'pvprules-{os.getpid()}'
rig.DYLIB = os.path.abspath(arg('--dylib', os.path.join(W, 'build', 'dsrig.dylib')))
TAG = arg('--tag', time.strftime(f'{PHASE}_%H%M%S'))
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')        # the wiki base template (PvP world)
HOME = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures/home_Elven.json')   # not in git (a save)        # each player's own save
H_UNITS = ['477ad25a-7c22-43f5-be28-db055895297e', '137a4264-054d-4b21-81f2-a69d349a3bd0']
G_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', 'acd20405-56c9-4360-80f6-b4c84e1a9ba4']
OUT = os.path.join(W, 'ext', 'pvprules', 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a'); results = {}
TRASH = os.path.expanduser('~/ToDelete_2026-10-20/pvprules_old_files')
ROW = {'mode': 0, 'win': 1, 'round_time': 2, 'pause': 3, 'vanilla': 4, 'map': 5, 'floor': 6, 'enemy_vision': 7, 'enemy_windows': 8,
       'enemy_portraits': 9, 'enemy_hpbars': 10, 'enemy_status': 11, 'enemy_viewrange': 12}


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def c(x, **k): return rig.send(x, k, timeout=180)


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def owner_playing():
    """the owner's game: a process whose command runs from the launcher's game copy or the Modded app (shells and tools
    that only mention those paths in their command line, like other lanes' wait loops, do not count)"""
    for pat in ('DSModLauncher/games/', 'Dungeon Settlers Modded.app'):
        r = subprocess.run(['pgrep', '-fl', pat], capture_output=True, text=True).stdout.splitlines()
        for line in r:
            cmd = line.split(' ', 1)[1] if ' ' in line else ''
            exe = cmd.split(' -', 1)[0]
            if pat in exe and '/Contents/MacOS/' in exe and not any(t in cmd.split(' ', 1)[0] for t in ('zsh', 'bash', 'python', 'pgrep')): return line[:160]
    return None


def owner_wait():
    while True:
        p = owner_playing()
        if not p: return
        log('owner game runs (%s): waiting 60 s' % p); time.sleep(60)


def fp_read_file(path, t_from):
    d = {}
    if not os.path.exists(path): return d
    for line in open(path):
        q = line.split()
        if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from: d[int(q[0])] = q[1]
    return d


def rules_file(x): return os.path.join(rig.idir(x), 'data', 'ModData', 'multiplayer', 'pvp_rules.txt')


def set_row(x, rid, want_value_text=None, steps=1, d=1):
    for _ in range(steps): c(x, cmd='mpm_pvp', action='page_arrow', row=ROW[rid], dir=d)


def start_title(x):
    owner_wait()
    os.environ['DSRIG_CUSTOM'] = '0'
    rig.start(x, fps=30, window=True)
    rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + x, timeout=1500); time.sleep(3)


def open_host_page(x, B):
    press = lambda n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))
    press('MULTIPLAYER'); press('HOST_C1'); time.sleep(2)
    return c(x, cmd='mpm_status').get('state')


def phase_rules(H, B):
    f = rules_file(H)
    if os.path.exists(f):
        os.makedirs(TRASH, exist_ok=True); shutil.move(f, os.path.join(TRASH, f'{H}_pvp_rules_{time.strftime("%H%M%S")}.txt'))
    start_title(H)
    st = open_host_page(H, B); log('host page', st)
    q0 = c(H, cmd='mpm_pvp'); r0 = c(H, cmd='pvpr_status')
    log('rows at first open', json.dumps(q0.get('rows'))[:900])
    check('1 the PvP Rules box shows 13 rows (5 old + map + floor + 6 visibility), visibility all Hidden by default',
          st == 'title_host' and len(q0.get('rows', [])) == 13 and all(r0['local'][k] == 0 for k in ROW if k.startswith('enemy_')), f"{st} rows {len(q0.get('rows', []))}")
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'ui_rules_default.png'))
    # preset A: Dungeon 1, floor 2, enemy health bars + view range shown, best of 1
    set_row(H, 'map', steps=2)                 # here -> random -> dungeon 1
    set_row(H, 'floor', steps=2)               # random -> floor 1 -> floor 2
    set_row(H, 'enemy_hpbars'); set_row(H, 'enemy_viewrange'); set_row(H, 'win', d=-1)
    c(H, cmd='mpm_pvp', action='preset_save', name='Dungeon duel')
    a = c(H, cmd='pvpr_status')
    # preset B: Dungeon 2 boss room, enemy vision shown
    set_row(H, 'map'); set_row(H, 'floor', d=-1)          # dungeon 2, floor 2 -> floor 1? (-1 from floor 2 = floor 1) then to boss
    for _ in range(10):
        if c(H, cmd='pvpr_status')['local']['floor'] == 5: break
        set_row(H, 'floor', d=-1)
    set_row(H, 'enemy_vision'); set_row(H, 'enemy_hpbars')
    c(H, cmd='mpm_pvp', action='preset_save_open'); time.sleep(1)
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'ui_rules_naming.png'))
    qn = c(H, cmd='mpm_pvp')
    check('2a Save as preset... opens the name field (a real text field) with Save / Cancel', qn.get('naming') == 1 and qn.get('field') == 1, f"naming {qn.get('naming')} field {qn.get('field')}")
    c(H, cmd='mpm_pvp', action='preset_save', name='Boss arena')
    # preset C saved then deleted
    set_row(H, 'mode'); c(H, cmd='mpm_pvp', action='preset_save', name='To delete'); c(H, cmd='mpm_pvp', action='preset_delete')
    set_row(H, 'mode')                         # back to Fight: equals "Boss arena" again
    b = c(H, cmd='pvpr_status'); q = c(H, cmd='mpm_pvp')
    log('after presets', json.dumps(b)[:1500])
    names = [p['name'] for p in b['presets']]
    check('2 presets saved / deleted in the box: "Dungeon duel" + "Boss arena", the deleted one gone, the box shows "Boss arena"',
          names == ['Dungeon duel', 'Boss arena'] and q.get('preset_now') == 'Boss arena' and b['local']['map'] == 3 and b['local']['floor'] == 5
          and b['local']['enemy_vision'] == 1 and a['local']['enemy_hpbars'] == 1, f"{names} now {q.get('preset_now')!r}")
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'ui_rules_presets.png'))
    # pick "Dungeon duel" with the preset arrow: its values come back into the box
    c(H, cmd='mpm_pvp', action='preset_arrow', dir=-1); p = c(H, cmd='pvpr_status'); q = c(H, cmd='mpm_pvp')
    check('3 the preset arrow applies "Dungeon duel" (Dungeon 1, Floor 2, health bars + view range shown, best of 1)',
          q.get('preset_now') == 'Dungeon duel' and p['local']['map'] == 2 and p['local']['floor'] == 2 and p['local']['enemy_hpbars'] == 1
          and p['local']['enemy_viewrange'] == 1 and p['local']['enemy_vision'] == 0 and q.get('bo') == 1, f"now {q.get('preset_now')!r} {p['local']}")
    saved_local = p['local']; saved_presets = p['presets']
    log('file', open(f).read())
    rig.stop(H); log('H stopped (restart)'); time.sleep(3)
    start_title(H)
    st = open_host_page(H, B); time.sleep(1)
    r = c(H, cmd='pvpr_status'); q = c(H, cmd='mpm_pvp')
    log('after restart', json.dumps(r)[:1500])
    check('4 after a restart: the last used rules and both presets are back, the box shows "Dungeon duel" and its values',
          r['local'] == saved_local and r['presets'] == saved_presets and q.get('preset_now') == 'Dungeon duel' and q.get('bo') == 1
          and any(row[1].startswith('Dungeon 1') for row in q.get('rows', [])), f"local equal {r['local'] == saved_local}, presets equal {r['presets'] == saved_presets}, now {q.get('preset_now')!r}")
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'ui_rules_after_restart.png'))


def phase_match(H, G, B):
    for x in (H, G):
        pth = os.path.join(rig.idir(x), 'mp.json')
        if os.path.exists(pth): os.replace(pth, pth + '.last')
        d = os.path.join(rig.idir(x), 'data', 'mp_owners')
        if os.path.exists(d): os.replace(d, d + '_' + time.strftime('%Y%m%d-%H%M%S'))
    cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
    shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
    work = {}
    for x in (H, G):
        work[x] = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work[x], exist_ok=True)
        shutil.copy2(HOME, os.path.join(work[x], 'my_home.json'))
    start_title(H)
    st = open_host_page(H, B)
    # the box: map + floor from the arguments, enemy health bars shown (the guest must get it), best of 1, 60 s
    want = {'map': MAP, 'floor': FLOOR, 'enemy_hpbars': 1, 'enemy_viewrange': 0, 'enemy_vision': 0, 'win': 0, 'round_time': 0}
    for k, v in want.items():
        for _ in range(8):
            if c(H, cmd='pvpr_status')['local'][k] == v: break
            set_row(H, k)
    loc = c(H, cmd='pvpr_status')['local']
    check('0 host page: the box set to map %d floor %d, health bars shown' % (MAP, FLOOR), all(loc[k] == v for k, v in want.items()), json.dumps(loc))
    c(H, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'ui_host_page.png'))
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))
    press(H, 'HOST_START')
    rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'), 'H loaded + hosting', timeout=900)
    wait('host lobby', lambda: c(H, cmd='mpm_status').get('state') == 'lobby', timeout=120, every=1)
    port = c(H, cmd='mpm_status').get('port')
    start_title(G)
    press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR'); c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
    wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(c(G, cmd='status')), timeout=900)
    time.sleep(3)
    log('H bring', c(H, cmd='pvp_units_bring', file=os.path.join(work[H], 'my_home.json'), guids=','.join(H_UNITS)))
    log('G bring', c(G, cmd='pvp_units_bring', file=os.path.join(work[G], 'my_home.json'), guids=','.join(G_UNITS)))
    wait('host has 4 units', lambda: c(H, cmd='pvp_units_status')['received'] == 4, timeout=120, every=1)
    log('mods ask', c(H, cmd='pvp_mods', ask=1))
    wait('guest mod report', lambda: len(c(H, cmd='pvp_mods').get('reports', [])) >= 1, timeout=60, every=1)
    press(H, 'START'); log('H lobby Start (menu)')
    wait('rules on both', lambda: all(c(x, cmd='pvpr_status').get('synced') for x in (H, G)), timeout=300, every=2)
    rs = {x: c(x, cmd='pvpr_status') for x in (H, G)}
    log('maps', rs[H]['map'], rs[G]['map'])
    check('1 the host\'s rules reached the guest: pvp_rule_get equal on both, enemy_hpbars 1, same map pick',
          rs[H]['get'] == rs[G]['get'] and rs[G]['get']['enemy_hpbars'] == 1 and rs[H]['map'] == rs[G]['map'] and not rs[H]['map']['here'],
          f"guest get {rs[G]['get']}, map {rs[G]['map']}")
    m = rs[H]['map']
    exp_region = {2: 0, 3: 1}.get(MAP, m['region']); exp_floor = {0: m['floor'], 5: 4}.get(FLOOR, FLOOR - 1) if MAP in (2, 3) else m['floor']
    check('1b the pick matches the box: region %s floor index %s%s' % (exp_region, exp_floor, ' (boss room)' if FLOOR == 5 else ''),
          m['region'] == exp_region and m['floor'] == exp_floor and bool(m['boss']) == (FLOOR == 5 and MAP in (2, 3)), json.dumps(m))
    wait('match set up on both', lambda: all(c(x, cmd='pvp_status').get('phase', 0) >= 1 for x in (H, G)), timeout=600, every=2)
    wait('both on the arena floor, idle and locked', lambda: all((lambda s: s.get('floor') == m['floor'] and s.get('map', 0) != 0 and not s.get('busy') and s.get('det', {}).get('locked') and not s.get('map_transition'))(c(x, cmd='status')) for x in (H, G)), timeout=900, every=2)
    time.sleep(3)
    ai = {x: c(x, cmd='pvp_arena_info') for x in (H, G)}
    for x in (H, G): log(x, 'arena_info', json.dumps(ai[x])[:700])
    check('2 both copies on the same generated floor (room hash), region %d floor %d' % (m['region'], m['floor']),
          ai[H]['room_hash'] == ai[G]['room_hash'] and ai[H]['rooms'] > 0 and ai[H].get('arena_rc') == 0, f"hash {ai[H]['room_hash']}/{ai[G]['room_hash']} rooms {ai[H]['rooms']}/{ai[G]['rooms']} rc {ai[H].get('arena_rc')}")
    check('3 the arena is empty: no living monster, no NPC (boss room: no boss)', all(ai[x]['monsters_alive'] == 0 and ai[x]['non_player_non_monster'] == 0 for x in (H, G)),
          f"monsters {ai[H]['monsters_alive']}/{ai[G]['monsters_alive']} others {ai[H]['non_player_non_monster']}/{ai[G]['non_player_non_monster']}")
    for x in (H, G): c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_arena.png'))
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=600, every=2)
    t_match = c(H, cmd='status')['tick']
    try:                                                       # v0.1.21: nobody starts alone: the guest proposes, the host agrees
        c(G, cmd='pvp_vote', op='propose'); wait('vote open', lambda: c(H, cmd='pvp_status').get('vote_open'), timeout=60, every=1)
        c(H, cmd='pvp_vote', op='agree')
        wait('fight begins', lambda: c(H, cmd='pvp_status').get('fight_start', 0) > 0, timeout=420, every=3)
        fought = True
    except RuntimeError:
        fought = False
    log('fight began' if fought else 'fight did not begin in 7 min (placing time)')
    for x in (H, G): c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_fight.png'))
    t_end = time.time() + 240
    while time.time() < t_end:
        s = c(H, cmd='pvp_status')
        if not s.get('active') or s.get('resetting'): break          # the reload starts fp.log again: keep the match's lines
        for x in (H, G): shutil.copy2(os.path.join(rig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_match_{x}.log'))
        time.sleep(3)
    for x in (H, G): c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{x}_later.png'))
    fh, fg = fp_read_file(os.path.join(OUT, f'fp_match_{H}.log'), t_match), fp_read_file(os.path.join(OUT, f'fp_match_{G}.log'), t_match)
    com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
    mh, mg = c(H, cmd='status')['mp'], c(G, cmd='status')['mp']
    check('4 fingerprints equal on both from the match start (>= 1000 common ticks), 0 desyncs; the fight began',
          len(com) >= 1000 and first is None and not mh.get('desyncs') and not mg.get('desyncs') and fought,
          f'{len(com)} common ticks, first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, fought {fought}')


def main():
    import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    B = json.loads(subprocess.run(['python3', os.path.join(W, 'ext', 'fepvp13', 'tools', 'mpm_ids.py'), W], capture_output=True, text=True).stdout)
    owner_wait()
    pair = None
    while not pair:
        pair = rig.lease(LANE, os.getpid())
        if not pair: log('no free pair, waiting'); time.sleep(30)
    H, G = pair[0], pair[1]
    log('pair', pair, 'dylib', rig.DYLIB, 'out', OUT, 'phase', PHASE, 'map', MAP, 'floor', FLOOR)
    try:
        if PHASE == 'rules': phase_rules(H, B)
        else: phase_match(H, G, B)
        results['ran to the end'] = True
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); results['no exception'] = False
    finally:
        results.setdefault('ran to the end', False)
        for x in (H, G):
            for fn in ('player.log', 'rig.log', 'fp.log'):
                pth = os.path.join(rig.idir(x), fn)
                if os.path.exists(pth): shutil.copy2(pth, os.path.join(OUT, f'{x}_{fn}'))
        if '--keep' not in A:
            for x in (H, G): rig.stop(x)
            for x in (H, G):
                pth = os.path.join(rig.idir(x), 'mp.json')
                if os.path.exists(pth + '.last'): shutil.copy2(pth + '.last', pth)
            log('released', rig.release(pair, LANE))
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        n = sum(1 for v in results.values() if not v)
        log('RESULT', 'PASS' if results and not n else 'FAIL', f'{len(results) - n}/{len(results)}', json.dumps(results))


if __name__ == '__main__':
    main()
