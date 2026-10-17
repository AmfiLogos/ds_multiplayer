#!/usr/bin/env python3
"""w_repair_gate.py <kit dir> <custom-modes release zip> <out>: Windows (DSMP bottle, -batchmode -nographics, wine lock
held by the gate through held_lock_run.py), all in Japanese (UserSetting.json):
  repair     an install hit by the v0.1.7-v0.1.9 updater (custom-modes 0.11.0 from an old bundle release) ends on the
             valid custom-modes 0.1.5: a local release server (loader/test/update/relserver.py) holds bundle-v0.1.16 with
             custom-modes-0.11.0.zip and custom-modes-v0.1.5 with the real release zip; Auto-update on; start 1 finds and
             downloads it, start 2 installs it in the starter
  window     that install's update window title is the Japanese text (starter.log, window hidden)
  pathwarn   the starter alone in a Chinese folder: the warning box text (logged with DSL_NO_ALERT) is the Japanese text
Prints "x/y checks"."""
import json, os, re, shutil, socket, subprocess, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_launcher/loader/test')
import w_pkg_test as t
w = t.w
KIT, CMZIP, OUT = (os.path.abspath(a) for a in sys.argv[1:4]); os.makedirs(OUT, exist_ok=True)
LD = os.path.expanduser(os.environ.get('LD', '~/ds_launcher'))
JA = json.load(open(f'{LD}/loader/lang/ja.json'))
t.owned = lambda: os.path.exists(f'{t.LOCK}/owner') and open(f'{t.LOCK}/owner').read().split('\n')[0].strip() in ('gate13', os.environ.get('GATE_WINE_OWNER', 'gate13'))
if not t.owned(): sys.exit('w_repair_gate: the wine lock is not held by the gate')
US = json.load(open(f'{H}/ds_launcher/loader/test_env/instances/X/data/UserSetting.json'))   # the game's own file format
US['GeneralSettingData']['SaveCurrentLanguageType'] = 7; US['GeneralSettingData']['IsLanguageInitialized'] = True
def put_us(data_dir): os.makedirs(data_dir, exist_ok=True); json.dump(US, open(os.path.join(data_dir, 'UserSetting.json'), 'w'), indent=1)
def ja_prefix(key): v = JA.get(key, ''); return v.split('%')[0] if isinstance(v, str) else ''
root = w.ROOT
if os.path.exists(root): os.rename(root, root + '.gate021-' + time.strftime('%Y%m%d-%H%M%S'))   # never removed
rc = w.install(KIT); t.check('kit installed', rc == 0, rc)
put_us(os.path.join(w.PROF, 'data'))
mj = os.path.join(root, 'mods', 'custom-modes', 'mod.json')                 # the polluted state: an old bundle build's number
s = open(mj, encoding='utf-8').read(); open(mj, 'w', encoding='utf-8').write(re.sub(r'"version": "[^"]+"', '"version": "0.11.0"', s, count=1))
RS = os.path.join(OUT, 'releases')
for tag, f, src in (('bundle-v0.1.16', 'custom-modes-0.11.0.zip', None), ('custom-modes-v0.1.5', 'custom-modes-0.1.5.zip', CMZIP)):
    d = os.path.join(RS, 'AmfiLogos', 'ds_custommodes', tag); os.makedirs(d, exist_ok=True)
    if src: shutil.copy2(src, os.path.join(d, f))
    else: open(os.path.join(d, f), 'wb').write(b'old bundle-era build (test stand-in)')
os.makedirs(os.path.join(RS, 'AmfiLogos', 'ds_modloader', 'v0.1.9'), exist_ok=True)   # no newer kit: the mods are checked
so = socket.socket(); so.bind(('127.0.0.1', 0)); port = so.getsockname()[1]; so.close()
srv = subprocess.Popen([sys.executable, f'{LD}/loader/test/update/relserver.py', RS, str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.environ.update(DSL_UPDATE_API=f'http://127.0.0.1:{port}', DSL_UPDATE_WINDOW='hidden')
os.makedirs(os.path.join(root, 'update'), exist_ok=True); open(os.path.join(root, 'update', 'settings.json'), 'w').write('{"auto":true}\n')
def wait_log(rx, limit=90):
    t0 = time.time()
    while time.time() - t0 < limit:
        m = re.search(rx, t.dlog())
        if m: return m
        time.sleep(2)
    return None
try:
    t.start(); t.check('start 1: title', t.title(600))
    m = wait_log(r'update: mod custom-modes: installed 0\.11\.0 has no release of its own: back to (\S+)')
    d = wait_log(r'custom-modes-0\.1\.4\.zip downloaded, sha256 ok')
    t.check('start 1: custom-modes 0.11.0 (no release of its own) goes back to 0.1.5, downloaded', m and m.group(1) == '0.1.5' and d,
            [l[13:] for l in t.dlog().splitlines() if 'update:' in l][-6:])
    w.stop(); time.sleep(3)
    t.start(); t.check('start 2: title', t.title(600))
    sl = open(os.path.join(w.PROF, 'starter.log'), encoding='utf-8', errors='replace').read() if os.path.exists(os.path.join(w.PROF, 'starter.log')) else ''
    ver = json.load(open(mj, encoding='utf-8')).get('version')
    t.check('start 2: the starter installed it, custom-modes is 0.1.5 again', ver == '0.1.5' and 'custom-modes 0.1.5: loaded' in t.dlog(), (ver, sl.strip()[-200:]))
    wt = re.findall(r'update window title: (.*)', sl); want = ja_prefix('Updating mods: %s')
    t.check(f'update window title in Japanese ("{want}...")', want and wt and wt[-1].startswith(want), wt[-1:] or 'no title line')
finally:
    w.stop(); srv.terminate()
# pathwarn: the starter alone in a Chinese folder (pathwarn_test.py's "start" step), with its texts and a Japanese setting
ZH = '游戏测试'; base = f'{w.DRIVE}/{ZH}'
if os.path.exists(base): os.makedirs(f'{w.DRIVE}/old_pathwarn', exist_ok=True); os.rename(base, f'{w.DRIVE}/old_pathwarn/{ZH}-{int(time.time())}')
d = f'{base}/DSM'; os.makedirs(f'{d}/game', exist_ok=True)
shutil.copy(f'{KIT}/files/Dungeon Settlers Modded.exe', d); shutil.copytree(f'{KIT}/files/lang', f'{d}/game/lang')
put_us(f'{d}/profiles/modded/data')
subprocess.run(['nice', '-n', '19', f'{w.C}/wine', '--bottle', 'DSMP', f'C:\\{ZH}\\DSM\\Dungeon Settlers Modded.exe'],
               env=dict(os.environ, DSL_NO_ALERT='1'), capture_output=True, timeout=120)
lg = f'{d}/profiles/modded/starter.log'; tx = open(lg, encoding='utf-8', errors='replace').read() if os.path.exists(lg) else ''
box = re.findall(r'dialog \(DSL_NO_ALERT, not shown\): (.*)', tx)
want = ja_prefix(next((k for k in JA if k.startswith('The game will not start from this folder:')), '')).strip()
t.check(f'pathwarn box text in Japanese ("{want}...")', want and box and box[-1].startswith(want) and 'started ' not in tx, box[-1:] or tx.strip()[-160:])
n = sum(1 for c in t.checks if c['ok'])
print(f'{n}/{len(t.checks)} checks', flush=True)
sys.exit(0 if n == len(t.checks) else 1)
