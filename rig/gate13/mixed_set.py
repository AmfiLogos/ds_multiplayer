#!/usr/bin/env python3
"""gate13 v13 gate: a mixed gameplay mod set is refused at join, and the refusal names the mod. Mac, two LOADER test
copies (dsload + the shipped modules of ~/ds_launcher/loader/build/mods_all/mac), hidden + muted, one rig pair lease.

Each copy starts the way the Mac app stub starts the game (app/stub/ds_modded.c): DSML_DATA = the copy's data folder,
DSML_SESSION = <data>/Mods/worlds.json (the loader writes modset_lines + modset there; the multiplayer HELLO sends them),
DSL_MODS_DIR = a snapshot of the shipped mods for this run, DSL_PROFILE = this copy's profile.

  host U : profile multiplayer + custom-modes + gcd-cdr. Multiplayer > PvP > Start -> lobby.
  guest W: round 1 profile multiplayer + custom-modes (no gcd-cdr) -> joins by address -> must be refused (checks 1, 2).
           round 2 (control) profile multiplayer + custom-modes + gcd-cdr -> joins -> accepted, lockstep starts (check 3).
  check 4: owner prefs unchanged, no copy left running, no copy on screen.

  python3 ~/ds_mp_gate13/rig/gate13/mixed_set.py [--host U] [--guest W]
Prints "PASS name ..." / "FAIL name ...", last line "RESULT x/y pass". Out: ext/gate13/out/mixed/mac_<time>/."""
import json, os, shutil, signal, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mu_lcopy as M
import lrig

A = sys.argv
arg = lambda k, d: A[A.index(k) + 1] if k in A else d
HX, GX = arg('--host', 'U'), arg('--guest', 'W')
LANE = f'gate13_mx-{os.getpid()}'
WT = M.WT
OUT = os.path.join(WT, 'ext', 'gate13', 'out', 'mixed', 'mac_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
SHIPPED = os.path.expanduser('~/ds_launcher/loader/build/mods_all/mac')
BTN = json.loads(os.popen(f'python3 {WT}/ext/fepvp13/tools/mpm_ids.py {WT}').read())
LOGF = open(os.path.join(OUT, 'test.log'), 'a'); results = []
NAMES = ('gcd-cdr', 'Global cooldown')


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOGF.write(s + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok)))
    s = ('PASS ' if ok else 'FAIL ') + name + (f'  [{str(info)[:600]}]' if info != '' else '')
    print(s, flush=True); LOGF.write(time.strftime('%H:%M:%S ') + s + '\n'); LOGF.flush()


def wait(what, pred, timeout=600, every=2):
    t1 = time.time()
    while time.time() - t1 < timeout:
        try:
            v = pred()
        except (ConnectionError, OSError, json.JSONDecodeError) as e:
            v = None; log('  (wait', what, 'retry:', repr(e)[:80], ')')
        if v: return v
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def snapshot_mods():
    """the shipped mods, copied once for this run (the gate may rebuild build/ meanwhile); sha of every file"""
    d = os.path.join(OUT, 'mods'); shas = {}
    for m in ('multiplayer', 'custom-modes', 'gcd-cdr'):
        shutil.copytree(os.path.join(SHIPPED, m), os.path.join(d, m))
        for root, _, fs in os.walk(os.path.join(d, m)):
            for f in fs:
                if f.endswith(('.dylib', '.json')): shas[os.path.relpath(os.path.join(root, f), d)] = M.sha(os.path.join(root, f))[:12]
    shutil.copyfile(M.DSLOAD, os.path.join(OUT, 'dsload.dylib'))
    shas['dsload.dylib'] = M.sha(M.DSLOAD)[:12]
    return d, shas


class LCopy(M.Copy):
    """mu_lcopy.Copy with the app stub's environment and its own mods folder / profile (no units-driver)"""
    def __init__(self, x, mods_dir, ids):
        self.ids = ids
        self.data = os.path.join(M.r.idir(x), 'data')
        super().__init__(x, OUT, {}, env={})
        self.mods_dir = mods_dir

    def install(self):
        if self.pids(): raise SystemExit(f'copy {self.x} is running (another lane?): not installing over it')
        M.put(os.path.join(OUT, 'dsload.dylib'), os.path.join(self.APP, 'Contents/Frameworks/dsload.dylib'))
        M.sh(f'codesign -f -s - "{self.APP}/Contents/MacOS/Dungeon Settlers"')

    def begin(self, ids, tag):
        """fresh mod-set state: worlds.json / mp.json of a former run moved to old/, then start"""
        self.ids = ids; self.MODS = ids
        old = os.path.join(self.I, 'old', 'mixed_' + time.strftime('%Y%m%d-%H%M%S') + '_' + tag); os.makedirs(old, exist_ok=True)
        for p in (os.path.join(self.data, 'Mods', 'worlds.json'), os.path.join(self.I, 'mp.json'), os.path.join(self.I, 'rig.log')):
            if os.path.exists(p): os.replace(p, os.path.join(old, os.path.basename(p)))
        os.makedirs(os.path.join(self.data, 'Mods'), exist_ok=True)
        self.extra = {'DSML_DATA': self.data, 'DSML_SESSION': os.path.join(self.data, 'Mods', 'worlds.json'),
                      'DSL_MODS_DIR': self.mods_dir, 'DSMP_CLIP_PB': 'dsmp-gate13-mx-test'}
        self.violations = []; self.stop_guard.clear(); self.p = None
        self.start(); self.tag = tag

    def stop_keep(self):
        self.stop()
        for f in ('player.log', 'dsload.log', 'rig.log'):
            src = os.path.join(OUT, f'{self.x}_{f}')
            if os.path.exists(src): os.replace(src, os.path.join(OUT, f'{self.x}_{self.tag}_{f}'))
        w = os.path.join(self.data, 'Mods', 'worlds.json')
        if os.path.exists(w): shutil.copy2(w, os.path.join(OUT, f'{self.x}_{self.tag}_worlds.json'))

    def logs(self):
        t = ''
        for f in ('dsload.log', 'rig.log'):
            p = os.path.join(self.I, f)
            if os.path.exists(p): t += open(p, errors='replace').read()
        return t


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    prefs0 = lrig.check_prefs(restore=False)
    log('owner prefs identical to the backup before:', prefs0)
    mods_dir, shas = snapshot_mods(); log('mods snapshot', mods_dir, shas)
    H = LCopy(HX, mods_dir, []); G = LCopy(GX, mods_dir, [])
    for c in (H, G): c.install()
    pair = M.lease(LANE, os.getpid()); log('lease', pair, '| host', HX, 'guest', GX, '| out', OUT)
    all_ = (H, G)
    try:
        run(H, G)
    except Exception as e:
        import traceback; log('ERROR', repr(e)); log(traceback.format_exc()); check('test ran to the end without an error', False, repr(e))
    finally:
        for c in all_:
            if c.p: c.stop_keep()
        log('released', M.release(pair, LANE))
        left = [x for x in (HX, GX) if subprocess.run(['pgrep', '-f', os.path.join(M.r.idir(x), M.r.EXE_REL)], capture_output=True).returncode == 0]
        p1 = lrig.check_prefs(restore=False)
        viol = H.violations + G.violations + getattr(G, 'old_viol', [])
        check('4 owner prefs unchanged, no copy left running, nothing on screen', p1 == prefs0 and p1 and not left and not viol,
              f'prefs identical before {prefs0} after {p1}; running {left}; guard {viol}')
        if M.sha(M.DSLOAD)[:12] != shas['dsload.dylib'] or any(M.sha(os.path.join(SHIPPED, k))[:12] != v for k, v in shas.items() if k != 'dsload.dylib' and os.path.exists(os.path.join(SHIPPED, k))):
            log('NOTE: the loader build changed during the run (this run used the snapshot taken at the start); rerun for the new build')
        n = sum(1 for _, ok in results if ok)
        print(f'RESULT {n}/{len(results)} pass', flush=True); LOGF.write(f'RESULT {n}/{len(results)} pass | out {OUT}\n')


def run(H, G):
    st = lambda c: c.rig('status')
    ms = lambda c: c.rig('mpm_status')
    press = lambda c, n: (c.rig('mpm_press', button=BTN[n]), time.sleep(1.2))[0]
    title = lambda c: (lambda s: s.get('scene') == 1 and not s.get('busy'))(st(c))

    def setup_line(c):
        L = c.logs()
        rs = [l for l in L.splitlines() if 'reg: running gameplay set' in l or 'launcher session:' in l or 'world check:' in l]
        return rs[-4:]

    # ---- host: multiplayer + custom-modes + gcd-cdr, PvP lobby
    H.begin(['multiplayer', 'custom-modes', 'gcd-cdr'], 'host')
    wait('host title', lambda: title(H), timeout=900)
    time.sleep(6)
    press(H, 'MULTIPLAYER'); press(H, 'HOST_C1'); time.sleep(1); press(H, 'HOST_START')
    wait('host world + lobby', lambda: (lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'))(st(H))
         and ms(H).get('state') == 'lobby', timeout=900)
    port = ms(H).get('port'); log('host port', port, '| host setup', setup_line(H))
    hw = json.load(open(os.path.join(H.data, 'Mods', 'worlds.json')))
    log('host worlds.json modset_lines', hw.get('modset_lines'), 'modset', hw.get('modset'))
    host_ok = any(l.startswith('gcd-cdr@') for l in hw.get('modset_lines', [])) and hw.get('modset')

    def join(tag, ids):
        G.begin(ids, tag)
        wait(f'guest title ({tag})', lambda: title(G), timeout=900)
        time.sleep(6)
        gw = json.load(open(os.path.join(G.data, 'Mods', 'worlds.json')))
        log(tag, 'guest worlds.json modset_lines', gw.get('modset_lines'), 'modset', gw.get('modset'))
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR'); G.rig('mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        t0 = time.time(); seen = {}; joined = False
        while time.time() - t0 < 120:
            s = st(G); m = ms(G)
            joined = bool(s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1) or bool(s.get('mp', {}).get('started'))
            for k in ('msg', 'notice', 'notice_pending', 'message'):
                if m.get(k): seen[k] = m[k]
            nb = m.get('notice_box') or {}
            if nb.get('text'): seen['notice_box'] = nb['text'] + f" (active {nb.get('active')})"
            if m.get('box', {}).get('text'): seen['box'] = m['box']['text']
            if joined: break
            if seen.get('msg', '').startswith('Could not join') and time.time() - t0 > 20: break
            time.sleep(2)
        hs = st(H)
        log(tag, 'guest joined', joined, '| guest state', ms(G).get('state'), '| guest mp', st(G).get('mp'), '| texts', seen)
        log(tag, 'host players', hs.get('mp', {}).get('players'), '| guest setup', setup_line(G))
        return joined, seen, gw

    # ---- round 1: guest without gcd-cdr
    joined, seen, gw = join('mixed', ['multiplayer', 'custom-modes'])
    time.sleep(2)
    glog = G.logs(); hlog = H.logs()
    ref_g = [l for l in glog.splitlines() if 'modset refusal' in l or 'reject reason' in l]
    ref_h = [l for l in hlog.splitlines() if 'modset' in l.lower() and ('refus' in l.lower() or 'cannot join' in l.lower() or 'reject' in l.lower())]
    log('guest refusal log lines', ref_g[-3:]); log('host refusal log lines', ref_h[-3:])
    guest_ok = gw.get('modset_lines') == [l for l in hw.get('modset_lines', []) if not l.startswith('gcd-cdr@')] and gw.get('modset') != hw.get('modset')
    if not (host_ok and guest_ok):
        log('SETUP NOT RIGHT: host lines', hw.get('modset_lines'), 'guest lines', gw.get('modset_lines'))
    check('1 mixed set: the join is refused (guest never enters the host world / lockstep)',
          host_ok and guest_ok and not joined and any('reject reason 8' in l for l in ref_g),
          f"reject reason 8 = DSNET_REJ_MODSET; setup host gcd-cdr line {bool(host_ok)}, guest set without it {guest_ok}; joined {joined}; guest msg {seen.get('msg')!r}; guest log {ref_g[-1:] }")
    shown = ' | '.join(str(v) for v in seen.values()) + ' | ' + ' | '.join(ref_g)
    named = [n for n in NAMES if n in shown]
    why = ''
    if not named and seen.get('msg', '').endswith('did not start. Try again.'):
        why = ('PRODUCT BUG: the server refused with DSNET_REJ_MODSET, but the guest page shows the generic text: mpmenu.c '
               'join_refused / modset_notice find mp_game_last_reject / mp_game_modset_text with dlsym(RTLD_DEFAULT), and dsload '
               'opens modules RTLD_NOW|RTLD_LOCAL (loader/start/mac/dsl_start_mac.c:51), so both are NULL; ')
    check('2 the refusal text the guest sees names the mod (gcd-cdr / Global cooldown)', bool(named) and not joined,
          f'{why}names found {named}; notice {seen.get("notice") or seen.get("notice_pending")!r}; notice box {seen.get("notice_box")!r}; log {ref_g[-1:]}')
    G.old_viol = list(G.violations)
    G.stop_keep()
    time.sleep(3)
    # ---- round 2 (control): guest with gcd-cdr too
    joined2, seen2, gw2 = join('same', ['multiplayer', 'custom-modes', 'gcd-cdr'])
    if joined2:
        wait('guest lockstep running', lambda: (lambda a: a.get('det', {}).get('locked') and a.get('tick', 0) > 0)(st(G)), timeout=300)
    tk = (st(H).get('tick'), st(G).get('tick'))
    check('3 control: gcd-cdr on both -> join accepted, lockstep starts',
          joined2 and gw2.get('modset') == hw.get('modset') and not seen2.get('msg', '').startswith('Could not join'),
          f"joined {joined2}; same modset {gw2.get('modset') == hw.get('modset')}; ticks host/guest {tk}; guest mp {st(G).get('mp')}; texts {seen2}")


if __name__ == '__main__':
    main()
