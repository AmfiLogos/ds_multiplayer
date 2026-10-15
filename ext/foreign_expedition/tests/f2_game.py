#!/usr/bin/env python3
"""F2: one game copy through the rig (leased pair, first copy only), with build/dsrig_fe.dylib (adds fe_stats).
  1. load the HOME save (copy of Elven Expedition), read every stat of the 2 travelling units
  2. build the hub (copy of the MP Moonlight save + the 2 units), load it, read the stats again, screenshot
  3. the game saves the hub; extract results (offline: +2 levels, +3 Strength on unit 0, to see a change), apply
  4. load the new home save, read the stats, check level / strength / marker; the game saves it again: marker kept?
Writes only into out/f2_<time>/ and the copy's own instance folder. Run: python3 tests/f2_game.py [--window]"""
import copy, glob, json, os, shutil, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import fe_save as fe
import rig

rig.DYLIB = os.path.join(ROOT, 'build', 'dsrig_fe.dylib')
STATS = ['Strength', 'Constitution', 'WillPower', 'Intelligence', 'Agility', 'Perception', 'MaxHealthHead', 'MaxHealthBody',
         'HealthTotal', 'Barrier', 'MaxExp', 'Weight', 'HealthHead', 'HealthBody', 'Energy', 'Hunger', 'Stress', 'Level', 'Exp',
         'MainSkillPoint', 'SubSkillPoint', 'MaxHealthTotal', 'PhysicalAttackPower', 'AttackSpeed', 'CriticalChance',
         'CriticalDamageBonus', 'MagicAttackPower', 'CooldownReduction', 'HitChance', 'DodgeChance', 'GuardChance',
         'GuardDamageReduction', 'LifeSteal', 'DamageDealtBonus', 'DamageTakenBonus', 'PhysicalDamageDealtBonus',
         'MagicDamageDealtBonus', 'PhysicalResistance', 'MagicResistance', 'Mood', 'MaxEnergy', 'MaxHunger', 'MaxStress',
         'MaxWeight', 'DespairOvercomeChance', 'MovementSpeed', 'VisionRange', 'HealthPercentageRegeneration',
         'EnergyRegeneration', 'HungerRegeneration', 'MiningCapability', 'LoggingCapability', 'GatheringCapability',
         'ConstructionCapability', 'TamingCapability', 'ResearchCapability', 'CraftingCapability', 'FarmingCapability',
         'CookingCapability', 'RecreationCapability', 'ExpGainedBonus', 'StressModifier']   # 1..62; 63+ kept by index
OUT = os.path.join(ROOT, 'out', 'f2_' + time.strftime('%Y%m%d-%H%M%S'))
os.makedirs(OUT)
LOG = open(os.path.join(OUT, 'f2.log'), 'a')


def log(*a):
    s = ' '.join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + '\n'); LOG.flush()


def named(v):
    return {(STATS[i] if i < len(STATS) else f'stat{i + 1}'): x for i, x in enumerate(v)}


def load_and_wait(x, path):
    log('load', os.path.basename(path), rig.send(x, {'cmd': 'load_save', 'file': path}))
    time.sleep(3)
    st = rig.wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('map', -1) == 0 and s.get('tick', -1) > 0,
                      'campaign loaded', timeout=400, every=2)
    rig.send(x, {'cmd': 'set_speed', 'speed': 0})               # paused: needs do not drift between reads
    time.sleep(2)
    return st


def stats(x, guids, tag):
    out = {}
    for g in guids:
        r = rig.send(x, {'cmd': 'fe_stats', 'guid': g})
        if not r.get('ok'):
            log(tag, g[:8], 'ERROR', r); out[g] = None; continue
        out[g] = named(r['stats'])
    json.dump(out, open(os.path.join(OUT, f'stats_{tag}.json'), 'w'), indent=1)
    return out


def game_save(x, name):
    before = set(glob.glob(os.path.join(rig.idir(x), 'data', 'Saves', '*.json')))
    t0 = time.time()
    log('game save', name, rig.send(x, {'cmd': 'fe_save', 'name': name}))
    for _ in range(60):
        new = [p for p in glob.glob(os.path.join(rig.idir(x), 'data', 'Saves', '*.json')) if p not in before or os.path.getmtime(p) >= t0]
        if new:
            time.sleep(2)
            p = max(new, key=os.path.getmtime)
            dst = os.path.join(OUT, 'game_' + os.path.basename(p))
            shutil.copy2(p, dst)
            return dst
        time.sleep(1)
    raise RuntimeError('the game wrote no save')


def diff_stats(a, b):
    return {k: (a[k], b[k]) for k in a if abs((a[k] or 0) - (b[k] or 0)) > 1e-4}


def main():
    window = '--window' in sys.argv
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < 3600:                 # max 3 pairs are shared with other lanes: wait for one
        pair = rig.lease('fe_f2', os.getpid())
        if not pair:
            time.sleep(30)
    if not pair:
        log('no free pair in 1 h'); sys.exit(2)
    x = pair[0]
    log('leased', pair, 'using copy', x, 'dylib', rig.DYLIB)
    try:
        run(x, window)
    finally:
        rig.stop(x)
        log('released', rig.release(pair))


def run(x, window):
    home_src = os.path.join(ROOT, 'fixtures', 'home_Elven.json')
    host_src = os.path.join(ROOT, 'fixtures', 'hub_Moonlight_mp.json')
    home, host = fe.load(home_src), fe.load(host_src)
    units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
    pick = [units[0][0], units[1][0]]
    exports = fe.export_units(home, pick, player_uid=1)
    hub, run_m = fe.build_hub(host, exports)
    assert not fe.check_refs(hub, gone=run_m['removed_host_guids'] + pick)
    hub_path = fe.write_new(hub, OUT)
    json.dump(run_m, open(os.path.join(OUT, 'run.json'), 'w'), indent=1)
    json.dump([m for _, m in exports], open(os.path.join(OUT, 'exports.json'), 'w'), indent=1)
    hg = [r['hub_guid'] for r in run_m['units']]
    log('hub', os.path.basename(hub_path), 'units', [g[:8] for g in hg], 'from', [g[:8] for g in pick])

    rig.touch(x)
    rig.start(x, window=window)
    rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title screen', timeout=300, every=2)
    home_copy = os.path.join(OUT, os.path.basename(home['CampaignSaveHeader']['SaveName']) + '.json')
    shutil.copy2(home_src, home_copy)

    # 1. home
    load_and_wait(x, home_copy)
    s_home = stats(x, pick, 'home')
    rig.send(x, {'cmd': 'screenshot', 'path': os.path.join(OUT, '1_home.png'), 'w': 960, 'h': 540})
    # 2. hub
    load_and_wait(x, hub_path)
    lu = rig.send(x, {'cmd': 'list_units'})
    mine = [u for u in lu.get('units', []) if u['guid'] in hg]
    log('hub list_units (injected):', json.dumps(mine))
    s_hub = stats(x, hg, 'hub')
    rig.send(x, {'cmd': 'screenshot', 'path': os.path.join(OUT, '2_hub.png'), 'w': 960, 'h': 540})
    if window:
        time.sleep(15)                                           # the hidden window's back buffer lags the load
        log('ui_capture', rig.send(x, {'cmd': 'ui_capture', 'path': os.path.join(OUT, '2_hub_ui.png')}))
    for h, g in zip(pick, hg):
        d = diff_stats(s_home[h], s_hub[g]) if s_home[h] and s_hub[g] else 'missing'
        log(f'STATS home {h[:8]} -> hub {g[:8]}: differences', json.dumps(d))
    # the game's own save of the hub: did it keep the units as injected?
    end_path = game_save(x, 'MPFE_' + run_m['run_id'][:8] + '_end')
    end = fe.load(end_path)
    p = fe.check_refs(end)
    log('game-saved hub: reference check', p[:5] or 'clean')
    for (cl, man), r in zip(exports, run_m['units']):
        a = fe.export_unit(hub, r['hub_guid']); b = fe.export_unit(end, r['hub_guid'])
        for t in (0, 3, 5, 6, 8, 10):
            ca, cb = fe.comp(a['unit']['components'], t), fe.comp(b['unit']['components'], t)
            if fe.canon(ca) != fe.canon(cb):
                keys = [k for k in set(ca) | set(cb) if fe.canon(ca.get(k)) != fe.canon(cb.get(k))]
                log(f'  game re-save {r["hub_guid"][:8]} component {t} changed: {keys}')
        log(f'  game re-save {r["hub_guid"][:8]}: items {len(a["items"])} -> {len(b["items"])},',
            'pusd equal' if fe.canon(a['unit']['pusd']) == fe.canon(b['unit']['pusd']) else 'pusd CHANGED')

    # 3. results: game-saved hub + an offline change on unit 0 so the merge is visible
    end2 = copy.deepcopy(end)
    st0 = fe.comp(fe.ec_of(end2)[hg[0]], 0)
    st0['ProgressionStats']['Level'] += 2
    st0['GeneratedStats']['Strength'] += 3
    rules = {'death_permanent': True, 'xp_counts': True, 'items_back': False}
    res = fe.extract_results(end2, run_m, rules, player_uid=1)
    led = os.path.join(OUT, 'claims.json')
    new_path, rep = fe.apply_results(home, res, {m['export_id']: m for _, m in exports}, led, OUT)
    log('apply ->', os.path.basename(new_path), rep)
    try:
        fe.apply_results(home, res, {m['export_id']: m for _, m in exports}, led, OUT); log('SECOND APPLY NOT REFUSED')
    except fe.Refused as e:
        log('second apply refused:', e)

    # 4. new home save in the game
    load_and_wait(x, new_path)
    s_new = stats(x, pick, 'new_home')
    rig.send(x, {'cmd': 'screenshot', 'path': os.path.join(OUT, '4_new_home.png'), 'w': 960, 'h': 540})
    for h in pick:
        d = diff_stats(s_home[h], s_new[h]) if s_home[h] and s_new[h] else 'missing'
        log(f'STATS home {h[:8]} before -> after apply: differences', json.dumps(d))
    resaved = fe.load(game_save(x, 'fe_marker_check'))
    log('marker after a game load + save:', resaved['ReportSaveData']['InGameRecordData']['Counts'].get(fe.MARK + res['run_id']))
    log('game-saved new home: reference check', fe.check_refs(resaved)[:5] or 'clean')
    lv = fe.comp(fe.ec_of(resaved)[pick[0]], 0)['ProgressionStats']['Level']
    log('quick slots home / after apply + game save:', home['PlayerUnitsSaveData']['QuickSlots'][pick[0]]['QuickSlots'],
        resaved['PlayerUnitsSaveData']['QuickSlots'][pick[0]]['QuickSlots'])
    log('unit 0 level in the game-saved new home:', lv, '(was', exports[0][1]['level'], ')')
    errs = [l for l in open(os.path.join(rig.idir(x), 'player.log'), errors='replace') if 'Exception' in l]
    log('player.log exceptions:', len(errs)); [log('  ', l.strip()[:200]) for l in errs[:8]]
    log('OUT', OUT)


if __name__ == '__main__':
    main()
