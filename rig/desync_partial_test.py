#!/usr/bin/env python3
"""lan_v10 core: forced desyncs -> desync report + partial resync (or the full-reload fallback), on one leased pair.

  desync_partial_test.py [--kinds rng,hp,pos,item] [--runs 10] [--fallback 3] [--save PATH] [--keep]

X hosts (mp.json) and loads the save (det on); Y joins from its title screen (join point). Then, per kind, --runs times:
det_corrupt {kind} on Y (outside the sim: sim RNG / a unit's hp -7 / a unit moved one cell / a map item's position entry
moved one cell) -> Y flags the desync at the next fingerprint -> its report part goes to X, X writes
data/desync_reports/<time>-player1-step<S>/ -> partial resync at the next PATCH_POINT. Checked every run: the partial
counter grew with no load (no reload screen) and no pause, Y back in sync, then 120 more steps with no new desync and
every common fp.log tick equal from the patch on; the report folder exists with both parts and names the broken section.
--fallback N: kind "task" (a unit's combat state: not patchable) must end in the full reload, equal after.
Results: build/core_test/partial_<time>/results.json (+ the copies' logs on a failure)."""
import json, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

results, timings = [], {}


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def st(x):
    for _ in range(20):
        try:
            s = rig.send(x, {'cmd': 'status'}, timeout=60)
        except Exception as e:
            s = {'error': str(e)}
        if 'tick' in s:
            return s
        time.sleep(1)
    raise RuntimeError(f'{x}: status not answered: {s}')


def mp(x):
    return st(x).get('mp', {})


def wait(what, pred, timeout=600, every=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def check(name, ok, info=''):
    results.append({'name': name, 'ok': bool(ok), 'info': info})
    log(('PASS ' if ok else 'FAIL ') + name, info)
    return ok


def fp_lines(x, t_from):
    d = {}
    for line in open(os.path.join(rig.idir(x), 'fp.log')):
        p = line.split()
        if len(p) >= 5 and p[0].isdigit() and int(p[0]) >= t_from:
            d[int(p[0])] = p[1:5]
    return d


def fp_equal_from(X, Y, t_from):
    a, b = fp_lines(X, t_from), fp_lines(Y, t_from)
    com = sorted(set(a) & set(b))
    first = next((t for t in com if a[t] != b[t]), None)
    return len(com), first


def last_partial_line(x, after_bytes):
    p = os.path.join(rig.idir(x), 'rig.log')
    with open(p, 'rb') as f:
        f.seek(after_bytes)
        txt = f.read().decode('utf8', 'replace')
    m = re.findall(r'PARTIAL RESYNC at step (\d+): .*?\((\d+) ms after the desync flag, apply ([\d.]+) ms\)', txt)
    why = re.findall(r'partial resync (?:applied|REFUSED): (.*)', txt)
    return m[-1] if m else None, why[-1] if why else None


SECTION = {'gold': 'full','rng': 'rng+tick', 'hp': 'units', 'pos': 'positions', 'item': 'positions', 'task': 'units'}


def ensure_items(X, Y, want=3):
    """item kind: loose items on a map (base workers haul them away over time). If none: the host drops items from unclaimed
    dungeon units' inventories (DropItemRequested through the normal order path, so both peers run it)."""
    d = rig.send(Y, {'cmd': 'det_items'}, timeout=60)
    if d.get('on_map', [0, 0])[1] >= 1:          # dungeon items: nobody walks to them (base workers haul base items)
        return d
    carried = [x for x in rig.send(X, {'cmd': 'det_items'}, timeout=60).get('carried', []) if x['map'] == 1][:want]
    for x in carried:
        rig.send(X, {'cmd': 'ord_hover', 'n': 0}, timeout=60)
        # DropItemRequested {IsInventory 1, Map 1, UnitIndex -1 (= use UnitGuid, not a party slot), UnitGuid, Index}
        r = rig.send(X, {'cmd': 'ord_inject', 'ev': 'DropItemRequested', 'u8@16': 1, 'i32@20': 1, 'i32@24': -1, 'i32@28': 0,
                         'guid@32': x['unit'], 'i32@48': x['slot']}, timeout=60)
        log('drop item for the item test', x['item'], 'from', x['unit'], 'slot', x['slot'], r.get('ok'))
    try:
        return wait('items on the dungeon map', lambda: (lambda d: d if d.get('on_map', [0, 0])[1] >= 1 else None)(rig.send(Y, {'cmd': 'det_items'}, timeout=60)), timeout=60)
    except RuntimeError:
        return rig.send(Y, {'cmd': 'det_items'}, timeout=60)


OUTCOMES = {}


def settle(Y, quiet_steps):
    """wait until Y ran `quiet_steps` steps with no new desync, no load, not desynced; returns its mp status then"""
    for _ in range(40):
        m0 = mp(Y)
        t_s = m0.get('timer', 0) + quiet_steps
        wait('settle', lambda: (lambda m: m.get('timer', 0) >= t_s and not m.get('needs_state') and not m.get('loading'))(mp(Y)), timeout=900)
        m1 = mp(Y)
        if m1.get('desyncs') == m0.get('desyncs') and m1.get('loads') == m0.get('loads') and not m1.get('desynced') and not m0.get('needs_state'):
            return m1
    return mp(Y)


def one_run(X, Y, kind, n, fallback=False):
    """corrupt Y once. Outcomes: "partial" (patched in place, deep check equal, no new difference in the next ~10 s
    probation) or "reload" (a relapse / deep difference after the patch, or a refused patch: the full reload). pos / hp /
    item must end "partial" (nothing hidden changes); rng may end either way (a corrupted stream can change hidden state
    before the patch). Every run must end equal (fp.log) with nobody stuck; the host must save a report."""
    tag = f'{kind}#{n}'
    my0, mx0 = mp(Y), mp(X)
    logpos = os.path.getsize(os.path.join(rig.idir(Y), 'rig.log'))
    if kind == 'item':
        ensure_items(X, Y)
    r = rig.send(Y, {'cmd': 'det_corrupt', 'kind': kind, 'n': n % 3}, timeout=60)
    if not r.get('ok'):
        return check(f'{tag}: corrupt', False, json.dumps(r))
    log(tag, 'corrupted', json.dumps(r)[:160])
    paused_seen = [False]

    def flagged():
        a, b = mp(X), mp(Y)
        if a.get('paused'):
            paused_seen[0] = True
        return b if b.get('desyncs', 0) > my0.get('desyncs', 0) else None
    try:
        wait(f'{tag}: desync flagged', flagged, timeout=60, every=0.3)
    except RuntimeError as e:
        return check(f'{tag}: desync flagged', False, str(e))
    ok = True
    # the end state: ~10 s probation + margin with no new difference and no load
    m = settle(Y, 700)
    pl, why = last_partial_line(Y, logpos)
    reloads = m.get('loads', 0) - my0.get('loads', 0)
    outcome = 'reload' if reloads else 'partial'
    OUTCOMES.setdefault(kind, {}).setdefault(outcome, 0); OUTCOMES[kind][outcome] += 1
    detail = (f'desyncs {my0.get("desyncs", 0)}->{m.get("desyncs")}, partials {my0.get("partials", 0)}->{m.get("partials")}, '
              f'loads {my0.get("loads", 0)}->{m.get("loads")}, deep ok {my0.get("deep_ok", 0)}->{m.get("deep_ok")} fails '
              f'{my0.get("deep_fails", 0)}->{m.get("deep_fails")}, other {my0.get("deep_other", 0)}->{m.get("deep_other")}; {why}')
    if fallback:
        ok &= check(f'{tag}: not patchable -> full reload', outcome == 'reload' and m.get('partials', 0) == my0.get('partials', 0), detail)
    elif kind == 'rng':
        ok &= check(f'{tag}: {"partial resync, deep check equal" if outcome == "partial" else "relapse after the patch -> full reload (safe)"}',
                    (outcome == 'partial' and m.get('partials', 0) == my0.get('partials', 0) + 1 and m.get('deep_ok', 0) > my0.get('deep_ok', 0))
                    or (outcome == 'reload' and reloads == 1), detail)
    else:
        ok &= check(f'{tag}: partial resync, no reload, deep check equal', outcome == 'partial' and m.get('partials', 0) == my0.get('partials', 0) + 1
                    and m.get('desyncs', 0) == my0.get('desyncs', 0) + 1 and m.get('deep_ok', 0) > my0.get('deep_ok', 0), detail)
    if outcome == 'partial':
        ok &= check(f'{tag}: nobody paused', not paused_seen[0])
        if pl:
            timings.setdefault(kind, []).append(int(pl[1]))
    # report on the host (the first report of this run)
    try:
        a = wait(f'{tag}: report on the host', lambda: (lambda q: q if q.get('reports_saved', 0) > mx0.get('reports_saved', 0) else None)(mp(X)), timeout=60)
        rp = a.get('report_path', '')
        files = sorted(os.listdir(rp)) if rp and os.path.isdir(rp) else []
        readme = open(os.path.join(rp, 'README.txt')).read() if 'README.txt' in files else ''
        need = ['README.txt', 'guest_state_bad.txt', 'host_state_bad.txt', 'guest_fp_window.log', 'host_fp_window.log',
                'guest_rig_log_tail.txt', 'host_rig_log_tail.txt']
        named = ('ClanSaveData' in readme) if kind == 'gold' else ('DIFFERENT' in readme and '(not sent)' not in readme)
        ok &= check(f'{tag}: report saved with both parts, a section named', all(f in files for f in need) and named,
                    f'{rp} ({len(files)} files)')
    except RuntimeError as e:
        ok &= check(f'{tag}: report on the host', False, str(e))
    # equal at the end: fp.log from the settled point on
    t0 = st(Y)['tick']
    t_end = mp(Y).get('timer', 0) + 120
    wait(f'{tag}: 120 steps after', lambda: mp(Y).get('timer', 0) >= t_end and mp(X).get('timer', 0) >= t_end, timeout=300)
    n_com, first = fp_equal_from(X, Y, t0)
    b = mp(Y)
    ok &= check(f'{tag}: equal at the end', not b.get('desynced') and b.get('desyncs') == m.get('desyncs') and n_com >= 60 and first is None,
                f'{n_com} common ticks from {t0}, first diff {first}, desyncs {m.get("desyncs")}->{b.get("desyncs")}')
    return ok


def main():
    kinds = arg('--kinds', 'rng,hp,pos,item').split(',')
    kinds.sort(key=lambda k: k != 'item')            # items first: the base workers haul loose items away over time
    runs, nfb = arg('--runs', 10), arg('--fallback', 3)
    out = os.path.join(rig.ROOT, 'build', 'core_test', 'partial_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    pair = rig.lease('core', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS[pair]
    log('pair', pair, 'port', port, 'kinds', kinds, 'runs', runs, 'fallback', nfb, 'dylib', rig.DYLIB)
    t_start = time.time()
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        fc = arg('--full-every', 600)                    # full-state check every N steps (both peers must agree)
        json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': fc},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'full_check_steps': fc},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=arg('--fps-x', 20))
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        save = arg('--save', '')
        if save:
            src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        else:
            from demo import make_demo_save
            src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
        log('X load', rig.send(X, {'cmd': 'load_save', 'file': src, 'det': 1}, timeout=120))
        wait('X locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=2)
        rig.start(Y, fps=arg('--fps-y', 30))
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)
        wait('Y loaded the host save', lambda: st(Y).get('det', {}).get('locked') and mp(Y).get('loads', 0) >= 1 and not mp(Y).get('needs_state'), timeout=900, every=2)
        t_go = mp(Y).get('timer', 0) + 120
        wait('120 steps in sync', lambda: mp(Y).get('timer', 0) >= t_go, timeout=300)
        check('join: in sync before the tests', not mp(Y).get('desynced') and not mp(Y).get('desyncs'), json.dumps(mp(Y))[:300])
        for kind in kinds:
            for n in range(runs):
                one_run(X, Y, kind, n)
        for n in range(nfb):
            one_run(X, Y, 'task', n, fallback=True)
        for n in range(arg('--gold', 0)):                # only the full-state check sees it: must reload, report names ClanSaveData
            one_run(X, Y, 'gold', n, fallback=True)
        my = mp(Y)
        check('full-state checks ran and were equal (outside the gold runs)', my.get('full_ok', 0) > 0 and my.get('full_fails', 0) == arg('--gold', 0),
              f'full ok {my.get("full_ok")} fails {my.get("full_fails")} slowest {my.get("full_ms_max")} ms (guest)')
        # a deep difference outside a patch window is now a desync like any other (it can only come from a forced
        # corruption here: the hp kind changes a body-part hp before the cached hp total shows it); counted, not a failure
        log('deep differences found before the fingerprint (flagged as desyncs):', my.get('deep_other', 0),
            f'deep ok {my.get("deep_ok")} fails {my.get("deep_fails")} missing {my.get("deep_missing")}')
    except Exception as e:
        check('run', False, repr(e))
    finally:
        npass = sum(r['ok'] for r in results); nfail = len(results) - npass
        summ = {'pair': pair, 'pass': npass, 'fail': nfail, 'minutes': round((time.time() - t_start) / 60, 1), 'outcomes': OUTCOMES,
                'flag_to_sync_ms': {k: {'n': len(v), 'median': sorted(v)[len(v) // 2], 'max': max(v)} for k, v in timings.items() if v},
                'results': results}
        json.dump(summ, open(os.path.join(out, 'results.json'), 'w'), indent=1)
        if nfail:
            for x in (X, Y):
                for f in ('rig.log', 'fp.log', 'player.log'):
                    p = os.path.join(rig.idir(x), f)
                    if os.path.exists(p):
                        shutil.copy2(p, os.path.join(out, f'{x}_{f}'))
        if '--keep' not in sys.argv:
            rig.stop(X); rig.stop(Y)
            rig.release(pair)
        log(f'SUMMARY {npass} PASS, {nfail} FAIL; outcomes {OUTCOMES}; flag -> in sync (ms): {summ["flag_to_sync_ms"]}; {out}')
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')


if __name__ == '__main__':
    main()
