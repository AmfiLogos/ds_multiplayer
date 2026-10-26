#!/usr/bin/env python3
"""sp_scan_test.py <base.dylib> <new.dylib> [--keys 30] [--popups 5] [--fps 120]

Multiplayer's view lookups (FindObjectOfType walks) in SINGLE PLAYER (no mp.json: no session), base and new side by
side on one leased pair: same save, same time, same load. Fails on what players feel: a key press or a stock popup that
lets the module walk the scene again and again (lane fpsmp, ext/fpsaudit/mp.md row 1: 15 walks per key press, 50 per
popup, in every world where the Esc menu was not opened yet), and a slower pickup of a view that just opened.

Each copy: the fpsaudit probe (ext/fpsaudit/mac/fpsprobe.c, inserted after the module; FPSPROBE_KEYS=1 answers MP's
Input.anyKeyDown with one fake key press per second while `keys N` runs), DSRIG_CUSTOM=0 (Units Laboratory's own scans
are lane fpsul's), 120 fps cap, batchmode, hidden. Phases: title idle; Load list; world (the rig save) idle; world with
one key press per second; world with a stock popup (type 10) opened and closed N times; then the Esc menu opened the way
the game opens it (PopUpViewContainer.ActivateView(1)): MP's Esc-menu clones must show up as fast as on base (the new
code searches for the Esc menu only while the popup container says it is open).
Counts from the rig `perf` command (view_lookups, poll_frames = most poll walks in one frame / frames with 2+, new only; actions such as a click's own lookup are not polls);
frame times from the probe's frames.bin (MainFlow.Update holds the module's frame work).
FAIL (new): more than 3.5 walks per key press, more than 10.5 per popup, any frame with 2+ poll walks, more walks at the idle
title than base, more than 6 scene searches (walks + sprite scans) in a steady single-player phase (the loader's
SCAN-RULE limit), Esc clones later than max(0.3 s, base + 0.1 s). Frame-time numbers are printed, not judged (noise).
"""
import datetime, json, os, shutil, statistics, struct, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig, rigports

HERE = os.path.dirname(os.path.abspath(__file__))
a = sys.argv[1:]
opt = lambda k, d=None: a[a.index(k) + 1] if k in a else d
BASE, NEW = os.path.abspath(a[0]), os.path.abspath(a[1])
KEYS = int(opt('--keys', 30)); POPUPS = int(opt('--popups', 5)); FPS = int(opt('--fps', 120))
PROBE = os.path.abspath(os.path.expanduser(opt('--probe', '~/ds_multiplayer/ext/fpsaudit/mac/fpsprobe_plain.dylib')))
OUT = os.path.join(HERE, 'out', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
LANE = 'fpsmp'
log = lambda *x: print(time.strftime('%H:%M:%S'), *x, flush=True)


def owner_playing():
    p = subprocess.run(['pgrep', '-f', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/|'
                        'steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents/MacOS/'], capture_output=True, text=True).stdout.split()
    return [x for x in p if x]


class Copy:
    def __init__(self, x, dylib, label):
        self.x, self.dylib, self.label = x, dylib, label
        self.pd = os.path.join(OUT, label); os.makedirs(self.pd)
        self.ph = {}                                        # phase -> {t0, t1, lookups, classes, frames}

    def cmd(self, line, timeout=15):                        # probe command (cmd.txt), answered at a frame start
        out = os.path.join(self.pd, 'cmd.out'); n0 = len(open(out).readlines()) if os.path.exists(out) else 0
        p = os.path.join(self.pd, 'cmd.txt'); open(p + '.tmp', 'w').write(line + '\n'); os.replace(p + '.tmp', p)
        t0 = time.time()
        while time.time() - t0 < timeout:
            if os.path.exists(out):
                ls = open(out).readlines()
                if len(ls) > n0:
                    f = ls[-1].split(); log(self.label, 'probe', line, '->', ls[-1].strip()); return float(f[0]) if f[1] == 'ok' else None
            time.sleep(0.1)
        log(self.label, 'probe', line, '-> no answer'); return None

    def perf(self, reset=False):
        return rig.send(self.x, {'cmd': 'perf', 'reset': 1} if reset else {'cmd': 'perf'})

    def begin(self, name):
        p = self.perf(reset=True); self.ph[name] = {'t0': time.time(), 'p0': p}

    def end(self, name):
        p = self.perf(); d = self.ph[name]; d['t1'] = time.time(); d['p1'] = p
        s0, s1 = d['p0']['mpm_scans'], p['mpm_scans']
        d['lookups'] = s1['view_lookups'][0] - s0['view_lookups'][0]
        c0 = {k: n for k, n, _ in d['p0']['view_lookups']}
        d['classes'] = {k: n - c0.get(k, 0) for k, n, _ in p['view_lookups'] if n - c0.get(k, 0)}
        d['poll_frames'] = s1.get('poll_frames')
        d['sprite_scans'] = s1['assets'][0] + s1['portrait'][0] - s0['assets'][0] - s0['portrait'][0]   # Resources.FindObjectsOfTypeAll
        d['frames'] = p['frames']

    def state(self):
        st = {}
        try:
            for l in open(os.path.join(self.pd, 'state.txt')):
                k, _, v = l.rstrip('\n').partition(' '); st[k] = v
        except OSError:
            pass
        return st


def frames(c):
    raw = open(os.path.join(c.pd, 'frames.bin'), 'rb').read()
    return [struct.unpack_from('<dffffIiqfI', raw, i * 48) for i in range(len(raw) // 48)]


def phase_frames(R, d):
    return [r for r in R if d['t0'] <= r[0] <= d['t1']]


def esc_clone_time(c, after, off):                          # rig.log "Esc-menu clones added" after the popup command
    day = datetime.date.today()                              # off: the log size before the command (the log keeps old runs)
    with open(os.path.join(rig.idir(c.x), 'rig.log'), 'rb') as f:
        f.seek(off); new = f.read().decode(errors='replace')
    for l in new.splitlines():
        if 'mpmenu: Esc-menu clones added' not in l: continue
        try: t = datetime.datetime.combine(day, datetime.datetime.strptime(l[:12], '%H:%M:%S.%f').time()).timestamp()
        except ValueError: continue
        if t >= after - 0.05: return t
    return None


pair = None; copies = []; res = []; fails = 0
def result(ok, what):
    global fails
    res.append(f"{'PASS' if ok else 'FAIL'} {what}"); fails += 0 if ok else 1; print(res[-1], flush=True)


try:
    if owner_playing(): raise SystemExit('the owner is playing: no test copies now')
    if not os.path.exists(PROBE): raise SystemExit(f'probe not found: {PROBE} (ext/fpsaudit/mac/fpsprobe.c, build line at its top)')
    pair = rig.lease(LANE)
    if not pair: raise SystemExit('no free pair (rig.py leases)')
    log('pair', pair)
    assert rig.check_prefs()
    copies = [Copy(pair[0], BASE, 'base'), Copy(pair[1], NEW, 'new')]
    work = {}
    for c in copies:
        mpj = os.path.join(rig.idir(c.x), 'mp.json')       # no mp.json = no auto host: single player
        if os.path.exists(mpj): os.makedirs(os.path.join(rig.idir(c.x), 'old'), exist_ok=True); os.replace(mpj, os.path.join(rig.idir(c.x), 'old', f'mp.json.fpsmp-{time.strftime("%H%M%S")}'))
        rig.EXTRA_ENV[c.x] = {'FPSPROBE_DIR': c.pd, 'FPSPROBE_KEYS': '1', 'DSRIG_CUSTOM': '0'}
        rig.DYLIB = c.dylib + ':' + PROBE
        rig.start(c.x, fps=FPS)
        w = os.path.join(rig.idir(c.x), 'save_in'); os.makedirs(w, exist_ok=True)
        work[c.x] = os.path.join(w, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, work[c.x])
    json.dump({'pair': pair, 'base': BASE.replace(os.path.expanduser('~'), '~'), 'new': NEW.replace(os.path.expanduser('~'), '~'),
               'keys': KEYS, 'popups': POPUPS, 'fps': FPS, 'load': os.getloadavg()[0]}, open(os.path.join(OUT, 'run.json'), 'w'), indent=1)
    for c in copies:
        rig.wait_for(c.x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    time.sleep(8)
    both = lambda f: [f(c) for c in copies]
    both(lambda c: c.begin('title')); time.sleep(30); both(lambda c: c.end('title'))
    both(lambda c: c.cmd('openload')); time.sleep(2)
    both(lambda c: c.begin('loadlist')); time.sleep(20); both(lambda c: c.end('loadlist'))
    for c in copies: rig.send(c.x, {'cmd': 'load_save', 'file': work[c.x], 'det': 0})
    time.sleep(3)
    for c in copies:
        rig.wait_for(c.x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'loaded', timeout=600, every=1)
    for c in copies: rig.send(c.x, {'cmd': 'set_speed', 'speed': 1})
    time.sleep(10)
    for c in copies: rigports.players_guard(rig.send, [c.x], 1, 'world')
    both(lambda c: c.begin('world_idle')); time.sleep(30); both(lambda c: c.end('world_idle'))
    both(lambda c: c.begin('world_keys')); both(lambda c: c.cmd(f'keys {KEYS}')); time.sleep(KEYS + 2); both(lambda c: c.end('world_keys'))
    both(lambda c: c.begin('world_popups'))
    for i in range(POPUPS):
        both(lambda c: c.cmd('popup 10')); time.sleep(1.5); both(lambda c: c.cmd('unpopup 10')); both(lambda c: c.cmd('speed 1')); time.sleep(1.5)
    both(lambda c: c.end('world_popups'))
    off = {c.label: os.path.getsize(os.path.join(rig.idir(c.x), 'rig.log')) for c in copies}
    # the Esc menu as the game opens it: PopUpViewContainer.ActivateView(SystemMenu 1) (the owner's logs: "popup 1 requested,
    # caller ViewObjectLoader.Update" after Escape). A hidden copy gets no real key (background app, no key window).
    t_esc = {c.label: c.cmd('popup 1') for c in copies}; time.sleep(3)
    tc = {c.label: esc_clone_time(c, t_esc[c.label], off[c.label]) if t_esc[c.label] else None for c in copies}
    esc = {k: (tc[k] - t_esc[k]) if tc[k] else None for k in tc}
    both(lambda c: c.cmd('unpopup 1'))
    for c in copies: rigports.players_guard(rig.send, [c.x], 1, 'end')

    # ---- numbers
    lines = []
    R = {c.label: frames(c) for c in copies}
    for c in copies:
        st = c.state(); lines.append(f"{c.label}: fake key presses answered {st.get('ak_true')} of {st.get('ak_calls')} anyKeyDown calls")
        for name, d in c.ph.items():
            secs = d['t1'] - d['t0']; F = phase_frames(R[c.label], d)
            mf2 = sum(1 for r in F if r[2] > 2.0); mfmax = max((r[2] for r in F), default=0)
            dts = sorted(r[1] for r in F); p99 = dts[int(len(dts) * 0.99)] if dts else 0
            d['per_min'] = d['lookups'] * 60 / secs
            lines.append(f"{c.label:4s} {name:12s} {secs:5.1f} s frames {len(F):5d} | walks {d['lookups']:4d} ({d['per_min']:6.1f}/min) sprite scans {d['sprite_scans']} "
                         f"{d['classes']} | poll_frames {d['poll_frames']} | MainFlow.Update > 2 ms {mf2 * 60 / secs:6.1f}/min, max {mfmax:5.1f} ms | dt p99 {p99:5.2f} ms")
        idle, keys = c.ph['world_idle'], c.ph['world_keys']
        kt = []
        try: kt = [float(l.split()[0]) for l in open(os.path.join(c.pd, 'keys.txt'))]
        except OSError: pass
        Fi = phase_frames(R[c.label], idle); med = statistics.median(r[2] for r in Fi) if Fi else 0
        Fk = phase_frames(R[c.label], keys); ex = []
        for k in kt:
            j = next((i for i, r in enumerate(Fk) if r[0] > k), None)
            if j is not None: ex.append(sum(r[2] for r in Fk[j:j + 3]) - 3 * med)
        c.per_press = (keys['lookups'] - idle['per_min'] * (keys['t1'] - keys['t0']) / 60) / max(len(kt), 1)
        pp = c.ph['world_popups']; c.per_popup = (pp['lookups'] - idle['per_min'] * (pp['t1'] - pp['t0']) / 60) / POPUPS
        c.press_ms = statistics.mean(ex) if ex else float('nan')
        lines.append(f"{c.label}: {len(kt)} presses: walks per press {c.per_press:.2f}, per popup {c.per_popup:.2f}; extra MainFlow.Update per press "
                     f"(3 frames) mean {c.press_ms:.2f} ms, max {max(ex) if ex else float('nan'):.2f} ms; Esc clones after the Esc menu opens: "
                     f"{'%.3f s' % esc[c.label] if esc[c.label] is not None else 'not seen'}")
    for l in lines: print(l, flush=True)
    b, n = copies
    result(n.per_press <= 3.5, f"walks per key press: new {n.per_press:.2f} (max 3.5: one per frame in the 3 kicked frames), base {b.per_press:.2f}")
    result(n.per_popup <= 10.5, f"walks per stock popup: new {n.per_popup:.2f} (max 10.5: one per frame in the 10 kicked frames), base {b.per_popup:.2f}")
    lf = n.ph['world_popups']['poll_frames']
    result(bool(lf) and lf[0] <= 1 and lf[1] == 0, f"most poll walks in one frame, frames with 2+ (whole run, new): {lf}")
    result(n.ph['title']['lookups'] <= b.ph['title']['lookups'], f"walks at the idle title: new {n.ph['title']['lookups']}, base {b.ph['title']['lookups']}")
    # SCAN-RULE (loader guard, lane fpsscan: more than 6 scene searches of one module within 60 s in a steady state fails):
    # the same limit on this module's own counters (view walks + sprite scans) per steady phase (each 20-33 s)
    for ph in ('title', 'loadlist', 'world_idle', 'world_keys'):
        d = n.ph[ph]; k = d['lookups'] + d['sprite_scans']
        result(k <= 6, f"single player {ph}: scene searches {k} in {d['t1'] - d['t0']:.0f} s (max 6: SCAN-RULE), base {b.ph[ph]['lookups'] + b.ph[ph]['sprite_scans']}")
    if esc['base'] is None: result(esc['new'] is not None, f"Esc clones shown (base did not log them): new {esc['new']}")
    else: result(esc['new'] is not None and esc['new'] <= max(0.3, esc['base'] + 0.1), f"Esc menu pickup: new {esc['new'] if esc['new'] is None else round(esc['new'], 3)} s, base {esc['base']:.3f} s")
    print(f"INFO world walks per minute with one key press per second: base {b.ph['world_keys']['per_min']:.1f}, new {n.ph['world_keys']['per_min']:.1f}; "
          f"extra MainFlow.Update per press: base {b.press_ms:.2f} ms, new {n.press_ms:.2f} ms", flush=True)
    open(os.path.join(OUT, 'summary.txt'), 'w').write('\n'.join(lines + res) + '\n')
    json.dump({c.label: {k: {kk: vv for kk, vv in v.items() if kk not in ('p0', 'p1')} for k, v in c.ph.items()} for c in copies},
              open(os.path.join(OUT, 'phases.json'), 'w'), indent=1)
finally:
    for c in copies:
        try: rig.stop(c.x)
        except Exception as e: print(c.x, 'stop failed', e)
    if pair: rig.release(pair, LANE)
    print('prefs ok' if rig.check_prefs() else 'PREFS CHANGED', flush=True)
    if rigports.EXTRA: print('EXTRA PLAYER lines: FAIL', flush=True); fails += 1
print(f"RESULT sp_scan_test: {len(res) - fails} PASS, {fails} FAIL ({OUT.replace(os.path.expanduser('~'), '~')})", flush=True)
sys.exit(1 if fails else 0)
