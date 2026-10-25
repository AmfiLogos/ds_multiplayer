#!/usr/bin/env python3
"""rigports.py without the game: the breaks that made two-player runs of different lanes join each other (2026-10-25).

1. Two pairs started at the same time never meet: two lane processes at once, each leases a port for its pair through
   rig.py (lease + PAIR_PORTS / pair_port) and starts a fake host + fake guest from mp.json through the start guard;
   every guest must reach ITS OWN run's host, and the two runs' ports differ (x10 rounds).
2. A copy holding another run's mp.json (copy T, 2026-10-25: client 127.0.0.1:7831) is set aside before the start, and
   a copy whose mp.json / host_port points at a port this run does not own is refused.
3. EXTRA PLAYER: a third player in the session fails the guard with its name; a left player's slot that drops within the
   settle time does not.
Temp lease folders and ports 30000-30999 only: never the real leases, never a game copy, never 47777.

  python3 rigports_test.py      -> "RESULT rigports PASS n/n" (exit 0) or FAIL lines
"""
import json, os, socket, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = tempfile.mkdtemp(prefix='rigports_test_')
os.environ.update(DSRIG_PORT_DIR=os.path.join(TMP, 'ports'), DSRIG_LEASE_DIR=os.path.join(TMP, 'leases'), DSRIG_PORT_RANGE='30000-30999')
os.makedirs(os.environ['DSRIG_LEASE_DIR'], exist_ok=True)
sys.path.insert(0, HERE)
import rigports  # noqa: E402

checks = []


def check(name, ok, info=''):
    checks.append(ok)
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info and not ok else ''), flush=True)


# ------------------------------------------------------------------ 1. two lanes at once
LANE = r'''
import json, os, socket, sys, threading, time
sys.path.insert(0, sys.argv[1]); inst_root = sys.argv[2]; lane = sys.argv[3]
import rig, rigports
pair = rig.lease(lane, os.getpid())
port = rig.PAIR_PORTS[pair]
X, Y = pair[0], pair[1]
hx, gy = os.path.join(inst_root, X), os.path.join(inst_root, Y)
for d in (hx, gy): os.makedirs(d, exist_ok=True)
rigports.write_mp(hx, {'role': 'host', 'port': port, 'name': lane + '-host'})
json.dump({'role': 'client', 'host': '127.0.0.1', 'port': 7831, 'name': lane + '-guest'}, open(os.path.join(gy, 'mp.json'), 'w'))
rig.fix_mp_port(Y)                       # the lane wrote an old fixed port: the start makes it the run's port
for d in (hx, gy): rigports.check_start(d)
mh, mg = json.load(open(os.path.join(hx, 'mp.json'))), json.load(open(os.path.join(gy, 'mp.json')))
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.bind(('127.0.0.1', mh['port'])); ls.listen(4)
def serve():
    while True:
        c, _ = ls.accept(); c.sendall((lane + '\n').encode()); c.close()
threading.Thread(target=serve, daemon=True).start()
time.sleep(0.3)                          # both lanes' hosts are up before any guest connects
c = socket.create_connection((mg['host'], mg['port']), 2); got = c.recv(64).decode().strip(); c.close()
print(json.dumps({'lane': lane, 'pair': pair, 'port': port, 'guest_port': mg['port'], 'met': got}), flush=True)
time.sleep(0.5); rig.release(pair, lane)
'''


def two_lanes(rnd):
    root = os.path.join(TMP, f'inst{rnd}')
    env = dict(os.environ, DSRIG_INSTANCES=root)
    ps = [subprocess.Popen([sys.executable, '-c', LANE, HERE, root, f'lane{k}'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
          for k in (1, 2)]
    out = [p.communicate(timeout=60) for p in ps]
    try:
        r = [json.loads(o[0].strip().splitlines()[-1]) for o in out]
    except Exception:
        return None, [o[1][-300:] for o in out]
    return r, None


fails = []
for rnd in range(10):
    r, err = two_lanes(rnd)
    if err:
        fails.append(f'round {rnd}: {err}'); continue
    if r[0]['port'] == r[1]['port'] or any(x['met'] != x['lane'] or x['guest_port'] != x['port'] or x['port'] in rigports.NEVER for x in r):
        fails.append(f'round {rnd}: {r}')
check('two pairs started at the same time: each guest meets its own host, ports differ (10 rounds)', not fails, fails[:2])

# ------------------------------------------------------------------ 2. stale mp.json, refusals
inst = os.path.join(TMP, 'lrig', 'T'); os.makedirs(inst)
mp = os.path.join(inst, 'mp.json')
json.dump({'role': 'client', 'host': '127.0.0.1', 'port': 7831, 'name': 'old'}, open(mp, 'w'))
os.utime(mp, (time.time() - 3600, time.time() - 3600))            # written by an earlier run
moved = rigports.clear_stale_mp(inst)
check('an mp.json from before this run is set aside before the start', moved and not os.path.exists(mp) and os.path.exists(moved), moved)
json.dump({'role': 'client', 'host': '127.0.0.1', 'port': 7831}, open(mp, 'w'))
check('this run\'s own new mp.json is kept', rigports.clear_stale_mp(inst) is None and os.path.exists(mp))


def refused(fn):
    try:
        fn(); return False
    except SystemExit as e:
        return 'refused' in str(e)


check('start refused: mp.json client at a port this run does not own (7831)', refused(lambda: rigports.check_start(inst)))
other = subprocess.Popen([sys.executable, '-c', f'import sys,time; sys.path.insert(0, {HERE!r}); import rigports; '
                          f'print(rigports.lease([{os.path.join(TMP, "lrig", "Q")!r}], "other lane"), flush=True); time.sleep(30)'],
                         stdout=subprocess.PIPE, text=True)
oport = int(other.stdout.readline())
json.dump({'role': 'host', 'port': oport}, open(mp, 'w'))
check("start refused: mp.json host on another live run's port", refused(lambda: rigports.check_start(inst)))
check("write_mp refused on another live run's port", refused(lambda: rigports.write_mp(inst, {'role': 'client', 'port': oport})))
check("a copy listed in another live run is refused by port_for", refused(lambda: rigports.port_for([os.path.join(TMP, 'lrig', 'Q')], 'me')))
mine = rigports.port_for([inst], 'me', share=True)
rigports.write_mp(inst, {'role': 'client', 'port': mine})
sf = os.path.join(inst, 'settings.json'); json.dump({'other-mod': {'x': 1}, 'multiplayer': {'host_port': 47777}}, open(sf, 'w'))
check('start refused: host_port 47777 (the owner\'s game port)', refused(lambda: rigports.check_start(inst, sf)))
rigports.set_host_port(inst, mine, sf); rigports.check_start(inst, sf)
s = json.load(open(sf))
check('own port: mp.json + host_port accepted, other settings kept', s == {'other-mod': {'x': 1}, 'multiplayer': {'host_port': mine}}, s)
other.kill(); other.wait()
check('a lease whose holder ended is freed (its port rests, never handed out again soon)',
      str(oport) not in rigports.leases() and rigports._locked(lambda d, t: str(oport) in d['rest']))
check('lease file never hands out 47777 or an old pair port', not any(int(p) in rigports.NEVER for p in rigports.leases()))


# ------------------------------------------------------------------ 3. EXTRA PLAYER
def fake(sessions):
    """send() of fake copies: sessions[x] = list of states [(present mask, names, own id)], one per status call"""
    calls = {x: 0 for x in sessions}
    def send(x, obj, timeout=None, connect_wait=None):
        st = sessions[x][min(calls[x], len(sessions[x]) - 1)]
        if obj['cmd'] == 'status':
            calls[x] += 1
            return {'scene': 2, 'mp': {'started': True, 'present': st[0], 'spectators': 0, 'player': st[2], 'host': st[2] == 0}}
        return {'ok': True, 'players': [{'id': i, 'name': n} for i, n in st[1].items()]}
    return send


three = (0b111, {0: 'Host', 1: 'Guest', 2: 'Bob of another lane'}, 0)
ok, lines = rigports.players_guard(fake({'H': [three], 'G': [(0b111, three[1], 1)]}), ['H', 'G'], 2, 'end', settle=0.5, report=False)
check('3 players in a 2-player run: the guard names the extra player', not ok and lines and all(l.startswith('EXTRA PLAYER Bob of another lane') for l in lines) and not rigports.EXTRA, lines)
two = (0b11, {0: 'Host', 1: 'Guest'}, 0)
ok, lines = rigports.players_guard(fake({'H': [two], 'G': [(0b11, two[1], 1)]}), ['H', 'G'], None, 'start', settle=0.5, report=False)
check('2 players in a 2-player run: ok', ok and not lines, lines)
ok, lines = rigports.players_guard(fake({'H': [three, three, two], 'G': [(0b11, two[1], 1)]}), ['H', 'G'], 2, 'end', settle=10, report=False)
check("a left player's slot that drops within the settle time: ok", ok, lines)

n = sum(checks)
print(f'RESULT rigports {"PASS" if n == len(checks) else "FAIL"} {n}/{len(checks)}  (temp {TMP})', flush=True)
sys.exit(0 if n == len(checks) else 1)
