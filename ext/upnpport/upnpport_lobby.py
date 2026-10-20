#!/usr/bin/env python3
"""upnpport lane (owner 2026-10-20): hosting by address opens a port on the router by UPnP and the lobby shows it.
Runs against the REAL home router (owner approved router changes for this test). One leased pair, only the host copy
runs (hidden, windowed, muted, notch fix off). The plain rig dylib = no loader settings: host port 47777.
  open    (en): DSRIG_UPNP=1 DSRIG_UPNP_SKIP_EXISTING=1 DSRIG_UPNP_LEASE=40 -> mapping added, listed, renewed, deleted on Leave
  already (fr): DSRIG_UPNP=1                                -> a listed mapping to this computer:47777 is shown, untouched
  off     (de): DSRIG_UPNP=0                                -> "Your router did not open a port" line, router untouched
At the end the router's mapping list must equal the one before the run (this lane never leaves a mapping).
  upnpport_lobby.py --dylib PATH --out DIR [--cases open,already,off]
Frames + log in DIR. Never commit DIR (it holds this home's addresses); the log masks them."""
import json, os, re, shutil, socket, sys, time, traceback, urllib.request
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'src', 'ui_test'))
import uxgame_pair as U
rig = U.rig
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
OUT = U.arg('--out', '/tmp/upnpport'); os.makedirs(OUT, exist_ok=True)
CASES = U.arg('--cases', 'open,already,off').split(',')
LOGF = open(os.path.join(OUT, 'run.log'), 'a')
results, LANG, MASK = {}, {}, {}
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, LEAVE=12, LEAVE_CONFIRM=68)
DESC = 'Dungeon Settlers host'


def mask(t):
    t = str(t)
    for k, v in MASK.items(): t = t.replace(k, v)
    return t


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + mask(' '.join(str(x) for x in a))
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])


# ---------------------------------------------------------------- the router, read from here (same calls as the game)
R = {}
def router():
    if R: return R
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2)
    s.sendto(b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 2\r\n'
             b'ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n', ('239.255.255.250', 1900))
    loc = None
    try:
        while not loc:
            d, _ = s.recvfrom(2000); m = re.search(rb'(?i)location:\s*(\S+)', d); loc = m and m.group(1).decode()
    except socket.timeout: pass
    if not loc: raise RuntimeError('no UPnP router answered')
    x = urllib.request.urlopen(loc, timeout=3).read().decode()
    m = re.search(r'<serviceType>([^<]*WAN(?:IP|PPP)Connection[^<]*)</serviceType>.*?<controlURL>([^<]*)</controlURL>', x, re.S)
    base = re.match(r'http://[^/]+', loc).group(0)
    R['svc'] = m.group(1); R['ctl'] = m.group(2) if m.group(2).startswith('http') else base + m.group(2)
    host = re.match(r'http://([^:/]+)', base).group(1)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.connect((host, 1900)); R['lan'] = u.getsockname()[0]; u.close()
    MASK[R['lan']] = '<this computer>'
    return R


def soap(action, args=''):
    r = router()
    body = (f'<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
            f'<s:Body><u:{action} xmlns:u="{r["svc"]}">{args}</u:{action}></s:Body></s:Envelope>')
    q = urllib.request.Request(r['ctl'], data=body.encode(), headers={'Content-Type': 'text/xml; charset="utf-8"', 'SOAPAction': f'"{r["svc"]}#{action}"'})
    try: return urllib.request.urlopen(q, timeout=3).read().decode()
    except urllib.error.HTTPError as e: return e.read().decode()


def mappings():
    out = []
    for i in range(128):
        t = soap('GetGenericPortMappingEntry', f'<NewPortMappingIndex>{i}</NewPortMappingIndex>')
        if '<errorCode>' in t: break
        f = dict(re.findall(r'<(New\w+)>([^<]*)</New\w+>', t))
        out.append(dict(proto=f.get('NewProtocol'), ext=int(f.get('NewExternalPort') or 0), client=f.get('NewInternalClient'),
                        port=int(f.get('NewInternalPort') or 0), desc=f.get('NewPortMappingDescription'), lease=int(f.get('NewLeaseDuration') or 0)))
    return out


def key(m): return (m['proto'], m['ext'], m['client'], m['port'], m['desc'])
def show(ms): return [f"{m['proto']} {m['ext']}->{m['client']}:{m['port']} '{m['desc']}' lease {m['lease']}" for m in ms]


# ---------------------------------------------------------------- game copy
prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = CODES.index(LANG.get('cur', 'en')); s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang
os.environ['DSL_NOTCH'] = '0'


def start(x, extra):
    for k in [k for k in os.environ if k.startswith('DSRIG_UPNP') or k.startswith('DSRIG_EXTADDR_')]: del os.environ[k]
    os.environ.update(extra)
    rig.start(x, fps=30, window=True)


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


def rlog(X): return open(os.path.join(rig.idir(X), 'rig.log'), errors='replace').read()


def leave(X):
    c(X, cmd='mpm_press', button=B['LEAVE']); time.sleep(2)
    if st(X).get('state') == 'lobby' or 'confirm' in json.dumps(st(X)).lower():
        c(X, cmd='mpm_press', button=B['LEAVE_CONFIRM']); time.sleep(2)
    wait('session ended', lambda: st(X).get('port', 1) == 0, timeout=30, every=1)


def one(X, case, base):
    lang = {'open': 'en', 'already': 'fr', 'off': 'de'}[case]; LANG['cur'] = lang
    env = {'open': {'DSRIG_UPNP': '1', 'DSRIG_UPNP_SKIP_EXISTING': '1', 'DSRIG_UPNP_LEASE': '40'},
           'already': {'DSRIG_UPNP': '1'}, 'off': {'DSRIG_UPNP': '0', 'DSRIG_EXTADDR_UPNP': '0'}}[case]
    mp = os.path.join(rig.idir(X), 'mp.json')
    if os.path.exists(mp): os.replace(mp, mp + '.last')
    def free47777():                                       # another lane's host on 47777 moves ours to a random port
        k = socket.socket(); k.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: k.bind(('0.0.0.0', 47777)); return True
        except OSError: return False
        finally: k.close()
    wait('port 47777 free', free47777, timeout=1800, every=10)
    start(X, env)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
    time.sleep(3)
    for b in ('MULTIPLAYER', 'HOST', 'LOAD_SAVE'): c(X, cmd='mpm_press', button=B[b]); time.sleep(1.5)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(U.SAVE)); shutil.copy2(U.SAVE, src)
    log(case, 'host load', c(X, cmd='load_save', file=src, det=0))
    wait('lobby', lambda: st(X).get('state') == 'lobby', timeout=900, every=3)
    s = wait('router + address done', lambda: (lambda s: s if s.get('inet_state', 0) >= 2 and s.get('up_state', 1) >= 2 else None)(st(X)), timeout=40, every=1)
    time.sleep(2); s = st(X)
    port, inet, ue, us = s.get('port'), s.get('internet', ''), s.get('up_ext', 0), s.get('up_state')
    if inet: MASK[inet.rsplit(':', 1)[0]] = '<internet>'
    box = s.get('lw', {}).get('addr', '') or ''; note = s.get('lw', {}).get('inv_note', '') or ''
    shot = os.path.join(OUT, f'{case}_{lang}_lobby.png'); c(X, cmd='mpm_shot', how='backbuffer', path=shot)
    log(case, 'listen', port, 'up_state', us, 'external', ue, 'box', box); log(case, 'note', note.replace('\n', ' | '))
    log(case, 'rig.log', [l for l in rlog(X).splitlines() if 'upnpport' in l or 'router port' in l][-8:])
    now = mappings(); log(case, 'router now', show(now))
    if case == 'open':
        mine = [m for m in now if m['proto'] == 'TCP' and m['ext'] == ue and m['client'] == R['lan'] and m['port'] == port and m['desc'] == DESC]
        check('open: state OPENED, external port set', us == 2 and ue, (us, ue))
        check('open: the router lists the new mapping external -> this computer:listen port', bool(mine) and key(mine[0]) not in {key(m) for m in base}, show(mine))
        check('open: the lobby box shows internet:external', inet.endswith(f':{ue}') and box == inet, box)
        want = tr(lang, 'Friends outside your home: %s (opened automatically on your router).') % inet
        check('open: the note says opened automatically', want in note, note)
        l0 = mine[0]['lease'] if mine else 0
        rx = rf'renewed the router mapping {ue} \(lease 40 s\): done'; n0 = len(re.findall(rx, rlog(X)))   # rig.log keeps older runs
        wait('renewal', lambda: len(re.findall(rx, rlog(X))) > n0, timeout=40, every=2)
        time.sleep(1); after = [m for m in mappings() if m['proto'] == 'TCP' and m['ext'] == ue]
        check('open: renewed at half the lease (rig.log + router lease back up)', after and after[0]['lease'] >= 30, f'lease {l0} -> {after[0]["lease"] if after else None}')
        dx = rf'deleted the router mapping {ue} \(session ended\): done'; d0 = len(re.findall(dx, rlog(X)))
        leave(X)
        gone = wait('mapping deleted', lambda: not any(m['proto'] == 'TCP' and m['ext'] == ue for m in mappings()) or None, timeout=20, every=1)
        check('open: Leave deletes the mapping (router no longer lists it)', gone and len(re.findall(dx, rlog(X))) > d0, 'gone')
    elif case == 'already':
        hit = [m for m in base if m['proto'] == 'TCP' and m['client'] == R['lan'] and m['port'] == port]
        check('already: state ALREADY with the listed external port', us == 3 and hit and ue == hit[0]['ext'], (us, ue, show(hit)))
        check('already: the lobby box shows internet:external', inet.endswith(f':{ue}') and box == inet, box)
        want = tr(lang, 'Friends outside your home: %s (your router already forwards this port).') % inet
        check('already: the note says the router already forwards it', want in note, note)
        leave(X); time.sleep(3)
        check('already: the listed mapping is untouched after Leave', {key(m) for m in hit} <= {key(m) for m in mappings()}, 'kept')
    else:
        check('off: state OFF', us == 5, us)
        want = tr(lang, 'Your router did not open a port. Friends outside your home need Steam invite, or open port %s on your router (also called port forwarding) to this computer.') % port
        want2 = tr(lang, 'Your router did not open a port. Friends outside your home need you to open port %s on your router (also called port forwarding) to this computer.') % port
        check('off: the note says the router did not open a port (forward N / Steam invite)', want in note or want2 in note, note)
        check('off: the router list did not change', {key(m) for m in now} == {key(m) for m in base}, 'same')
    rig.stop(X)


def main():
    rig.DYLIB = os.path.abspath(U.arg('--dylib', os.path.join(HERE, '..', '..', 'build', 'dsrig.dylib')))
    base = mappings(); log('router before', show(base))
    pair = None
    for _ in range(240):
        pair = rig.lease('upnpport', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    X = pair[0]; log('pair', pair, 'dylib', rig.DYLIB)
    try:
        for case in CASES:
            try: one(X, case, base)
            except Exception: check(f'{case}: run reached the end', False, traceback.format_exc()[-600:])
            finally:
                try: rig.stop(X)
                except Exception: pass
    finally:
        rig.release(pair, 'upnpport')
        end = mappings(); log('router after', show(end))
        check('router list at the end = before the run (no stray mapping)', {key(m) for m in end} == {key(m) for m in base},
              show([m for m in end if key(m) not in {key(b) for b in base}]))
        log('check-prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
