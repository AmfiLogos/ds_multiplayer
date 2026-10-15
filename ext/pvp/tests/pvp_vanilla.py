#!/usr/bin/env python3
"""PvP "Vanilla check" pair test (owner 2026-10-14 ~20:00; design board PvPVanillaCheck). Lane "pvp", dylib ext/pvp/build/dsrig.dylib.

  pvp_vanilla.py [--tag NAME] [--keep]

Launcher session files (the keys of ~/ds_launcher core/src/flow.rs Session) are written to OUT; the guest G is started
with DSRIG_DSML_SESSION (the env path; rig.start, hidden window, 960x540), later ones are loaded with the rig command pvp_mods {"session": path}.
  V0 G read its session from the env at start; H has none (not started by the launcher)
  V1 On + a gameplay mod on G: Start Match (window, real click path) refused, the window stays, the note names G and
     the mod; G gets the same message; no match set up
  V2 the guest's PvP window shows the host's choice (On / Off) as the host changes it
  V3 On, the host has a gameplay mod: refused, names the host's own mod
  V4 Off: G must have the host's gameplay mods: none -> refused "Missing"; same set -> allowed; same names, other
     files -> refused; an extra mod -> refused "Not allowed"
  V5 On, G has client mods only: Start Match from the window runs the match to its end; the rule is in the match state
  D  no desync
Logs and frames: ext/pvp/out/<tag>/."""
import json, os, shutil, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig
sys.path.insert(0, os.path.join(ROOT, 'research', 'ux_shots'))
import ux                                     # hidden window at a real resolution; frames from the back buffer

rig.DYLIB = os.environ.get('PVP_DYLIB') or os.path.join(ROOT, 'ext', 'pvp', 'build', 'dsrig.dylib')   # PVP_DYLIB: test another build (the src merge)
A = sys.argv
arg = lambda k, d: type(d)(A[A.index(k) + 1]) if k in A else d
TAG = arg('--tag', time.strftime('van_%H%M%S')); FLOOR = arg('--floor', 2); SEED = arg('--seed', 424242); TICKS = arg('--ticks', 4000)
OUT = os.path.join(ROOT, 'ext', 'pvp', 'out', TAG); os.makedirs(OUT, exist_ok=True)
results = {}
LOG = open(os.path.join(OUT, 'test.log'), 'a')


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def c(_inst, **k):
    return rig.send(_inst, k, timeout=120)


def st(x):
    for _ in range(10):
        s = c(x, cmd='status')
        if 'tick' in s:
            return s
        time.sleep(2)
    raise RuntimeError(f'{x}: no status')


def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def lease():
    for i in range(480):
        p = rig.lease('pvp', os.getpid())
        if p:
            return p
        if i % 10 == 0:
            log('no free pair yet, waiting')
        time.sleep(30)
    raise RuntimeError('no free pair')


def fp_read(x, t_from):
    d = {}
    for line in open(os.path.join(rig.idir(x), 'fp.log')):
        q = line.split()
        if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from:
            d[int(q[0])] = q[1]
    return d


def shot(x, name):
    c(x, cmd='screenshot', path=os.path.join(OUT, f'frame_{name}.png'))




def ushot(x, name):
    c(x, cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, f'ui_{name}.png'))


def sess(name, lines, rule='mods', client=()):
    p = os.path.join(OUT, f'session_{name}.json')
    json.dump({'world': None, 'modset': ('%016x' % (abs(hash(tuple(lines))) % (1 << 63))) if lines else 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
               'modset_lines': list(lines), 'rule': rule, 'offer': None, 'offer_error': None,
               'client_mods': [{'id': i, 'version': v} for i, v in client], 'worlds': {}}, open(p, 'w'), indent=1)
    return p


HELL = ['hell-difficulty@2.0.0:1111aaaa', 'hell-difficulty-data@2.0.0:2222bbbb']
HELL_OTHER = ['hell-difficulty@2.0.0:1111aaaa', 'hell-difficulty-data@2.0.0:9999ffff']
GCD = ['gcd-cdr@1.0.0:3333cccc']
CLIENT = [('notch-fix', '1.0.0'), ('weapon-skill-slot', '1.0.0')]


def main():
    pair = lease(); H, G = pair[0], pair[1]
    port = rig.pair_port(H)
    log('pair', pair, 'out', OUT)
    assert rig.check_prefs(), 'owner prefs differ before start'
    json.dump({'role': 'host', 'port': port, 'name': 'Blue', 'allow_speedup': False, 'allow_pause': True, 'min_players': 2},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Red', 'need_save': True},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    s_g_hell = sess('g_hell', HELL, 'vanilla', CLIENT)
    s_g_client = sess('g_client', [], 'vanilla', CLIENT)
    s_g_same = sess('g_same', HELL, 'mods', CLIENT)
    s_g_other = sess('g_other', HELL_OTHER, 'mods')
    s_g_extra = sess('g_extra', HELL + GCD, 'mods')
    s_h_hell = sess('h_hell', HELL, 'mods')
    s_h_none = sess('h_none', [], 'vanilla', [('notch-fix', '1.0.0')])
    both = lambda f: all(f(x) for x in (H, G))
    ps = lambda x: c(x, cmd='pvp_status')
    pv = lambda x, **k: c(x, cmd='mpm_pvp', **k)
    mods = lambda x, **k: c(x, cmd='pvp_mods', **k)

    def reported(n_game):                       # host has G's report with n_game gameplay mods
        return lambda: any(r[2] == n_game for r in mods(H)['reports'])

    def g_switch(path, n_game, vanilla):
        mods(G, session=path); mods(H, ask=vanilla)
        wait('G report', reported(n_game), timeout=60, every=1)
    try:
        ux.LEASED = pair; ux.start(H, 1512, 982)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        time.sleep(20)
        os.environ['DSRIG_DSML_SESSION'] = s_g_hell
        try:
            rig.start(G, window=True)            # rig.start passes DSRIG_* to the game (ux.start does not)
        finally:
            os.environ.pop('DSRIG_DSML_SESSION', None)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500)
        wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900)
        time.sleep(3)
        mg, mh = mods(G), mods(H)
        log('mods G', mg); log('mods H', mh)
        check('V0 G read its launcher session from the env at start; H has none', mg['from'] == s_g_hell and mg['known'] == 1 and mg['gameplay'] == 2 and mg['client'] == 2 and mh['known'] == 0,
              f"G from {mg['from']} known {mg['known']} gameplay {mg['gameplay']}; H known {mh['known']}")
        us = c(H, cmd='list_units')['units']
        players = sorted([u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u], key=lambda u: u['guid'])
        for u in players[:2]:
            c(H, cmd='mp_claim', guid=u['guid'])
        for u in players[2:4]:
            c(G, cmd='mp_claim', guid=u['guid'])
        time.sleep(2)
        for x in (H, G):
            c(x, cmd='mpm_esc')
        time.sleep(3)
        for x in (H, G):
            pv(x, action='press_esc')
        time.sleep(2)
        w = pv(H); log('H window', w)
        wait('G report (window opened)', reported(2), timeout=60, every=1)
        # host rules: best of 1, 1:00 rounds (short match for V5)
        pv(H, action='arrow', row=1, dir=-1); time.sleep(0.5)
        pv(H, action='arrow', row=2, dir=-1); time.sleep(0.5); pv(H, action='arrow', row=2, dir=-1); time.sleep(0.5)
        wh = pv(H)
        if wh['vanilla'] != 1:
            pv(H, action='arrow', row=5, dir=1); time.sleep(1); wh = pv(H)
        log('V1 rules', wh)
        ushot(H, 'H_window_on'); ushot(G, 'G_window_on')
        r1 = pv(H, action='press_start'); time.sleep(3)
        w1, g1 = pv(H), pv(G)
        log('V1 host', w1); log('V1 guest', g1)
        ushot(H, 'H_refused_on'); ushot(G, 'G_refused_on')
        p1 = ps(H)
        check('V1 On + gameplay mod on the guest: refused, window stays, note names Red and the mod; the guest is told; no match',
              w1['window'] == 1 and 'Red' in w1['note'] and 'hell-difficulty 2.0.0' in w1['note'] and 'hell-difficulty' in mods(G)['last'] and p1['phase'] == 0,
              f"note '{w1['note']}' / G last '{mods(G)['last']}' / phase {p1['phase']}")
        # V2: the guest's window follows the host's choice
        sh_on = g1['vanilla_shown']
        pv(H, action='arrow', row=5, dir=1); time.sleep(3)
        g_off = pv(G); ushot(G, 'G_window_off')
        pv(H, action='arrow', row=5, dir=1); time.sleep(3)
        g_on = pv(G)
        check('V2 the guest window shows the host choice (On, then Off, then On)', sh_on == 1 and g_off['vanilla_shown'] == 0 and g_on['vanilla_shown'] == 1,
              f"{sh_on} {g_off['vanilla_shown']} {g_on['vanilla_shown']}")
        # V3: On, the host itself has a gameplay mod
        mods(H, session=s_h_hell); g_switch(s_g_client, 0, 1)
        r3 = mods(H, gate=1); log('V3', r3)
        check('V3 On + gameplay mod on the host: refused, names the host\'s own mod', r3['gate'] == 1 and 'your gameplay mods' in r3['why'] and 'hell-difficulty' in r3['why'], r3['why'])
        # V4: Off, the host's set is required
        r4a = mods(H, gate=0)
        g_switch(s_g_same, 2, 0); r4b = mods(H, gate=0)
        g_switch(s_g_other, 2, 0); r4c = mods(H, gate=0)
        g_switch(s_g_extra, 3, 0); r4d = mods(H, gate=0)
        for k, r in (('none', r4a), ('same', r4b), ('other files', r4c), ('extra', r4d)):
            log('V4', k, r['gate'], r['why'])
        check('V4 Off: host set required (none refused "Missing"; same allowed; other files refused; extra refused "Not allowed")',
              r4a['gate'] == 1 and 'Missing: hell-difficulty 2.0.0, hell-difficulty-data 2.0.0' in r4a['why'] and r4b['gate'] == 0 and
              r4c['gate'] == 1 and 'other files' in r4c['why'] and r4d['gate'] == 1 and 'Not allowed: gcd-cdr 1.0.0' in r4d['why'],
              f"{r4a['gate']} {r4b['gate']} {r4c['gate']} {r4d['gate']}")
        # V5: On, guest with client mods only: the real Start runs the match
        mods(H, session=s_h_none); g_switch(s_g_client, 0, 1)
        pv(H, action='press_close'); time.sleep(1); pv(H, action='press_esc'); time.sleep(2)   # reopen: asks again
        wv = pv(H); log('V5 window', wv)
        if wv['vanilla'] != 1:
            pv(H, action='arrow', row=5, dir=1); time.sleep(1)
        pv(G, action='press_close'); time.sleep(1)
        log('V5 start', pv(H, action='press_start'))
        wait('match on both', lambda: both(lambda x: ps(x)['active']), timeout=600, every=2)
        ga = {x: c(x, cmd='pvp_status') for x in (H, G)}
        log('V5 status', ga)
        time.sleep(3); ushot(H, 'H_match_on'); ushot(G, 'G_match_on')
        wait('match over and back in the world', lambda: both(lambda x: ps(x)['phase'] == 0 and ps(x)['matches'] == 1 and st(x).get('floor') == 0 and not st(x).get('busy')), timeout=900, every=3)
        vr = {x: pv(x).get('vanilla_shown') for x in (H, G)}
        check('V5 On + client mods only: Start Match from the window runs the match to its end', all(ga[x]['active'] and ga[x]['vanilla'] == 1 for x in (H, G)), f'vanilla in state {[ga[x]["vanilla"] for x in (H, G)]}')
        mh2, mg2 = st(H)['mp'], st(G)['mp']
        check('D no desync', mh2.get('desyncs', 0) == 0 and mg2.get('desyncs', 0) == 0)
        json.dump({'results': results}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
    finally:
        if '--keep' not in A:
            for x in (H, G):
                rig.stop(x)
            rig.release(pair, 'pvp')   # rule 10: only our own lease
            log('stopped + released', pair, 'prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
    log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
