"""pvpfix lane: shared steps of the PvP v1 pair tests (from ext/fepvp13/tests/pvp_v1_pair.py), hidden rig copies only.
lease -> host page "PvP" -> PvP world + lobby -> guest joins -> both bring 2 units -> lobby Start -> match set up.
Owner play rule: never start copies while the owner's modded game runs (wait_owner)."""
import json, os, shutil, subprocess, sys, time
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(W, 'rig')); import rig  # noqa: E402  (worktree rig.py: EXTRA_ENV)
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')
HOME = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures/home_Elven.json')
H_UNITS = ['477ad25a-7c22-43f5-be28-db055895297e', '137a4264-054d-4b21-81f2-a69d349a3bd0']   # Ivan, Silvarion
G_UNITS = ['92be08ca-b7de-497a-a4ea-9aac4be0b603', 'acd20405-56c9-4360-80f6-b4c84e1a9ba4']   # Leo, Leonis



def owner_plays():
    """owner play rule: his game BINARY runs (launcher copy or the app); shells / test copies never count"""
    import re as _re
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/[^ ]*/MacOS/Dungeon Settlers|Dungeon Settlers Modded.app/Contents/MacOS/'], capture_output=True, text=True).stdout
    return any(l and not _re.search(r'/instances/|/private/tmp/|/tmp/|ToDelete|/ds_rel0[0-9]+/|/test_env/|pgrep|zsh -c|bash -c', l) for l in out.splitlines())

class T:
    def __init__(self, name, argv):
        self.A = argv
        rig.DYLIB = os.path.abspath(argv[argv.index('--dylib') + 1]) if '--dylib' in argv else os.path.join(W, 'build_pvpfix', 'dsrig.dylib')
        self.tag = argv[argv.index('--tag') + 1] if '--tag' in argv else time.strftime(name + '_%Y%m%d-%H%M%S')
        self.out = os.path.join(W, 'ext', 'pvpfix', 'out', self.tag); os.makedirs(self.out, exist_ok=True)
        self.logf = open(os.path.join(self.out, 'test.log'), 'a'); self.results = {}
        self.lane = f'pvpfix-{os.getpid()}'
        cm = os.path.join(os.path.dirname(rig.DYLIB), 'custom-modes.dylib')
        snap = os.path.join(self.out, 'dsrig.dylib'); shutil.copy2(rig.DYLIB, snap)
        if os.path.exists(cm): shutil.copy2(cm, os.path.join(self.out, 'custom-modes.dylib'))   # Units Laboratory next to it (copies with DSRIG_CUSTOM=1 load it)
        rig.DYLIB = snap   # this run's own copy: later builds never mix in
        self.B = json.loads(subprocess.run([sys.executable, os.path.join(W, 'ext/fepvp13/tools/mpm_ids.py'), W], capture_output=True, text=True).stdout)

    def log(self, *a):
        s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); self.logf.write(s + '\n'); self.logf.flush()

    def check(self, name, ok, info=''):
        self.results[name] = bool(ok); self.log(('PASS ' if ok else 'FAIL ') + name + (f'  [{str(info)[:600]}]' if info != '' else ''))
        return bool(ok)

    def c(self, _who, **k): return rig.send(_who, k, timeout=180)

    def wait(self, what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def press(self, x, n): self.c(x, cmd='mpm_press', button=self.B[n]); time.sleep(1.2)

    def shot(self, x, name, ui=True):
        p = os.path.join(self.out, f'{name}_{x}.png')
        self.c(x, cmd='mpm_shot', how='backbuffer', path=p) if ui else self.c(x, cmd='screenshot', path=p)
        return p

    @staticmethod
    def wait_owner(log):
        """owner play rule: no test copies while his modded game runs (launcher copy or the app)"""
        while True:
            if not owner_plays(): return
            log('owner is playing: waiting 60 s'); time.sleep(60)

    def run(self, body):
        import signal; signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
        self.wait_owner(self.log)
        pair = None
        while not pair:
            pair = rig.lease(self.lane, os.getpid())
            if not pair: self.log('no free pair, waiting'); time.sleep(30)
        self.H, self.G = pair[0], pair[1]
        self.log('pair', pair, 'dylib', rig.DYLIB, 'out', self.out)
        try:
            body(self)
            self.results['ran to the end'] = True
        except Exception as e:
            import traceback; self.log('ERROR', repr(e)); self.log(traceback.format_exc()); self.results['no exception'] = False
        finally:
            self.results.setdefault('ran to the end', False)
            for x in (self.H, self.G): rig.stop(x)
            for x in (self.H, self.G):
                p = os.path.join(rig.idir(x), 'mp.json')
                if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
                for f in ('player.log', 'rig.log', 'fp.log'):
                    if os.path.exists(os.path.join(rig.idir(x), f)): shutil.copy2(os.path.join(rig.idir(x), f), os.path.join(self.out, f'{x}_{f}'))
            self.log('released', rig.release(pair, self.lane))
            self.log('prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
            n = sum(1 for v in self.results.values() if not v)
            self.log('RESULT', 'PASS' if self.results and not n else 'FAIL', f'{len(self.results) - n}/{len(self.results)}', json.dumps(self.results))

    # ------------------------------------------------------------------ flow steps
    def prepare(self, game_saves=None, env=None):
        H, G = self.H, self.G
        for x in (H, G):
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p): os.replace(p, p + '.last')
            d = os.path.join(rig.idir(x), 'data', 'mp_owners')
            if os.path.exists(d): os.replace(d, d + '_' + time.strftime('%Y%m%d-%H%M%S'))
            # the PvP Rules box keeps the last used rules per copy: an earlier test's (CTF, 300 s rounds, map 2) set it apart
            r = os.path.join(rig.idir(x), 'data', 'ModData', 'multiplayer', 'pvp_rules.txt')
            if os.path.exists(r): os.replace(r, r + '.last_' + time.strftime('%Y%m%d-%H%M%S'))
        cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
        shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
        self.work = {}
        for x in (H, G):
            self.work[x] = os.path.join(rig.idir(x), 'save_in'); os.makedirs(self.work[x], exist_ok=True)
            shutil.copy2(HOME, os.path.join(self.work[x], 'my_home.json'))
        rig.EXTRA_ENV.clear()
        for x, d in (game_saves or {}).items(): rig.EXTRA_ENV[x] = {'DSML_GAME_SAVES': d}
        for x, e in (env or {}).items(): rig.EXTRA_ENV.setdefault(x, {}).update(e)

    def host_world(self, bo_down=1, secs_down=2):
        H = self.H
        os.environ.setdefault('DSRIG_CUSTOM', '0')
        rig.start(H, fps=30, window=True)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title H', timeout=1500); time.sleep(3)
        self.press(H, 'MULTIPLAYER'); self.press(H, 'HOST_C1'); time.sleep(1)
        for _ in range(bo_down): self.c(H, cmd='mpm_pvp', action='page_arrow', row=1, dir=-1)
        for _ in range(secs_down): self.c(H, cmd='mpm_pvp', action='page_arrow', row=2, dir=-1)
        self.press(H, 'HOST_START')
        rig.wait_for(H, lambda s: s.get('scene') == 2 and s.get('tick', -1) > 0 and not s.get('busy') and s.get('det', {}).get('locked'), 'H loaded + hosting', timeout=900)
        self.wait('host lobby', lambda: self.c(H, cmd='mpm_status').get('state') == 'lobby', timeout=120, every=1)
        self.port = self.c(H, cmd='mpm_status').get('port')

    def guest_join(self):
        G = self.G
        rig.start(G, fps=30, window=True)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title G', timeout=1500); time.sleep(2)
        self.press(G, 'MULTIPLAYER'); self.press(G, 'JOIN_ADDR'); self.c(G, cmd='mpm_type', text=f'127.0.0.1:{self.port}'); self.press(G, 'CONNECT')
        self.wait('G loaded the host save and locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(self.c(G, cmd='status')), timeout=900)
        time.sleep(3)

    def bring_both(self):
        H, G = self.H, self.G
        self.log('H bring', self.c(H, cmd='pvp_units_bring', file=os.path.join(self.work[H], 'my_home.json'), guids=','.join(H_UNITS)))
        self.log('G bring', self.c(G, cmd='pvp_units_bring', file=os.path.join(self.work[G], 'my_home.json'), guids=','.join(G_UNITS)))
        self.wait('host has 4 units', lambda: self.c(H, cmd='pvp_units_status')['received'] == 4, timeout=90, every=1)
        self.c(H, cmd='pvp_mods', ask=1)
        self.wait('guest mod report', lambda: len(self.c(H, cmd='pvp_mods').get('reports', [])) >= 1, timeout=60, every=1)

    def lobby_start(self):
        H, G = self.H, self.G
        self.press(H, 'START'); self.log('H lobby Start (menu)')
        self.wait('match on both', lambda: all(self.c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=600, every=2)
        self.t_match = self.c(H, cmd='status')['tick']

    def units(self, x):
        s = self.c(x, cmd='pvp_status'); cells = {u['guid']: u for u in self.c(x, cmd='list_units')['units']}
        return s, cells

    def fp_compare(self, t_from=0, settle=3):
        """fp.log of both copies (world fingerprint per tick) over their common ticks; no pause (PvP turns player pauses
           off during a match). Returns (common, first_diff, desyncs)."""
        H, G = self.H, self.G
        time.sleep(settle)
        rd = lambda x: {int(q[0]): q[1] for q in (l.split() for l in open(os.path.join(rig.idir(x), 'fp.log'), errors='replace')) if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from}
        fh, fg = rd(H), rd(G); com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
        ds = (self.c(H, cmd='status')['mp'].get('desyncs'), self.c(G, cmd='status')['mp'].get('desyncs'))
        return len(com), first, ds
