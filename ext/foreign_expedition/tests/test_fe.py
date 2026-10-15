#!/usr/bin/env python3
"""Offline F1 tests on COPIES of real saves (fixtures/). Writes only into out/test_<time>/.
Run: python3 tests/test_fe.py"""
import copy, hashlib, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
import fe_save as fe

FIX = os.path.join(ROOT, 'fixtures')
OUT = os.path.join(ROOT, 'out', 'test_' + time.strftime('%Y%m%d-%H%M%S'))
os.makedirs(OUT)
HOME_P, HUB_P = os.path.join(FIX, 'home_Elven.json'), os.path.join(FIX, 'hub_Moonlight_mp.json')
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
FIX_SHA = {p: sha(p) for p in (HOME_P, HUB_P)}
fails = []


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + (f'  [{detail}]' if detail else ''))
    if not cond:
        fails.append(name)


def strip(cl):
    """closure without the parts that are allowed to differ after a move (task, position, difficulty holders)"""
    c = copy.deepcopy(cl)
    c['unit'].pop('position', None)
    a = fe.comp(c['unit']['components'], 3)
    if a:
        a['AffecterHolders'] = [h for h in a['AffecterHolders'] if not fe.DIFF_RE.match(h['Key'])]
    return c


home, host = fe.load(HOME_P), fe.load(HUB_P)
check('fixtures are clean before', not fe.check_refs(home) and not fe.check_refs(host), str(fe.check_refs(home)[:3]))

# pick the two most geared units
units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
pick = [units[0][0], units[1][0]]
exports = fe.export_units(home, pick, player_uid=7)
check('export: 2 closures', len(exports) == 2, ', '.join(f"{m['unit_guid'][:8]} L{m['level']} {len(c['items'])} items" for c, m in exports))
for cl, man in exports:
    s = json.dumps(cl)
    check(f'export {man["unit_guid"][:8]}: no zero-holder / summon leak', '"UNIT_' not in s.replace('"UNIT_PlayerUnit"', ''))

# ---- hub build: the same unit twice (two players from one ancestor save) + the second unit
entries = [exports[0], exports[1], copy.deepcopy(exports[0])]
entries[2][1]['export_id'] = 'f' * 32
entries[2][1]['player_uid'] = 9
host_before = fe.canon(host)
hub, run = fe.build_hub(host, entries, run_id='a1b2c3d4' + '0' * 24)
check('build_hub leaves the host save untouched', fe.canon(host) == host_before)
gone = run['removed_host_guids'] + pick + [ig for cl, _ in exports for ig in cl['items']]
p = fe.check_refs(hub, gone=gone)
check('hub: reference check clean (no host unit, no source guid anywhere)', not p, '; '.join(p[:4]))
hg = [r['hub_guid'] for r in run['units']]
check('hub: 3 units, 3 distinct new guids', len(set(hg)) == 3 and fe.population(hub) == 3 and hub['ClanSaveData']['CurrentPopulation'] == 3)
check('hub: new campaign guid, iron off, stem = SaveName',
      hub['CampaignSaveHeader']['CampaignGuid'] != host['CampaignSaveHeader']['CampaignGuid']
      and hub['ClanSaveData']['CampaignGuid'] == hub['CampaignSaveHeader']['CampaignGuid']
      and not hub['CampaignSaveHeader']['IronModeEnabled']
      and not hub['CampaignSettingSaveData']['CampaignStartSettingSaveData']['IronModeEnabled'])
pos = [tuple(hub['EntityContainerSaveData']['EntityPositions']['Base'][g].values()) for g in hg]
check('hub: 3 distinct positions', len(set(pos)) == 3, str(pos))
for (cl, man), r in zip(entries, run['units']):
    back = fe.export_unit(hub, r['hub_guid'])
    inv = {r['hub_guid']: man['unit_guid']}
    inv.update({v: k for k, v in r['item_map'].items()})
    back = fe.deep_replace(back, inv)
    check(f'round trip {man["unit_guid"][:8]} -> hub: closure equal (except position)', fe.canon(strip(back)) == fe.canon(strip(cl)))
hub_path = fe.write_new(hub, OUT)
re_hub = fe.load(hub_path)
check('hub file written and re-read equal', fe.canon(re_hub) == fe.canon(hub), hub_path)
try:
    fe.write_new(hub, OUT); check('write_new refuses to overwrite', False)
except fe.Refused:
    check('write_new refuses to overwrite', True)

# ---- simulate the run: player 7's first unit gains 2 levels + a skill; second dies. Player 7 loots 1 storage item.
end = copy.deepcopy(hub)
r0, r1 = run['units'][0], run['units'][1]
st = fe.comp(fe.ec_of(end)[r0['hub_guid']], 0)
st['ProgressionStats']['Level'] += 2
st['ProgressionStats']['Exp'] = 12.5
st['GeneratedStats']['Strength'] += 3
sk = fe.comp(fe.ec_of(end)[r0['hub_guid']], 8)
sk['PassiveSkills'] = sk['PassiveSkills'] + ['SKILL_TestFE']
fe_qs = end['PlayerUnitsSaveData']['QuickSlots'][r0['hub_guid']]['QuickSlots']
end['PlayerUnitsSaveData']['QuickSlots'][r0['hub_guid']]['QuickSlots'] = fe_qs[:4]      # what the game did in F2
fe.remove_unit(end, r1['hub_guid'])
end['ClanSaveData']['CurrentPopulation'] = fe.population(end)
# a hub storage item the player "crafted": first item in any storage slot of the hub
stor = []
for b, bv in end['EntityContainerSaveData']['BuildingContainerSaveData']['Base']['BuildingSaveData'].items():
    for s in (fe.comp(fe.ec_of(end).get(b), 5) or {}).get('ItemSlots') or []:
        if s['Guid'] != fe.ZERO and 'Storage' in bv['Key']:
            stor.append((s['Guid'], s['Key'], s['Amount']))
loot, crafted = stor[0], next(x for x in stor if x[1] != stor[0][1])
traveled = list(r0['item_map'].values())[:1]
tags = {loot[0]: {'found_in_dungeon': True}, crafted[0]: {'found_in_dungeon': False}, traveled[0]: {'found_in_dungeon': True}}
rules = {'death_permanent': True, 'xp_counts': True, 'items_back': True}
res = fe.extract_results(end, run, rules, player_uid=7, loot=tags)
check('extract: 2 units for player 7 (alive + dead); only dungeon loot comes back (crafted + travelled copy refused)',
      [u['status'] for u in res['units']] == ['alive', 'dead'] and list(res['items']) == [loot[0]], f'{loot} / crafted {crafted}')

manifests = {m['export_id']: m for _, m in exports}
LED = os.path.join(OUT, 'claims.json')
home_dir = os.path.join(OUT, 'home_saves')
path, rep = fe.apply_results(home, res, manifests, LED, home_dir)
check('apply: new save written', os.path.exists(path), os.path.basename(path))
new = fe.load(path)
H0, H1 = pick
p = fe.check_refs(new, gone=[H1] + [ig for ig in exports[1][0]['items']])
check('apply: reference check clean (dead unit + its gear gone everywhere)', not p, '; '.join(p[:4]))
c0 = fe.comp(fe.ec_of(new)[H0], 0)
check('apply: unit level +2, exp, strength from the hub', c0['ProgressionStats']['Level'] == exports[0][1]['level'] + 2
      and c0['ProgressionStats']['Exp'] == 12.5
      and c0['GeneratedStats']['Strength'] == fe.comp(exports[0][0]['unit']['components'], 0)['GeneratedStats']['Strength'] + 3)
check('apply: home needs kept (StateStats)', c0['StateStats'] == fe.comp(fe.ec_of(home)[H0], 0)['StateStats'])
check('apply: new passive skill', 'SKILL_TestFE' in fe.comp(fe.ec_of(new)[H0], 8)['PassiveSkills'])
check('apply: home quick slot bar kept (hub cut it to 4)',
      new['PlayerUnitsSaveData']['QuickSlots'][H0] == home['PlayerUnitsSaveData']['QuickSlots'][H0])
check('apply: equipment = home equipment', fe.comp(fe.ec_of(new)[H0], 6) == fe.comp(fe.ec_of(home)[H0], 6))
check('apply: difficulty holders = home', fe.difficulty_keys(new) == fe.difficulty_keys(home))
check('apply: H1 removed, population 7', fe.side_of(new, H1) is None and new['ClanSaveData']['CurrentPopulation'] == 7)
placed = [s for comps in fe.ec_of(new).values() for c in comps for s in (c['Data'].get('ItemSlots') or [])
          if s['Key'] == loot[1] and s['Guid'] not in fe.ec_of(home)]
check('apply: looted item placed in home storage with a new guid', len(placed) == 1 and placed[0]['Amount'] == loot[2], str(placed[:1]))
check('apply: marker in Counts', new['ReportSaveData']['InGameRecordData']['Counts'].get(fe.MARK + res['run_id']) == 1)
check('apply: stem = SaveName, same campaign, iron flag kept',
      os.path.basename(path)[:-5] == new['CampaignSaveHeader']['SaveName']
      and new['CampaignSaveHeader']['CampaignGuid'] == home['CampaignSaveHeader']['CampaignGuid']
      and new['CampaignSaveHeader']['IsAutoSave'] == home['CampaignSaveHeader']['IronModeEnabled'])

# ---- idempotent: second apply refused by the ledger; with a lost ledger, by the in-save marker
n_files = len(os.listdir(home_dir))
for label, led, target in (('ledger', LED, home), ('marker (ledger lost)', os.path.join(OUT, 'lost.json'), new)):
    try:
        fe.apply_results(target, res, manifests, led, home_dir); check(f'second apply refused by {label}', False)
    except fe.Refused as e:
        check(f'second apply refused by {label}', True, str(e))
check('no file written by the refused applies', len(os.listdir(home_dir)) == n_files)

# ---- changed at home: refused until a choice; 'home' keeps the home unit
home2 = copy.deepcopy(home)
fe.comp(fe.ec_of(home2)[H0], 0)['ProgressionStats']['Level'] += 1
res2 = dict(res, run_id='b' * 32); res2['result_hash'] = fe.fnv64(fe.canon({k: v for k, v in res2.items() if k != 'result_hash'}))
try:
    fe.apply_results(home2, res2, manifests, os.path.join(OUT, 'c2.json'), home_dir); check('conflict asks for a choice', False)
except fe.Refused as e:
    check('conflict asks for a choice', 'choose' in str(e), str(e))
p2, rep2 = fe.apply_results(home2, res2, manifests, os.path.join(OUT, 'c2.json'), home_dir,
                            choices={exports[0][1]['export_id']: 'home'})
n2 = fe.load(p2)
check('conflict choice "home" keeps the home level', fe.comp(fe.ec_of(n2)[H0], 0)['ProgressionStats']['Level'] == exports[0][1]['level'] + 1)

# ---- tampered result refused
bad = copy.deepcopy(res); bad['units'][0]['status'] = 'alive'; bad['rules']['xp_counts'] = False
try:
    fe.apply_results(home, bad, manifests, os.path.join(OUT, 'c3.json'), home_dir); check('tampered result refused', False)
except fe.Refused as e:
    check('tampered result refused', True, str(e))

# ---- death not permanent: home unit untouched
res4 = dict(res, run_id='c' * 32, rules={'death_permanent': False, 'xp_counts': False, 'items_back': False}, items={})
res4['result_hash'] = fe.fnv64(fe.canon({k: v for k, v in res4.items() if k != 'result_hash'}))
p4, _ = fe.apply_results(home, res4, manifests, os.path.join(OUT, 'c4.json'), home_dir)
n4 = fe.load(p4)
check('rules off: both home units unchanged', all(fe.canon(fe.ec_of(n4)[g]) == fe.canon(fe.ec_of(home)[g]) for g in pick))

# ---- owner 2026-10-14: death NOT permanent -> the dead unit goes home in its LAST multiplayer save state
snap = copy.deepcopy(hub)                                   # an earlier hub save: unit 1 alive, one level up there
fe.comp(fe.ec_of(snap)[r1['hub_guid']], 0)['ProgressionStats']['Level'] += 1
rules5 = {'death_permanent': False, 'xp_counts': True, 'items_back': False}
res5 = fe.extract_results(end, dict(run, run_id='d' * 32), rules5, player_uid=7, snapshots=[snap])
check('extract: dead unit takes its closure from the latest snapshot', 'from_snapshot' in res5['units'][1] and 'closure_end' in res5['units'][1])
p5, rep5 = fe.apply_results(home, res5, manifests, os.path.join(OUT, 'c5.json'), home_dir)
n5 = fe.load(p5)
check('death not permanent: unit home at its last multiplayer state (level +1), alive unit +2',
      fe.comp(fe.ec_of(n5)[H1], 0)['ProgressionStats']['Level'] == exports[1][1]['level'] + 1
      and fe.comp(fe.ec_of(n5)[H0], 0)['ProgressionStats']['Level'] == exports[0][1]['level'] + 2, str(rep5))
check('death not permanent: reference check clean', not fe.check_refs(n5))
res6 = fe.extract_results(end, dict(run, hub_campaign_guid=run['hub_campaign_guid']), rules5, player_uid=7, snapshots=[])
check('death not permanent, no snapshot: no closure (home unit stays as it was)', 'closure_end' not in res6['units'][1])

check('fixtures unchanged on disk', all(sha(p) == h for p, h in FIX_SHA.items()))
print(f'\n{len(fails)} failed' if fails else '\nALL PASS', '| out:', OUT)
json.dump({'run': {k: v for k, v in run.items() if k != 'removed_host_guids'}, 'hub': hub_path, 'apply': path},
          open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
sys.exit(1 if fails else 0)
