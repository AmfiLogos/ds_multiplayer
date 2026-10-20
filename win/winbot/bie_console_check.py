#!/usr/bin/env python3
"""bie_console_check.py - v0.1.16.1 hotfix check on the Windows VM: a player's start with a BepInEx mod shows no BepInEx
console window; the starter sets BepInEx.cfg [Logging.Console] Enabled = false and keeps the rest of the file; the disk
log (BepInEx\\LogOutput.log) is still written. Real install, REAL starter (Steam on, owner's choice), winrig starter mode.

  WINBOT_TARGET=vm WINBOT_STARTER=1 bie_console_check.py <out> <bepinex test mod dir> <pinned BepInEx zip>
Start 1: BepInEx's first start (it writes BepInEx.cfg). Start 2: the cfg is edited first (console on + a real setting
changed in another section, [Caching] EnableAssemblyCache = false): the starter must turn the console off again and keep
every other setting. Compared per key (BepInEx re-saves the cfg a few seconds after the start and drops unknown lines). The test mod and the zip are moved out
after (never deleted)."""
import json, os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import winrig as W                                                   # noqa: E402

OUT, MOD, ZIP = sys.argv[1], sys.argv[2], sys.argv[3]
CFG = W.GAME + r'\BepInEx\config\BepInEx.cfg'; BLOG = W.GAME + r'\BepInEx\LogOutput.log'
CONSOLE_HOSTS = ('conhost.exe', 'openconsole.exe', 'windowsterminal.exe')
MARK = ('Caching', 'EnableAssemblyCache')                           # a real setting: BepInEx keeps values, drops comments
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(n, ok, d=''):
    checks.append({'check': n, 'ok': bool(ok), 'detail': str(d)[:500]}); log('PASS' if ok else 'FAIL', n, '' if ok else d)


def console_sec(t):
    i = t.find('[Logging.Console]'); j = t.find('\n[', i + 1)
    return re.findall(r'(?m)^Enabled = (\w+)', t[i:j if j > 0 else len(t)]) if i >= 0 else []


def settings(t):
    """{(section, key): value} of a BepInEx cfg (comments and blank lines left out)"""
    d, sec = {}, ''
    for l in t.replace('\r', '').splitlines():
        l = l.strip()
        if l.startswith('[') and l.endswith(']'):
            sec = l[1:-1]
        elif l and not l.startswith('#') and '=' in l:
            k, _, v = l.partition('='); d[(sec, k.strip())] = v.strip()
    return d


def watch_consoles(w, base, until_title):
    """console windows seen while the game starts (BepInEx's console has 'BepInEx' in its title)"""
    seen = set(); t = time.time()
    while time.time() - t < 1800:
        for p in w.a('ps').get('procs', []):
            for v in p.get('windows', []):
                tt = v.get('title', '')
                if 'bepinex' in tt.lower() or (p['name'].lower() in CONSOLE_HOSTS and p['pid'] not in base):
                    seen.add(f"{p['name']} {p['pid']}: {tt}")
        if until_title():
            return sorted(seen)
        time.sleep(1)
    raise W.GameError('no title within 30 min')


def start_and_watch(w, n):
    base = {p['pid'] for p in w.a('ps').get('procs', []) if p['name'].lower() in CONSOLE_HOSTS}
    w.kill_all()
    import threading
    res = {}
    th = threading.Thread(target=lambda: res.update(t=w.start(timeout=1800)), daemon=True); th.start()
    seen = watch_consoles(w, base, lambda: not th.is_alive())
    th.join()
    w.shot(os.path.join(OUT, f'start{n}_title_screen.png'))           # the whole VM screen: no console window
    return seen, res.get('t')


def main():
    os.makedirs(OUT, exist_ok=True)
    w = W.Win(); w.up(); assert w.starter, 'set WINBOT_STARTER=1 (the real player starter)'
    mid = json.load(open(os.path.join(MOD, 'mod.json')))['id']
    md = W.ROOT + '\\mods\\' + mid; cz = W.ROOT + '\\cache\\' + os.path.basename(ZIP)
    w.kill_all()
    for f in os.listdir(MOD):
        w.put(os.path.join(MOD, f), md + '\\' + f)
    if not w.exists(cz):
        w.upload(ZIP, cz)                                            # the pinned zip: no download
    try:
        w.prepare(lang=0, active=['custom-modes', 'gcd-cdr', mid, 'multiplayer'], settings={})
        seen, t = start_and_watch(w, 1)
        check('start 1 (BepInEx first start): title reached', True, f'{t:.0f} s')
        check('start 1: no BepInEx console window', not seen, seen)
        dl = w.text(W.PROF + r'\dsload.log') or ''
        check('start 1: the loader ran the BepInEx mod', 'BepInEx plugin' in dl or 'bepinex' in dl.lower(), [l for l in dl.splitlines() if 'bepinex' in l.lower()][-3:])
        cfg = w.text(CFG) or ''
        check('start 1: BepInEx.cfg [Logging.Console] Enabled = false', console_sec(cfg) == ['false'], console_sec(cfg))
        check('start 1: BepInEx\\LogOutput.log written', w.exists(BLOG) and w.a('stat', path=BLOG).get('size', 0) > 0, w.a('stat', path=BLOG))
        w.stop()
        # start 2: a player turned the console on and changed another setting
        i = cfg.find('[Logging.Console]'); j = cfg.find('\n[', i + 1)
        sec = cfg[i:j].replace('Enabled = false', 'Enabled = true')
        edited = cfg[:i] + sec + cfg[j:]
        assert settings(edited).get(MARK) == 'true', settings(edited).get(MARK)
        edited = re.sub(r'(?m)^EnableAssemblyCache = true', 'EnableAssemblyCache = false', edited, count=1)
        w.write(CFG, edited.encode('utf-8'))
        seen, t = start_and_watch(w, 2)
        cfg2 = w.text(CFG) or ''
        check('start 2: no BepInEx console window (the cfg had it on)', not seen, seen)
        check('start 2: the starter turned the console off again', console_sec(cfg2) == ['false'], console_sec(cfg2))
        want = dict(settings(edited)); want[('Logging.Console', 'Enabled')] = 'false'
        got = settings(cfg2)
        diff = {f'{k[0]}.{k[1]}': (want.get(k), got.get(k)) for k in set(want) | set(got) if want.get(k) != got.get(k)}
        check('start 2: the rest of BepInEx.cfg kept (EnableAssemblyCache = false, every other setting)', got.get(MARK) == 'false' and not diff,
              diff or 'all settings kept')
        check('start 2: LogOutput.log written again', w.a('stat', path=BLOG).get('size', 0) > 0)
        w.logs(os.path.join(OUT, 'logs'))
        open(os.path.join(OUT, 'BepInEx.cfg.after'), 'w').write(cfg2)
    finally:
        w.stop()
        keep = W.WORK + r'\to_delete\bieconsole_' + time.strftime('%Y%m%d-%H%M%S')
        w.move(md, keep + '\\' + mid)                                 # the test mod out again (moved, never deleted)
        w.prepare(active=['custom-modes', 'gcd-cdr', 'multiplayer'])
    ok = all(c['ok'] for c in checks)
    json.dump({'pass': ok, 'checks': checks}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log('RESULT', 'PASS' if ok else 'FAIL', f'{sum(c["ok"] for c in checks)}/{len(checks)}')


if __name__ == '__main__':
    main()
