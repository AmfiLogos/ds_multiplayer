#!/usr/bin/env python3
"""w_cjk_gate.py <kit dir> <out> [<lang code, default ja>]: Windows (DSMP bottle, -batchmode -nographics, wine lock held by
the gate through held_lock_run.py): the release kit installed fresh, the game started in a CJK language
(UserSetting.json), then: the loader set that language with every text and none dropped; the loader and multiplayer took
the game's own fonts for it; the loader's title button is in that language; multiplayer and Custom Modes run in it with
none dropped; the updater offers a mod only from its own <id>-v<ver> release (local release server). The Mods window cannot open with -nographics (no UI sprites). Prints "x/y checks"."""
import glob, json, os, re, shutil, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_launcher/loader/test')
import w_pkg_test as t
w = t.w
KIT, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]); CODE = sys.argv[3] if len(sys.argv) > 3 else 'ja'
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']; LANG = CODES.index(CODE)
FONT = {'ja': ('NotoSerifJP', 'KaiseiHarunoUmi'), 'zh-Hans': ('NotoSerifSC', 'MaShanZheng'), 'zh-Hant': ('SourceHanSerifTC', 'LXGWWenKaiTC')}
LD = os.path.expanduser(os.environ.get('LD', '~/ds_launcher'))
EN = json.load(open(f'{LD}/loader/lang/en.json')); TRL = json.load(open(f'{LD}/loader/lang/{CODE}.json'))
LOCK = t.LOCK
os.makedirs(OUT, exist_ok=True)
t.owned = lambda: os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().split('\n')[0].strip() in ('gate13', os.environ.get('GATE_WINE_OWNER', 'gate13'))
if not t.owned(): sys.exit('w_cjk_gate: the wine lock is not held by the gate')
root = w.ROOT
if os.path.exists(root): os.rename(root, root + '.gate020-' + time.strftime('%Y%m%d-%H%M%S'))   # clean install (never removed)
rc = w.install(KIT); t.log('install rc', rc); t.check('release kit installed', rc == 0, rc)
data = os.path.join(w.PROF, 'data'); os.makedirs(data, exist_ok=True)
us = json.load(open(f'{H}/ds_launcher/loader/test_env/instances/X/data/UserSetting.json'))   # the game's own file format
us['GeneralSettingData']['SaveCurrentLanguageType'] = LANG; us['GeneralSettingData']['IsLanguageInitialized'] = True
json.dump(us, open(os.path.join(data, 'UserSetting.json'), 'w'), indent=1)
# a local release server (loader/test/update/relserver.py; the real API has an hourly limit): ds_custommodes holds an old
# bundle release with custom-modes-0.11.0.zip and the mod's own custom-modes-v0.1.5; the updater must offer 0.1.5
import socket, subprocess
RS = os.path.join(OUT, 'releases')
for tag, f in (('bundle-v0.1.16', 'custom-modes-0.11.0.zip'), ('custom-modes-v0.1.5', 'custom-modes-0.1.5.zip')):
    d = os.path.join(RS, 'AmfiLogos', 'ds_custommodes', tag); os.makedirs(d, exist_ok=True); open(os.path.join(d, f), 'wb').write(b'test only ' + tag.encode())
os.makedirs(os.path.join(RS, 'AmfiLogos', 'ds_modloader', 'v0.1.9'), exist_ok=True)
so = socket.socket(); so.bind(('127.0.0.1', 0)); port = so.getsockname()[1]; so.close()
srv = subprocess.Popen([sys.executable, f'{LD}/loader/test/update/relserver.py', RS, str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.environ['DSL_UPDATE_API'] = f'http://127.0.0.1:{port}'
os.makedirs(os.path.join(root, 'update'), exist_ok=True); open(os.path.join(root, 'update', 'settings.json'), 'w').write('{"auto":false}\n')   # offer only, no download
try:
    t.start(); s = t.title(600); t.check(f'title reached in {CODE}', s, s)
    def logs(): return t.dlog() + (open(os.path.join(w.PROF, 'rig.log'), errors='replace').read() if os.path.exists(os.path.join(w.PROF, 'rig.log')) else '')
    def wait_for(rx, limit=60):                         # lines come a few seconds after the title (fonts, title column)
        t0 = time.time()
        while time.time() - t0 < limit:
            m = re.search(rx, logs())
            if m: return m
            time.sleep(1)
        return None
    m = wait_for(r'language (\d+) \(([\w-]+)\) from .*?; texts .*?: (\d+) loaded, (\d+) dropped')   # Windows paths hold ':'
    t.check(f'loader language {CODE}, all {len(EN)} texts loaded, none dropped', m and m.group(2) == CODE and int(m.group(3)) >= len(EN) and m.group(4) == '0', m.group(0) if m else 'no language line')
    want = FONT.get(CODE, ('Lora', 'Cinzel'))
    bf = wait_for(r'ui: body text font from [^(]*\(%s\), material "([^"]+)"' % re.escape(CODE))
    tf = wait_for(r'ui: title font "([^"]+)" \([^,]*, %s\)' % re.escape(CODE))
    mf = wait_for(r'mpmenu: i18n: body font for %s: (\S+)' % re.escape(CODE))
    t.check(f'the game\'s {CODE} fonts in the loader and multiplayer (body {want[0]}, title {want[1]})',
            bf and tf and mf and bf.group(1).startswith(want[0]) and tf.group(1).startswith(want[1]) and mf.group(1).startswith(want[0]), (bf and bf.group(1), tf and tf.group(1), mf and mf.group(1)))
    tb = wait_for(r'ui: title button "%s" added to the title column' % re.escape(TRL.get('Mods', '?')))
    t.check(f'the loader\'s title button "Mods" in {CODE} ({TRL.get("Mods")}) on the title', tb, 'not added')
    mp = wait_for(r'mpmenu: i18n: game language %d = %s \((\d+) texts, (\d+) dropped' % (LANG, re.escape(CODE)))
    cm = wait_for(r'\[custom-modes\] i18n: language %s [^:]*: (\d+) texts, (\d+) dropped' % re.escape(CODE))
    t.check(f'multiplayer and Custom Modes in {CODE}, texts loaded, none dropped', mp and cm and int(mp.group(1)) > 0 and mp.group(2) == '0' and int(cm.group(1)) > 0 and cm.group(2) == '0',
            (mp and mp.group(0), cm and cm.group(0)))
    # the updater (local releases): the mod's own newer release is offered, the old bundle zip with a higher number is not
    wait_for(r'update: kit: installed'); time.sleep(5); L = t.dlog()
    up = [l[13:] for l in L.splitlines() if 'update: mod ' in l]
    t.check('auto-update: custom-modes 0.1.5 offered from custom-modes-v0.1.5, not 0.11.0 from bundle-v0.1.16',
            any('custom-modes: installed 0.1.4, newer 0.1.5' in l for l in up) and not any('0.11.0' in l for l in up), up or 'no mod update line')
    # the Mods window itself cannot open here: -nographics (the bottle has no graphics) gives the UI kit no sprites
finally:
    w.stop()
    if 'srv' in globals(): srv.terminate()
n = sum(1 for c in t.checks if c['ok'])
json.dump(t.checks, open(os.path.join(OUT, 'checks.json'), 'w'), indent=1, ensure_ascii=False)
print(f'{n}/{len(t.checks)} checks', flush=True)
sys.exit(0 if n == len(t.checks) else 1)
