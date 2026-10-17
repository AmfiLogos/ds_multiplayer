#!/usr/bin/env python3
"""i18n game test, one leased rig pair (hidden windows, muted, 960x540), standalone dsrig.dylib of ~/ds_mp_ship020t
(branch i18n + local test commands) + Custom Modes next to it; build/lang = mp + Custom Modes texts merged.
  pairtest.py <host lang> <guest lang>
Guest name: 12 Chinese characters (cut to 10 on the wire). Host page, join page, lobby (both), in game (status line,
tooltips, Esc menu, Teams screen): shots + text dumps. Output: ext/i18n_game/out/pair_<h>_<g>/"""
import json, os, re, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig
class _J:
    def __getattr__(self, k): return getattr(json, k)
    @staticmethod
    def dumps(o, **k): k.setdefault('ensure_ascii', False); return json.dumps(o, **k)
rig.json = _J()                                   # raw UTF-8 on the socket (the C side reads no \u escapes)
CODES = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
HL, PL = int(sys.argv[1]), int(sys.argv[2])
rig.DYLIB = os.path.expanduser('~/ds_mp_ship020t/build/dsrig.dylib')
OUT = os.path.expanduser(f'~/ds_mp_ship020/ext/gate020/i18n/out/pair2_{CODES[HL]}_{CODES[PL]}'); os.makedirs(OUT, exist_ok=True)
LOGF = open(os.path.join(OUT, 'test.log'), 'a')
LANE = f'ship020i-{os.getpid()}'
B = dict(MULTIPLAYER=1, BACK=3, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, HOST_SAVE=121, HOST_NEW=122, HOST_FE=123)
GNAME = '龙之谷探险队长王小明大人'          # 12 characters, 36 bytes
LANGS = {}
prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANGS[x]; s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def main():
    import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    pair = rig.lease(LANE)
    if not pair: log('no free pair'); return 1
    H, P = pair[0], pair[1]; LANGS[H] = HL; LANGS[P] = PL
    c = lambda x, **k: rig.send(x, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n] if isinstance(n, str) else n), time.sleep(1.5))[0]

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                v = pred()
                if v: return v
            except Exception as e: log('wait', what, repr(e))
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def view(x, name):
        time.sleep(1.2)
        p = os.path.join(OUT, f'{x}_{name}.png'); a = c(x, cmd='mpm_shot', path=p, how='backbuffer')
        j = c(x, cmd='mpm_alltexts')
        t = [e for e in j.get('texts', []) if e.get('rect') and (e['rect'][2] - e['rect'][0]) > 0 and e['rect'][2] > 0 and e['rect'][0] < 1000]
        json.dump(t, open(os.path.join(OUT, f'{x}_{name}.texts.json'), 'w'), ensure_ascii=False, indent=0)
        log('view', x, name, a.get('ok'), len(t), 'texts')
        return t

    busy = [q for x in pair for q in rig._procs_all()[x]]
    if busy: log('copies already running:', busy); rig.release(pair, LANE); return 1
    stamp = time.strftime('%Y%m%d-%H%M%S')
    for x in (H, P):
        for f in ('mp.json', 'mp_tags.txt', 'mp_history.json', 'mp_menu.json'):
            p = os.path.join(rig.idir(x), f)
            if os.path.exists(p): os.replace(p, f'{p}.before_i18ngame_{stamp}')
        for d in ('fe',):
            p = os.path.join(rig.idir(x), 'data', d)
            if os.path.isdir(p): os.replace(p, f'{p}_i18ngame_{stamp}')
    json.dump({'name': GNAME}, open(os.path.join(rig.idir(P), 'mp_menu.json'), 'w'), ensure_ascii=False)
    json.dump({'name': 'Hôte'}, open(os.path.join(rig.idir(H), 'mp_menu.json'), 'w'), ensure_ascii=False)
    port = rig.PAIR_PORTS[pair]
    json.dump({'role': 'host', 'port': port, 'name': 'Хозяин\ue000',   # gate control: U+E000 is in no font -> TMP must log one missing-glyph warning
                 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(H), 'mp.json'), 'w'), ensure_ascii=False)
    log('pair', pair, 'dylib', rig.DYLIB, 'langs', HL, PL)
    rc = 0
    try:
        for x in (H, P):
            os.environ['DSRIG_MISS_LOG'] = os.path.join(OUT, f'{x}_miss.tsv'); rig.start(x, window=True, fps=30)
        for x in (H, P): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(4)
        for x in (H, P):
            for _ in range(3):
                if not c(x, cmd='mpm_fewin').get('mode'): break
                c(x, cmd='mpm_press', button=110); time.sleep(1)
        view(H, '0_title'); view(P, '0_title')
        press(H, 'MULTIPLAYER'); view(H, '1_mp_page')
        press(H, 'HOST_SAVE'); view(H, '2_host_page')
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'host_world.json')
        import shutil; shutil.copy2(os.path.join(rig.SEED_DATA, 'Saves', 'ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json'), src)
        log('H load', c(H, cmd='load_save', file=src, det=1))
        wait('H locked', lambda: st(H).get('det', {}).get('locked'), timeout=900)
        wait('H hosts its world', lambda: c(H, cmd='mp_world').get('campaign'), timeout=120)
        time.sleep(5); view(H, '3_lobby_alone')
        # a refused join first (the next join expects another world): the reject box on the guest
        press(P, 'MULTIPLAYER'); press(P, 'JOIN_ADDR')
        c(P, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5); view(P, '1_join_page')
        log('expect bad world', c(P, cmd='mp_expect_world', id='00000000-1111-2222-3333-444444444444'))
        log('reject join', c(P, cmd='mpm_join_tcp', host='127.0.0.1', port=port, name=GNAME))
        def boxon(x):
            j = c(x, cmd='mpm_status')
            return j if (j.get('box') or {}).get('on') else None
        try: wait('P reject box', lambda: boxon(P), timeout=60, every=1)
        except Exception as e: log('no reject box', repr(e))
        time.sleep(1.5); view(P, '1b_reject_box'); log('P status', json.dumps(c(P, cmd='mpm_status'), ensure_ascii=False)[:600])
        log('box click', c(P, cmd='mpm_box', action='confirm')); time.sleep(3); view(P, '1c_after_reject')
        stp = c(P, cmd='mpm_status').get('state'); log('P state after reject', stp)
        if stp != 'join': press(P, 'MULTIPLAYER'); press(P, 'JOIN_ADDR'); c(P, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5)
        c(P, cmd='mpm_press', button=B['CONNECT'])
        wait('P loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 and not s.get('mp', {}).get('needs_state'))(st(P)), timeout=900, every=1)
        time.sleep(4); view(H, '4_lobby_with_guest'); view(P, '2_lobby')
        log('H players', json.dumps(c(H, cmd='ui_players_lists'), ensure_ascii=False)[:600])
        press(H, 'START')
        wait('H + P running', lambda: not mp(H).get('paused') and not mp(P).get('paused'), timeout=180)
        time.sleep(8); view(H, '5_game'); view(P, '3_game')
        LJ = {}
        def trl(x, en):
            code = CODES[LANGS[x]]
            if code == 'en': return en
            if code not in LJ: LJ[code] = json.load(open(os.path.expanduser(f'~/ds_mp_ship020t/build/lang/{code}.json')))
            v = LJ[code].get(en, en); return v[0] if isinstance(v, list) else v
        def tip(x, en, name):
            t = trl(x, en)
            a = c(x, cmd='ui_tip_hover', title=t); time.sleep(1.5); a = c(x, cmd='ui_tip_hover', title=t)
            view(x, name); log(x, 'tip', en, t, a)
        for x in (H, P):
            for en, nm in (('Players', '6a_tip_players'), ('Settings', '6b_tip_settings'), ('Connections', '6c_tip_conn')): tip(x, en, nm)
            c(x, cmd='ui_tip_hover')
        c(P, cmd='mpm_esc'); time.sleep(2); view(P, '7_esc'); c(P, cmd='mpm_return_game'); time.sleep(1.5)
        c(H, cmd='mpm_esc'); time.sleep(2); view(H, '7_esc'); c(H, cmd='mpm_return_game'); time.sleep(1.5)
        a = c(H, cmd='ui_teams', action='open'); log('teams open', a); time.sleep(3)
        view(H, '8_teams'); view(P, '8_teams_guest')
        log('teams state', json.dumps(c(H, cmd='ui_teams', action='state'), ensure_ascii=False)[:800])
        c(H, cmd='ui_teams', action='close'); time.sleep(1)
        # message boxes on the guest: session end (the host quits to the title), then a refused join, then a refused Steam join with the long name
        a = c(H, cmd='mpm_quit_title'); log('host quit to title', a)
        try: wait('P session-end box', lambda: boxon(P), timeout=60, every=1)
        except Exception as e: log('no box seen', repr(e))
        time.sleep(1.5); view(P, '9_session_end_box'); log('P status', json.dumps(c(P, cmd='mpm_status'), ensure_ascii=False)[-400:])
        log('box click', c(P, cmd='mpm_box', action='confirm')); time.sleep(3); view(P, '9b_after_box')
        rig.wait_for(P, lambda s: s.get('scene') == 1, 'P title', timeout=300); time.sleep(3)
        log('fail_join', c(P, cmd='mpm_fail_join', msg='', host=GNAME))
        time.sleep(3); view(P, '11_steam_refused_box'); log('P status', json.dumps(c(P, cmd='mpm_status'), ensure_ascii=False)[-400:])
        c(P, cmd='mpm_box', action='confirm'); time.sleep(1)
    except Exception as e:
        import traceback; traceback.print_exc(); log('ERROR', repr(e)); rc = 1
    finally:
        for x in (H, P):
            try: rig.stop(x)
            except Exception as e: log('stop', x, repr(e))
        for x in (H, P):
            for f in ('rig.log',):
                q = os.path.join(rig.idir(x), f)
                if os.path.exists(q):
                    t = open(q, errors='replace').read().splitlines()[-3000:]
                    open(os.path.join(OUT, f'{x}_{f}'), 'w').write('\n'.join(t))
        rig.release(pair, LANE); log('released', pair)
        log('prefs identical after:', rig.check_prefs(restore=True))
    return rc


if __name__ == '__main__':
    sys.exit(main())
