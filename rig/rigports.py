#!/usr/bin/env python3
"""Ports of test copies: ONE helper for every pair / host test (rig.py, the loader's lrig.py, tests with their own start).

Why (2026-10-25): two-player runs of different lanes joined each other again and again. lrig gave port 7891 to every
K..Z copy, rig pairs had fixed ports (AB 7791 .. IJ 7871), mp.json files stayed in instances (copy T: client
127.0.0.1:7831 joined another lane's pair) and menu hosts listened on 47777 (the owner's game). Runs then passed or
failed for the wrong reason (a hostmods run with 3 players instead of 2).

Rules (LANE_RULES.md "Ports"):
- A run gets its OWN TCP port: lease() / port_for(). A random port in 21000-29999 that no socket holds and no lease used
  in the last 6 h. Never 47777, never an old fixed pair port. One lease file for the whole Mac (port_leases.json beside
  rig.py, under a file lock): port, holder process (+ its start time), the copies (instance folders), the lane.
  A lease whose holder process ended is freed at the next call; the port then rests 6 h.
- write_mp() / set_host_port(): the port into a copy's mp.json / the loader setting "host_port" (menu host).
- clear_stale_mp(): an mp.json written before this run started is set aside (mp.json.left-<time>, never deleted).
- check_start(): a copy whose mp.json (host or client on this Mac) or host_port points at a port it does not own is
  REFUSED. Owned = a live lease lists the copy, or its holder is this process or one of its parents.
- players_guard(): the session's player count at the start and the end of a pair test; above the expected count =
  "EXTRA PLAYER <name>" (the run fails). rig.stop / lrig.stop also check each copy they stop (end_check); any EXTRA
  PLAYER line makes the process end with exit code 1 and a last "RESULT FAIL EXTRA PLAYER" line; the gate's chk fails
  on it too (gate13/lib.sh).
Test: rigports_test.py (two pairs at the same time never meet, stale mp.json, refusals, EXTRA PLAYER).
Env: DSRIG_PORT_DIR + DSRIG_PORT_RANGE (tests: lease file folder, port range), DSRIG_EXPECT_PEERS (players beyond this
process's copies, e.g. a Windows VM guest).
"""
import atexit, errno, fcntl, json, os, random, socket, subprocess, sys, time

DIR = os.environ.get('DSRIG_PORT_DIR') or os.path.expanduser('~/ds_multiplayer/rig')
FILE = os.path.join(DIR, 'port_leases.json')
LOCK = os.path.join(DIR, 'port_leases.lock')
LOG = os.path.join(DIR, 'port_leases.log')
LO, HI = (int(v) for v in (os.environ.get('DSRIG_PORT_RANGE') or '21000-29999').split('-'))   # tests: another range
REST_S = 6 * 3600                                  # a freed port is not leased again for 6 h (stale copies may point at it)
NEVER = {7777, 7791, 7795, 7811, 7831, 7851, 7871, 7891, 47777}    # the old fixed ports + the game's default (owner)
LOCAL = {'', '127.0.0.1', 'localhost', '::1', '0.0.0.0', '::'}

STARTED = {}                                       # instance folder -> copy name, started by this process (rig / lrig start)
EXTRA = []                                         # EXTRA PLAYER lines seen by this process
_peers = [int(os.environ.get('DSRIG_EXPECT_PEERS') or 0)]


def key(inst):
    return os.path.realpath(inst)


def _log(msg):
    try:
        with open(LOG, 'a') as f:
            f.write(time.strftime('%Y-%m-%d %H:%M:%S ') + f'pid {os.getpid()} ' + msg + '\n')
    except OSError:
        pass


# ------------------------------------------------------------------ processes
def _ps():
    """{pid: (ppid, start time text, command)} of every process (one ps call)"""
    out = subprocess.run(['ps', '-axo', 'pid=,ppid=,lstart=,comm='], capture_output=True, text=True).stdout
    t = {}
    for l in out.splitlines():
        p = l.split()
        if len(p) >= 8 and p[0].isdigit() and p[1].isdigit():
            t[int(p[0])] = (int(p[1]), ' '.join(p[2:7]), ' '.join(p[7:]))
    return t


def _chain(table, pid=None):
    """pid (default this process) and all its parents, nearest first"""
    out, p = [], pid or os.getpid()
    while p and p not in out:
        out.append(p)
        p = table.get(p, (0,))[0]
    return out


def _alive(e, table):
    return e.get('pid') in table and table[e['pid']][1] == e.get('pstart')


def run_t0():
    """start of this run: the oldest python process in this process's parent chain (a driver that starts the tests)"""
    table = _ps(); t0 = None
    for p in _chain(table):
        if p in table and 'python' in os.path.basename(table[p][2]).lower():
            try:
                t0 = time.mktime(time.strptime(table[p][1], '%a %b %d %H:%M:%S %Y'))
            except ValueError:
                pass
    return t0 if t0 is not None else time.time()


# ------------------------------------------------------------------ the lease file
def _locked(fn):
    os.makedirs(DIR, exist_ok=True)
    with open(LOCK, 'a') as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        try:
            d = json.load(open(FILE))
        except (OSError, ValueError):
            d = {}
        d.setdefault('ports', {}); d.setdefault('rest', {})
        table = _ps(); now = time.time()
        for p, e in list(d['ports'].items()):
            if not _alive(e, table):
                d['rest'][p] = now; del d['ports'][p]
                _log(f'port {p} ({e.get("owner")}, holder {e.get("pid")}) freed: holder ended')
        for p, t in list(d['rest'].items()):
            if now - t > REST_S:
                del d['rest'][p]
        r = fn(d, table)
        tmp = f'{FILE}.{os.getpid()}'
        with open(tmp, 'w') as f:
            json.dump(d, f, indent=1)
        os.replace(tmp, FILE)
        return r


def port_free(port):
    """no socket on this Mac holds the TCP port (IPv4 any + loopback, IPv6 any)"""
    for fam, addr in ((socket.AF_INET, '0.0.0.0'), (socket.AF_INET, '127.0.0.1'), (socket.AF_INET6, '::')):
        s = socket.socket(fam, socket.SOCK_STREAM)
        try:
            s.bind((addr, port))
        except OSError as e:
            if fam == socket.AF_INET6 and e.errno in (errno.EAFNOSUPPORT, errno.EADDRNOTAVAIL):
                continue
            return False
        finally:
            s.close()
    return True


def _new(d, table, insts, owner, pid):
    pid = pid if pid in table else os.getpid()
    used = set(d['ports']) | set(d['rest'])
    for _ in range(500):
        p = random.randint(LO, HI)
        if str(p) in used or p in NEVER or not port_free(p):
            continue
        d['ports'][str(p)] = {'owner': owner, 'pid': pid, 'pstart': table[pid][1], 'copies': sorted(set(insts)), 'since': time.time()}
        _log(f'port {p} leased: {owner}, holder {pid}, copies {[os.path.basename(i) for i in insts]}')
        return p
    raise SystemExit(f'rigports: no free TCP port in {LO}-{HI}')


def _listed(d, k):
    return [(int(p), e) for p, e in d['ports'].items() if k in e['copies']]


def lease(copies, owner, pid=None):
    """a NEW port for a run: copies = their instance folders, holder = pid (default this process). Returns the port."""
    insts = [key(c) for c in copies]
    return _locked(lambda d, table: _new(d, table, insts, owner, pid or os.getpid()))


def port_for(copies, owner, pid=None, share=False):
    """the run's port for these copies: the live lease that lists one of them and is held by pid / this process or a
    parent, else (share=True) the newest lease held by this process or a parent, else a NEW lease (holder pid, default
    this process). The copies are added to it. A copy listed in a lease of another live run: refused (SystemExit)."""
    insts = [key(c) for c in copies]
    def f(d, table):
        mine = set(_chain(table)) | ({pid} if pid else set())
        for k in insts:
            for p, e in _listed(d, k):
                if e['pid'] in mine:
                    e['copies'] = sorted(set(e['copies']) | set(insts)); return p
                raise SystemExit(f'refused: copy {os.path.basename(k)} belongs to a live run of {e["owner"]} '
                                 f'(holder pid {e["pid"]}, port {p}); use another copy or wait for that run')
        if share:
            ours = sorted((e['since'], p) for p, e in d['ports'].items() if e['pid'] in mine)
            if ours:
                e = d['ports'][ours[-1][1]]; e['copies'] = sorted(set(e['copies']) | set(insts)); return int(ours[-1][1])
        return _new(d, table, insts, owner, pid or os.getpid())
    return _locked(f)


def release(port):
    """give a port back (its holder's run is over); it rests 6 h"""
    def f(d, table):
        e = d['ports'].pop(str(port), None)
        if e:
            d['rest'][str(port)] = time.time(); _log(f'port {port} ({e.get("owner")}) released')
        return e is not None
    return _locked(f)


def owned(port, inst):
    """a live lease of port lists the copy, or is held by this process or a parent (then the copy is added)"""
    k = key(inst)
    def f(d, table):
        e = d['ports'].get(str(port))
        if not e:
            return False
        if k in e['copies']:
            return True
        if e['pid'] in _chain(table):
            e['copies'] = sorted(set(e['copies']) | {k}); return True
        return False
    try:
        return int(port) > 0 and _locked(f)
    except (TypeError, ValueError):
        return False


def leases():
    return _locked(lambda d, table: json.loads(json.dumps(d['ports'])))


# ------------------------------------------------------------------ the copy's files
def _read(p):
    try:
        return json.load(open(p))
    except (OSError, ValueError):
        return None


def _write(p, d):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + '.tmp', 'w') as f:
        json.dump(d, f)
    os.replace(p + '.tmp', p)


def write_mp(inst, cfg):
    """mp.json of a copy: cfg = {'role': 'host'|'client', 'port': <leased port>, 'host': '127.0.0.1' (client), ...};
    refused when the port is not this copy's (lease it first)"""
    port = cfg.get('port')
    if cfg.get('role') == 'client':
        cfg = dict({'host': '127.0.0.1'}, **cfg)
    if not owned(port, inst):
        raise SystemExit(f'refused: mp.json of {os.path.basename(inst)}: port {port} is not leased to this run (rigports.lease / port_for first)')
    _write(os.path.join(inst, 'mp.json'), cfg)


def loader_settings_file(inst, env):
    """the loader's settings.json of a copy started with env: DSL_SETTINGS_FILE, else <DSML_DATA>/Mods, else the profile's
    folder (DSL_PROFILE, else <DSL_MODS_DIR or the app's Contents/Resources/mods>/profile.json). Sources (ds_modloader):
    src/dsl_settings.c settings_path, src/dsl_reg.c dsl_reg_data_dir, src/dsl_main.c dsl_core_start,
    start/mac/dsl_start_mac.c mods_default."""
    if env.get('DSL_SETTINGS_FILE'):
        return env['DSL_SETTINGS_FILE']
    if env.get('DSML_DATA'):
        return os.path.join(env['DSML_DATA'], 'Mods', 'settings.json')
    prof = env.get('DSL_PROFILE') or os.path.join(env.get('DSL_MODS_DIR') or os.path.join(inst, 'DungeonSettlers.app', 'Contents', 'Resources', 'mods'), 'profile.json')
    return os.path.join(os.path.dirname(prof), 'settings.json')


def set_host_port(inst, port, settings_file=None):
    """the multiplayer setting "host_port" of a loader copy (the port a menu host listens on; without it: 47777, the
    owner's game): settings_file default <inst>/data/Mods/settings.json; other settings are kept"""
    p = settings_file or os.path.join(inst, 'data', 'Mods', 'settings.json')
    if not owned(port, inst):
        raise SystemExit(f'refused: host_port of {os.path.basename(inst)}: port {port} is not leased to this run')
    d = _read(p) if os.path.exists(p) else {}
    if not isinstance(d, dict):
        raise SystemExit(f'refused: {p} is not a JSON object: host_port not set')
    if (d.get('multiplayer') or {}).get('host_port') != port:
        d.setdefault('multiplayer', {})['host_port'] = int(port)
        _write(p, d)
    return p


def clear_stale_mp(inst, t0=None):
    """an mp.json written before this run started (another lane's, an old run's) is set aside before a start; returns
    its new name or None"""
    p = os.path.join(inst, 'mp.json')
    try:
        mt = os.path.getmtime(p)
    except OSError:
        return None
    if mt >= (run_t0() if t0 is None else t0):
        return None
    d = _read(p) or {}
    dst = p + time.strftime('.left-%Y%m%d-%H%M%S')
    os.replace(p, dst)
    print(f'{os.path.basename(inst)}: mp.json from before this run set aside ({d.get("role")} {d.get("host", "")}:{d.get("port")}) -> {dst}', flush=True)
    return dst


def check_start(inst, settings_file=None):
    """GUARD before a copy starts: its mp.json (host, or client of a host on this Mac) and its host_port setting must
    point at a port leased to this run; else SystemExit (the copy would join or host another lane's session)"""
    name = os.path.basename(inst)
    mp = _read(os.path.join(inst, 'mp.json')) if os.path.exists(os.path.join(inst, 'mp.json')) else None
    if mp is not None:
        if not isinstance(mp, dict):
            raise SystemExit(f'refused: copy {name}: mp.json is not a JSON object')
        role, port = mp.get('role'), mp.get('port')
        local = role == 'host' or str(mp.get('host', '')).strip().lower() in LOCAL or str(mp.get('host', '')).startswith('127.')
        if role in ('host', 'client') and local and not owned(port, inst):
            raise SystemExit(f'refused: copy {name}: mp.json ({role}, port {port}) points at a port this run does not own '
                             f'(lease one: rigports.port_for / lease, write it with rigports.write_mp)')
    if settings_file and os.path.exists(settings_file):
        hp = ((_read(settings_file) or {}).get('multiplayer') or {}).get('host_port')
        if hp is not None and not owned(hp, inst):
            raise SystemExit(f'refused: copy {name}: host_port {hp} in {settings_file} is not a port of this run')


# ------------------------------------------------------------------ players in the session
def _send(send, x, obj):
    try:
        return send(x, obj, timeout=8, connect_wait=0)
    except TypeError:
        return send(x, obj)


def session_view(send, x):
    """copy x's multiplayer session: {'ids': {player id: name}, 'spect': n, 'me': own id, 'host': bool}; None = none"""
    try:
        s = _send(send, x, {'cmd': 'status'})
    except Exception:
        return None
    mp = (s or {}).get('mp') or {}
    if not mp.get('started'):
        return None
    names = {}
    try:
        r = _send(send, x, {'cmd': 'mp_steam_state'})
        names = {int(p['id']): p.get('name') or '' for p in (r or {}).get('players', [])}
    except Exception:
        pass
    pm = int(mp.get('present') or 0)
    return {'ids': {i: names.get(i, '') for i in range(32) if pm >> i & 1}, 'spect': bin(int(mp.get('spectators') or 0)).count('1'),
            'me': mp.get('player'), 'host': bool(mp.get('host'))}


def players_guard(send, copies, expected=None, phase='', settle=12.0, report=True):
    """the session's player count seen by each copy (players + spectators) must not pass expected (default: the number
    of copies). Above it for longer than `settle` s (a left player's slot drops after 5 s) = one "EXTRA PLAYER <name>"
    line per player that is none of these copies. Returns (ok, lines); report=True: printed and kept in EXTRA (the
    process then ends with exit code 1)."""
    copies = list(copies); expected = expected or len(copies)
    t_end = time.time() + settle
    while True:
        views = {x: session_view(send, x) for x in copies}
        bad = {x: v for x, v in views.items() if v and len(v['ids']) + v['spect'] > expected}
        if not bad or time.time() >= t_end:
            break
        time.sleep(2)
    ours = {v['me'] for v in views.values() if v and v.get('me') is not None}
    lines = []
    for x, v in bad.items():
        n = len(v['ids']) + v['spect']
        extra = [nm or f'player {i}' for i, nm in v['ids'].items() if i not in ours] or [f'spectator x{v["spect"]}' if v['spect'] else 'unknown']
        lines += [f'EXTRA PLAYER {nm} (copy {x} sees {n} in its session, expected {expected}{"; " + phase if phase else ""})' for nm in extra]
    if report:
        for l in lines:
            print(l, flush=True)
        EXTRA.extend(lines)
    return not lines, lines


def expect_peers(n):
    """players in this process's sessions that are not copies it started (a Windows VM guest): the stop check allows them"""
    _peers[0] = int(n)


def note_started(inst, name):
    STARTED[key(inst)] = name


def note_released(insts):
    for i in insts:
        STARTED.pop(key(i), None)


def end_check(send, x, inst):
    """rig.stop / lrig.stop, before the copy quits: at most this process's copies (+ expect_peers) in its session"""
    if key(inst) not in STARTED:
        return True
    return players_guard(send, [x], len(STARTED) + _peers[0], 'stop check')[0]


@atexit.register
def _fail_on_extra():
    """registered at import, so it runs last: a run that saw an EXTRA PLAYER ends with exit code 1"""
    if EXTRA:
        print(f'RESULT FAIL EXTRA PLAYER: {len(EXTRA)} line(s), first: {EXTRA[0]}', flush=True)
        sys.stderr.flush()
        os._exit(1)


def main():
    a = sys.argv[1:]
    if a[:1] == ['leases']:
        for p, e in sorted(leases().items()):
            print(p, e['owner'], 'holder', e['pid'], 'since', time.strftime('%H:%M', time.localtime(e['since'])), [os.path.basename(c) for c in e['copies']])
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
