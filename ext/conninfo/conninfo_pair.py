#!/usr/bin/env python3
"""conninfo lane (owner 2026-10-20: "add a mention that it's a connection through Steam"): every players row says how
that player is connected, and the guest's lobby says "Connected to <host> through Steam" / "Connected to <ip:port>".
One leased pair (hidden, windowed, muted, notch fix off). Host X (English) hosts the base save; guest Y (German) joins
by address from the title. The host's internet address comes from a local stand-in web server (203.0.113.10, test
range), so no real address reaches a frame; the host's "Same home" line and address box are blacked out in the frames.
  1 direct:  host row 1 "Direct (127.0.0.1:<guest port>)", host row 0 = its internet address; guest rows + status line
             "Verbunden mit 127.0.0.1:<port>"
  2 Steam (synthetic: the rig has no Steam): mpm_conn {mark:1} on the host marks the guest's loopback port the way the
             Steam bridge marks its own (dsnet_relay_port_mark): host row 1 "Steam" (no address), guest status line
             "Über Steam mit <host> verbunden", guest rows "Steam"
  3 in game (host presses Start): the Players panel's right column says "Steam" for the other player on both copies
  conninfo_pair.py --dylib PATH --out DIR
Never commit DIR. private_* frames show the host panel's own address lines (this Mac's addresses): look, never share."""
import http.server, json, os, shutil, sys, threading, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
SAVE = os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=9, JOIN_ADDR=14, CONNECT=15)
FAKE_IP = '203.0.113.10'


def arg(n, d):
    return sys.argv[sys.argv.index(n) + 1] if n in sys.argv else d


OUT = arg('--out', '/tmp/conninfo'); os.makedirs(OUT, exist_ok=True)
LOGF = open(os.path.join(OUT, 'run.log'), 'a')
results, LANG = {}, {}


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:600])


def tr(lang, en):
    try: return json.load(open(os.path.join(HERE, '..', '..', 'lang', f'{lang}.json'))).get(en, en)
    except Exception: return en


prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = CODES.index(LANG.get(x, 'en')); s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang
os.environ['DSL_NOTCH'] = '0'
os.environ.setdefault('DSRIG_CUSTOM', '0')                 # without Custom Modes (its lobby hooks are not this lane's)


class FakeIp(http.server.BaseHTTPRequestHandler):           # stands in for api.ipify.org
    def do_GET(self):
        self.send_response(200); self.send_header('Content-Type', 'text/plain'); self.end_headers(); self.wfile.write(FAKE_IP.encode())
    def log_message(self, *a): pass


def c(x, **k): return rig.send(x, k, timeout=60, connect_wait=0)
def ms(x): return c(x, cmd='mpm_status')


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
            if r: return r
        except Exception: pass
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def shot(x, name, mask=None):
    p = os.path.join(OUT, name + '.png')
    log('frame', name, c(x, cmd='mpm_shot', how='backbuffer', path=p).get('ok'))
    if mask:                                                  # this Mac's own addresses (box + "Same home" line): black
        time.sleep(1.5)
        try:
            im = Image.open(p).convert('RGB'); d = ImageDraw.Draw(im); H = im.size[1]
            for r in mask:
                if r[2] > r[0]: d.rectangle([r[0] - 4, H - r[3] - 4, r[2] + 4, H - r[1] + 4], fill=(0, 0, 0))
            im.save(p)
        except Exception as e: log('mask failed', e)
    return p


def rows(s): return [r.get('name', '') for r in s.get('lw', {}).get('rows', [])]
def conn(x): return {r['player']: r for r in c(x, cmd='mpm_conn').get('rows', [])}


def run(X, Y, port_web):
    for x, nm in ((X, 'Hosta'), (Y, 'Guesto')):
        I = rig.idir(x)
        for f in ('mp.json', 'mp_menu.json', 'clipboard.txt'):
            p = os.path.join(I, f)
            if os.path.exists(p): os.replace(p, p + '.last')
        json.dump({'name': nm}, open(os.path.join(I, 'mp_menu.json'), 'w'))
    LANG[X], LANG[Y] = 'en', 'de'
    os.environ.update({'DSRIG_EXTADDR_UPNP': '0', 'DSRIG_EXTADDR_WEB_URL': f'http://127.0.0.1:{port_web}/'})
    rig.start(X, fps=30, window=True); rig.start(Y, fps=30, window=True)
    for x in (X, Y): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'title {x}', timeout=1500)
    time.sleep(3)
    for b in ('MULTIPLAYER', 'HOST', 'LOAD_SAVE'): c(X, cmd='mpm_press', button=B[b]); time.sleep(1.5)
    work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(SAVE)); shutil.copy2(SAVE, src)
    log('host load', c(X, cmd='load_save', file=src, det=0))
    wait('lobby', lambda: ms(X).get('state') == 'lobby', timeout=900, every=3)
    port = wait('host port', lambda: ms(X).get('port'), timeout=60, every=1)
    wait('internet address', lambda: ms(X).get('inet_state', 0) >= 2, timeout=30, every=1)
    for b in ('MULTIPLAYER', 'JOIN_ADDR'): c(Y, cmd='mpm_press', button=B[b]); time.sleep(1.5)
    log('type', c(Y, cmd='mpm_type', text=f'127.0.0.1:{port}')); c(Y, cmd='mpm_press', button=B['CONNECT'])
    wait('guest in the lobby', lambda: ms(Y).get('state') == 'guest_lobby' and ms(Y).get('lw', {}).get('active'), timeout=900, every=3)
    time.sleep(4)
    hp = f'127.0.0.1:{port}'

    # 1 direct
    hc, gc = conn(X), conn(Y)
    sx, sy = ms(X), ms(Y)
    g1 = hc.get(1, {})
    check('direct: host knows the guest is direct, with its address', g1.get('via') == 2 and g1.get('words', '').startswith('Direct (127.0.0.1:'), g1)
    check('direct: host lobby row of the guest shows "Direct (ip:port)"', any('Direct (127.0.0.1:' in r for r in rows(sx)[1:]), rows(sx))
    check('direct: host lobby own row shows its internet address', FAKE_IP + f':{port}' in rows(sx)[0], rows(sx)[0])
    want_g = tr('de', 'Direct (%s)') % hp
    check('direct: guest sees the host row as direct with the address it joined', gc.get(0, {}).get('words') == want_g, gc.get(0))
    check('direct: guest own row says Direct, no address', gc.get(1, {}).get('words') == tr('de', 'Direct'), gc.get(1))
    check('direct: guest status line "Connected to <ip:port>" (de)', sy.get('lw', {}).get('conn') == tr('de', 'Connected to %s') % hp, sy.get('lw', {}).get('conn'))
    check('direct: guest lobby rows carry the connection words', any(want_g in r for r in rows(sy)), rows(sy))
    shot(X, '1_direct_host_lobby_en', sx.get('lw', {}).get('addr_rects')); shot(Y, '1_direct_guest_lobby_de')

    # 2 Steam (synthetic bridge mark)
    m = c(X, cmd='mpm_conn', mark=1); log('mark', m.get('marked'))
    time.sleep(3)
    hc, gc = conn(X), conn(Y); sx, sy = ms(X), ms(Y)
    st_de = tr('de', 'Steam')
    check('steam: host sees the bridged guest as Steam', hc.get(1, {}).get('via') == 3 and hc.get(1, {}).get('words') == 'Steam', hc.get(1))
    check('steam: host lobby guest row says Steam, no address', any(r.endswith('>Steam</color></size>') for r in rows(sx)[1:]) and not any('127.0.0.1' in r for r in rows(sx)[1:]), rows(sx))
    want = tr('de', 'Connected to %s through Steam') % 'Hosta'
    check('steam: guest status line "Connected to <host> through Steam" (de)', sy.get('lw', {}).get('conn') == want, sy.get('lw', {}).get('conn'))
    check('steam: guest rows say Steam, no address anywhere', all(gc.get(i, {}).get('words') == st_de for i in (0, 1)) and not any('127.0.0.1' in r for r in rows(sy)), (gc, rows(sy)))
    shot(X, '2_steam_host_lobby_en', sx.get('lw', {}).get('addr_rects')); shot(Y, '2_steam_guest_lobby_de')

    # 3 in game: the Players panel (a session from mp.json: no lobby, like the in-game tests)
    for x in (X, Y): rig.stop(x)
    import socket; so = socket.socket(); so.bind(('127.0.0.1', 0)); P = so.getsockname()[1]; so.close()
    json.dump({'role': 'host', 'port': P, 'name': 'Hosta', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': P, 'name': 'Guesto'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
    for x in (X, Y):
        uj = os.path.join(rig.idir(x), 'data', 'mp_ui.json')
        if os.path.exists(uj): os.replace(uj, uj + '.last')
        open(uj, 'w').write('{"see_through":0,"names":1,"list_open":1}\n')
    rig.start(X, fps=30, window=True)
    rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host 2', timeout=1500)
    log('host load 2', c(X, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: rig.send(X, {'cmd': 'status'}, timeout=30).get('det', {}).get('locked'), timeout=900, every=3)
    rig.start(Y, fps=30, window=True)
    wait('guest loaded', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(rig.send(Y, {'cmd': 'status'}, timeout=30)), timeout=1500, every=3)
    time.sleep(8)
    def panel(x):
        v = c(x, cmd='ui_view').get('panel', []); return str(v[7].get('text', '')) if len(v) > 7 else ''   # PL_BODYR: the rows' right column
    tx = panel(X)
    check('in game: host Players panel says Direct (ip:port) for the guest', 'Direct (127.0.0.1:' in tx, tx[:500])
    shot(X, 'private_3_ingame_direct_host_en'); shot(Y, '3_ingame_direct_guest_de')
    log('mark 2', c(X, cmd='mpm_conn', mark=1).get('marked')); time.sleep(3)
    tx, ty = panel(X), panel(Y)
    check('in game: host Players panel says Steam for the guest', '>Steam</color></margin>' in tx and '127.0.0.1' not in tx, tx[:500])
    check('in game: guest Players panel says Steam for the host', '>Steam</color></margin>' in ty and '127.0.0.1' not in ty, ty[:500])
    shot(X, 'private_4_ingame_steam_host_en'); shot(Y, '4_ingame_steam_guest_de')


def main():
    rig.DYLIB = os.path.abspath(arg('--dylib', os.path.join(HERE, '..', '..', 'build', 'dsrig.dylib')))
    srv = http.server.HTTPServer(('127.0.0.1', 0), FakeIp); threading.Thread(target=srv.serve_forever, daemon=True).start()
    pair = None
    for _ in range(240):
        pair = rig.lease('conninfo', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]; log('pair', pair, 'dylib', rig.DYLIB)
    try:
        run(X, Y, srv.server_address[1])
    except Exception:
        check('run reached the end', False, traceback.format_exc()[-800:])
    finally:
        for x in (X, Y):
            try: rig.stop(x)
            except Exception: pass
            for f in ('rig.log',):
                p = os.path.join(rig.idir(x), f)
                if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        rig.release(pair, 'conninfo')
        log('check-prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
