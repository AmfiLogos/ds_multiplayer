#!/usr/bin/env python3
"""lan_v10 core: ux_flow F17 (Recent worlds row must not land in another world), F9 (invite during a single-player game
asks first), F19 (gear-rule refusal is said), on one leased pair.

  ux_core_pair.py [--keep]

X hosts the ElvenExpedition copy (mp.json host). Y has no mp.json (a player on the title):
 F17  Y joins X with a wrong expected world id -> refused, reason DSNET_REJ_OTHER_WORLD (7), the message names X's world,
      Y loads nothing.
 F9   Y loads its own single-player world (MoonlightBrigade copy). An invite to X arrives (TCP stand-in for Steam):
      Y does NOT join, the question is pending, Y's world is untouched. Cancel -> nothing happens. Invite again ->
      "Save and join" with X's real world id expected -> a new autosave of Y's world, then Y joins and loads X's world
      (same campaign id), in sync.
 F19  Y (guest, gear rule "own units only") equips an item on a unit it does not own -> refused with the notice text.
Results: build/core_test/ux_<time>/results.json"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig

results = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    for _ in range(20):
        try:
            s = c(x, cmd='status')
        except Exception as e:
            s = {'error': str(e)}
        if 'tick' in s or 'scene' in s:
            return s
        time.sleep(1)
    raise RuntimeError(f'{x}: status not answered: {s}')


def wait(what, pred, timeout=600, every=1.0):
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


def rig_log_since(x, pos):
    with open(os.path.join(rig.idir(x), 'rig.log'), 'rb') as f:
        f.seek(pos)
        return f.read().decode('utf8', 'replace')


def main():
    out = os.path.join(rig.ROOT, 'build', 'core_test', 'ux_' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    pair = rig.lease('core', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS[pair]
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj):
            os.replace(yj, yj + '.off')                          # Y: a player on the title, no session file
        rig.start(X, fps=20)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title X', timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'host_world.json')
        shutil.copy2(os.path.join(rig.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'), src)
        log('X load', c(X, cmd='load_save', file=src, det=1))
        wait('X locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=2)
        wait('X hosts its world', lambda: c(X, cmd='mp_world').get('campaign'), timeout=120)
        xw = c(X, cmd='mp_world'); log('X world', xw)
        rig.start(Y, fps=30)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title Y', timeout=1500)

        # ---- F17
        bad = '00000000-1111-2222-3333-444444444444'
        c(Y, cmd='mp_expect_world', id=bad)
        r = c(Y, cmd='mpm_join_tcp', host='127.0.0.1', port=port, name='Guest')
        time.sleep(3)
        yw = c(Y, cmd='mp_world'); sy = st(Y)
        check('F17: join for another world refused', r.get('rc') != 0 and yw.get('last_reject') == 7 and yw.get('other_world') and not yw.get('session')
              and sy.get('scene') == 1, f'rc {r.get("rc")}, {yw}, Y scene {sy.get("scene")}')

        # ---- F9: Y plays its own world
        ysrc = os.path.join(rig.idir(Y), 'save_in'); os.makedirs(ysrc, exist_ok=True)
        ysave = os.path.join(ysrc, 'own_world.json')
        shutil.copy2(os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'), ysave)
        log('Y load own world', c(Y, cmd='load_save', file=ysave, det=0))
        wait('Y in its own world', lambda: (lambda s: s.get('scene') == 2 and not s.get('busy'))(st(Y)), timeout=900, every=2)
        time.sleep(5)
        own = c(Y, cmd='mp_world').get('campaign')
        saves_dir = os.path.join(rig.idir(Y), 'data', 'Saves')
        before = {f: os.path.getmtime(os.path.join(saves_dir, f)) for f in os.listdir(saves_dir)}
        r = c(Y, cmd='mp_test_invite', host='127.0.0.1', port=port, **{'from': 'The owner'})
        time.sleep(4)
        yw = c(Y, cmd='mp_world')
        check('F9: invite during a single-player game waits for an answer', r.get('pending') == 1 and yw.get('invite_pending') == 1 and not yw.get('session')
              and yw.get('campaign') == own, f'{r} {yw}')
        log('cancel', c(Y, cmd='mp_invite_answer', choice=0))
        time.sleep(3)
        yw = c(Y, cmd='mp_world')
        check('F9: Cancel keeps the own world', not yw.get('invite_pending') and not yw.get('session') and yw.get('campaign') == own, json.dumps(yw))
        c(Y, cmd='mp_test_invite', host='127.0.0.1', port=port, **{'from': 'The owner'})
        c(Y, cmd='mp_expect_world', id=xw['campaign'])               # F17 positive: the right world joins
        pos = os.path.getsize(os.path.join(rig.idir(Y), 'rig.log'))
        r = c(Y, cmd='mp_invite_answer', choice=2)
        log('save and join', r)
        if not r.get('ok'):
            raise RuntimeError(f'save and join refused: {r}')
        wait('Y in the host world', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                            and not s.get('mp', {}).get('needs_state'))(st(Y)), timeout=900, every=2)
        time.sleep(8)
        after = {f: os.path.getmtime(os.path.join(saves_dir, f)) for f in os.listdir(saves_dir)}
        new = [f for f in after if not f.startswith('mp_sync') and (f not in before or after[f] > before[f])]
        yw = c(Y, cmd='mp_world'); my = st(Y).get('mp', {})
        txt = rig_log_since(Y, pos)
        check('F9: Save and join wrote an autosave of the own world first', bool(new) and 'autosave written' in txt, f'new/changed saves {new}')
        check('F9 + F17: joined the expected host world, in sync', r.get('rc') == 0 and yw.get('campaign') == xw['campaign'] and not my.get('desynced') and not my.get('desyncs'),
              f'Y campaign {yw.get("campaign")} X {xw["campaign"]}, mp {json.dumps(my)[:200]}')

        # ---- F19: equip on a unit Y does not own (unclaimed: host only)
        us = [u for u in c(Y, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead')]
        pos = os.path.getsize(os.path.join(rig.idir(Y), 'rig.log'))
        c(Y, cmd='ord_hover', n=0)
        r = c(Y, cmd='ord_inject', ev='EquipItemRequested', **{'i32@16': 0, 'guid@40': us[0]['guid'], 'i32@56': 0})
        time.sleep(2)
        txt = rig_log_since(Y, pos)
        line = next((l for l in txt.splitlines() if 'ui: ' in l and 'gear' in l), '')
        check('F19: gear refusal said on screen', 'refused by the gear mode' in txt and 'Only the host can change the gear of a unit nobody owns' in line,
              f'inject {r}; notice: {line.strip()[:160]}')
    except Exception as e:
        check('run', False, repr(e))
    finally:
        npass = sum(r['ok'] for r in results)
        json.dump({'pair': pair, 'pass': npass, 'fail': len(results) - npass, 'results': results}, open(os.path.join(out, 'results.json'), 'w'), indent=1)
        if len(results) - npass:
            for x in (X, Y):
                p = os.path.join(rig.idir(x), 'rig.log')
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(out, f'{x}_rig.log'))
        yj = os.path.join(rig.idir(Y), 'mp.json')
        if os.path.exists(yj + '.off'):
            os.replace(yj + '.off', yj)
        if '--keep' not in sys.argv:
            rig.stop(X); rig.stop(Y); rig.release(pair)
        log(f'SUMMARY {npass} PASS, {len(results) - npass} FAIL; {out}')
        log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')


if __name__ == '__main__':
    main()
