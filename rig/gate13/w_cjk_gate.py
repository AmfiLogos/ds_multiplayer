#!/usr/bin/env python3
"""w_cjk_gate.py <kit dir> <out> [<lang code, default ja>]: Windows (DSMP bottle, -batchmode -nographics, wine lock held by
the gate through held_lock_run.py): the release kit installed fresh, the game started in a CJK language
(UserSetting.json), then: the loader set that language with every text and none dropped; it took the game's own fonts
for it; the loader's title button and the Mods window (DSL_UI_TEST state) show that language's texts and no loader text
in English. Prints "x/y checks"."""
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
t.owned = lambda: os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().split('\n')[0].strip() in ('gate13', os.environ.get('GATE_WINE_OWNER', 'gate13'))
UI = os.path.join(OUT, 'ui'); os.makedirs(UI, exist_ok=True)
SEQ = [0]
def ui(op, **kw):
    SEQ[0] += 1; seq = str(SEQ[0]); kw.update({'op': op, 'seq': seq})
    tmp = os.path.join(UI, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 40:
        try:
            j = json.load(open(os.path.join(UI, 'result.json')))
            if j.get('seq') == seq: return j
        except Exception: pass
        time.sleep(0.2)
    return {'error': f'ui {op} timed out'}
def english_left(strings):
    """loader texts shown in English although the language has its own text"""
    return sorted({s for s in strings if s in EN and isinstance(TRL.get(s), str) and TRL[s] != s and re.search('[A-Za-z]{3}', s)})
def all_strings(o):
    if isinstance(o, str): yield o
    elif isinstance(o, dict):
        for v in o.values(): yield from all_strings(v)
    elif isinstance(o, list):
        for v in o: yield from all_strings(v)

if not t.owned(): sys.exit('w_cjk_gate: the wine lock is not held by the gate')
root = w.ROOT
if os.path.exists(root): os.rename(root, root + '.gate020-' + time.strftime('%Y%m%d-%H%M%S'))   # clean install (never removed)
rc = w.install(KIT); t.log('install rc', rc); t.check('release kit installed', rc == 0, rc)
data = os.path.join(w.PROF, 'data'); os.makedirs(data, exist_ok=True)
us = json.load(open(f'{H}/ds_launcher/loader/test_env/instances/X/data/UserSetting.json'))   # the game's own file format
us['GeneralSettingData']['SaveCurrentLanguageType'] = LANG; us['GeneralSettingData']['IsLanguageInitialized'] = True
json.dump(us, open(os.path.join(data, 'UserSetting.json'), 'w'), indent=1)
os.environ['DSL_UI_TEST'] = w.winpath(UI)
try:
    t.start(); s = t.title(600); t.check(f'title reached in {CODE}', s, s)
    L = t.dlog()
    m = re.search(r'language (\d+) \(([\w-]+)\) from [^;]*; texts [^:]*: (\d+) loaded, (\d+) dropped', L)
    t.check(f'loader language {CODE}, all {len(EN)} texts loaded, none dropped', m and m.group(2) == CODE and int(m.group(3)) >= len(EN) and m.group(4) == '0', m.group(0) if m else 'no language line')
    bf = re.search(r'ui: body text font from [^(]*\(%s\), material "([^"]+)"' % re.escape(CODE), L)
    tf = re.search(r'ui: title font "([^"]+)" \([^,]*, %s\)' % re.escape(CODE), L)
    want = FONT.get(CODE, ('Lora', 'Cinzel'))
    t.check(f'the game\'s {CODE} fonts (body {want[0]}, title {want[1]})', bf and tf and bf.group(1).startswith(want[0]) and tf.group(1).startswith(want[1]), (bf and bf.group(1), tf and tf.group(1)))
    n0 = t.logs_all().count('reg test: title'); t.request({'cmd': 'title'}); t.wait_text('reg test: title', n0, 30)
    tl = [l for l in t.logs_all().splitlines() if 'reg test: title' in l][-1:] or ['']
    t.check(f'title button "Mods" in {CODE} ({TRL.get("Mods")})', TRL.get('Mods') and TRL['Mods'] in tl[0], tl[0][-200:])
    o = ui('open'); time.sleep(2); st = ui('state')
    json.dump(st, open(os.path.join(OUT, 'mods_state.json'), 'w'), ensure_ascii=False, indent=1)
    strs = list(all_strings(st)); left = english_left(strs)
    ja = sum(1 for x in strs if re.search('[぀-ヿ㐀-鿿가-힯]', x))
    t.check(f'Mods window open, its texts in {CODE} ({ja} CJK texts), no loader text in English', o.get('ok') == 1 and st.get('ok') == 1 and st.get('open') == 1 and ja > 0 and not left, (o.get('msg') or o.get('error'), st.get('open'), left[:8], ja))
    ui('close')
finally:
    w.stop()
n = sum(1 for c in t.checks if c['ok'])
json.dump(t.checks, open(os.path.join(OUT, 'checks.json'), 'w'), indent=1, ensure_ascii=False)
print(f'{n}/{len(t.checks)} checks', flush=True)
sys.exit(0 if n == len(t.checks) else 1)
