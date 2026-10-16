#!/usr/bin/env python3
"""fepvp: one copy, through the menu: Multiplayer > Foreign Expedition > Start -> the neutral hub (wiki base template,
research done, regions open) loads and the lobby opens. No "host from a save / new campaign" step.
  hub_smoke.py [--dylib PATH] [--keep]
Writes ext/fepvp13/out/hub_smoke_<time>/ (log, the host's save of the hub)."""
import json, os, shutil, sys, time
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1]) if '--dylib' in A else os.path.join(W, 'build', 'dsrig.dylib')
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # test copy of the wiki file
OUT = os.path.join(W, 'ext', 'fepvp13', 'out', 'hub_smoke_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
LOG = open(os.path.join(OUT, 'test.log'), 'a'); results = []
B = json.loads(os.popen(f'python3 {W}/ext/fepvp13/tools/mpm_ids.py {W}').read())


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a); print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def check(name, ok, info=''):
    results.append((name, bool(ok))); log(('PASS ' if ok else 'FAIL ') + name + (f'  [{info}]' if info else ''))


def main():
    pair = None
    while not pair:
        pair = rig.lease('fepvp', os.getpid())
        if not pair: log('no free pair, waiting'); time.sleep(30)
    H = pair[0]; log('pair', pair, 'dylib', rig.DYLIB)
    c = lambda **k: rig.send(H, k, timeout=180)
    press = lambda n: (c(cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
    try:
        p = os.path.join(rig.idir(H), 'mp.json')
        if os.path.exists(p): os.replace(p, p + '.last')
        cache = os.path.join(rig.idir(H), 'data', 'Mods', 'custom-modes', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
        shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
        rig.start(H, fps=30, window=True)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500); time.sleep(3)
        press('MULTIPLAYER'); press('HOST_FE')
        ms = c(cmd='mpm_status'); log('host page', ms.get('state'), json.dumps(ms.get('settings')))
        check('host page: Foreign Expedition, death permanent Off by default', ms.get('state') == 'title_host' and ms['settings']['fe'] == 1 and ms['settings']['fe_death'] == 0, json.dumps(ms['settings']))
        c(cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'host_page.png'))
        press('LOAD_SAVE')
        t0 = time.time()
        rig.wait_for(H, lambda s: s.get('mp', {}).get('started') and s.get('det', {}).get('locked'), 'hub loaded + host session', timeout=900)
        log('hub up in %.0f s' % (time.time() - t0))
        ms = c(cmd='mpm_status'); st = c(cmd='status'); log('menu', ms.get('state'), '| status', json.dumps(st)[:500])
        check('lobby open on the hub (no load list, no new campaign screen)', ms.get('state') == 'lobby', ms.get('state'))
        c(cmd='mpm_shot', how='backbuffer', path=os.path.join(OUT, 'lobby.png'))
        chk = os.path.join(rig.idir(H), 'data', 'Saves', 'fepvp_hub_check.json')
        if os.path.exists(chk): os.replace(chk, os.path.join(OUT, 'old_hub_check.json'))
        r = c(cmd='mp_save', name='fepvp_hub_check'); log('mp_save (the game saves the running hub)', r); time.sleep(5)
        saves = [chk] if os.path.exists(chk) else []
        if saves:
            d = json.load(open(saves[-1])); shutil.copy2(saves[-1], os.path.join(OUT, 'hub_save.json'))
            rd = d['ResearchSaveData']['ResearchDatas']
            done = sum(1 for v in rd.values() if not v['IsLocked'] and v['Workload'] >= v['RequiredWorkload'])
            reg = [r['Region'] for r in d['ClanSaveData']['ExpeditionMapData'] if not r['IsLocked']]
            check('hub: every research done', done == len(rd) and done > 30, f'{done}/{len(rd)}')
            check('hub: every region open', len(reg) >= 10, str(reg))
            check('hub: version = the game version, iron off', d['CampaignSaveHeader']['Version'].startswith('DS_B.0.4') and not d['CampaignSaveHeader']['IronModeEnabled'], d['CampaignSaveHeader']['Version'])
        else:
            check('hub save found', False)
    finally:
        if '--keep' not in A: rig.stop(H)
        p = os.path.join(rig.idir(H), 'mp.json')
        if os.path.exists(p + '.last'): shutil.copy2(p + '.last', p)
        for f in ('player.log', 'rig.log'):
            if os.path.exists(os.path.join(rig.idir(H), f)): shutil.copy2(os.path.join(rig.idir(H), f), os.path.join(OUT, f'{H}_{f}'))
        if '--keep' not in A: log('released', rig.release(pair, 'fepvp'))
        n = sum(1 for _, ok in results if not ok)
        log(f'{len(results) - n}/{len(results)} PASS' + (f', {n} FAIL' if n else ''), '| out:', OUT, '| prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')


if __name__ == '__main__':
    main()
