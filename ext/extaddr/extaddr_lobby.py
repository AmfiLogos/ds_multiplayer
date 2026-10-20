#!/usr/bin/env python3
"""extaddr lane (owner 2026-10-20): the host's lobby shows the INTERNET address (router UPnP, else https://api.ipify.org)
in the address box (Copy copies it) and "Same home: <local>" under it. One leased pair, only the host copy runs (hidden,
windowed, muted, notch fix off). Three starts:
  upnp  (en): DSRIG_EXTADDR_* unset          -> found via upnp, box = internet, Copy = internet
  web   (de): DSRIG_EXTADDR_UPNP=0           -> found via web
  fail  (ja): UPNP=0 + WEB_URL to a dead port -> unknown: box = local, "Internet address unknown" line
  extaddr_lobby.py --dylib PATH --out DIR [--cases upnp,web,fail]
Frames + log in DIR. Never commit DIR (it holds this Mac's real addresses)."""
import ipaddress, json, os, re, shutil, subprocess, sys, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'src', 'ui_test'))
import uxgame_pair as U
rig = U.rig
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
OUT = U.arg('--out', '/tmp/extaddr'); os.makedirs(OUT, exist_ok=True)
CASES = U.arg('--cases', 'upnp,web,fail').split(',')
LOGF = open(os.path.join(OUT, 'run.log'), 'a')
results, LANG = {}, {}
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, COPY=16)


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])


prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = CODES.index(LANG.get('cur', 'en')); s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang
os.environ['DSL_NOTCH'] = '0'


def start(x, extra):                                       # U.start + the case's DSRIG_EXTADDR_* switches
    for k in [k for k in os.environ if k.startswith('DSRIG_EXTADDR_')]: del os.environ[k]
    os.environ.update(extra)
    rig.start(x, fps=30, window=True)                     # DSRIG_HIDEWIN: hidden window (frames render), DSRIG_* env passed


def c(x, **k): return rig.send(x, k, timeout=60, connect_wait=0)
def st(x): return c(x, cmd='mpm_status')
def tr(lang, en):
    try: return json.load(open(os.path.join(HERE, '..', '..', 'lang', f'{lang}.json'))).get(en, en)
    except Exception: return en


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
            if r: return r
        except Exception: pass
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def note_text(x):
    j = c(x, cmd='mpm_alltexts'); want = tr(LANG['cur'], 'Same home: %s').split('%s')[0]
    return next((t.get('text', '') for t in j.get('texts', []) if want in t.get('text', '')), '')


def one(X, case):
    lang = {'upnp': 'en', 'web': 'de', 'fail': 'ja'}[case]; LANG['cur'] = lang
    env = {'upnp': {}, 'web': {'DSRIG_EXTADDR_UPNP': '0'},
           'fail': {'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': 'https://127.0.0.1:9/'}}[case]
    mp = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(mp): os.replace(mp, mp + '.last')
    start(X, env)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
    time.sleep(3)
    for b in ('MULTIPLAYER', 'HOST', 'LOAD_SAVE'): c(X, cmd='mpm_press', button=B[b]); time.sleep(1.5)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
    log(case, 'host load', c(X, cmd='load_save', file=src, det=0))
    wait('lobby', lambda: st(X).get('state') == 'lobby', timeout=900, every=3)
    s = wait('lookup done', lambda: (lambda s: s if s.get('inet_state', 0) >= 2 else None)(st(X)), timeout=30, every=1)
    time.sleep(2); s = st(X)
    port = s.get('port'); direct = s.get('direct', ''); inet = s.get('internet', '')
    box = s.get('lw', {}).get('addr', '') or ''
    note = s.get('lw', {}).get('inv_note', '') or ''
    rl = open(os.path.join(rig.idir(X), 'rig.log'), errors='replace').read()
    via = re.findall(r'mpmenu: internet address (\w+).*?\(via (\w*)\)', rl)
    shot = os.path.join(OUT, f'{case}_{lang}_lobby.png'); log(case, 'frame', c(X, cmd='mpm_shot', how='backbuffer', path=shot).get('ok'), shot)
    log(case, 'state', s.get('inet_state'), 'via', via[-1:] , 'port', port)
    same = tr(lang, 'Same home: %s') % direct
    if case in ('upnp', 'web'):
        ip = inet.rsplit(':', 1)[0] if inet else ''
        pub = bool(ip) and not ipaddress.ip_address(ip).is_private and not ipaddress.ip_address(ip) in ipaddress.ip_network('100.64.0.0/10')
        check(f'{case}: internet address found via {case}', s.get('inet_state') == 2 and via and via[-1] == ('found', case), (s.get('inet_state'), via[-1:]))
        check(f'{case}: internet address is public and ends with the listening port', pub and inet.endswith(f':{port}'), f'port {port} ok={pub}')
        check(f'{case}: the lobby box shows the internet address', box == inet, 'box == internet' if box == inet else 'box differs')
        check(f'{case}: the note has the same-home line with the local address', same in note and direct.endswith(f':{port}'), note.replace(ip, '<inet>') if ip else note)
        if case == 'upnp':
            clip = os.path.join(rig.idir(X), 'clipboard.txt')
            if os.path.exists(clip): os.replace(clip, clip + time.strftime('.prev_%Y%m%d_%H%M%S'))
            c(X, cmd='mpm_press', button=B['COPY']); time.sleep(1.5)
            got = open(clip).read().strip() if os.path.exists(clip) else ''
            check('upnp: Copy puts the internet address on the clipboard', got == inet, 'same' if got == inet else 'differs')
    else:
        unk = tr(lang, 'Internet address unknown. Friends outside your home: open port %s on your router.').split('%s')[0][:12]
        check('fail: state unknown, no internet address', s.get('inet_state') == 4 and not inet, s.get('inet_state'))
        check('fail: the lobby box shows the local address', box == direct, (box, direct))
        check('fail: the note says the internet address is unknown + same home', unk in note and same in note, note)
    rig.stop(X)


def main():
    rig.DYLIB = os.path.abspath(U.arg('--dylib', os.path.join(HERE, '..', '..', 'build', 'dsrig.dylib')))
    pair = None
    for _ in range(240):
        pair = rig.lease('extaddr', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    X = pair[0]; log('pair', pair, 'dylib', rig.DYLIB)
    try:
        for case in CASES:
            try: one(X, case)
            except Exception: check(f'{case}: run reached the end', False, traceback.format_exc()[-600:])
            finally:
                try: rig.stop(X)
                except Exception: pass
    finally:
        rig.release(pair, 'extaddr')
        log('check-prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
