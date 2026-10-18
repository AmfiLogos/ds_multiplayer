#!/usr/bin/env python3
"""win_checklist.py - the Windows VM release checklist, bot-style (winrig.py + the Mac test copy W of mac/mac.sh).

Every step is a control-port command or a file/log check through the guest agent; waits are on events (port
answers, menu states, log lines). Screenshots only where the screen is the thing checked: translated text and
layout (item 3, and the ZET-U windows of item 2). They are the game's own frames (DSL_UI_TEST shot), saved to
<out>/shots for a person to look at; the runner does not judge them.

  win_checklist.py --out DIR --setup NEW_Setup.exe [--prev-setup OLD_Setup.exe] [--items 1,2,3,4,5]
                   [--mac-build DIR] [--save-base F] [--save-dungeon F]
Items (ship013's v0.1.15 list):
  1  fresh install AND update over the previous release: ZET-U installed and OFF; a normal save loads with no ZET-U
     save warning; (6) the crash-report question shows once on the first start, No is stored, not asked again
  2  ZET-U on (max units 8): a dungeon save loads (Windows hang regression, v0.1.15); units on the map; shots
  3  German + Russian shots: title (saves notice, kit notice), Mods window, a mod's Settings page, Load list,
     "Get the host's mods" box
  4  "Get the host's mods": plan, Get, Apply, soft restart, the restarted game rejoins, world arrives, 0 desyncs
  5  cross-play with fights: 3 dungeon entries in the same processes, lend + take back, quit to title; 0 crashes, 0 desyncs
  S  (test builds) the control port follows a soft restart: --mp-dll = a multiplayer.dll with the winbot port fix
Output: <out>/summary.json, <out>/RESULT.txt, <out>/shots/*.png, <out>/logs_<item>/ (VM logs).
"""
import argparse, json, os, socket, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import winrig as W                                                   # noqa: E402

MACSH = os.path.join(HERE, 'mac', 'mac.sh')
MAC_CTL = os.path.expanduser('~/ds_winlane013/ds_modloader/loader/test_env/instances/W/ctl.sock')
SAVES = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves')
ZETU = 'zetu-suite'
BASE_MODS = ['custom-modes', 'gcd-cdr', 'hell-difficulty', 'multiplayer']
LANG = {'en': 0, 'de': 3, 'ru': 4}                                   # GeneralSettingData.SaveCurrentLanguageType (ds_i18n.h order)


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


class Report:
    def __init__(self, out):
        self.out, self.items, self.t0 = out, {}, time.time()
        os.makedirs(os.path.join(out, 'shots'), exist_ok=True)

    def check(self, item, name, ok, detail=''):
        self.items.setdefault(item, {'checks': [], 'secs': 0})['checks'].append({'check': name, 'ok': bool(ok), 'detail': str(detail)[:400]})
        log('PASS' if ok else 'FAIL', f'[{item}]', name, str(detail)[:200])
        return ok

    def save(self):
        s = {'started': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(self.t0)), 'minutes': round((time.time() - self.t0) / 60, 1),
             'items': self.items, 'pass': all(c['ok'] for i in self.items.values() for c in i['checks'])}
        json.dump(s, open(os.path.join(self.out, 'summary.json'), 'w'), indent=1)
        lines = [f"{'PASS' if s['pass'] else 'FAIL'} in {s['minutes']} min"]
        for k, v in self.items.items():
            bad = [c['check'] + ': ' + c['detail'] for c in v['checks'] if not c['ok']]
            lines.append(f"item {k}: {'PASS' if not bad else 'FAIL'} ({v['secs'] / 60:.1f} min)" + ''.join('\n  ' + b for b in bad))
        open(os.path.join(self.out, 'RESULT.txt'), 'w').write('\n'.join(lines) + '\n')
        return s


# ------------------------------------------------------------------ the Mac side (hidden test copy W, loader + the release's Mac mods)
class Mac:
    def __init__(self, build=None):
        self.env = dict(os.environ)
        if build:
            self.env['MAC_BUILD'] = build

    def sh(self, *a, timeout=600):
        r = subprocess.run([MACSH, *a], capture_output=True, text=True, env=self.env, timeout=timeout)
        try:
            return json.loads(r.stdout)
        except ValueError:
            return {'raw': (r.stdout + r.stderr)[-600:]}

    def cmd(self, obj=None, timeout=60, **k):
        s = socket.socket(socket.AF_UNIX); s.settimeout(timeout); s.connect(MAC_CTL)
        s.sendall(json.dumps(obj if obj is not None else k).encode() + b'\n')
        buf = b''
        while not buf.endswith(b'\n'):
            d = s.recv(1 << 20)
            if not d:
                break
            buf += d
        s.close()
        return json.loads(buf)

    def status(self):
        return self.cmd(cmd='status')

    def start(self, active, settings=None):
        """(re)start copy W with these mods enabled (DSL_PROFILE) and mod settings (DSL_SETTINGS_FILE)"""
        self.sh('stop')
        m = os.path.join(os.path.dirname(MAC_CTL), 'data', 'Mods')
        os.makedirs(m, exist_ok=True)
        json.dump({'enabled': list(active), 'order': list(active)}, open(os.path.join(m, 'active.json'), 'w'))
        json.dump(settings or {}, open(os.path.join(m, 'settings.json'), 'w'))
        self.env.update(DSML_SESSION=os.path.join(m, 'worlds.json'), DSL_SETTINGS_FILE=os.path.join(m, 'settings.json'),
                        DSL_PROFILE=os.path.join(m, 'active.json'))
        r = self.sh('start')
        t = time.time()
        while time.time() - t < 240:
            try:
                s = self.status()
                if s.get('scene') == 1 and not s.get('busy'):
                    return r
            except OSError:
                pass
            time.sleep(1)
        raise W.GameError('Mac copy W: no title after 240 s')

    def host(self, save):
        r = self.sh('host', save)
        self.answer_boxes()
        return r

    def answer_boxes(self):
        """a stock Yes/No popup on the host blocks the join (OLD, v0.1.14 too): No"""
        m = self.cmd(cmd='mpm_status')
        if (m.get('box') or {}).get('on'):
            self.cmd(cmd='mpm_box', action='cancel')
            return m['box'].get('text')

    def port(self):
        return self.cmd(cmd='mpm_status').get('port')

    def peers(self):
        pm = (self.status().get('mp') or {}).get('present', 0) or 0
        return [i for i in range(32) if pm & (1 << i)]

    def stop(self):
        return self.sh('stop')


# ------------------------------------------------------------------ helpers
def fresh_logs(w, rep, item):
    d = os.path.join(rep.out, f'logs_{item}')
    w.logs(d)
    return d


def scan_logs(w, rep, item):
    """crashes and Windows exceptions in the VM logs (the same lines vmwin2 read by hand)"""
    pl = w.text(W.PROF + r'\player.log') or ''
    rl = w.text(W.PROF + r'\rig.log') or ''
    bad = [l for l in pl.splitlines() if 'InvalidCastException' in l or 'NullReferenceException' in l and 'dsl' in l.lower()]
    rep.check(item, 'no CRASH line in rig.log', 'CRASH ' not in rl, [l for l in rl.splitlines() if 'CRASH' in l][:2])
    rep.check(item, 'no InvalidCastException in player.log', not bad, bad[:2])


def load_with_boxes(w, guest_save, timeout=240):
    """load_save; a box shown during the load (save warning, notice) is recorded and confirmed. Returns (status, boxes)"""
    boxes = []
    w.cmd(cmd='load_save', file=w.expand(guest_save), det=0)
    t = time.time()
    while time.time() - t < timeout:
        b = (w.mpm().get('box') or {})
        if b.get('on'):
            boxes.append(b.get('text', '')); w.box('confirm'); time.sleep(0.5); continue
        s = w.loaded()
        if s:
            return s, boxes
        time.sleep(0.5)
    raise W.GameError(f'save did not load within {timeout} s (boxes {boxes})')


def put_save(w, local, name):
    w.put(local, W.DATA + '\\Saves\\' + name)
    return W.DATA + '\\Saves\\' + name


def mods_state(w):
    w.ui({'op': 'open'})
    st = w.ui({'op': 'state'})
    w.ui({'op': 'close'})
    return {c['name']: c for c in st.get('controls', [])}


# ------------------------------------------------------------------ items
def item1(w, rep, a):
    """fresh install + update; ZET-U off; normal save without ZET-U warning; crash consent once (item 6)"""
    I = '1'
    for kind, setup in (('fresh', a.setup), ('update', a.setup)):
        if kind == 'fresh':
            if w.exists(W.ROOT):
                w.kill_all()
                w.move(W.ROOT, W.WORK + r'\to_delete\install_' + time.strftime('%Y%m%d-%H%M%S'))   # never deleted
        else:
            if not a.prev_setup:
                rep.check(I, 'update over the previous release', False, 'no --prev-setup given'); continue
            w.kill_all(); w.install(a.prev_setup)
        t = time.time(); rc = w.install(setup)
        rel = (w.text(W.ROOT + r'\RELEASE') or '').strip()
        rep.check(I, f'{kind}: Setup /S', rc == 0 and rel, f'rc {rc}, RELEASE {rel}, {time.time() - t:.0f} s')
        act = w.json_file(W.DATA + r'\Mods\active.json') or {}
        rep.check(I, f'{kind}: {ZETU} installed', w.exists(W.ROOT + '\\mods\\' + ZETU + r'\mod.json'))
        rep.check(I, f'{kind}: {ZETU} not enabled in the profile', ZETU not in act.get('enabled', []), act)
        w.prepare(consent=None)
        w.start()
        if kind == 'fresh':                                          # item 6: the question on the first start
            b = w.wait(lambda: (w.mpm().get('box') or {}).get('on') and w.mpm()['box'], 'the crash-report question', 30)
            rep.check(I, 'crash-report question shown on the first start', 'crash' in b.get('text', '').lower(), b.get('text'))
            w.box('cancel')
            c = w.wait(lambda: w.json_file(W.ROOT + r'\crash\consent.json'), 'consent.json', 10)
            rep.check(I, 'No stored (consent.json)', c.get('consent') == 'no', c)
        ms = mods_state(w)
        z = ms.get('mod:' + ZETU, {})
        rep.check(I, f'{kind}: Mods window shows {ZETU} OFF', z and not z.get('on'), z)
        sp = put_save(w, a.save_base, 'WINBOT_base.json')
        s, boxes = load_with_boxes(w, sp)
        rep.check(I, f'{kind}: normal save loads with no ZET-U warning', not any('zet' in b.lower() for b in boxes), boxes or 'no box')
        scan_logs(w, rep, I)
        fresh_logs(w, rep, f'1_{kind}')
        if kind == 'fresh':                                          # item 6: not asked again
            w.start()
            time.sleep(3)
            b = w.mpm().get('box') or {}
            rep.check(I, 'crash-report question not asked on the next start', not (b.get('on') and 'crash' in b.get('text', '').lower()), b)
        w.stop()


def item2(w, rep, a):
    I = '2'
    w.prepare(active=BASE_MODS[:3] + [ZETU, 'multiplayer'], settings={ZETU: {'max_units': 8}})
    w.start()
    sp = put_save(w, a.save_dungeon, 'WINBOT_dungeon.json')
    t = time.time()
    try:
        s, boxes = load_with_boxes(w, sp, 240)
        rep.check(I, 'ZET-U on: dungeon save loads (no NOW LOADING hang)', True, f'{time.time() - t:.0f} s, map {s.get("map")}, boxes {boxes}')
        us = [u for u in w.cmd(cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead')]
        rep.check(I, 'player units on the map', len(us) > 0, f'{len(us)} units (8-unit expedition needs a save with 8 units)')
        w.gshot(os.path.join(rep.out, 'shots', '2_zetu_dungeon.png'))
    except W.GameError as e:
        rep.check(I, 'ZET-U on: dungeon save loads (no NOW LOADING hang)', False, e)
    scan_logs(w, rep, I)
    fresh_logs(w, rep, '2')
    w.stop()


def item3(w, rep, a, mac):
    """German + Russian shots (screen checks: kept as pictures)"""
    I = '3'
    host = mac_host_with_extra(mac, a)
    for lang in ('de', 'ru'):
        w.prepare(lang=LANG[lang], active=BASE_MODS, settings={})
        w.start()
        sh = lambda n: w.gshot(os.path.join(rep.out, 'shots', f'3_{lang}_{n}.png'))
        sh('title')
        w.ui({'op': 'open'}); sh('mods')
        r = w.ui({'op': 'click', 'target': 'settings:' + ZETU})
        rep.check(I, f'{lang}: {ZETU} Settings page opens', r.get('ok'), r); sh('settings')
        w.ui({'op': 'close'})
        w.cmd(cmd='mpm_open_load'); w.wait(lambda: w.mpm().get('state') in ('pick_save', 'idle'), 'load list', 10)
        w.cmd(cmd='mpm_list', flip=1); time.sleep(1); sh('load')
        w.cmd(cmd='mpm_press', button=W.B['BACK'])
        w.join(f'{W.hub_host()}:{host}', wait=None)
        w.wait(lambda: w.cmd(cmd='mpm_hostmods').get('box'), "the host's mods box", 60)
        hm = w.mpm().get('box') or {}
        rep.check(I, f"{lang}: \"Get the host's mods\" box shown", hm.get('on'), hm.get('text', '')[:120]); sh('hostmods')
        lab = [l for l in hm.get('text', '').splitlines() if 'max_units' in l]
        rep.check(I, f'{lang}: no raw setting id in the box', not lab, lab)
        w.box('cancel')
        w.stop()


def mac_host_with_extra(mac, a):
    """Mac copy W hosts the base save with ZET-U on + a setting the VM lacks; returns its menu port"""
    mac.start(BASE_MODS[:3] + [ZETU, 'multiplayer'], {ZETU: {'max_units': 7}})
    work = os.path.join(os.path.dirname(MAC_CTL), 'save_in_base.json')
    import shutil; shutil.copy2(a.save_base, work)
    mac.host(work)
    return mac.port()


def item4(w, rep, a, mac):
    I = '4'
    port = mac_host_with_extra(mac, a)
    w.prepare(lang=0, active=BASE_MODS, settings={})
    w.start()
    w.join(f'{W.hub_host()}:{port}', wait=None)
    w.wait(lambda: w.cmd(cmd='mpm_hostmods').get('box'), "the host's mods box", 60)
    hm = w.mpm().get('box') or {}
    rep.check(I, "the host's mods box lists the differences", ZETU in hm.get('text', '').lower() or 'zet' in hm.get('text', '').lower(), hm.get('text', '')[:300])
    old = w.pid
    for f in ('dsload.log',):
        w.mark(f)
    t = time.time()
    w.box('confirm')                                                 # Get
    port_ok = True
    try:
        w.wait_log('dsload.log', ['soft:'], 300)
        w.wait(lambda: [p for p in w.procs() if p['pid'] != old], 'the restarted game process', 120)
        rep.check(I, 'soft restart: a new game process', True, f'{time.time() - t:.0f} s after Get')
        new = w.wait_restart(old, 120)
        rep.check(I, 'soft restart: the new game took the control port', new != old, f'pid {old} -> {new}, {time.time() - t:.0f} s')
    except W.GameError as e:
        rl = w.text(W.PROF + r'\rig.log') or ''
        if 'bind/listen failed' in rl:                               # the release build without the winbot port fix
            port_ok = False
            w.pid = next((p['pid'] for p in w.procs() if p['pid'] != old), None)
            log('no control port in the restarted game (build without the port fix): host view + logs')
        else:
            rep.check(I, 'soft restart', False, e)
            return
    act = w.json_file(W.DATA + r'\Mods\active.json') or {}
    st = w.json_file(W.DATA + r'\Mods\settings.json') or {}
    rep.check(I, "host's mods taken", ZETU in act.get('enabled', []), act)
    rep.check(I, "host's setting taken", (st.get(ZETU) or {}).get('max_units') == 7, st)
    try:
        if port_ok:
            s = w.wait(w.in_mp_game, "the restarted game rejoined and the host's world arrived", 240, 1)
            m = s.get('mp') or {}
            rep.check(I, 'rejoined, world loaded', True, f"{time.time() - t:.0f} s after Get; {w.cmd(cmd='mpm_hostmods')}")
            rep.check(I, '0 desyncs', not m.get('desyncs') and not m.get('desynced'), m.get('desyncs'))
        else:                                                        # a build without the port fix: the host's view + B's log
            def joined():
                m = mac.status().get('mp') or {}
                return 1 in mac.peers() and not (m.get('busy_mask') or 0) and m
            w.wait(lambda: joined() and (time.sleep(5) or joined()), "the host sees the restarted guest present and not busy", 240, 2)
            rl = w.text(W.PROF + r'\rig.log') or ''
            rep.check(I, 'rejoined, world loaded (host view)', 'did not arrive' not in rl, f"{time.time() - t:.0f} s after Get")
            m = mac.status().get('mp') or {}
            rep.check(I, '0 desyncs (host)', not m.get('desyncs') and not m.get('desynced'), m.get('desyncs'))
    except W.GameError as e:
        rep.check(I, 'rejoined, world loaded', False, f"{e}; host peers {mac.peers()}")
    scan_logs(w, rep, I)
    fresh_logs(w, rep, '4')
    w.stop(); mac.stop()


# -- item 5: cross-play, Mac hosts, the VM joins (xplay.py of lane vmwin2, both sides on the control port)
def item5(w, rep, a, mac):
    I = '5'
    mac.start(BASE_MODS, {})
    work = os.path.join(os.path.dirname(MAC_CTL), 'save_in_dungeon.json')
    import shutil; shutil.copy2(a.save_dungeon, work)
    mac.host(work)
    w.prepare(lang=0, active=BASE_MODS, settings={})
    w.start()
    w.join(f'{W.hub_host()}:{mac.port()}', wait='lobby')
    mac.cmd(cmd='mpm_press', button=W.B['START'])
    S = {'mac': mac, 'vm': w}
    c = lambda side, **k: S[side].cmd(**k)
    st = lambda side: S[side].cmd(cmd='status')
    tm = lambda side, **k: c(side, cmd='ui_teams', **k)

    def alive(where):
        ok_vm = w.alive()
        ok_mac = 'scene' in mac.status()
        rep.check(I, f'both alive: {where}', ok_vm and ok_mac, {'vm': ok_vm, 'mac': ok_mac})
        if not (ok_vm and ok_mac):
            raise W.GameError('a game process died')

    def land(side):
        s = st(side)
        return s.get('map') == 1 and not s.get('busy') and (s.get('det') or {}).get('locked')

    def host_unit(map_):
        us = [u for u in c('mac', cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u and not u.get('downed')]
        for u in us:
            if c('mac', cmd='mp_owner', guid=u['guid']).get('owner') == 0:
                return u['guid']
        g = next((u['guid'] for u in us if c('mac', cmd='mp_owner', guid=u['guid']).get('owner') == -1), None)
        if g:
            c('mac', cmd='mp_claim', guid=g)
            w.wait(lambda: c('mac', cmd='mp_owner', guid=g).get('owner') == 0, 'host owns the unit', 40)
        return g

    def fight(where):
        us = [u for u in c('mac', cmd='list_units')['units'] if 'cell' in u and not u.get('dead')]
        pl = [u for u in us if u.get('faction') == 1 and c('mac', cmd='mp_owner', guid=u['guid']).get('owner') == 0]
        en = [u for u in us if u.get('faction') == 3]
        if not pl or not en:
            log('no fight', where); return
        p = pl[0]['cell']; en.sort(key=lambda u: (u['cell'][0] - p[0]) ** 2 + (u['cell'][1] - p[1]) ** 2)
        for u in pl:
            c('mac', cmd='mp_move', guid=u['guid'], x=en[0]['cell'][0], y=en[0]['cell'][1])
        time.sleep(25)                                                 # the fight itself runs on game time
        alive(f'fight {where}')

    w.wait(lambda: land('mac') and land('vm'), 'both in the dungeon (entry 1)', 600, 1)
    alive('entry 1')
    for cy in range(1, 4):
        if cy > 1:
            f1 = host_unit(0)
            tm('mac', action='open'); tm('mac', action='go')
            w.wait(lambda: tm('mac').get('stock', {}).get('open'), 'stock window', 60)
            tm('mac', action='stock_select', units=f1); tm('mac', action='stock_next')
            w.wait(lambda: tm('mac').get('stock', {}).get('state') == 1, 'stock step 2', 60)
            tm('mac', action='stock_accept')
            w.wait(lambda: tm('vm').get('prop', {}).get('shown'), f'guest summary {cy}', 60)
            tm('vm', action='approve')
            w.wait(lambda: land('mac') and land('vm'), f'both in the dungeon (entry {cy})', 900, 1)
            alive(f'entry {cy}')
        host_unit(1)
        fight(f'entry {cy}')
        if cy == 1:
            g = host_unit(1)
            name = next((u.get('name', '') for u in c('mac', cmd='list_units')['units'] if u['guid'] == g), '')
            r1 = c('mac', cmd='ui_lend', guid=g, action='lend', to=1)
            w.wait(lambda: c('vm', cmd='ui_lend', guid=g).get('controller') == 1, 'the VM sees the unit lent to it', 30)
            r2 = c('mac', cmd='ui_lend', guid=g, action='take')
            w.wait(lambda: c('vm', cmd='ui_lend', guid=g).get('controller') != 1, 'the VM sees the unit taken back', 30)
            m = w.mpm(); said = json.dumps([m.get('notice'), (m.get('chat') or {}).get('last')], ensure_ascii=False)
            rep.check(I, 'lend + take back', r1.get('ok') and r2.get('ok'), {'lend': r1.get('rc'), 'take': r2.get('rc')})
            rep.check(I, 'the take-back message names the unit', name and name in said, f'{name}: {said[:200]}')
            alive('lend / take back')
        if cy < 3:
            portal = c('mac', cmd='ui_buildings', map=1, subtype=2)['buildings'][0]['guid']
            c('mac', cmd='ui_interact', unit=host_unit(1), building=portal, map=1)
            rs = w.wait(lambda: (lambda x, y: (x, y) if x.get('open') and y.get('open') else None)(c('mac', cmd='ui_result'), c('vm', cmd='ui_result')),
                        'return screen on both', 900, 1)
            P = 'mac' if rs[0].get('chooser') == 0 else 'vm'
            c(P, cmd='ui_result', action='return')
            w.wait(lambda: c(P, cmd='ui_result', action='confirm') and not c(P, cmd='ui_result').get('open'), 'return confirmed', 60, 2)
            w.wait(lambda: all((lambda s: s.get('map') == 0 and not s.get('busy'))(st(x)) for x in ('mac', 'vm')), 'both on the base map', 900, 1)
            alive(f'back on the base after entry {cy}')
    for side in ('mac', 'vm'):
        m = st(side).get('mp', {})
        rep.check(I, f'{side}: 0 desyncs', not m.get('desynced') and not m.get('desyncs'), m.get('desyncs'))
    r = c('vm', cmd='mp_quit')
    w.wait(lambda: w.on_title(), 'VM at the title after quit', 300, 1)
    alive('VM at the title')
    scan_logs(w, rep, I)
    fresh_logs(w, rep, '5')
    w.stop(); mac.stop()


def itemS(w, rep, a):
    """the winbot port fix on the VM: the test build's multiplayer.dll (mod.json hash updated, version kept), single
    player Mods window Apply = soft restart, the new process must answer on the port. Files restored after."""
    I = 'S'
    if not a.mp_dll:
        rep.check(I, 'port fix proof', False, 'no --mp-dll given'); return
    import hashlib
    md = W.ROOT + r'\mods\multiplayer'
    keep = W.WORK + r'\to_delete\mp_release_' + time.strftime('%Y%m%d-%H%M%S')
    mj = w.json_file(md + r'\mod.json')
    dll = open(a.mp_dll, 'rb').read()
    w.kill_all()
    for f in ('multiplayer.dll', 'mod.json'):
        w.move(md + '\\' + f, keep + '\\' + f)
    try:
        mj['platforms']['win-x64'].setdefault('files', {})['multiplayer.dll'] = hashlib.sha256(dll).hexdigest()
        w.upload(a.mp_dll, md + r'\multiplayer.dll'); w.write(md + r'\mod.json', json.dumps(mj, indent=1))
        w.prepare(lang=0, active=BASE_MODS, settings={})
        w.start()
        rl = w.text(W.PROF + r'\rig.log') or ''
        rep.check(I, 'test multiplayer.dll loaded (ctl.pid written)', w.exists(W.PROF + r'\ctl.pid'), [l for l in rl.splitlines() if 'control port' in l][-1:])
        old = w.pid; t = time.time()
        w.ui({'op': 'open'}); w.ui({'op': 'click', 'target': 'mod:gcd-cdr'}); w.ui({'op': 'click', 'target': 'apply'})
        new = w.wait_restart(old, 180)
        w.wait(w.on_title, 'the restarted game at the title', 120)
        rl = w.text(W.PROF + r'\rig.log') or ''
        rep.check(I, 'after a soft restart the new process answers on the control port', new != old,
                  f'pid {old} -> {new} in {time.time() - t:.0f} s; ' + ' | '.join(l[13:] for l in rl.splitlines() if 'control port' in l)[-300:])
        w.ui({'op': 'open'}); w.ui({'op': 'click', 'target': 'mod:gcd-cdr'}); w.ui({'op': 'click', 'target': 'apply'})   # back on
        w.wait_restart(new, 180)
        fresh_logs(w, rep, 'S')
    finally:
        w.stop()
        for f in ('multiplayer.dll', 'mod.json'):
            w.move(md + '\\' + f, keep + r'\winbot_' + f)
            w.move(keep + '\\' + f, md + '\\' + f)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True); p.add_argument('--setup'); p.add_argument('--prev-setup')
    p.add_argument('--items', default='1,2,3,4,5'); p.add_argument('--mac-build'); p.add_argument('--mp-dll')
    p.add_argument('--save-base', default=os.path.join(SAVES, 'base_25846389.json'))
    p.add_argument('--save-dungeon', default=os.path.join(SAVES, 'boss_golem_25846389.json'))
    a = p.parse_args()
    rep = Report(os.path.abspath(a.out))
    w = W.Win(); w.up()
    mac = Mac(a.mac_build)
    fns = {'1': lambda: item1(w, rep, a), '2': lambda: item2(w, rep, a), '3': lambda: item3(w, rep, a, mac),
           '4': lambda: item4(w, rep, a, mac), '5': lambda: item5(w, rep, a, mac), 'S': lambda: itemS(w, rep, a)}
    for k in a.items.split(','):
        t = time.time()
        log(f'---- item {k}')
        try:
            fns[k]()
        except Exception as e:                                         # one item failing does not stop the others
            rep.check(k, 'item ran to the end', False, f'{type(e).__name__}: {e}')
            traceback.print_exc()
            try:
                fresh_logs(w, rep, f'{k}_fail'); w.stop()
            except Exception:
                pass
        rep.items.setdefault(k, {'checks': [], 'secs': 0})['secs'] = time.time() - t
        rep.save()
    s = rep.save()
    log('RESULT', 'PASS' if s['pass'] else 'FAIL', f"{s['minutes']} min", os.path.join(rep.out, 'RESULT.txt'))


if __name__ == '__main__':
    main()
