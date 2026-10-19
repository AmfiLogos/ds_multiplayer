#!/usr/bin/env python3
"""A guest joins a host whose save opened the stock Lock Consecration notice on load (v0.1.16 fix, ord_game.c h_b8_0).

  popup_join_test.py [--answer yes|no] [--play 120] [--save PATH]

The game's load (CampaignLoadingEventHandler.LoadCampaign) waits while NeedsLockConsecrationNotice(): an older save
without CampaignSettingSaveData.CampaignStartSettingSaveData.LockConsecrationNoticeAcknowledged opens a Yes/No box.
Until v0.1.15 the host's answer (ReplyLockConsecrationNoticeRequested) was captured as an order; orders run only in
steps, steps never run while the load waits, so the load never ended: no join point, the guest got "The host's world
did not arrive" after 90 s.
X hosts and loads the save (default: the wiki's Template save, ~/dungeonsettlersdb/site/assets/saves/base_25846389.json), the box must
show; X answers (ui_notice confirm / cancel); Y joins by address. Checks: Y gets the world within 180 s, no notice on Y,
then --play seconds of orders from both sides with 0 desyncs (mp counters, DESYNC / FULL-STATE / deep lines, det
tick_errors, fp.log equal). Hidden copies (window hidden, guard on). POPUP_DYLIB = a dsrig.dylib to test (default the
rig's). Output: POPUP_OUT, else build/core_test/popupjoin_<time>/results.json."""
import json, os, random, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

rig.DYLIB = os.environ.get('POPUP_DYLIB') or rig.DYLIB


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


ANSWER = arg('--answer', 'yes')
PLAY = arg('--play', 120)
OUT = os.environ.get('POPUP_OUT') or os.path.join(rig.ROOT, 'build', 'core_test', 'popupjoin_' + time.strftime('%Y%m%d-%H%M%S'))
checks, info = [], {}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, detail)
    return ok


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    s = {}
    for _ in range(20):
        try:
            s = c(x, cmd='status')
        except Exception as e:
            s = {'error': str(e)}
        if 'tick' in s or 'scene' in s:
            return s
        time.sleep(0.5)
    raise RuntimeError(f'{x}: no status')


def wait(what, pred, timeout=900, every=1.0):
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


def notice_text(x):
    """the Lock Consecration box's text while it shows, else None"""
    t = c(x, cmd='ui_texts', min=10).get('texts', [])
    return next((e['text'] for e in t if 'CommonNotice' in e.get('path', '') and 'Lock Consecration' in e.get('text', '')), None)


def flog(x, name, pos=0):
    try:
        with open(os.path.join(rig.idir(x), name), 'rb') as f:
            f.seek(pos); return f.read().decode('utf8', 'replace')
    except OSError:
        return ''


def fsize(x, name):
    p = os.path.join(rig.idir(x), name)
    return os.path.getsize(p) if os.path.exists(p) else 0


def fp_map(x, t0):
    out = {}
    for line in flog(x, 'fp.log').splitlines():
        p = line.split()
        if len(p) >= 5 and p[0].isdigit() and int(p[0]) > t0:
            out[int(p[0])] = p[1]
    return out


def order_move(x, guid, cell):
    c(x, cmd='ord_hover', n=0)
    r1 = c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
    r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
    return r1.get('ok') and r2.get('ok')


TEMPLATE = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki's public Template save (Day 0, no notice flag)


def the_save(work):
    """--save as given, else the wiki Template save (an older save without LockConsecrationNoticeAcknowledged)"""
    dst = os.path.join(work, 'popup.json')
    shutil.copy2(arg('--save', TEMPLATE), dst)
    return dst


def play(X, Y):
    us = [u for u in c(X, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    mine_y = [u['guid'] for u in us[:2]]
    for g in mine_y:
        c(Y, cmd='mp_claim', guid=g)
    time.sleep(3)
    for x in (X, Y):
        c(x, cmd='det_frame_reset')
    rp = {x: fsize(x, 'rig.log') for x in (X, Y)}
    m0 = {x: st(x).get('mp', {}) for x in (X, Y)}
    e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (X, Y)}
    tick0 = max(st(X)['tick'], st(Y)['tick'])
    orders = {X: 0, Y: 0}; rnd = random.Random(16); t0 = time.time()
    while time.time() - t0 < PLAY:
        rig.touch(X)
        for x in (X, Y):
            uu = [u for u in c(x, cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
            cand = [u for u in uu if (u['guid'] in mine_y) == (x == Y)]
            if cand:
                u = rnd.choice(cand)
                if order_move(x, u['guid'], (u['cell'][0] + rnd.randint(-6, 6), u['cell'][1] + rnd.randint(-6, 6))):
                    orders[x] += 1
        time.sleep(10)
    time.sleep(3)
    sx, sy = st(X), st(Y)
    lines = {k: [l for l in flog(Y, 'rig.log', rp[Y]).splitlines() + flog(X, 'rig.log', rp[X]).splitlines() if pat in l]
             for k, pat in (('desync', 'DESYNC:'), ('full_diff', 'FULL-STATE CHECK DIFFERS'), ('deep_diff', 'deep hash differs'), ('report', 'Desync report saved'))}
    a, b = fp_map(X, tick0), fp_map(Y, tick0); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    te = {x: s.get('det', {}).get('tick_errors', 0) - e0[x] for x, s in ((X, sx), (Y, sy))}
    ds = {x: s.get('mp', {}).get('desyncs', 0) - m0[x].get('desyncs', 0) for x, s in ((X, sx), (Y, sy))}
    info['play'] = {'play_s': PLAY, 'orders': orders, 'ticks': [tick0, sx.get('tick'), sy.get('tick')], 'fp_common_ticks': len(com),
                    'fp_first_diff': first, 'desyncs': ds, 'tick_errors': te, 'full_fails': sy.get('mp', {}).get('full_fails'),
                    **{k: v[:5] for k, v in lines.items()}}
    check('play: both gave orders, the world advanced', orders[X] >= 3 and orders[Y] >= 3 and (sy.get('tick') or 0) > tick0 + 100,
          f"orders {orders}, ticks {info['play']['ticks']}")
    check('play: 0 desyncs (mp counters, desync / full-state / deep lines, det tick_errors, fp.log equal)',
          not any(ds.values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None
          and not sy.get('mp', {}).get('full_fails'), json.dumps({k: info['play'][k] for k in ('desyncs', 'tick_errors', 'fp_common_ticks', 'fp_first_diff', 'full_fails')}))


def main():
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time(); pair = rig.lease(f'popup-{os.getpid()}', os.getpid())
    while not pair and time.time() - t0 < 3600:                  # the pair limit: wait for a lane to give one back
        log('no free pair: retry in 2 min'); time.sleep(120); pair = rig.lease(f'popup-{os.getpid()}', os.getpid())
    if not pair:
        raise SystemExit('no free pair for 60 min')
    X, Y = pair[0], pair[1]; port = rig.PAIR_PORTS[pair]
    info.update(pair=pair, answer=ANSWER, dylib=rig.DYLIB)
    yj = os.path.join(rig.idir(Y), 'mp.json')
    try:
        run(X, Y, port, yj)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        for x in (X, Y):
            for f in ('rig.log', 'player.log', 'fp.log'):
                p = os.path.join(rig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        rig.stop(Y); rig.stop(X)
        if os.path.exists(yj + '.off'):
            os.replace(yj + '.off', yj)
        rig.release(pair)
    res = {'checks': checks, 'all_pass': bool(checks) and all(k['ok'] for k in checks), 'info': info}
    json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
    log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', os.path.join(OUT, 'results.json'))
    return res


def run(X, Y, port, yj):
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    if os.path.exists(yj):
        os.replace(yj, yj + '.off')
    rig.start(X, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    c(X, cmd='load_save', file=the_save(work), det=1)
    txt = wait('the Lock Consecration notice on X', lambda: notice_text(X), timeout=300, every=2)
    if not check('X: the save opens the Lock Consecration notice on load', txt, txt):
        return
    r = c(X, cmd='ui_notice', button='confirm' if ANSWER == 'yes' else 'cancel')
    gone = wait('notice closed', lambda: not notice_text(X), timeout=30, every=1)
    check(f'X answers {ANSWER}: the notice closes', r.get('ok') and gone, r)
    rig.start(Y, window=True)
    rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
    tj = time.time()
    c(Y, cmd='mpm_join_tcp', host='127.0.0.1', port=port, name='Guest')
    sy = wait('Y has the world', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                        and not s.get('mp', {}).get('needs_state') else None)(st(Y)), timeout=180, every=1)
    info['join_s'] = round(time.time() - tj, 1)
    if not check('Y gets the world (loaded, locked) within 180 s', sy, f"{info['join_s']} s; X {json.dumps(st(X).get('mp', {}))[:300]}"):
        return
    time.sleep(3)
    check('Y: no Lock Consecration notice (the answer came with the save)', not notice_text(Y))
    play(X, Y)


if __name__ == '__main__':
    sys.exit(0 if main()['all_pass'] else 1)
