#!/usr/bin/env python3
"""Mac side of the int013 cross-machine multiplayer test (Mac copy <-> Windows VM). Called by mac.sh.

One hidden test copy (letter W) in ~/ds_winlane013/ds_modloader/loader/test_env/instances/W, run by the loader lane's
lrig.py (batchmode, guard process, muted, own data/prefs domain). Injected: the int013 build's dsload.dylib; modules =
the int013 build's mods_all/mac set (multiplayer, custom-modes, gcd-cdr, hell-difficulty, no profile = all enabled,
the same set as the Windows kit). One rig pair lease (lane vmwin-mac) is held by a sleep process while the copy runs.
"""
import hashlib, importlib.util, json, os, shutil, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
WL = os.path.expanduser('~/ds_winlane013')
LOADER = os.path.join(WL, 'ds_modloader', 'loader')
BUILD = os.environ.get('MAC_BUILD') or os.path.join(LOADER, 'build')   # MAC_BUILD: another build folder (dsload.dylib + mods_all/mac)
sys.path.insert(0, os.path.join(LOADER, 'test'))
import lrig                                                        # noqa: E402

lrig.LROOT = LOADER
lrig.INST = os.path.join(LOADER, 'test_env', 'instances')
lrig.__file__ = os.path.abspath(__file__)                          # lrig.start spawns "<this> _guard W" (same INST)
X = os.environ.get('MAC_COPY', 'W')
LANE = 'vmwin-mac'
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
STATE = os.path.join(os.environ.get('WINBOT_STATE') or os.path.expanduser('~/.ds_winbot'), 'mac_state.json')   # outside the repo
SEED = os.path.join(lrig.SEED_DATA, 'Saves')
SAVES = {'hub': 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json',      # base map: Teams / expedition
         'dungeon': 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'}   # map 1 (in a dungeon)
B = dict(MULTIPLAYER=1, HOST=2, BACK=3, LOAD_SAVE=7, START=10, LEAVE=12, JOIN_ADDR=14, CONNECT=15)   # enum mpm_button (mpmenu.h)

_spec = importlib.util.spec_from_file_location('realrig', RIG)
realrig = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(realrig)


def out(o):
    print(json.dumps(o, indent=1) if not isinstance(o, str) else o, flush=True)


def state():
    try:
        return json.load(open(STATE))
    except Exception:
        return {}


def save_state(d):
    json.dump(d, open(STATE + '.tmp', 'w'), indent=1); os.replace(STATE + '.tmp', STATE)


def touch():
    p = state().get('pair')
    if p:
        realrig._with_leases(lambda d: d.get(p, {}).update(used=time.time()) if d.get(p, {}).get('lane') == LANE else None)


def running():
    return lrig.alive(lrig.read_pid(X))


def c(**k):
    if not running():
        raise SystemExit(f'copy {X} is not running (mac.sh start)')
    touch()
    return lrig.send(X, k, timeout=60, connect_wait=30)


def sha(p):
    try:
        return hashlib.sha256(open(p, 'rb').read()).hexdigest()
    except FileNotFoundError:
        return None


def app():
    return os.path.join(lrig.idir(X), 'DungeonSettlers.app')


# ------------------------------------------------------------------ install (int013 build into the copy)
def files_of(d):
    r = {}
    for root, _, fs in os.walk(d):
        for f in fs:
            p = os.path.join(root, f); r[os.path.relpath(p, d)] = sha(p)
    return r


def install():
    a = app()
    if not os.path.exists(os.path.join(a, 'Contents/MacOS/Dungeon Settlers')):
        raise SystemExit(f'no copy {X}: run {HERE}/make_instance_wl.sh {X} first')
    fw = os.path.join(a, 'Contents/Frameworks/dsload.dylib'); md = os.path.join(a, 'Contents/Resources/mods')
    src_mods = os.path.join(BUILD, 'mods_all', 'mac')
    changed = False
    if sha(fw) != sha(os.path.join(BUILD, 'dsload.dylib')):
        shutil.copyfile(os.path.join(BUILD, 'dsload.dylib'), fw + '.tmp'); os.chmod(fw + '.tmp', 0o755); os.replace(fw + '.tmp', fw)
        changed = True
    if not os.path.isdir(md) or files_of(md) != files_of(src_mods):
        tmp = md + '.new'
        if os.path.exists(tmp):
            os.makedirs(os.path.expanduser('~/ToDelete_2026-10-18/vmwin2'), exist_ok=True)   # no deletes (owner rule)
            os.replace(tmp, os.path.expanduser('~/ToDelete_2026-10-18/vmwin2/mods.new.' + time.strftime('%Y%m%d-%H%M%S')))
        shutil.copytree(src_mods, tmp)
        if os.path.exists(md):
            old = os.path.join(lrig.idir(X), 'old'); os.makedirs(old, exist_ok=True)
            os.replace(md, os.path.join(old, 'mods.' + time.strftime('%Y%m%d-%H%M%S')))
        os.replace(tmp, md)
        changed = True
    if changed:
        subprocess.run(['codesign', '-f', '-s', '-', os.path.join(a, 'Contents/MacOS/Dungeon Settlers')], capture_output=True)
    return proof()


def proof():
    a = app(); md = os.path.join(a, 'Contents/Resources/mods')
    heads = {}
    for r in ('ds_modloader', 'ds_multiplayer', 'ds_custommodes'):
        g = subprocess.run(['git', '-C', os.path.join(WL, r), 'log', '-1', '--format=%h %D'], capture_output=True, text=True).stdout.strip()
        heads[r] = g
    p = {'copy': lrig.idir(X), 'repos': heads,
         'dsload.dylib': {'sha256': sha(os.path.join(a, 'Contents/Frameworks/dsload.dylib')), 'same_as_int013_build': sha(os.path.join(a, 'Contents/Frameworks/dsload.dylib')) == sha(os.path.join(BUILD, 'dsload.dylib'))},
         'mods_same_as_int013_build': files_of(md) == files_of(os.path.join(BUILD, 'mods_all', 'mac')) if os.path.isdir(md) else False,
         'multiplayer PINNED_COMMIT': (open(os.path.join(md, 'multiplayer', 'PINNED_COMMIT')).read().strip() if os.path.exists(os.path.join(md, 'multiplayer', 'PINNED_COMMIT')) else None),
         'modules': {}}
    for m in sorted(os.listdir(md)) if os.path.isdir(md) else []:
        mj = os.path.join(md, m, 'mod.json')
        if os.path.exists(mj):
            j = json.load(open(mj)); e = j.get('platforms', {}).get('mac-arm64', {}).get('entry')
            p['modules'][m] = {'version': j.get('version'), 'entry': e, 'sha256': (sha(os.path.join(md, m, e)) or '')[:16]}
    return p


# ------------------------------------------------------------------ lease
def take_lease():
    s = state()
    if s.get('pair') and s.get('holder') and realrig._pid_alive(s['holder']):
        d = realrig._with_leases(lambda d: dict(d))
        if d.get(s['pair'], {}).get('lane') == LANE and d[s['pair']].get('pid') == s['holder']:
            return s
    h = subprocess.Popen(['/bin/sleep', '86400'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    r = subprocess.run([sys.executable, RIG, 'lease', LANE, '--pid', str(h.pid)], capture_output=True, text=True)
    pair = r.stdout.strip().split()[-1] if not r.returncode and r.stdout.strip() else None
    if not pair:
        os.kill(h.pid, signal.SIGTERM)
        raise SystemExit('no pair lease: ' + (r.stderr.strip() or r.stdout.strip())[-300:])
    s.update(pair=pair, holder=h.pid, pair_port=realrig.PAIR_PORTS[pair]); save_state(s)
    return s


def drop_lease():
    s = state(); msg = 'no lease held'
    if s.get('pair'):
        r = subprocess.run([sys.executable, RIG, 'release', s['pair'], LANE], capture_output=True, text=True)
        msg = f"{s['pair']}: " + (r.stdout + r.stderr).strip()
    if s.get('holder') and realrig._pid_alive(s['holder']):
        try:
            os.kill(s['holder'], signal.SIGTERM)
        except OSError:
            pass
    for k in ('pair', 'holder', 'pair_port'):
        s.pop(k, None)
    save_state(s)
    return msg


# ------------------------------------------------------------------ helpers
def st():
    for _ in range(15):
        try:
            s = c(cmd='status')
            if 'scene' in s:
                return s
        except Exception as e:
            s = {'error': str(e)}
        time.sleep(1)
    raise SystemExit(f'status not answered: {s}')


def wait(what, pred, timeout, every=2.0):
    t0 = time.time(); last = None
    while time.time() - t0 < timeout:
        last = pred()
        if last:
            return last
        time.sleep(every)
    raise SystemExit(f'timeout ({timeout} s) waiting for {what}; try again (the copy keeps going)')


def on_title():
    s = st(); return s if s.get('scene') == 1 and not s.get('busy') else None


def press(n):
    r = c(cmd='mpm_press', button=B[n]); time.sleep(1.2); return r


def wait_title(t):
    return wait('the title screen', on_title, t)


# ------------------------------------------------------------------ commands
def cmd_start():
    if running():
        out({'already_running': lrig.read_pid(X), **proof()}); return
    s = take_lease()
    p = install()
    mj = os.path.join(lrig.idir(X), 'mp.json')
    if os.path.exists(mj):
        os.replace(mj, mj + '.off')                                # menu flow only (host / join by address)
    pm = os.path.join(lrig.idir(X), 'mp_menu.json')                # player name shown to the other side (default = Mac account name)
    try:
        d = json.load(open(pm))
    except Exception:
        d = {}
    d['name'] = os.environ.get('MAC_NAME', 'Mac'); json.dump(d, open(pm + '.tmp', 'w')); os.replace(pm + '.tmp', pm)
    for f in ('player.log', 'dsload.log', 'rig.log', 'fp.log'):
        q = os.path.join(lrig.idir(X), f)
        if os.path.exists(q):
            os.replace(q, q + '.prev')
    pid = lrig.start(X, window=os.environ.get('MAC_WINDOW') == '1', fps=30)
    s.update(pid=pid, started=time.time()); save_state(s)
    out({'started': pid, 'pair': s['pair'], 'pair_port': s['pair_port'], 'mode': 'hidden window' if os.environ.get('MAC_WINDOW') == '1' else 'batchmode', **p,
         'next': 'mac.sh status until scene 1 (title, ~40-90 s), then mac.sh host or mac.sh join <ip> <port>'})


def cmd_stop():
    r = {}
    if running():
        lrig.stop(X)
    r['copy_running'] = running()
    r['lease'] = drop_lease()
    out(r)


def cmd_host(which='hub'):
    wait_title(80)
    src = os.path.join(SEED, SAVES.get(which, which))
    work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    dst = os.path.join(work, os.path.basename(src)); shutil.copy2(src, dst)   # a copy: the backup is only read
    steps = [press('MULTIPLAYER'), press('HOST'), press('LOAD_SAVE')]
    time.sleep(2)
    r = c(cmd='load_save', file=dst, det=0)
    ok = wait('host session + lockstep', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None)(st()), 85, 3)
    m = c(cmd='mpm_status')
    s = state(); s.update(role='host', port=m.get('port')); save_state(s)
    out({'host': True, 'save': os.path.basename(src), 'presses': steps, 'load': r, 'port': m.get('port'), 'direct': m.get('direct'),
         'mpm_state': m.get('state'), 'tick': ok.get('tick'), 'listen': listen_info(m.get('port'))})


def listen_info(port):
    pid = lrig.read_pid(X)
    o = subprocess.run(['lsof', '-nP', '-a', '-p', str(pid), '-iTCP', '-sTCP:LISTEN'], capture_output=True, text=True).stdout
    return [' '.join(l.split()[-2:]) for l in o.splitlines()[1:]]


def cmd_join(ip, port):
    wait_title(80)
    steps = []
    for _ in range(3):                                              # back out of an earlier Join / Host page
        if c(cmd='mpm_status').get('state') in ('title_join', 'title_host', 'title_friends', 'failed', 'pick_save'):
            steps.append(press('BACK'))
    if c(cmd='mpm_status').get('state') != 'title_menu':
        steps.append(press('MULTIPLAYER'))
    steps.append(press('JOIN_ADDR'))
    f = c(cmd='mpm_status').get('field') or {}
    if f.get('exists'):                                             # menu path: type into the address field, Connect
        steps += [c(cmd='mpm_type', text=f'{ip}:{port}'), press('CONNECT')]
    else:                                                           # no field: mpm_type would set the system clipboard
        steps += [{'no_address_field': f}, press('BACK'), c(cmd='mpm_join_tcp', host=ip, port=int(port), name=os.environ.get('MAC_NAME', 'Mac'))]
    s = state(); s.update(role='guest', join=[ip, int(port)]); save_state(s)
    out({'join': f'{ip}:{port}', 'steps': steps, 'mpm': short_mpm(c(cmd='mpm_status')),
         'next': 'mac.sh status until det.locked and mp.loads >= 1 (world download + load can take minutes)'})


def short_mpm(m):
    return {k: m.get(k) for k in ('state', 'msg', 'notice', 'message', 'port', 'direct', 'box') if k in m}


def units_view(s):
    us = c(cmd='list_units').get('units', [])
    pl = [u for u in us if u.get('faction') == 1 and not u.get('dead')]
    mp = s.get('mp') or {}
    me = mp.get('player')
    v = {'player_units': len(pl), 'mine': [], 'others': {}, 'free': [], 'reserved': []}
    for u in pl:
        nm = f"{u.get('name', '?')} {u['guid'][:8]} {u.get('cell')}"
        if not mp.get('started'):
            v['free'].append(nm); continue
        o = c(cmd='mp_owner', guid=u['guid']).get('owner')
        if o == me:
            v['mine'].append(nm)
        elif o == -1:
            v['free'].append(nm)
        elif o == -2:
            v['reserved'].append(nm)
        else:
            v['others'].setdefault(str(o), []).append(nm)
    v['enemies_alive'] = sum(1 for u in us if u.get('faction') == 3 and not u.get('dead'))
    return v


def cmd_status():
    s0 = state()
    if not running():
        out({'running': False, 'pair': s0.get('pair'), 'last_role': s0.get('role')}); return
    s = st()
    r = {'running': lrig.read_pid(X), 'pair': s0.get('pair'), 'role': s0.get('role'),
         'scene': s.get('scene'), 'map': s.get('map'), 'tick': s.get('tick'), 'busy': s.get('busy'),
         'det_locked': (s.get('det') or {}).get('locked'), 'tick_errors': (s.get('det') or {}).get('tick_errors')}
    mp = s.get('mp') or {}
    if mp:
        pm = mp.get('present', 0) or 0
        r['mp'] = {k: mp.get(k) for k in ('started', 'host', 'player', 'paused', 'speed', 'loads', 'desyncs', 'desynced', 'orders', 'orders_dropped_owner', 'busy_mask', 'own_hash', 'spectator') if k in mp}
        r['mp']['peers'] = [i for i in range(32) if pm & (1 << i)]
    try:
        m = c(cmd='mpm_status')
        r['menu'] = short_mpm(m)
        r['chat_last'] = (m.get('chat') or {}).get('last')
    except SystemExit:
        raise
    except Exception as e:
        r['menu'] = {'error': str(e)}
    if s.get('scene') == 2 and not s.get('busy'):
        r['units'] = units_view(s)
    if s0.get('role') == 'host' or mp.get('host'):
        r['left'] = c(cmd='mp_left') if mp.get('started') else None
    r['listen'] = listen_info(0)
    out(r)


def cmd_chat(text):
    if c(cmd='mpm_status').get('state') != 'playing':
        raise SystemExit('chat opens only in a running game (menu state playing); host: mac.sh press START closes the lobby')
    c(cmd='mpm_chat', open=1); time.sleep(1)                     # same steps + pauses as ~/ds_multiplayer/win/v8_xplay.py
    c(cmd='mpm_chat', text=text); time.sleep(0.5)
    typing = (c(cmd='mpm_status').get('chat') or {}).get('typing')
    c(cmd='mpm_chat', enter=1); time.sleep(3)
    m = c(cmd='mpm_status')
    out({'sent': text, 'typing_before_enter': typing, 'chat': {k: (m.get('chat') or {}).get(k) for k in ('lines', 'last', 'typing', 'buf')}})


def cmd_teams(args):
    k = {'cmd': 'ui_teams'}
    for a in args:                                                  # action=open guid=... button=1
        key, _, v = a.partition('=')
        try:
            k[key] = json.loads(v)
        except ValueError:
            k[key] = v
    out(c(**k))


def cmd_expedition(n=3):
    """host: Teams > Go to dungeon (stock window) with up to n of this player's units, Accept. guest: answer the
    receiver window (Next + approve). Same steps as rig/gate13/mu_dive_mac.py."""
    s = st(); mp = s.get('mp') or {}
    tm = lambda **k: c(cmd='ui_teams', **k)
    if not mp.get('started'):
        raise SystemExit('no multiplayer session (host or join first)')
    if mp.get('host'):
        if s.get('map') != 0:
            raise SystemExit(f"not on the base map (map {s.get('map')}): host the hub save (mac.sh host hub)")
        us = [u for u in c(cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
        mine = [u['guid'] for u in us if c(cmd='mp_owner', guid=u['guid']).get('owner') in (mp.get('player'), -1)][:n]
        log = [('open', tm(action='open'))]; time.sleep(1)
        log.append(('go', tm(action='go')))
        wait('stock window step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm()), 60)
        log.append(('select', tm(action='stock_select', units=','.join(mine)))); time.sleep(1)
        log.append(('next', tm(action='stock_next')))
        wait('proposer step', lambda: tm().get('stock', {}).get('state') == 1, 60)
        log.append(('accept', tm(action='stock_accept'))); time.sleep(3)
        out({'role': 'host', 'units': mine, 'steps': log, 'teams': tm(), 'next': 'guest: mac.sh expedition (approve) / host alone: status map != 0'})
    else:
        t = wait('the receiver window or proposal', lambda: (lambda t: t if (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown') else None)(tm()), 60)
        log = []
        if t.get('stock', {}).get('open'):
            log.append(('next', tm(action='stock_next')))
            wait('summary', lambda: tm().get('prop', {}).get('shown'), 60)
        log.append(('approve', tm(action='approve')))
        out({'role': 'guest', 'steps': log, 'teams': tm()})


def cmd_leave():
    r = c(cmd='mp_quit', mode='stock')
    if not r.get('ok'):
        r = {'stock': r, 'event': c(cmd='mp_quit', mode='title')}
    t = wait('the title after leaving', on_title, 80, 1)
    out({'leave': r, 'scene': t.get('scene'), 'mp': (t.get('mp') or {}).get('started')})


def cmd_rejoin():
    j = state().get('join')
    if not j:
        raise SystemExit('no earlier join (mac.sh join <ip> <port>)')
    if not on_title():
        cmd_leave()
    cmd_join(*j)


def cmd_logs(d):
    os.makedirs(d, exist_ok=True); got = []
    for f in ('player.log', 'dsload.log', 'dsload.prev.log', 'rig.log', 'fp.log', 'guard.log', 'stdout.log', 'player.log.prev', 'dsload.log.prev', 'rig.log.prev', 'data/ErrorLog.txt'):
        p = os.path.join(lrig.idir(X), f)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(d, 'mac_' + f.replace('/', '_'))); got.append(f)
    for p in (os.path.join(lrig.idir(X), 'data'),):
        for f in os.listdir(p):
            if f.startswith('mp_') and os.path.isfile(os.path.join(p, f)):
                shutil.copy2(os.path.join(p, f), os.path.join(d, 'mac_data_' + f)); got.append('data/' + f)
    json.dump(proof(), open(os.path.join(d, 'mac_build_proof.json'), 'w'), indent=1)
    out({'dir': d, 'files': got + ['mac_build_proof.json']})


def main():
    a = sys.argv[1:]
    if not a:
        print(open(os.path.join(HERE, 'mac.sh')).read().split('\n# ---')[0]); return
    k = a[0]
    if k == '_guard':
        lrig.guard(a[1]); return
    if k == 'start': cmd_start()
    elif k == 'stop': cmd_stop()
    elif k == 'host': cmd_host(a[1] if len(a) > 1 else 'hub')
    elif k == 'join': cmd_join(a[1], a[2])
    elif k == 'status': cmd_status()
    elif k == 'chat': cmd_chat(' '.join(a[1:]))
    elif k == 'teams': cmd_teams(a[1:])
    elif k == 'expedition': cmd_expedition()
    elif k == 'leave': cmd_leave()
    elif k == 'rejoin': cmd_rejoin()
    elif k == 'logs': cmd_logs(a[1])
    elif k == 'press': out(press(a[1].upper()))
    elif k == 'cmd': out(c(**json.loads(a[1])))
    elif k == 'proof': out(proof())
    else:
        raise SystemExit('unknown command ' + k)


if __name__ == '__main__':
    main()
