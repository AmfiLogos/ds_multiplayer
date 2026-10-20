#!/usr/bin/env python3
"""Steam friends picker (src/ui_friends.c), one hidden host copy, NO Steam: DSRIG_TEST_STEAMFRIENDS=1 gives a fake
friends list (ids 1001.., never real) and a fake invite (Steam is never called; id 1007 fails).
Flow: title > Multiplayer > Host > Load save > lobby; lobby note under Steam invite; press the real Steam invite button
(overlay counts as off) -> picker: sorted rows, click rows (fail + ok), page arrows, Escape closes, lobby back.
Fails on old code (no picker: the overlay call only). Usage: picker_test.py --dylib <dsrig.dylib> --out <dir> [--lang N]
(--lang: game language type, 0 en 1 ko 2 fr 3 de 4 ru 5 zh-Hans 6 zh-Hant 7 ja 8 es 9 pt-BR; text checks only in English;
the dylib's folder needs lang/*.json for other languages)"""
import os, sys, time, re, json
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

res = {}
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(n, ok, info=''): res[n] = bool(ok); log(('PASS ' if ok else 'FAIL ') + n, info)

def main():
    rig.DYLIB = os.path.abspath(sys.argv[sys.argv.index('--dylib') + 1])
    out = os.path.abspath(sys.argv[sys.argv.index('--out') + 1]); os.makedirs(out, exist_ok=True)
    os.environ['DSRIG_TEST_STEAMFRIENDS'] = '1'; os.environ['DSRIG_BG'] = '1'
    pair = rig.lease('steaminv'); assert pair, 'no free pair'
    H = pair[0]
    c = lambda **k: rig.send(H, k, timeout=120)
    press = lambda b: (c(cmd='mpm_press', button=b), time.sleep(1.2))
    fr = lambda **k: c(cmd='mpm_friends', **k)
    shot = lambda n: (c(cmd='mpm_shot', path=os.path.join(out, n), how='backbuffer'), time.sleep(1))
    LANG = int(sys.argv[sys.argv.index('--lang') + 1]) if '--lang' in sys.argv else 0
    EN = LANG == 0
    prep0 = rig.prepare_data
    def prep(x):
        prep0(x)
        p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG; s['GeneralSettingData']['IsLanguageInitialized'] = True
        json.dump(s, open(p, 'w'), indent=1)
    rig.prepare_data = prep
    try:
        p = os.path.join(rig.idir(H), 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
        rig.start(H, window=True)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500); time.sleep(3)
        press(1); press(2); press(7); time.sleep(2)                     # Multiplayer > Host > Load save
        sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); from demo import make_demo_save
        src = os.path.join(rig.idir(H), 'save_in', 'demo_buffed.json'); os.makedirs(os.path.dirname(src), exist_ok=True); make_demo_save(src)
        c(cmd='load_save', file=src, det=0)
        rig.wait_for(H, lambda s: s.get('mp', {}).get('started') and s.get('scene') == 2 and not s.get('busy'), 'host session', timeout=900)
        rig.wait_for(H, lambda s: c(cmd='mpm_status').get('state') == 'lobby', 'lobby', timeout=120); time.sleep(3)
        s = fr(action='state')
        check('lobby note under Steam invite', (s.get('lobby_note', '').startswith('Your friend must open') or not EN) and s.get('lobby_note'), s.get('lobby_note'))
        shot('1_lobby.png')
        press(9)                                                        # the real lobby Steam invite button (OnPressButton)
        s = fr(action='state'); names = [r['name'] for r in s.get('rows', [])]
        check('Steam invite opens the picker (overlay off)', s.get('open') == 1 and s.get('overlay') == 0, f"open {s.get('open')} overlay {s.get('overlay')}")
        check('lobby hidden under the picker', s.get('lobby_hidden') == 1)
        exp = ['Playing Pia', 'Fails Fred', 'Online Al', 'Online Uli', 'zed online']
        check('sorted: plays DS, then online A-Z', names[:5] == exp and s.get('n') == 20 and s.get('pages') == 3, str(names))
        shot('2_picker.png')
        fr(action='row', i=1); time.sleep(1.2); s = fr(action='state')
        check('failed invite says so', (s['status'].startswith('Steam did not send the invite to Fails Fred') and s['rows'][1]['state'] == 'Not sent' or not EN) and 'Fails Fred' in s['status'], s['status'])
        fr(action='row', i=2); time.sleep(1.2); s = fr(action='state')
        check('invite sent says so', (s['status'] == 'Invite sent to Online Al.' and s['rows'][2]['state'] == 'Invite sent' or not EN) and 'Online Al' in s['status'], s['status'])
        shot('3_sent.png')
        rl = open(os.path.join(rig.idir(H), 'rig.log'), errors='replace').read()
        check('fake invite reached mp_steam_invite_friend (Steam not called)', 'test invite to fake friend 1009: rc 0' in rl and 'test invite to fake friend 1007: rc -1' in rl)
        fr(action='next'); time.sleep(1.2); s = fr(action='state')
        check('next page', s['page'] == 1 and len(s['rows']) == 8, str([r['name'] for r in s['rows']]))
        fr(action='next'); time.sleep(1.2); s = fr(action='state')
        check('last page: 4 rows', s['page'] == 2 and len(s['rows']) == 4, str([r['name'] for r in s['rows']]))
        shot('4_page3.png')
        fr(action='prev'); time.sleep(1.2)
        e = c(cmd='mpm_escape_ours'); time.sleep(1.5); s = fr(action='state')
        check('Escape closes the picker, lobby back', e.get('taken') == 1 and s.get('open') == 0 and s.get('lobby_hidden') == 0, str(e))
        shot('5_closed.png')
        press(9); time.sleep(0.5); fr(action='close'); time.sleep(1.2); s = fr(action='state')
        check('Close button closes', s.get('open') == 0)
    finally:
        try:
            rl = open(os.path.join(rig.idir(H), 'rig.log'), errors='replace').read().splitlines()
            open(os.path.join(out, 'rig_friends.log'), 'w').write('\n'.join(l for l in rl if re.search(r'ui_friends|invite|dsnet:', l)) + '\n')
        except Exception as ex: log('log copy failed', ex)
        rig.release(pair, 'steaminv')
    ok = all(res.values()) and res
    print('RESULT', 'PASS' if ok else 'FAIL', sum(res.values()), '/', len(res)); sys.exit(0 if ok else 1)

main()
