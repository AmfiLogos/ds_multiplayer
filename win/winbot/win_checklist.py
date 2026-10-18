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
  2  ZET-U on (max units 8): the 8-unit checks of the loader's Mac test loader/test/zetu_ui_test.py (party cap, portraits,
     supplies panels, low-units risk, 8 in the dungeon, Inventory / Status / Skill with 8, result screen; dsload.log lines
     + the test module zetu-probe, built by zetu_probe/build_probe.sh) and the Windows load hang of v0.1.15; shots
  3  German + Russian shots: title (saves notice, kit notice), Mods window, a mod's Settings page, Load list,
     "Get the host's mods" box
  4  "Get the host's mods": plan, Get, Apply, soft restart, the restarted game rejoins, world arrives, 0 desyncs
  5  cross-play with fights: 3 dungeon entries in the same processes, lend + take back, quit to title; 0 crashes, 0 desyncs
  P  (v0.1.15 security fix) without DSRIG_PORT nothing listens on 47811; with it the tool connects
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
LANG = {'en': 0, 'de': 3, 'ru': 4}
TEST_ENV = {'DSL_SOFT_TEST': '1'}                                    # the loader's test request files (warn_load ...), test starts only
GAME_SAVE_NAME = 'AshHunter__AutoSave_c88ac7e819f54532914b71c9929c4ba8_0.json'
SEED_7 = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
PROBE = os.path.join(HERE, 'zetu_probe', 'build', 'zetu-probe')                                   # GeneralSettingData.SaveCurrentLanguageType (ds_i18n.h order)


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
        self.env = dict(os.environ, DSRIG_CTL='1')                     # v0.1.15: the Mac control socket opens only with it
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
        if os.path.exists(os.path.join(m, 'worlds.json')):           # a fresh session: no remembered world mod sets (moved, never deleted)
            old = os.path.join(os.path.dirname(MAC_CTL), 'old'); os.makedirs(old, exist_ok=True)
            os.replace(os.path.join(m, 'worlds.json'), os.path.join(old, 'worlds_%s.json' % time.strftime('%Y%m%d-%H%M%S')))
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
        if not r.get('port'):
            raise W.GameError(f'Mac copy W did not host {os.path.basename(save)}: {str(r)[-300:]}')
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
    """load_save; the loader's game-save warning (dsl_saves.c, answered Load through the DSL_SOFT_TEST request file)
    and a stock box shown during the load are recorded and confirmed. Returns (status, warnings + box texts)"""
    seen = []
    mark = w.mark('dsload.log')
    w.cmd(cmd='load_save', file=w.expand(guest_save), det=0)
    t = time.time()
    while time.time() - t < timeout:
        new = w.read(W.PROF + r'\dsload.log', offset=mark)[0].decode('utf-8', 'replace')
        for l in new.splitlines():
            if 'saves: warning shown' in l and l not in seen:
                seen.append(l); time.sleep(1.5)
                w.write(W.PROF + r'\softrestart\reg_request.json', json.dumps({'cmd': 'warn_load'}))
        b = (w.mpm().get('box') or {})
        if b.get('on'):
            seen.append('box: ' + b.get('text', '')); w.box('confirm'); time.sleep(0.5); continue
        s = w.loaded()
        if s and s.get('tick', -1) > 0:
            return s, seen
        time.sleep(0.5)
    raise W.GameError(f'save did not load within {timeout} s (seen {seen})')


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
    for kind, setup in ((('update', a.setup),) if a.update_only else (('fresh', a.setup), ('update', a.setup))):
        if kind == 'update' and a.update_only:
            pass                                                     # the new Setup over the installed release
        elif kind == 'fresh':
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
        w.start(env=TEST_ENV)
        if kind == 'fresh':                                          # item 6: the question on the first start
            b = w.wait(lambda: (w.mpm().get('box') or {}).get('on') and w.mpm()['box'], 'the crash-report question', 30)
            rep.check(I, 'crash-report question shown on the first start', 'crash' in b.get('text', '').lower(), b.get('text'))
            w.box('cancel')
            c = w.wait(lambda: (lambda j: j if j and j.get('consent') != 'unasked' else None)(w.json_file(W.ROOT + r'\crash\consent.json')), 'consent.json answered', 15)
            rep.check(I, 'No stored (consent.json)', c.get('consent') == 'no', c)
        ms = mods_state(w)
        z = ms.get('mod:' + ZETU, {})
        rep.check(I, f'{kind}: Mods window shows {ZETU} OFF', z and not z.get('on'), z)
        sp = W.WORK + r'\\fake_gamesaves\\' + GAME_SAVE_NAME          # a game-folder save (DSML_GAME_SAVES): the save warning path
        w.put(a.save_base, sp)
        s, boxes = load_with_boxes(w, sp)
        rep.check(I, f'{kind}: normal save loads with no ZET-U warning', not any('zet' in b.lower() for b in boxes), boxes or 'no warning')
        scan_logs(w, rep, I)
        fresh_logs(w, rep, f'1_{kind}')
        if kind == 'fresh':                                          # item 6: not asked again
            w.start()
            time.sleep(3)
            b = w.mpm().get('box') or {}
            rep.check(I, 'crash-report question not asked on the next start', not (b.get('on') and 'crash' in b.get('text', '').lower()), b)
        w.stop()


def probe(w, line, limit=20):
    """zetu-probe request (the loader's test module, built by zetu_probe/build_probe.sh): the answer lines in dsload.log"""
    done = W.PROF + r'\zetu_probe_req.txt.done'
    if w.exists(done):                  # the probe renames the request to .done; on Windows rename() does not replace, so the
        w.move(done, W.WORK + r'\to_delete\probe_' + f'{time.time():.3f}.txt')   # request would run again every 10 frames
    mark = w.mark('dsload.log')
    w.write(W.PROF + r'\zetu_probe_req.txt', line + '\n')
    try:
        w.wait_log('dsload.log', ['zetu-probe: '], limit, since=mark)
    except W.GameError:
        return []
    time.sleep(0.5)
    return w.read(W.PROF + r'\dsload.log', offset=mark)[0].decode('utf-8', 'replace').splitlines()   # all new lines (risk rows have no prefix)


def item2(w, rep, a):
    """ZET-U max units 8 on Windows: the checks of the loader's Mac test loader/test/zetu_ui_test.py (run), through
    the control port, the loader's dsload.log lines and zetu-probe; shots = the game frame for each window"""
    I = '2'
    pm = W.ROOT + r'\mods\zetu-probe'
    w.kill_all()
    for f in ('zetu-probe.dll', 'mod.json'):
        w.put(os.path.join(PROBE, f), pm + '\\' + f)
    try:
        _item2(w, rep, a, I)
    finally:
        w.stop()
        w.move(pm, W.WORK + r'\to_delete\zetu-probe_' + time.strftime('%Y%m%d-%H%M%S'))   # test module out again (moved)


def _item2(w, rep, a, I):
    import re
    en = ['custom-modes', 'gcd-cdr', ZETU, 'zetu-probe', 'multiplayer']
    w.prepare(lang=0, active=en, settings={ZETU: {'max_units': 8}})
    w.start(env=TEST_ENV)
    L = w.text(W.PROF + r'\dsload.log') or ''
    rep.check(I, 'setting read: max units 8', 'max units in the dungeon 8' in L, [l for l in L.splitlines() if 'zetu' in l][-3:])
    rep.check(I, 'zetu UI ready', ZETU in L and 'expedition UI' in L)
    rep.check(I, 'probe ready', 'zetu-probe: ready' in L)
    sp = put_save(w, SEED_7, os.path.basename(SEED_7))
    t = time.time()
    s, seen = load_with_boxes(w, sp, 400)
    rep.check(I, 'ZET-U on: the save loads (no NOW LOADING hang)', True, f'{time.time() - t:.0f} s, map {s.get("map")}')
    sh = lambda n: w.gshot(os.path.join(rep.out, 'shots', f'2_{n}.png'))
    tm = lambda **k: w.cmd(cmd='ui_teams', **k)
    units = lambda m: [x for x in (w.cmd(cmd='list_units', map=m).get('units') or []) if x.get('faction') == 1 and not x.get('dead')]
    dl = lambda m: w.read(W.PROF + r'\dsload.log', offset=m)[0].decode('utf-8', 'replace')
    def wait_map(m, limit):
        return w.wait(lambda: (lambda s: s if s.get('map') == m and not s.get('busy') else None)(w.status()), f'map {m}', limit, 1)
    def risks():
        return [l.split('risk row ', 1)[1] for l in probe(w, 'risks') if 'risk row ' in l]
    LOW = 'Insufficient units selected'
    w.cmd(cmd='set_speed', speed=1)
    if units(1):                                                     # the seed save: 4 in the dungeon, 3 at the base
        probe(w, 'result'); time.sleep(4)
        d = units(1)
        w.cmd(cmd='trn_changemap', **{'from': 1, 'to': 0, 'guids': ','.join(u['guid'] for u in d)})
        wait_map(0, 120); time.sleep(3)
    base = units(0)
    for _ in range(4):                                               # recruit until 8 (the game's own recruit event)
        if len(base) >= 8:
            break
        probe(w, 'recruit')
        try:
            w.wait(lambda: len(units(0)) > len(base), 'a recruit', 15)
        except W.GameError:
            pass
        base = units(0)
    rep.check(I, '8 player units at the base', len(base) >= 8, len(base))
    g8 = [u['guid'] for u in base[:8]]
    tm(action='go'); w.wait(lambda: (tm(action='state').get('stock') or {}).get('open'), 'expedition window', 30)
    tm(action='stock_select', units=','.join(g8[:4])); time.sleep(1)
    tm(action='stock_next'); w.wait(lambda: (tm(action='state').get('stock') or {}).get('state') == 1, 'supplies step', 30)
    rk = risks()
    rep.check(I, '7: 4 of max 8: the low unit count risk shows (4 / 8)', any(LOW in x and '4' in x and '8' in x for x in rk), rk)
    tm(action='stock_close'); time.sleep(2)
    m0 = w.mark('dsload.log')
    tm(action='go'); w.wait(lambda: (tm(action='state').get('stock') or {}).get('open'), 'expedition window', 30)
    tm(action='stock_select', units=','.join(g8)); time.sleep(2)
    L = dl(0)                                                        # whole log, like the Mac test: some lines come once per build of a view
    sel = re.findall(r'zetu-suite.*expedition: (\d+) selected', L)
    sh('expedition_8_select')
    lines = [l for l in L.splitlines() if 'zetu-suite' in l and ('portraits:' in l or 'count text' in l)][-3:]
    rep.check(I, '1: 8 units selected in the expedition window', sel and sel[-1] == '8', sel[-3:])
    rep.check(I, '3: portraits 8 in one row, no overlap', re.search(r'portraits: 8 \(4 stock \+ 4 copies\).*, no overlap', L), lines)
    rep.check(I, '3: count text says (8/8)', re.search(r"count text now '[^']*\(8/8\)'", L), lines)
    m0 = w.mark('dsload.log')
    tm(action='stock_next'); w.wait(lambda: (tm(action='state').get('stock') or {}).get('state') == 1, 'supplies step', 30); time.sleep(1)
    L = dl(0); sh('expedition_8_supplies')
    st = tm(action='state').get('stock') or {}
    rep.check(I, '2: supplies step: 8 panels in one row, no overlap', re.search(r'supplies: 8 unit panels .*, no overlap', L),
              [l for l in L.splitlines() if 'supplies' in l][-2:])
    rep.check(I, '2: each of the 8 units has its own panel', len({p for p in (st.get('panels') or []) if p}) == 8, st.get('panels'))
    rk = risks()
    rep.check(I, '7: 8 of max 8: no low unit count risk', not any(LOW in x for x in rk), rk)
    tm(action='stock_accept')
    wait_map(1, 180); time.sleep(5)
    rep.check(I, '4: 8 units in the dungeon', len(units(1)) == 8, len(units(1)))
    p0 = w.a('stat', path=W.PROF + r'\player.log').get('size', 0)
    probe(w, 'select_all'); time.sleep(2); sh('dungeon_8_units')
    for t, n, view in ((8, 'inventory', 'EntityInventoryUI'), (6, 'status', 'EntityStatusUI'), (7, 'skill', 'EntitySkillUI')):
        m0 = w.mark('dsload.log')
        probe(w, f'toggle {t}')
        try:
            w.wait_log('dsload.log', [view + ':'], 10, since=m0)
        except W.GameError:
            pass
        time.sleep(1.5); sh(f'dungeon_8_{n}')
        L = dl(0)
        fit = re.findall(view + r': (\d+) units shown, row scale ([\d.]+), x (-?\d+)\.\.(-?\d+) of (\d+) px', L)
        rep.check(I, f'5: {n} window: 8 panels (4 stock + 4 copies)', re.search(view + r': 8 panels \(4 stock \+ 4 copies\)', L), [l for l in L.splitlines() if view in l][-2:])
        rep.check(I, f'5: {n} window: the 8 units inside the screen', fit and fit[-1][0] == '8' and int(fit[-1][2]) >= 0 and int(fit[-1][3]) <= int(fit[-1][4]), fit[-1:])
        probe(w, f'toggle {t}'); time.sleep(1)
    P = w.read(W.PROF + r'\player.log', offset=p0)[0].decode('utf-8', 'replace')
    exc = [l for l in P.splitlines() if 'Exception' in l]
    rep.check(I, '5: Inventory / Status / Skill with 8 selected: no exception', not exc, exc[:3])
    m0 = w.mark('dsload.log')
    probe(w, 'result'); time.sleep(4); sh('results_8')
    L = dl(m0)
    rep.check(I, '6: result screen: 8 portraits, no overlap', re.search(r'result: 8 portraits \(4 stock \+ 4 copies\).*, no overlap', L)
              and re.search(r'result: 8 units \+ 0 dead, 8 portraits shown', L), [l for l in L.splitlines() if 'result' in l][-3:])
    scan_logs(w, rep, I)
    fresh_logs(w, rep, '2')


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
        w.cmd(cmd='mpm_list', close=1); time.sleep(0.5)
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
    # the host's save must have no load popup: the wiki Template save shows the stock "Lock Consecration" Yes/No popup on
    # load, and a host with that popup open never sends the join save (known join deadlock, fixed only in branch popupfix)
    work = os.path.join(os.path.dirname(MAC_CTL), 'save_in_host.json')
    import shutil; shutil.copy2(a.save_host, work)
    mac.host(work)
    return mac.port()


def item4(w, rep, a, mac):
    if a.mp_dll:                                                     # lead 2026-10-18: the winbot dll for item 4 only
        with test_mp_dll(w, a):
            return item4_run(w, rep, a, mac)
    return item4_run(w, rep, a, mac)


def item4_run(w, rep, a, mac):
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
        newp = [p['pid'] for p in w.procs() if p['pid'] != old]
        if newp:                                                     # a build without the winbot port fix: B never gets the port
            port_ok = False
            w.pid = newp[0]
            log('no control port in the restarted game (build without the port fix): host view + logs')
        else:
            rep.check(I, 'soft restart', False, e)
            return
    act = w.json_file(W.DATA + r'\Mods\active.json') or {}
    st = w.json_file(W.DATA + r'\Mods\settings.json') or {}
    rep.check(I, "host's mods taken", ZETU in act.get('enabled', []), act)
    rep.check(I, "host's setting taken", (st.get(ZETU) or {}).get('max_units') == 7, st)
    try:
        if port_ok:                                                  # the v2 lobby holds the world until the host's Start
            w.wait(lambda: (mac.answer_boxes(), w.mpm().get('state') in ('guest_lobby', 'guest_loading', 'playing'))[1], 'the restarted game in the host lobby', 120, 1)
            rep.check(I, 'the restarted game rejoined by itself (in the host lobby)', True, f'{time.time() - t:.0f} s after Get')
            mac.cmd(cmd='mpm_press', button=W.B['START'])
            s = w.wait(lambda: (mac.answer_boxes(), w.in_mp_game())[1], "the host's world arrived after Start", 240, 1)
            m = s.get('mp') or {}
            rep.check(I, 'rejoined, world loaded', True, f"{time.time() - t:.0f} s after Get; {w.cmd(cmd='mpm_hostmods')}")
            rep.check(I, '0 desyncs', not m.get('desyncs') and not m.get('desynced'), m.get('desyncs'))
        else:                                                        # a build without the port fix: the host's view + B's log
            w.wait(lambda: 1 in mac.peers(), 'the restarted guest in the host lobby', 120, 1)
            mac.cmd(cmd='mpm_press', button=W.B['START'])
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
    w.join(f'{W.hub_host()}:{mac.port()}', wait=None)
    w.wait(lambda: (mac.answer_boxes(), w.mpm().get('state') in ('guest_lobby', 'guest_loading', 'playing'))[1], 'the VM in the host lobby', 300, 1)
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

    used = set()
    w.wait(lambda: (mac.answer_boxes(), land('mac') and land('vm'))[1], 'both in the dungeon (entry 1)', 600, 1)
    alive('entry 1')
    for cy in range(1, 4):
        if cy > 1:
            # a fresh unit each dive, as ext/fogfix/fogfix_pair.py: the unit back from the last dive is not offered
            # again at once ("Choose at least one unit"); a free one is taken through Teams (left, then right click)
            free = next((col['model'] for col in tm('mac').get('columns', []) if col.get('player') == -1), [])
            f1 = next((g for g in free if g not in used), None)
            if not f1:
                raise W.GameError(f'no free unit left for entry {cy}')
            used.add(f1)
            tm('mac', action='open'); tm('vm', action='open'); time.sleep(2)
            tm('mac', action='click', guid=f1, button=0); time.sleep(1.5); tm('mac', action='click', guid=f1, button=1)
            mine = lambda side: any(col.get('player') == 0 and f1 in col.get('model', []) for col in tm(side).get('columns', []))
            w.wait(lambda: mine('mac') and mine('vm'), f'host takes a fresh unit ({cy})', 60, 1)
            tm('mac', action='close'); tm('vm', action='close'); time.sleep(1)
            for k in range(3):                                         # as lane vmwin2's xplay.py: up to 3 proposals
                tm('mac', action='open'); time.sleep(1); tm('mac', action='go')
                w.wait(lambda: tm('mac').get('stock', {}).get('open'), 'stock window', 60)
                tm('mac', action='stock_select', units=f1); time.sleep(1); tm('mac', action='stock_next')
                w.wait(lambda: tm('mac').get('stock', {}).get('state') == 1, 'stock step 2', 60)
                tm('mac', action='stock_accept')
                try:
                    w.wait(lambda: tm('vm').get('prop', {}).get('shown'), f'guest summary {cy}', 45); break
                except W.GameError:
                    log(f'no guest summary (entry {cy}, try {k + 1}): host teams', json.dumps(tm('mac'))[:300])
            else:
                raise W.GameError(f'timeout: guest summary {cy} (3 proposals)')
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
            w.mark('rig.log')
            r2 = c('mac', cmd='ui_lend', guid=g, action='take')
            w.wait(lambda: c('vm', cmd='ui_lend', guid=g).get('controller') != 1, 'the VM sees the unit taken back', 30)
            try:
                said = w.wait_log('rig.log', ['took back'], 15)                  # the borrower's notice ("Mac took back <unit>")
            except W.GameError as e:
                said = str(e)
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


import contextlib


@contextlib.contextmanager
def test_mp_dll(w, a):
    """the winbot multiplayer.dll in the modded test profile's mods folder for one item; the release files are moved
    aside and moved back after (never deleted). mod.json keeps its version; only the dll's sha256 changes."""
    import hashlib
    md = W.ROOT + r'\mods\multiplayer'
    keep = W.WORK + r'\to_delete\mp_release_' + time.strftime('%Y%m%d-%H%M%S')
    mj = w.json_file(md + r'\mod.json')
    w.kill_all()
    for f in ('multiplayer.dll', 'mod.json'):
        w.move(md + '\\' + f, keep + '\\' + f)
    try:
        mj['platforms']['win-x64'].setdefault('files', {})['multiplayer.dll'] = hashlib.sha256(open(a.mp_dll, 'rb').read()).hexdigest()
        w.upload(a.mp_dll, md + r'\multiplayer.dll'); w.write(md + r'\mod.json', json.dumps(mj, indent=1))
        yield
    finally:
        w.stop()
        for f in ('multiplayer.dll', 'mod.json'):
            w.move(md + '\\' + f, keep + r'\winbot_' + f)
            w.move(keep + '\\' + f, md + '\\' + f)
        log('release multiplayer.dll + mod.json back in place')


def itemS(w, rep, a):
    """the winbot port fix on the VM: Mods window Apply (single player) = soft restart; the new process must answer on
    the control port. Needs --mp-dll (a multiplayer.dll with the fix)."""
    I = 'S'
    if not a.mp_dll:
        rep.check(I, 'port fix proof', False, 'no --mp-dll given'); return
    with test_mp_dll(w, a):
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


HTTP_PROBE = r'''$c=New-Object Net.Sockets.TcpClient('127.0.0.1',%d); $s=$c.GetStream(); $s.ReadTimeout=5000
$b=[Text.Encoding]::ASCII.GetBytes("POST / HTTP/1.1`r`nHost: 127.0.0.1`r`n`r`n{""cmd"":""ping""}`n"); $s.Write($b,0,$b.Length)
$o=New-Object IO.MemoryStream; $buf=New-Object byte[] 4096; $closed='no'
try { while ($true) { $n=$s.Read($buf,0,4096); if ($n -le 0) { $closed='yes'; break }; $o.Write($buf,0,$n) } } catch { $closed='timeout' }
"closed=$closed"; [Text.Encoding]::UTF8.GetString($o.ToArray())'''


def http_probe(w, port):
    """one connection: an HTTP request line, headers, then a valid ping as the body. Must be refused and closed:
    a web page's POST to 127.0.0.1 would look like this"""
    w.write(W.WORK + r'\winbot\http_probe.ps1', HTTP_PROBE % port)
    return w.a('run', exe=r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
               args='-NoProfile -ExecutionPolicy Bypass -File ' + w.expand(W.WORK + r'\winbot\http_probe.ps1'), wait_ms=20000, capture=True)['out']


def itemP(w, rep, a):
    """the v0.1.15 security fix: the control channel opens only when DSRIG_PORT is set"""
    I = 'P'
    w.prepare(lang=0, active=BASE_MODS, settings={})
    w.kill_all()
    m = w.mark('dsload.log') if w.exists(W.PROF + r'\dsload.log') else 0
    # what the player starter sets (ds_modloader loader/start/win/ds_modded_win.c:686-701 at int014), and nothing else.
    # Two values differ, for the owner's rules: DSL_STEAM=0 (no Steam on the VM) and DSML_GAME_SAVES = a test folder
    # (never the owner's LocalLow / Steam Cloud saves). No DSRIG_CTL, no DSRIG_PORT, no token, no test variables.
    pe = {'SteamAppId': '2798330', 'SteamGameId': '2798330', 'DSRIG_DIR': W.PROF, 'DSRIG_LIVE': '1', 'DSRIG_FPS': '60',
          'DSL_MODS_DIR': W.ROOT + r'\mods', 'DSL_PROFILE': W.DATA + r'\Mods\active.json', 'DSL_LOG_DIR': W.PROF,
          'DSML_SESSION': W.DATA + r'\Mods\worlds.json', 'DSML_BUILD': '25846389', 'DSML_APP': W.GAME, 'DSML_DATA': W.DATA,
          'DSML_GAME_SAVES': W.WORK + r'\fake_gamesaves', 'DSL_STEAM': '0', 'DSML_ROOT': W.ROOT, 'DOORSTOP_DISABLE': '1'}
    env = [f'{k}={w.expand(v)}' for k, v in pe.items()]
    leak = [k for k in w._genv if k.startswith(('DSRIG', 'DSL_', 'DSML'))] if getattr(w, '_genv', None) else []
    rep.check(I, 'player start: environment used (the agent adds none of the test variables)', not leak, ' '.join(sorted(pe)) + (f' | agent env has {leak}' if leak else ''))
    r = w.a('run', exe=w.expand(W.GAME + r'\DungeonSettlers.exe'), args=w.expand(f'-logFile "{W.PROF}\\player.log"'),
            cwd=w.expand(W.GAME), env=env, show=True, wait_ms=0)
    w.pid = r['pid']; t = time.time()
    try:
        line = w.wait_log('dsload.log', ['fades its title in', 'ui: title font'], 180, since=m)   # a rotated (shorter) log is read from 0
        time.sleep(5)
        rep.check(I, 'without DSRIG_PORT: the game reaches the title', True, f'{time.time() - t:.0f} s: {line[:80]}')
        ns = w.a('run', exe=r'C:\Windows\System32\cmd.exe', args='/c netstat -ano -p tcp | findstr LISTENING', wait_ms=20000, capture=True)['out']
        lis = [l.strip() for l in ns.splitlines() if ':47811 ' in l]
        rep.check(I, 'without DSRIG_PORT: nothing listens on 47811', not lis, lis)
        g = W.hub_req({'op': 'game', 'line': '{"cmd":"ping"}', 'port': 47811, 'timeout_ms': 3000})
        rep.check(I, 'without DSRIG_PORT: the tool cannot connect', not g.get('ok'), g.get('err') or g.get('resp'))
        rl = w.text(W.PROF + r'\rig.log') or ''
        rep.check(I, 'without DSRIG_PORT: no "control port ... listening" in rig.log since the start',
                  'listening' not in rl[-20000:].split('---- rig start')[-1], [l for l in rl.splitlines() if 'control port' in l][-1:])
    finally:
        w.a('kill', pid=w.pid); w.kill_all()
    w.start()                                                        # winrig always sets DSRIG_PORT + DSRIG_CTL
    rep.check(I, 'with the test variables: the tool connects', (w.ping() or {}).get('pid') == w.pid, w.pid)
    out = http_probe(w, w.port)
    refused = 'closed=yes' in out and '"pong"' not in out
    if a.http_record:                                                # v0.1.15 has no HTTP refusal: record what happens
        rep.check(I, f'test mode, HTTP-looking connection (recorded): {"refused + closed" if refused else "NOT refused"}', True, out.strip()[-300:])
    else:
        rep.check(I, 'test mode: an HTTP-looking connection is refused and closed, its ping body not answered', refused, out.strip()[-300:])
    rep.check(I, 'the game still runs after the HTTP probe', w.alive() and (w.ping() or {}).get('pid') == w.pid)
    fresh_logs(w, rep, 'P')
    w.stop()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True); p.add_argument('--setup'); p.add_argument('--prev-setup')
    p.add_argument('--items', default='1,2,3,4,5'); p.add_argument('--mac-build'); p.add_argument('--mp-dll')
    p.add_argument('--save-base', default=os.path.join(SAVES, 'base_25846389.json'))
    p.add_argument('--save-dungeon', default=os.path.join(SAVES, 'boss_golem_25846389.json'))
    p.add_argument('--save-host', default=SEED_7)
    p.add_argument('--update-only', action='store_true')
    p.add_argument('--http-record', action='store_true')             # item P: record the HTTP case instead of failing on it             # item 1: only the new Setup over the installed one                    # Mac host for items 3 / 4 (vmwin2's passing rejoin: this save)
    a = p.parse_args()
    rep = Report(os.path.abspath(a.out))
    w = W.Win(); w.up()
    mac = Mac(a.mac_build)
    fns = {'1': lambda: item1(w, rep, a), '2': lambda: item2(w, rep, a), '3': lambda: item3(w, rep, a, mac),
           '4': lambda: item4(w, rep, a, mac), '5': lambda: item5(w, rep, a, mac), 'S': lambda: itemS(w, rep, a), 'P': lambda: itemP(w, rep, a)}
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
