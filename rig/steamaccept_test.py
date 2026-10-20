#!/usr/bin/env python3
"""Accepted Steam invites reach the multiplayer code (steamaccept, 2026-10-20). Fails on v0.1.19 and older.

  steamaccept_test.py            STEAMACCEPT_DYLIB = the dsrig.dylib to test (default the rig's)

The game's CallbackDispatcher.RunFrame has the P/Invoke wrapper of SteamAPI_ManualDispatch_GetNextCallback inlined: it
reads the wrapper's static slot (the native function) and calls it. Until v0.1.19 we hooked the wrapper, which never
runs, so GameLobbyJoinRequested (333) never reached dsnet_steam: accepting an invite did nothing.
The rig has no Steam and no second account: one hidden copy, a synthetic queue through the REAL slot.
Checks:
  1. the slot the mod found BY NAME is the slot RunFrame's own code loads (decoded from this copy's GameAssembly);
  2. the slot holds the mod's wrapper; every callback of the queue (304, 1101 stats, 333, 1102 stats) comes back to the
     caller in order with its own param pointer and size (stock callbacks unchanged), 333 is handed on once;
  3. title: the 333 starts the guest join ("Steam invite accepted (steam:<lobby>)");
  4. in a world: the 333 opens "Join <host>?" (invite pending, the box text on screen); Cancel keeps the world.
Output: STEAMACCEPT_OUT, else build/core_test/steamaccept_<time>/results.json."""
import glob, json, os, shutil, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

rig.DYLIB = os.environ.get('STEAMACCEPT_DYLIB') or rig.DYLIB
OUT = os.environ.get('STEAMACCEPT_OUT') or os.path.join(rig.ROOT, 'build', 'core_test', 'steamaccept_' + time.strftime('%Y%m%d-%H%M%S'))
RUNFRAME = {'25846389': 0x1167898}          # Steamworks.CallbackDispatcher$$RunFrame (Mac dump), the reference build
TEMPLATE = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public Template save
checks, info = [], {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, detail)
    return ok


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    return c(x, cmd='status')


def wait(pred, timeout=300, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
        except (OSError, ValueError, RuntimeError):
            r = None
        if r:
            return r
        time.sleep(every)
    return None


def rlog(x, pos=0):
    try:
        with open(os.path.join(rig.idir(x), 'rig.log'), 'rb') as f:
            f.seek(pos); return f.read().decode('utf8', 'replace')
    except OSError:
        return ''


def runframe_slots(x):
    """every adrp + ldr x,[x,#imm] target in RunFrame's first 0x600 bytes, from this copy's GameAssembly (arm64)"""
    ga = (glob.glob(os.path.join(rig.idir(x), '**', 'GameAssembly.dylib'), recursive=True) or [None])[0]
    if not ga:
        return None, set()
    d = open(ga, 'rb').read(); rf = RUNFRAME['25846389']; out = set(); base = 0
    if struct.unpack_from('>I', d, 0)[0] == 0xCAFEBABE:            # universal binary: the arm64 slice
        for k in range(struct.unpack_from('>I', d, 4)[0]):
            cpu, _, off, _, _ = struct.unpack_from('>IIIII', d, 8 + 20 * k)
            if cpu == 0x0100000C:
                base = off
    for i in range(0, 0x600, 4):
        a, b = struct.unpack_from('<II', d, base + rf + i)
        if (a & 0x9F000000) == 0x90000000 and (b & 0xFFC00000) == 0xF9400000 and ((b >> 5) & 31) == (a & 31):
            imm = ((a >> 29) & 3) | (((a >> 5) & 0x7FFFF) << 2)
            imm -= (1 << 21) if imm & (1 << 20) else 0
            out.add(((rf + i) & ~0xFFF) + imm * 4096 + ((b >> 10) & 0xFFF) * 8)
    return ga, out


def inject(x, lobby):
    r = c(x, cmd='mp_test_steamcb', lobby=str(lobby))
    ok = r.get('ok') and r.get('wrapped') == 1 and [o[0] for o in r.get('out', [])] == r.get('in') \
        and all(o[2] == 1 for o in r.get('out', [])) and [o[1] for o in r.get('out', [])] == [12, 12, 16, 12] \
        and r.get('through') == 4 and r.get('fed') == 1
    return r, ok


def run(X):
    rig.start(X, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
    time.sleep(2)
    s0 = c(X, cmd='mp_steamcb'); info['slot_before'] = s0
    ga, slots = runframe_slots(X)
    rva = int(s0.get('slot_rva', '0'), 16) if s0.get('ok') else 0
    check('1. slot found by name = the slot CallbackDispatcher.RunFrame loads (game code)',
          s0.get('state') == 1 and s0.get('by') == 'name' and rva in slots, f'{s0}; RunFrame loads {sorted(hex(v) for v in slots)} ({ga})')
    pos = os.path.getsize(os.path.join(rig.idir(X), 'rig.log'))
    r, ok = inject(X, 109775241000000001)
    info['title_inject'] = r
    check('2. the slot holds our wrapper; 304/1101/333/1102 pass unchanged in order, 333 handed on once', ok, r)
    line = wait(lambda: next((l for l in rlog(X, pos).splitlines() if 'Steam invite accepted (steam:109775241000000001)' in l), None), timeout=30)
    check('3. title: the accepted invite starts the guest join', line, line)
    c(X, cmd='mp_steam_leave')                       # the rig has no Steam: the join stops at "Steam not ready"; reset
    time.sleep(2)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    sv = os.path.join(work, 'steamaccept.json'); shutil.copy2(TEMPLATE, sv)
    c(X, cmd='load_save', file=sv, det=0)
    if not wait(lambda: (lambda s: s.get('scene') == 2 and not s.get('busy'))(st(X)), timeout=900, every=2):
        return check('X in a world', False)
    time.sleep(8)
    own = c(X, cmd='mp_world').get('campaign')
    r, ok = inject(X, 109775241000000002)
    info['world_inject'] = r
    w = wait(lambda: (lambda w: w if w.get('invite_pending') == 1 else None)(c(X, cmd='mp_world')), timeout=20)
    box = wait(lambda: next((e['text'] for e in c(X, cmd='ui_texts', min=4).get('texts', [])
                             if 'Join ' in e.get('text', '') and '?' in e['text']), None), timeout=20)
    check('4. in a world: the accepted invite opens "Join <host>?" (pending, box shown)', ok and w and not w.get('session') and box, f'{w} box={box!r}')
    c(X, cmd='mp_invite_answer', choice=0); time.sleep(3)
    w = c(X, cmd='mp_world')
    check('4b. Cancel keeps the own world', not w.get('invite_pending') and not w.get('session') and w.get('campaign') == own, w)
    info['slot_after'] = c(X, cmd='mp_steamcb')


def main():
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time(); pair = rig.lease(f'steamaccept-{os.getpid()}', os.getpid())
    while not pair and time.time() - t0 < 3600:
        log('no free pair: retry in 2 min'); time.sleep(120); pair = rig.lease(f'steamaccept-{os.getpid()}', os.getpid())
    if not pair:
        raise SystemExit('no free pair for 60 min')
    X = pair[0]; info.update(pair=pair, dylib=rig.DYLIB)
    mj = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(mj):
        os.replace(mj, mj + '.off')
    try:
        run(X)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for f in ('rig.log', 'player.log'):
            p = os.path.join(rig.idir(X), f)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f'{X}_{f}'))
        rig.stop(X)
        if os.path.exists(mj + '.off'):
            os.replace(mj + '.off', mj)
        rig.release(pair)
    res = {'checks': checks, 'all_pass': bool(checks) and all(k['ok'] for k in checks), 'info': info}
    json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
    log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', os.path.join(OUT, 'results.json'))
    return res


if __name__ == '__main__':
    sys.exit(0 if main()['all_pass'] else 1)
