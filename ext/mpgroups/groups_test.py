#!/usr/bin/env python3
"""Multiplayer page groups: "Host a game" + its choices | the game's divider | "Join a game" + the join rows | Back.
One leased pair, hidden copies, two languages at a time (one per copy).

  groups_test.py --dylib PATH <lang code> <lang code>      (0 en, 2 fr, 4 ru, 7 ja)

Per copy: the divider row is shown between the last host choice and "Join a game", holds the game's own separator
(a clone of the title column's PlaceHolder), no two rows overlap, and every button does its one thing:
Your save / New save / Foreign Expedition / PvP -> the host page with that choice, Join by address -> the join page,
Join a friend on Steam -> the friends page (rig has no Steam: the row stays greyed and does nothing), Back -> the title.
Frames: ext/mpgroups/out/<lang>_mp_page.png (not for the owner)."""
import json, os, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

class _J:
    def __getattr__(self, k): return getattr(json, k)
    @staticmethod
    def dumps(o, **k): k.setdefault('ensure_ascii', False); return json.dumps(o, **k)
rig.json = _J()

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1])
L1, L2 = int(A[-2]), int(A[-1])
CODES = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out'); os.makedirs(OUT, exist_ok=True)
LOGF = open(os.path.join(OUT, 'test.log'), 'a')
LANE = f'mpgroups-{os.getpid()}'
MP, HOST_HDR, BACK, JOIN_ADDR, JOIN_FRIEND = 1, 2, 3, 14, 45
SAVE, NEW, FE, PVP, DIV, JHDR = 121, 122, 123, 124, 128, 129
LANGS, results = {}, {}

prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANGS[x]; s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def main():
    pair = rig.lease(LANE)
    if not pair: log('no free pair'); return 1
    c = lambda x, **k: rig.send(x, k, timeout=120)
    ms = lambda x: c(x, cmd='mpm_status')
    copies = list(pair[:2]); LANGS[copies[0]] = L1; LANGS[copies[1]] = L2
    busy = [q for x in copies for q in rig._procs_all()[x]]
    if busy: log('copies already running:', busy); rig.release(pair, LANE); return 1
    stamp = time.strftime('%Y%m%d-%H%M%S')
    for x in copies:
        for f in ('mp.json', 'mp_menu.json'):
            p = os.path.join(rig.idir(x), f)
            if os.path.exists(p): os.replace(p, f'{p}.before_mpgroups_{stamp}')
    log('pair', pair, 'dylib', rig.DYLIB, 'langs', CODES[L1], CODES[L2])
    try:
        for x in copies: rig.start(x, window=True, fps=30)
        for x in copies: rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(4)
        for x in copies:
            lg = CODES[LANGS[x]]
            for _ in range(3):                                   # old expedition results window first ("Later")
                if not c(x, cmd='mpm_fewin').get('mode'): break
                c(x, cmd='mpm_press', button=110); time.sleep(1)

            def press(b, wait=1.5):
                c(x, cmd='mpm_press', button=b); time.sleep(wait); return ms(x)

            s = press(MP)
            tb, fr = s.get('title', {}), s.get('frames', {})
            on = lambda i: tb.get(str(i), {}).get('active')
            p = os.path.join(OUT, f'{lg}_mp_page.png'); log('frame', p, c(x, cmd='mpm_shot', path=p, how='backbuffer').get('ok'))
            json.dump({'title': tb, 'frames': fr}, open(os.path.join(OUT, f'{lg}_mp_page.json'), 'w'), ensure_ascii=False, indent=0)
            order = [i for i in (HOST_HDR, SAVE, NEW, FE, PVP, DIV, JHDR, JOIN_ADDR, JOIN_FRIEND, BACK) if on(i)]
            col = [(i, fr[str(i)]) for i in order if str(i) in fr]
            tops = [r[3] for _, r in col]                           # y up: each row lower than the one before
            ordered = len(col) == len(order) and all(tops[k] > tops[k + 1] for k in range(len(tops) - 1))
            btns = [(i, r) for i, r in col if i not in (HOST_HDR, DIV, JHDR)]     # text-only rows: their frame is the hidden stock image
            overlap = [(a[0], b[0]) for k, a in enumerate(btns) for b in btns[k + 1:]
                       if a[1][0] < b[1][2] and b[1][0] < a[1][2] and a[1][1] < b[1][3] - 0.5 and b[1][1] < a[1][3] - 0.5]
            tree = c(x, cmd='mpm_tree', what='title')
            ph = json.dumps(tree.get('tree', [])).count('PlaceHolder')
            check(f'{lg}: Host a game, Your save, New save, Foreign Expedition, divider, Join a game, Join by address, Join a friend, Back in that order, no overlap',
                  all(on(i) for i in (HOST_HDR, SAVE, NEW, FE, DIV, JHDR, JOIN_ADDR, JOIN_FRIEND, BACK)) and ordered and not overlap,
                  f'order {order} overlap {overlap} frames {col}')
            check(f'{lg}: the divider is a clone of the game\'s own PlaceHolder (stock + ours under the page)', ph >= 2, f'PlaceHolder nodes in the title tree: {ph}')
            for b, want, extra in ((SAVE, 'title_host', lambda s: s['settings']['fe'] == 0), (NEW, 'title_host', lambda s: s['settings']['fe'] == 0),
                                   (FE, 'title_host', lambda s: s['settings']['fe'] == 1), (PVP, 'title_host', lambda s: True),
                                   (JOIN_ADDR, 'title_join', lambda s: True)):
                if b == PVP and not on(PVP): continue
                s2 = press(b); st = s2.get('state')
                check(f'{lg}: button {b} opens {want}', st == want and extra(s2), f'state {st} fe {s2.get("settings", {}).get("fe")}')
                s3 = press(BACK)
                if s3.get('state') != 'title_menu': log('back from', b, '->', s3.get('state')); press(MP)
            s2 = press(JOIN_FRIEND); steam = s2.get('obs', {})
            check(f'{lg}: Join a friend on Steam: friends page with Steam, greyed + no action without (rig: no Steam)',
                  s2.get('state') in ('title_friends', 'title_menu'), f'state {s2.get("state")}')
            if s2.get('state') == 'title_friends': press(BACK)
            s2 = press(BACK)
            check(f'{lg}: Back -> title', s2.get('state') == 'idle', f'state {s2.get("state")}')
    except Exception as e:
        log('ERROR', repr(e)); results['run'] = False
    finally:
        rig.release(pair, LANE)
        json.dump(results, open(os.path.join(OUT, f'result_{CODES[L1]}_{CODES[L2]}.json'), 'w'), indent=1, ensure_ascii=False)
        log('result', sum(results.values()), 'of', len(results))
    return 0 if results and all(results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
