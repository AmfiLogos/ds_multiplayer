#!/usr/bin/env python3
"""Multiplayer page without Custom Modes: the PvP entry is still shown, greyed, and a click does nothing.
One leased pair, hidden copies, both started with DSRIG_CUSTOM=0 (no Custom Modes), one language per copy.

  pvp_entry_test.py --dylib PATH <lang code> <lang code>      (0 en, 7 ja, ...)

Break it catches: the entry missing (players cannot find PvP), or a click on it opening a host page for a mode that
is not there. With Custom Modes on, ext/mpgroups/groups_test.py checks the real PvP button.
Frames: ext/mpsub/out/<lang>_mp_page_no_cm.png."""
import json, os, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1])
L1, L2 = int(A[-2]), int(A[-1])
CODES = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out'); os.makedirs(OUT, exist_ok=True)
LANE = f'mpsub-{os.getpid()}'
MP, BACK, SLOTS = 1, 3, (123, 124, 125, 126)    # host choice buttons; the greyed PvP entry takes the first free one
LANGS, results = {}, {}
os.environ['DSRIG_CUSTOM'] = '0'

prep0 = rig.prepare_data
def prepare_lang(x):
    prep0(x)
    p = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
    s['GeneralSettingData']['SaveCurrentLanguageType'] = LANGS[x]; s['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(s, open(p, 'w'), indent=1)
rig.prepare_data = prepare_lang


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)


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
    for x in copies:                                     # a left-over mp.json would start a join (as groups_test.py)
        for f in ('mp.json', 'mp_menu.json'):
            p = os.path.join(rig.idir(x), f)
            if os.path.exists(p): os.replace(p, f'{p}.before_mpsub_{stamp}')
    log('pair', pair, 'langs', CODES[L1], CODES[L2])
    try:
        for x in copies: rig.start(x, window=True, fps=30)
        for x in copies: rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(4)
        for x in copies:
            lg = CODES[LANGS[x]]
            c(x, cmd='mpm_press', button=MP); time.sleep(1.5); s = ms(x)
            tb = s.get('title', {})
            pv = [b for b in SLOTS if tb.get(str(b), {}).get('active') and 'PvP' in str(tb.get(str(b), {}).get('text'))]
            e0 = tb.get(str(pv[0]), {}) if pv else {}
            c(x, cmd='mpm_shot', path=os.path.join(OUT, f'{lg}_mp_page_no_cm.png'), how='backbuffer')
            check(f'{lg}: no Custom Modes: one PvP entry shown, greyed', len(pv) == 1 and '#A69085' in str(e0.get('text')),
                  f'PvP slots {pv}: ' + json.dumps({b: tb.get(str(b)) for b in SLOTS}, ensure_ascii=False))
            if pv: c(x, cmd='mpm_press', button=pv[0])
            time.sleep(1.5); s2 = ms(x)
            check(f'{lg}: a click on the greyed PvP entry does nothing (stays on the Multiplayer page)', s2.get('state') == 'title_menu', f'state {s2.get("state")}')
            c(x, cmd='mpm_press', button=BACK); time.sleep(1)
    except Exception as e:
        log('ERROR', repr(e)); results['run'] = False
    finally:
        rig.release(pair, LANE)
        log('result', sum(results.values()), 'of', len(results))
    return 0 if results and all(results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
