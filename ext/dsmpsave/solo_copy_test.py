#!/usr/bin/env python3
"""dsmpsave lane: the single-player copy of a world new to multiplayer (owner 2026-10-25: "when logging in multiplayer into
a world for the first time, i should have the option to create a backup of it which stays single player").
ONE copy (the host; the lease's first copy), started only while the 1-minute load is under 15 (lead's rule while the owner
plays). Worlds: copies of the wiki base save with their own world ids, in the copy's data/Saves: NEW (never in
multiplayer), TAGGED (a line in mp_tags.txt, like a world a friend joined).
Flow: Multiplayer > Host > Your save, pick NEW (load_save det 0 = a click in the stock list) -> the box "Keep a single-player
copy of this world?" while the session waits -> Yes -> the session starts -> back to the title; host NEW again (no box);
host TAGGED (no box); load the copy alone (single player) and look at it.
Real breaks caught:
  - the session starts before the answer (the copy could hold multiplayer progress);
  - no box for a world new to multiplayer; the box again for the same world; a box for a world with the tag;
  - the copy is not the world as it was (any value changed), keeps the original's world id, has a multiplayer tag, is not
    next to the original, or the original save changed;
  - the game cannot load the copy, or loads another world (other units).
Usage: solo_copy_test.py [--dylib PATH] [--src DIR] [--lang N] [--shot DIR] [--no]
  default: this worktree's build/dsrig.dylib and src/, English
  --lang N   the copy's game language (LanguageType: 0 en, 3 de, 4 ru, 7 ja ...); --shot DIR: a game frame of the box there
  --no       answer No instead: no copy, never asked again for that world (then stop: no TAGGED / copy steps)
Output: PASS / FAIL per check, then RESULT PASS|FAIL <n> PASS, <m> FAIL. Logs: ext/dsmpsave/out/solocopy_<time>/."""
import json, os, shutil, signal, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

ARG = lambda k, d: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d
rig.DYLIB = os.path.abspath(os.path.expanduser(ARG('--dylib', os.path.join(W, 'build', 'dsrig.dylib'))))
SRC = os.path.abspath(os.path.expanduser(ARG('--src', W)))
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # the wiki base save (public)
LANE = 'dsmpsave-solo'
LANG = int(ARG('--lang', '0')); SHOT = ARG('--shot', None); ANSWER_NO = '--no' in sys.argv
CODE = {0: 'en', 1: 'ko', 2: 'fr', 3: 'de', 4: 'ru', 5: 'zh-Hans', 6: 'zh-Hant', 7: 'ja', 8: 'es', 9: 'pt-BR'}
_fmt = json.load(open(os.path.join(W, 'lang', CODE[LANG] + '.json'))).get('%s (solo)', '%s (solo)') if LANG else '%s (solo)'
COPY_NAME = _fmt.replace('%s', 'Solo Test')            # the copy's name in the copy's language
LOAD_MAX, LOAD_WAIT_S, LEASE_WAIT_S = 15.0, 20 * 60, 20 * 60
NEW_G, TAG_G = '5010c0de-0000-4000-8000-00000000a001', '5010c0de-0000-4000-8000-00000000a002'
NEW_STEM = 'SoloTest__AutoSave_' + NEW_G.replace('-', '') + '_0'
TAG_STEM = 'TagTest__AutoSave_' + TAG_G.replace('-', '') + '_0'
OUT = os.path.join(HERE, 'out', 'solocopy_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
os.environ.setdefault('DSRIG_CUSTOM', '0')        # no Units Laboratory: nothing here needs it (as the gate's noul runs)
_prep = rig.prepare_data
def _prep_lang(x):                                   # test copies only: their own UserSetting.json
    _prep(x)
    us = os.path.join(rig.idir(x), 'data', 'UserSetting.json'); u = json.load(open(us))
    u['GeneralSettingData']['SaveCurrentLanguageType'] = LANG; u['GeneralSettingData']['IsLanguageInitialized'] = True
    json.dump(u, open(us, 'w'), indent=1)
rig.prepare_data = _prep_lang
LOG = open(os.path.join(OUT, 'run.log'), 'a'); results = []


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def say(s):
    print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); say(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info and not ok else ''))
    if info and ok: log('  info:', info)


def result_line(extra=''):
    n = sum(1 for _, ok in results if not ok)
    say(f'RESULT {"PASS" if results and not n else "FAIL"} {len(results) - n} PASS, {n} FAIL' + (f'  ({extra})' if extra else ''))


def world_save(path, guid, stem, clan):
    w = json.load(open(TPL))
    h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    h['CampaignGuid'] = cl['CampaignGuid'] = guid
    h['SaveName'] = stem; h['ClanName'] = cl['ClanName'] = clan; h['Version'] = 'DS_B.0.4.29'   # this build's version: no version box
    css = w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['CampaignName'] = clan; css['LockConsecrationNoticeAcknowledged'] = True; css.setdefault('LockConsecrationEnabled', False)
    json.dump(w, open(path, 'w'), indent=2)


def identity_out(w):
    """the world without what the copy changes on purpose (world id, names, SaveName)"""
    w = json.loads(json.dumps(w)); h, cl = w['CampaignSaveHeader'], w['ClanSaveData']
    for k in ('CampaignGuid', 'ClanName', 'SaveName'): h.pop(k, None)
    for k in ('CampaignGuid', 'ClanName'): cl.pop(k, None)
    w.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData', {}).pop('CampaignName', None)
    return w


def run(H, ctx):
    B = json.loads(subprocess.run([sys.executable, os.path.join(W, 'ext', 'fepvp13', 'tools', 'mpm_ids.py'), SRC],
                                  capture_output=True, text=True, check=True).stdout)
    c = lambda **k: rig.send(H, k, timeout=180)
    st = lambda: c(cmd='status')
    ms = lambda: c(cmd='mpm_status')
    press = lambda n: (c(cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
    on_title = lambda s: s.get('scene') == 1 and not s.get('busy')
    saves = os.path.join(rig.idir(H), 'data', 'Saves')
    rl = os.path.join(rig.idir(H), 'rig.log')

    def wait(what, pred, timeout=600, every=2):
        t1 = time.time()
        while time.time() - t1 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def rig_log_since(off):
        with open(rl, 'rb') as f: f.seek(off); return f.read().decode('utf-8', 'replace')

    def host(stem):
        """Multiplayer > Host (Your save) > the load list > pick (det off). Returns the rig.log offset before the pick."""
        wait('menu idle on the title', lambda: on_title(st()) and ms().get('state') in ('idle', 'title_menu'), timeout=120)
        press('MULTIPLAYER'); time.sleep(1); press('HOST'); time.sleep(1); press('LOAD_SAVE'); time.sleep(2)
        off = os.path.getsize(rl)
        log('pick', stem, c(cmd='load_save', file=os.path.join(saves, stem + '.json'), det=0))
        return off

    def to_title():
        c(cmd='mp_quit', mode='title'); rig.wait_for(H, on_title, 'title', timeout=300)
        wait('menu idle', lambda: ms().get('state') in ('idle', 'title_menu'), timeout=120); time.sleep(3)

    rig.start(H, fps=30, window=True)
    rig.wait_for(H, on_title, 'title', timeout=1500); time.sleep(3)
    files0 = set(os.listdir(saves)); new_bytes = open(os.path.join(saves, NEW_STEM + '.json'), 'rb').read()

    # 1. NEW: the box, the session waits, Yes
    off = host(NEW_STEM)
    m = wait('the single-player copy box', lambda: (lambda r: r if r.get('solo', {}).get('state') == 2 and r.get('box', {}).get('on') else None)(ms()), timeout=600)
    check('NEW (never in multiplayer): the box asks before the session starts', 'Solo Test' in m['box'].get('text', '') and COPY_NAME in m['box'].get('text', ''), m['box'].get('text', '')[:160])
    if SHOT:
        os.makedirs(SHOT, exist_ok=True); time.sleep(2)
        log('shot', c(cmd='mpm_shot', how='backbuffer', path=os.path.join(SHOT, f'solo_box_{CODE[LANG]}.png')))
    time.sleep(5)
    s = st(); m = ms()
    check('the session waits while the box is up (no host session, menu still loading)', not s.get('mp', {}).get('started') and m.get('state') == 'host_loading',
          f"mp started {s.get('mp', {}).get('started')}, menu {m.get('state')}")
    units0 = sorted((u['guid'], u['name']) for u in c(cmd='list_units', map=0)['units'] if u['faction'] == 1)
    log('NEW units', units0)
    if ANSWER_NO:
        log('answer No', c(cmd='mpm_box', action='cancel'))
        wait('host session after the answer', lambda: st().get('mp', {}).get('started'), timeout=300)
        time.sleep(3)
        made = sorted(f for f in set(os.listdir(saves)) - files0 if f.endswith('.json') and not f.startswith('mp_'))
        sl = open(os.path.join(rig.idir(H), 'mp_solo.txt')).read() if os.path.exists(os.path.join(rig.idir(H), 'mp_solo.txt')) else ''
        check('after No: the session starts, no copy is made, the answer is kept', not made and ms().get('solo', {}).get('made') == 0 and f'{NEW_G}\tno' in sl, f'{made} / {sl!r}')
        to_title()
        n0 = ms().get('solo', {}).get('asked'); off = host(NEW_STEM)
        wait('host session (NEW again)', lambda: st().get('mp', {}).get('started'), timeout=600); time.sleep(3)
        check('after No, NEW again: no box', ms().get('solo', {}).get('asked') == n0 and f'not asked for world {NEW_G} (asked before)' in rig_log_since(off), json.dumps(ms().get('solo')))
        ctx['done'] = True
        return
    log('answer Yes', c(cmd='mpm_box', action='confirm'))
    wait('host session after the answer', lambda: st().get('mp', {}).get('started'), timeout=300)
    check('after Yes: the session starts with the original world', ms().get('solo', {}).get('made') == 1, json.dumps(ms().get('solo')))
    copies = sorted(f for f in set(os.listdir(saves)) - files0 if f.endswith('.json') and not f.startswith('mp_'))
    log('new files in data/Saves:', copies)
    check('one copy, next to the original (same folder)', len(copies) == 1, str(copies))
    if copies:
        ctx['copy'] = copies[0]
        cw, ow = json.load(open(os.path.join(saves, copies[0]))), json.load(open(os.path.join(saves, NEW_STEM + '.json')))
        cg = cw['CampaignSaveHeader']['CampaignGuid']; ctx['copy_guid'] = cg
        check('the original save did not change', open(os.path.join(saves, NEW_STEM + '.json'), 'rb').read() == new_bytes)
        check('the copy is a new world: own world id (header = clan record), not the original\'s anywhere',
              cg != NEW_G and cw['ClanSaveData']['CampaignGuid'] == cg and NEW_G not in open(os.path.join(saves, copies[0])).read()
              and NEW_G.replace('-', '') not in open(os.path.join(saves, copies[0])).read(), cg)
        check('the copy is named "<world> (solo)" and loads by its file name', cw['CampaignSaveHeader']['ClanName'] == COPY_NAME
              and cw['CampaignSaveHeader']['SaveName'] + '.json' == copies[0], f"{cw['CampaignSaveHeader']['ClanName']} / {cw['CampaignSaveHeader']['SaveName']}")
        check('the copy is the world as it was (every other value equal)', identity_out(cw) == identity_out(ow))
        tags = open(os.path.join(rig.idir(H), 'mp_tags.txt')).read() if os.path.exists(os.path.join(rig.idir(H), 'mp_tags.txt')) else ''
        check('the copy has no multiplayer tag', cg not in tags)
    to_title()

    # 2. NEW again: asked once only
    n0 = ms().get('solo', {}).get('asked')
    off = host(NEW_STEM)
    wait('host session (NEW again)', lambda: st().get('mp', {}).get('started'), timeout=600)
    time.sleep(3)
    check('NEW again: no box (asked once per world)', ms().get('solo', {}).get('asked') == n0 and f'not asked for world {NEW_G} (asked before)' in rig_log_since(off),
          json.dumps(ms().get('solo')))
    to_title()

    # 3. TAGGED: a world a friend was in never asks
    off = host(TAG_STEM)
    wait('host session (TAGGED)', lambda: st().get('mp', {}).get('started'), timeout=600)
    time.sleep(3)
    check('TAGGED (has the Multiplayer tag): no box', ms().get('solo', {}).get('asked') == n0 and f'not asked for world {TAG_G} (it has the Multiplayer tag)' in rig_log_since(off),
          json.dumps(ms().get('solo')))
    to_title()

    # 4. the copy alone: the game loads it, and it is the copy (its id, the same units)
    if ctx.get('copy'):
        log('load the copy', c(cmd='load_save', name=ctx['copy'][:-5], det=0))
        rig.wait_for(H, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', 0) > 0, 'the copy loaded', timeout=600)
        time.sleep(3)
        units1 = sorted((u['guid'], u['name']) for u in c(cmd='list_units', map=0)['units'] if u['faction'] == 1)
        c(cmd='fe_save', name='mp_solo_check'); time.sleep(2)
        p = os.path.join(saves, 'mp_solo_check.json'); hd = json.load(open(p))['CampaignSaveHeader'] if os.path.exists(p) else {}
        if os.path.exists(p): shutil.move(p, os.path.join(OUT, 'mp_solo_check.json'))
        check('the game loads the copy alone: its world id, its name, the same units as the original',
              hd.get('CampaignGuid') == ctx.get('copy_guid') and hd.get('ClanName') == COPY_NAME and units1 == units0 and units0,
              f"guid {hd.get('CampaignGuid')} name {hd.get('ClanName')} units {len(units1)}/{len(units0)}")
        tags = open(os.path.join(rig.idir(H), 'mp_tags.txt')).read() if os.path.exists(os.path.join(rig.idir(H), 'mp_tags.txt')) else ''
        check('the copy still has no multiplayer tag after its own load', ctx.get('copy_guid', '?') not in tags)
    ctx['done'] = True


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))
    log('dylib', rig.DYLIB, '| out', OUT)
    t0 = time.time()
    while os.getloadavg()[0] >= LOAD_MAX:
        if time.time() - t0 >= LOAD_WAIT_S:
            result_line(f'setup: 1-minute load stayed at {LOAD_MAX:.0f} or more'); sys.exit(2)
        log(f'1-minute load {os.getloadavg()[0]:.1f}: waiting 60 s'); time.sleep(60)
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < LEASE_WAIT_S:
        pair = rig.lease(LANE, os.getpid())
        if not pair: log('no free pair: waiting 30 s'); time.sleep(30)
    if not pair:
        result_line('setup: no free pair'); sys.exit(2)
    H = pair[0]; ctx = {}; moved = []
    log('pair', pair, '(only', H, 'runs)')
    try:
        sp = os.path.realpath(os.path.join(rig.idir(H), 'data', 'Saves')); inst = os.path.realpath(rig.INST)
        if not (sp.startswith(inst + os.sep) and 'DSModLauncher' not in sp and 'CanOpener' not in sp):
            raise SystemExit(f'{H}: data/Saves is not under the rig instances: {sp}')
        log(H, 'data/Saves =', sp, '(under the rig instances)')
        root = rig.idir(H); aside = os.path.join(OUT, 'aside'); os.makedirs(aside)
        for f in ('mp.json', 'mp_solo.txt'):                    # a session file would host at boot; earlier answers
            if os.path.exists(os.path.join(root, f)): shutil.move(os.path.join(root, f), os.path.join(aside, f)); moved.append(f)
        os.makedirs(sp, exist_ok=True)
        for f in os.listdir(sp):                                # earlier runs' test worlds and copies
            if f.startswith(('SoloTest__', 'TagTest__')): shutil.move(os.path.join(sp, f), os.path.join(aside, f))
        tp = os.path.join(root, 'mp_tags.txt')
        if os.path.exists(tp): shutil.copy2(tp, os.path.join(aside, 'mp_tags.txt'))
        kept = [l for l in (open(tp).read().splitlines() if os.path.exists(tp) else []) if not l.startswith('5010c0de')]
        open(tp, 'w').write('\n'.join(kept + [TAG_G + '\tAlice\tBob']) + '\n')
        world_save(os.path.join(sp, NEW_STEM + '.json'), NEW_G, NEW_STEM, 'Solo Test')
        world_save(os.path.join(sp, TAG_STEM + '.json'), TAG_G, TAG_STEM, 'Tag Test')
        os.utime(os.path.join(sp, TAG_STEM + '.json'), (time.time() - 60, time.time() - 60))
        run(H, ctx)
    except Exception as e:
        log('ERROR', repr(e)[:300]); LOG.write(traceback.format_exc()); LOG.flush(); ctx['err'] = repr(e)[:160]
    finally:
        rig.stop(H)
        check('setup: every step ran', ctx.get('done'), ctx.get('err', 'stopped early'))
        if 'mp.json' in moved: shutil.copy2(os.path.join(OUT, 'aside', 'mp.json'), os.path.join(rig.idir(H), 'mp.json'))   # other tests read it
        for f in ('player.log', 'rig.log'):
            p = os.path.join(rig.idir(H), f)
            if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{H}_{f}'))
        log('released', rig.release(pair, LANE), '| out:', OUT)
        result_line()


if __name__ == '__main__':
    main()
