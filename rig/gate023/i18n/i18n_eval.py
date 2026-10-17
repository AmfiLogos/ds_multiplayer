#!/usr/bin/env python3
"""i18n_eval.py <out dir> [--pair <host code> <guest code>] [--player-log <file>]...: pass / fail lines for an i18n game run
(cycle.py or pairtest2.py output). Checks:
  miss    every missing-key log (DSI_MISS_LOG / DSRIG_MISS_LOG: *miss*.tsv*) is empty
  glyphs  no TMP "was not found in the ... font asset" warning in the player.log files (a box on screen)
  fonts   for every language the loader set up, its body font is the game's own choice (LanguageSetting.GetFont)
  english no text on screen equals an English key of the loader / multiplayer / Custom Modes tables while the active
          language has another text for it (a mod text left in English)
Views: cycle.py names them s<n>_* (start language) and <code>_* (after the switch to <code>); pairtest2.py C_* / D_* (the
pair's first letter = host). Last line: "RESULT n/n pass"."""
import glob, json, os, re, sys
A = sys.argv[1:]
OUT = os.path.abspath(A[0]); PAIR = None; PLOGS = []
if '--pair' in A: i = A.index('--pair'); PAIR = (A[i + 1], A[i + 2])
for i, a in enumerate(A):
    if a == '--player-log': PLOGS.append(A[i + 1])
H = os.path.expanduser('~')
TABLES = [f'{H}/ds_ld_ship023t/loader/lang', f'{H}/ds_mp_ship023t/lang', f'{H}/ds_cm_ship023t/lang']
CODES = ['en', 'ko', 'fr', 'de', 'ru', 'zh-Hans', 'zh-Hant', 'ja', 'es', 'pt-BR']
FONT = {'ja': 'NotoSerifJP', 'zh-Hans': 'NotoSerifSC', 'zh-Hant': 'SourceHanSerifTC'}   # others: the game's Lora
res = []
def check(n, ok, d=''): res.append(bool(ok)); print(('PASS ' if ok else 'FAIL ') + n + ('' if ok or not d else f': {str(d)[:600]}'), flush=True)

# miss
miss = [p for p in glob.glob(f'{OUT}/**/*miss*.tsv*', recursive=True) if os.path.isfile(p)]
bad = {os.path.basename(p): open(p, errors='replace').read().splitlines()[:5] for p in miss if os.path.getsize(p) > 0}
check(f'miss: {len(miss)} missing-key log(s), all empty', not bad, bad)

# glyphs (pair: the host name carries U+E000, in no font: its warning proves the game logs missing glyphs)
logs = PLOGS + glob.glob(f'{OUT}/**/player*.log', recursive=True)
if PAIR and not PLOGS:
    for x in sorted({os.path.basename(p)[0] for p in glob.glob(f'{OUT}/*.texts.json')}): logs.append(f'{H}/ds_multiplayer/instances/{x}/player.log')
hits, ctl = [], 0
for p in logs:
    for l in (open(p, errors='replace') if os.path.exists(p) else []):
        if 'was not found in the' in l and 'font asset' in l:
            if re.search(r'(?i)\\?u?E000', l): ctl += 1
            else: hits.append(l.strip()[:200])
if PAIR: check(f'glyphs control: the U+E000 in the host name was reported ({ctl} line(s))', ctl > 0, 'no warning: the game does not log missing glyphs, the glyph check proves nothing')
check(f'glyphs: no missing-glyph warning in {len(logs)} player log(s)', logs and not hits, hits[:5] if logs else 'no player.log')

# fonts
dl = ''.join(open(p, errors='replace').read() for p in glob.glob(f'{OUT}/**/dsload*.log', recursive=True))
seen = re.findall(r'ui: body text font from [^(]*\(([\w-]+)\), material "([^"]+)"', dl)
wrong = sorted({(c, m) for c, m in seen if not m.startswith(FONT.get(c, 'Lora'))})
if not PAIR: check(f'fonts: {len({c for c, _ in seen})} language(s) set up, each with the game\'s font', seen and not wrong, wrong or 'no font line')

# english
en = {}; tr = {c: {} for c in CODES}
for d in TABLES:
    for c in CODES:
        p = os.path.join(d, f'{c}.json')
        if os.path.exists(p): tr[c].update(json.load(open(p)))
STOCK = {'RenameUnitUI', 'Tutorial', 'Transform_ScaleAdjust'}   # stock objects with English placeholder text (not ours)
strip = lambda t: re.sub(r'<[^>]+>', '', t).strip()
def lang_of(fn):
    b = os.path.basename(fn)
    if PAIR: return PAIR[0] if b[0] == HOSTL else PAIR[1]
    for c in sorted(CODES, key=len, reverse=True):
        if b.startswith(c + '_'): return c
    return START
dumps = sorted(glob.glob(f'{OUT}/*.texts.json'))
if PAIR: HOSTL = min(os.path.basename(p)[0] for p in dumps) if dumps else 'C'
else:
    m = re.search(r'language (\d) \(([\w-]+)\) from', dl); START = m.group(2) if m else 'en'
left = []
for p in dumps:
    L = lang_of(p)
    if L == 'en': continue
    try: texts = json.load(open(p))
    except Exception: continue
    for t in texts:
        if t.get('path', '').split('/')[0] in STOCK: continue   # the game's hidden templates keep their English placeholders
        s = strip(t.get('text', ''))
        if len(s) < 3 or not re.search('[A-Za-z]{3}', s): continue
        if s in tr['en'] and isinstance(tr[L].get(s), str) and tr[L][s] != s: left.append((os.path.basename(p), L, s[:60]))
check(f'english: no mod text left in English in {len(dumps)} view(s)', dumps and not left, sorted(set(left))[:12] if dumps else 'no views')
print(f'RESULT {sum(res)}/{len(res)} pass')
sys.exit(0 if all(res) else 1)
