#!/usr/bin/env python3
"""Foctopus 2026-10-20, bugs 5 and 7, on one hidden copy (pair lease held by hold.py). No mp.json (the owner's app).
  A  Host > New save > New campaign > the screen's own Cancel: the menu leaves the host intent; a later single-player
     load is NOT hosted; save writes counted while keys are sent.
  B  Host alone in a world, Leave: the world runs again (ticks move), multiplayer roles cleared, det unlocked.
  C  Host > New campaign with the tutorial: no session, the notice says why, the world stays single player.
Usage: foc_proof.py [A B C] ; leases a pair (first copy used); LANGTYPE=3|4|7 = that game language (C notice shot);
FOC_DYLIB = the build (default ../../build_fixfoc/dsrig.dylib). Results: out/proof_<X>.json. Old code (origin/main da9705e
+ these test commands): A1 A2 A4 B2 B3 C2 fail."""
import glob, json, os, sys, time, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
import rigports
rig.DYLIB = os.path.abspath(os.environ.get('FOC_DYLIB') or os.path.join(HERE, '..', '..', 'build_fixfoc', 'dsrig.dylib'))
os.environ.setdefault('DSRIG_BG', '1')
PAIR = rig.lease(f'fixfoc-{os.getpid()}'); assert PAIR, 'no free pair'
def _done():                                               # also after a failed wait: stop, give the pair back, mp.json back
    rig.stop(PAIR[0]); rig.release(PAIR)
    mj = os.path.join(rig.idir(PAIR[0]), 'mp.json')
    if os.path.exists(mj + '.fixfoc_aside') and not os.path.exists(mj): os.replace(mj + '.fixfoc_aside', mj)
import atexit; atexit.register(_done)
X = PAIR[0]; CASES = sys.argv[1:] or ['A', 'B', 'C']; LT = os.environ.get('LANGTYPE')
OUT = os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True); os.makedirs(os.path.join(HERE, 'shots'), exist_ok=True)   # test output: never commit
TUT_KEY = 'The tutorial is single-player only: you play it alone. To play together, start a campaign without the tutorial'
R = {}; LOG = open(os.path.join(OUT, f'proof_{X}{os.environ.get("FOC_TAG", "")}{"_l" + LT if LT else ""}.log'), 'a')
def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()
def check(n, ok, info=''): R[n] = bool(ok); log('PASS' if ok else 'FAIL', n, info)
c = lambda **k: rig.send(X, k, timeout=120)
def mst(): return c(cmd='mpm_status')
def st(): return c(cmd='status')
def press(b): c(cmd='mpm_press', button=b); time.sleep(2)
def wait(what, pred, t=600, every=3):
    t0 = time.time()
    while time.time() - t0 < t:
        r = pred()
        if r: return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
SAVES = os.path.join(rig.idir(X), 'data', 'Saves'); RIGLOG = os.path.join(rig.idir(X), 'rig.log')
def save_state(): return {os.path.basename(f): os.path.getmtime(f) for f in glob.glob(SAVES + '/*.json')}
def writes(a, b): return sorted(k for k in b if a.get(k) != b[k])
def save_calls(): return sum(1 for l in open(RIGLOG, errors='replace') if 'DBG Save' in l)
def shot(name):
    p = os.path.join(rig.idir(X), 'proof.png'); c(cmd='mpm_shot', path=p, how='backbuffer'); time.sleep(2)
    subprocess.run(['cp', p, os.path.join(HERE, 'shots', name)]); log('shot', name)
def fresh():
    rig.stop(X)
    mj = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(mj): os.replace(mj, mj + '.fixfoc_aside')
    if LT and not getattr(rig, '_foc_lang', 0):           # after the rig's own data prep (as loader test l1_savebackup)
        _prep = rig.prepare_data
        def _prep_lang(x):
            _prep(x); us = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
            s['GeneralSettingData']['SaveCurrentLanguageType'] = int(LT); json.dump(s, open(us, 'w'), indent=1)
        rig.prepare_data = _prep_lang; rig._foc_lang = 1
    rig.start(X, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=600); time.sleep(3)
def keys_for(sec):
    t0 = time.time()
    while time.time() - t0 < sec: c(cmd='mpm_key', chars='wasd 1'); time.sleep(1.5)
def in_world(min_tick=0):
    return wait('world', lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') and (s.get('tick') or 0) > min_tick else None)(st()), 900)
def sp_save():
    return sorted(glob.glob(SAVES + '/MoonlightBrigade__AutoSave_*_0.json'), key=os.path.getmtime)[-1]
def mp_started(): return c(cmd='mp_status').get('mp', {}).get('started')

if 'A' in CASES:
    fresh(); press(1); press(122); press(8)
    check('A0 new-campaign screen open under the host intent', c(cmd='mpm_newcampaign', probe=1).get('open') == 1 and mst()['state'] == 'pick_new')
    c(cmd='mpm_newcampaign', cancel=1)
    try: wait('host page', lambda: mst()['state'] == 'title_host', 120, 2)   # 90 frames; a hidden nice-19 copy runs ~8 fps
    except RuntimeError: pass
    m = mst()
    check('A1 Cancel: host intent dropped (back on the host page)', m['state'] == 'title_host', m['state'])
    s0, n0 = save_state(), save_calls()
    log('single-player load', c(cmd='load_save', file=sp_save(), det=0)); in_world(); time.sleep(10)
    m = mst(); started = mp_started()
    check('A2 a later single-player load is not hosted', m['state'] == 'idle' and m['obs']['role'] == 0 and not started, f"{m['state']} role {m['obs']['role']} started {started}")
    t1 = st()['tick']; keys_for(40); t2 = st()['tick']; s1, n1 = save_state(), save_calls()
    check('A3 40 s of keys after Cancel: no save written', not writes(s0, s1) and n1 == n0, f'files {writes(s0, s1)} Save() calls {n1 - n0}; ticks {t1}->{t2}')
    check('A4 the world runs', t2 > t1, f'{t1}->{t2}')
if 'B' in CASES:
    fresh(); press(1); press(121); press(7); time.sleep(2)
    log('host from a save', c(cmd='load_save', file=sp_save(), det=0))
    wait('lobby', lambda: mst()['state'] == 'lobby', 600); press(10)
    wait('playing', lambda: mst()['state'] == 'playing', 60); time.sleep(5)
    check('B0 hosting alone, det locked, role host', c(cmd='trn_status')['cfg']['role'] == 1 and st()['det']['locked'])
    ok, ls = rigports.players_guard(rig.send, [X], 1, 'before Leave')   # the whole session (seconds): one check, start = end
    check('B0b players: no extra player in the hosted session', ok, '; '.join(ls))
    press(12); c(cmd='mpm_box', action='confirm'); time.sleep(4)
    m = mst(); check('B1 Leave: no session', m['state'] == 'idle' and not mp_started(), m['state'])
    s0, n0 = save_state(), save_calls(); t1 = st()['tick']; keys_for(30); t2 = st()['tick']; s1, n1 = save_state(), save_calls()
    d = st()['det']; role = c(cmd='trn_status')['cfg']['role']
    check('B2 after Leave the world runs (ticks move)', t2 > t1 + 50, f'{t1}->{t2}')
    check('B3 after Leave: det unlocked, autosave role cleared', not d['locked'] and not d['hold'] and role == -1, f"locked {d['locked']} role {role}")
    check('B4 30 s of keys after Leave: no save written', not writes(s0, s1) and n1 == n0, f'files {writes(s0, s1)} Save() calls {n1 - n0}')
if 'C' in CASES:
    fresh(); press(1); press(122); press(8)
    log('new campaign with the tutorial', c(cmd='mpm_newcampaign', name='Foc Tut', tutorial=1, scenario=0, seed='4242'))
    def ready():
        c(cmd='mpm_skipintro'); s = st(); return s if s.get('scene') == 2 and not s.get('busy') and (s.get('tick') or 0) > 21600 else None
    s = wait('tutorial world', ready, 900); time.sleep(8)
    m = mst(); started = mp_started()
    check('C1 tutorial world: in region 7 (tutorial)', s.get('region') == 7, s.get('region'))
    check('C2 no session in the tutorial', m['state'] == 'failed' and m['obs']['role'] == 0 and not started, f"{m['state']} role {m['obs']['role']} started {started}")
    time.sleep(10); po = c(cmd='mpm_newcampaign', popups=1).get('open', [])
    check('C2b no box while the tutorial pop-up covers the screen', not mst()['box']['on'] or po == [0], po)
    for _ in range(6):                                                     # the player closes the tutorial's own pop-ups
        for i in c(cmd='mpm_newcampaign', popups=1).get('open', []):
            if i: c(cmd='mpm_newcampaign', close=i)
        time.sleep(4)
        if mst()['box']['on']: break
    m = wait('notice box', lambda: (lambda q: q if q['box']['on'] else None)(mst()), 60, 2)
    code = {'3': 'de', '4': 'ru', '7': 'ja'}.get(LT or '', 'en')
    want = json.load(open(os.path.join(HERE, '..', '..', 'lang', code + '.json'), encoding='utf-8'))[TUT_KEY] if code != 'en' else TUT_KEY
    check('C3 the notice box says why (in the game language)', m['box']['on'] and want in m['box']['text'], m['box']['text'].replace('\n', ' / ')[:300])
    time.sleep(30); shot(f'proof_C_notice{"_l" + LT if LT else ""}.png')   # backbuffer of a hidden ~8 fps copy lags: wait
    check('C3b the box stays until OK', mst()['box']['on'])
    d = st()['det']; role = c(cmd='trn_status')['cfg']['role']        # (the tutorial's own first notice pauses the game)
    check('C4 no multiplayer left on the tutorial world (det off, autosave role clear)', not d['locked'] and not d['hold'] and role == -1, f"det {d['on']}/{d['locked']} role {role}")
json.dump(R, open(os.path.join(OUT, f'proof_{X}{os.environ.get("FOC_TAG", "")}{"_l" + LT if LT else ""}.json'), 'w'), indent=1)
log('RESULT', 'ALL PASS' if R and all(R.values()) else 'FAIL', json.dumps(R))
