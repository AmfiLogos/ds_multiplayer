#!/usr/bin/env python3
"""hostaddr lane (owner 2026-10-18): the host's Players panel shows "Address: <LAN IP>:<port>" lines with a Copy button and
the router hint; the guest's panel shows no address. One leased pair (lane hostaddr), hidden windows, notch fix off.
  hostaddr_pair.py --dylib PATH --out DIR --host-lang ja [--tag T]
Lang codes: en ko fr de ru zh-Hans zh-Hant ja es pt-BR. Frames + log in DIR."""
import json, os, re, shutil, sys, time, traceback
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/src/ui_test'))
import uxgame_pair as U
rig = U.rig
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
results, LANGS = {}, {}
OUT = U.arg('--out', '/tmp/hostaddr'); os.makedirs(OUT, exist_ok=True)
TAG = U.arg('--tag', 'run')
LOGF = open(os.path.join(OUT, f'{TAG}.log'), 'a')
c, st, wait = U.c, U.st, U.wait


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:700])


prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = CODES.index(LANGS.get(x, 'en')); s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang
os.environ['DSL_NOTCH'] = '0'


def shot(x, name):
    p = os.path.join(OUT, f'{TAG}_{name}.png')
    r = c(x, cmd='mpm_shot', how='backbuffer', path=p); log('frame', p, r.get('ok'))


def main():
    rig.DYLIB = U.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/dsrig.dylib'))
    pair = None
    for _ in range(240):                                     # the machine-wide limit is 4 pairs: wait up to 40 min
        pair = rig.lease('hostaddr', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]; port = rig.PAIR_PORTS.get(pair, 7795)
    LANGS[X], LANGS[Y] = U.arg('--host-lang', 'en'), 'en'
    log('pair', pair, 'dylib', rig.DYLIB, 'langs', LANGS)
    try:
        json.dump({'role': 'host', 'port': port, 'name': 'The owner'}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):
            d = os.path.join(rig.idir(i), 'data'); os.makedirs(d, exist_ok=True)
            open(os.path.join(d, 'mp_ui.json'), 'w').write('{"list_open":1}\n')
        U.start(X, 1512, 982)
        wait('title host', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(X)), timeout=1500, every=5)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
        log('host load', c(X, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(X).get('det', {}).get('locked'), timeout=900, every=3)
        U.start(Y, 1512, 982)
        wait('title guest', lambda: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(Y)), timeout=1500, every=5)
        wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900, every=3)
        time.sleep(8)
        h = c(X, cmd='ui_host_addr'); g = c(Y, cmd='ui_host_addr')
        addrs = [m.group(1) for ln in h.get('lines', []) for m in [re.search(r'(\d+\.\d+\.\d+\.\d+:\d+)', ln)] if m]
        check(f'{TAG}: host panel shows address lines with the port', addrs and all(a.endswith(f':{port}') for a in addrs) and len(addrs) == h.get('count'), h)
        check(f'{TAG}: host panel shows the router hint', bool(h.get('hint')), h.get('hint'))
        check(f'{TAG}: guest panel shows no address', g.get('host') == 0 and not g.get('lines') and not g.get('hint'), g)
        shot(X, f'host_{LANGS[X]}'); shot(Y, 'guest')
        clip = os.path.join(rig.idir(X), 'clipboard.txt')
        if os.path.exists(clip): os.rename(clip, clip + time.strftime('.prev_%Y%m%d_%H%M%S'))
        r = c(X, cmd='ui_host_addr', copy=0); time.sleep(1)
        got = open(clip).read().strip() if os.path.exists(clip) else ''
        check(f'{TAG}: Copy puts the first address on the clipboard', addrs and got == addrs[0], f'{got!r} vs {addrs[:1]}')
        shot(X, f'host_{LANGS[X]}_copied')
    except Exception:
        check('run reached the end', False, traceback.format_exc()[-700:])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try: rig.stop(i)
                except Exception as e: log('stop', i, e)
            rig.release(pair, 'hostaddr')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
