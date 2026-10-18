#!/usr/bin/env python3
"""cycle.py <start lang> <l1,l2,...>: start in <start lang>, views, Apply (progress shots), then in the restarted\ncopy live-switch through every lang of the list (Mods window open during each switch) with views per lang, then a\nsecond Apply in the last lang (progress shots). Based on ldtest.py.\ni18n game test, one loader copy (X, hidden, windowed 960x540, muted, notch fix off), full stack: dsload + multiplayer +
custom-modes + gcd-cdr + hell-difficulty from ~/ds_ld_ship024t/loader/build (pins = the i18n trees + local test commits).
  ldtest.py <lang> [<lang2>] [--progress]
Start in <lang> (UserSetting.json), title / Mods window / hover cards / mp pages: shots + text dumps.
<lang2>: live switch on the title (test_set_lang = LanguageSetting.SetCurrentLanguageType), same views again.
--progress: Apply (gcd-cdr off) -> soft-restart progress frames.
Take a pair lease first (the caller does). Output: ext/i18n_game/out/ld_<lang>[_<lang2>]/"""
import json, os, sys, time, threading, signal, shutil, glob
LT = os.path.expanduser('~/ds_ld_ship024t/loader/test'); sys.path.insert(0, LT)
import l1_test as T
import l1_v13_test as V
r = T.r
CODES = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
args = [a for a in sys.argv[1:] if not a.startswith('--')]
LANG = int(args[0]); ORDER = [int(x) for x in args[1].split(',')] if len(args) > 1 else []
LANG2 = None; PROGRESS = True
X = 'X'
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
TAG = f'cycle_{CODES[LANG]}'
OUT = os.path.expanduser(f'~/ds_mp_ship024/ext/gate024/i18n/out/{TAG}'); os.makedirs(OUT, exist_ok=True)
T.OUT = OUT; V.OUT = OUT; V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
MISS = os.path.join(OUT, 'miss.tsv')
log, check = T.log, T.check
B = dict(MULTIPLAYER=1, HOST=2, BACK=3, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, HOST_SAVE=121, HOST_NEW=122, HOST_FE=123)

prep0 = r.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG; s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
r.prepare_data = prepare_lang


def c(**k): return r.send(X, k, timeout=60, connect_wait=0)


def texts(name):
    """every active TMP text (path, text, font, overflow) -> <name>.texts.json; returns the list"""
    j = c(cmd='mpm_alltexts')
    t = [x for x in j.get('texts', []) if x.get('rect') and (x['rect'][2] - x['rect'][0]) > 0 and x['rect'][2] > 0 and x['rect'][0] < 1000]
    json.dump(t, open(os.path.join(OUT, name + '.texts.json'), 'w'), ensure_ascii=False, indent=0)
    return t


def shot(name):
    p = os.path.join(OUT, name + '.png')
    a = c(cmd='mpm_shot', path=p, how='backbuffer')
    log('shot', name, a.get('ok'), a.get('error', ''))
    return p


def view(name):
    time.sleep(1.2); shot(name); return texts(name)


def trw(en):
    l = CUR[0]
    if not l: return en
    try: return json.load(open(os.path.expanduser(f'~/ds_mp_ship024t/lang/{CODES[l]}.json'))).get(en, en)
    except Exception: return en
CUR = [LANG]


def press(n): c(cmd='mpm_press', button=B[n] if isinstance(n, str) else n); time.sleep(1.5)


def views(pfx):
    """title, Mods window (+ hover card), mp pages; returns {name: texts}"""
    out = {}
    out['title'] = view(f'{pfx}1_title')
    mark = len(T.dlog()); T.request({'cmd': 'title'})
    m = V.wait_line(r'reg test: title (.*)', mark, 10); log('title buttons', m.group(1) if m else None)
    out['title_buttons'] = m.group(1) if m else None
    V.ui('open'); out['mods'] = view(f'{pfx}2_mods')
    mw, _ = V.tip('win:apply', f'{pfx}2_mods_hover_apply'); log('mods hover', mw.group(0) if mw else None)
    if not os.path.exists(os.path.join(OUT, f'{pfx}2_mods_hover_apply.png')): shot(f'{pfx}2_mods_hover_apply')
    out['mods_hover'] = texts(f'{pfx}2_mods_hover_apply')
    try: st = V.ui('state'); json.dump(st, open(os.path.join(OUT, f'{pfx}2_mods.state.json'), 'w'), ensure_ascii=False, indent=0)
    except Exception as e: log('state', repr(e))
    V.ui('close'); time.sleep(1)
    press('MULTIPLAYER'); out['mp_page'] = view(f'{pfx}3_mp_page')
    c(cmd='mpm_tip', title=trw('Your save')); time.sleep(1.5); out['mp_tip'] = view(f'{pfx}3_mp_page_tip'); c(cmd='mpm_tip')
    press('HOST_SAVE'); out['host'] = view(f'{pfx}4_host_page')
    press('BACK'); press('JOIN_ADDR'); out['join'] = view(f'{pfx}5_join_page')
    press('BACK'); time.sleep(0.5); press('BACK')
    st = c(cmd='mpm_status'); log('mp state after backs', st.get('state'))
    if st.get('state') not in ('title', 'idle', None): press('BACK')
    return out



def progress(tag, ids):
    import l1_progress_test as P
    P.OUT = OUT; P.CAP = os.path.join(OUT, 'cap'); os.makedirs(P.CAP, exist_ok=True)
    pid = [p for p in T.game_pids()][0]
    T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ids})
    a = P.shoot_when(f'{tag}_progress_low', lambda p, s: 0 < p <= 20)
    b = P.shoot_when(f'{tag}_progress_mid', lambda p, s: 30 <= p < 90)
    dt, nb = T.wait_swap(pid, t_req, tag)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title after apply', 180); time.sleep(4)
    return {'low': a, 'mid': b, 'swap_s': dt, 'new_pid': nb}


def main():
    for p in T.game_pids(): raise SystemExit(f'copy X already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    fw = os.path.join(T.I, 'DungeonSettlers.app', 'Contents', 'Frameworks', 'lang'); os.makedirs(fw, exist_ok=True)
    for f in glob.glob(os.path.expanduser('~/ds_ld_ship024t/loader/lang/*.json')): shutil.copy2(f, fw)
    if os.path.exists(MISS): os.replace(MISS, MISS + '.%d' % time.time())
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'lang': LANG, 'order': ORDER}
    try:
        T.start_a(enabled=('multiplayer', 'custom-modes', 'gcd-cdr', 'hell-difficulty'),
                  extra_env={'DSL_UI_TEST': V.UI, 'DSRIG_HIDEWIN': '1', 'DSI_MISS_LOG': MISS, 'DSRIG_MISS_LOG': MISS + '.mp'})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title', 600); time.sleep(4)
        S['start'] = views('s')
        S['apply1'] = progress('s', ['multiplayer', 'custom-modes', 'hell-difficulty']); log('apply1', S['apply1'])
        S['after_apply1'] = view('s9_title_after_apply')
        for L in ORDER:
            k = CODES[L]
            V.ui('open'); time.sleep(1)
            S['switch_' + k] = c(cmd='mpm_test_lang', lang=L); CUR[0] = L; time.sleep(4)
            view(f'{k}_0_mods_open_during_switch')
            V.ui('close'); time.sleep(1)
            S[k] = views(f'{k}_')
        if ORDER:
            S['apply2'] = progress(CODES[ORDER[-1]], ['multiplayer', 'custom-modes', 'hell-difficulty', 'gcd-cdr']); log('apply2', S['apply2'])
            view(f'{CODES[ORDER[-1]]}_9_title_after_apply')
    except Exception as e:
        import traceback; traceback.print_exc(); check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'dsload.prev.log', 'rig.log', 'player.log'):
            q = os.path.join(T.I, f)
            if os.path.exists(q): shutil.copy2(q, os.path.join(OUT, f))
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str, ensure_ascii=False)
        log('done', OUT)


if __name__ == '__main__':
    main()
