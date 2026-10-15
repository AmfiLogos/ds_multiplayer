#!/usr/bin/env python3
"""C library (build/fe_cli, ASan) on the same fixtures and scenarios as test_fe.py; results checked with fe_save.py.
Run: python3 tests/test_c.py   (build: src/fe/build_cli.sh)"""
import copy, hashlib, json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
import fe_save as fe
CLI = os.path.join(ROOT, 'build', 'fe_cli')
FIX = os.path.join(ROOT, 'fixtures')
OUT = os.path.join(ROOT, 'out', 'test_c_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
HOME_P, HUB_P, NORM_P = (os.path.join(FIX, f) for f in ('home_Elven.json', 'hub_Moonlight_mp.json', 'hub_Moonlight_normal.json'))
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
FIX_SHA = {p: sha(p) for p in (HOME_P, HUB_P, NORM_P)}
fails = []
o = lambda n: os.path.join(OUT, n)


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + (f'  [{detail}]' if detail else ''), flush=True)
    if not cond: fails.append(name)


def cli(*a, ok=True):
    r = subprocess.run([CLI, *a], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    if ok and r.returncode: print('CLI', a[0], 'rc', r.returncode, r.stdout[-400:], r.stderr[-1500:])
    if 'ERROR: AddressSanitizer' in r.stderr or 'runtime error' in r.stderr: fails.append('sanitizer ' + a[0]); print(r.stderr[-2000:])
    return r


def dump(obj, name):
    json.dump(obj, open(o(name), 'w')); return o(name)


home = fe.load(HOME_P)
units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
pick = [units[0][0], units[1][0]]
r = cli('export', HOME_P, 'uid7', o('entries.json'), *pick)
entries = json.load(open(o('entries.json')))
py = [fe.export_unit(home, g) for g in pick]
check('C export = Python export (closures equal)', all(e['closure'] == p for e, p in zip(entries, py)))
check('C manifest fields', all(e['manifest']['unit_guid'] == g and e['manifest']['player_uid'] == 'uid7' for e, g in zip(entries, pick)))

for label, host_p in (('VeryHard hub', HUB_P), ('Normal hub', NORM_P)):
    tag = 'vh' if host_p == HUB_P else 'nm'
    ents = entries + [copy.deepcopy(entries[0])]
    ents[2]['manifest']['export_id'] = 'f' * 32; ents[2]['manifest']['player_uid'] = 'uid9'
    cli('hub', host_p, dump(ents, f'ents_{tag}.json'), 'a1b2c3d4' + '0' * 24, o(f'hub_{tag}.json'), o(f'run_{tag}.json'))
    hub, run = fe.load(o(f'hub_{tag}.json')), json.load(open(o(f'run_{tag}.json')))
    host = fe.load(host_p)
    gone = run['removed_host_guids'] + pick + [ig for e in entries for ig in e['closure']['items']]
    p = fe.check_refs(hub, gone=gone)
    check(f'{label}: Python reference check of the C hub clean', not p, '; '.join(p[:3]))
    check(f'{label}: C reference check agrees', cli('check', o(f'hub_{tag}.json'), dump(gone, f'gone_{tag}.json'), ok=False).returncode == 0)
    check(f'{label}: 3 units, new campaign, iron off', fe.population(hub) == 3 and hub['CampaignSaveHeader']['CampaignGuid'] != host['CampaignSaveHeader']['CampaignGuid']
          and not hub['CampaignSaveHeader']['IronModeEnabled'] and not hub['CampaignSettingSaveData']['CampaignStartSettingSaveData']['IronModeEnabled'])
    check(f'{label}: injected units carry the hub difficulty', all(
        sorted(h['Key'] for h in fe.comp(fe.ec_of(hub)[r_['hub_guid']], 3)['AffecterHolders'] if fe.DIFF_RE.match(h['Key'])) == fe.difficulty_keys(host)
        for r_ in run['units']), str(fe.difficulty_keys(host)))
    for e, r_ in zip(ents, run['units']):
        back = fe.export_unit(hub, r_['hub_guid'])
        inv = {r_['hub_guid']: e['manifest']['unit_guid']}; inv.update({v: k for k, v in r_['item_map'].items()})
        back = fe.deep_replace(back, inv)
        for c in (back, e['closure']):
            c['unit'].pop('position', None)
            a = fe.comp(c['unit']['components'], 3); a['AffecterHolders'] = [h for h in a['AffecterHolders'] if not fe.DIFF_RE.match(h['Key'])]
        check(f'{label}: round trip {e["manifest"]["unit_guid"][:8]} equal (except position, difficulty)', back == e['closure'])

# ---- results + apply on the Normal hub (difficulty must come back as home's)
hub, run = fe.load(o('hub_nm.json')), json.load(open(o('run_nm.json')))
r0, r1 = run['units'][0], run['units'][1]
end = copy.deepcopy(hub)
st = fe.comp(fe.ec_of(end)[r0['hub_guid']], 0); st['ProgressionStats']['Level'] += 2; st['GeneratedStats']['Strength'] += 3
fe.remove_unit(end, r1['hub_guid']); end['ClanSaveData']['CurrentPopulation'] = fe.population(end)
stor = []
for b, bv in end['EntityContainerSaveData']['BuildingContainerSaveData']['Base']['BuildingSaveData'].items():
    for s_ in (fe.comp(fe.ec_of(end).get(b), 5) or {}).get('ItemSlots') or []:
        if s_['Guid'] != fe.ZERO and 'Storage' in bv['Key']: stor.append((s_['Guid'], s_['Key'], s_['Amount']))
loot, crafted = stor[0], next(x for x in stor if x[1] != stor[0][1])
tags = {loot[0]: {'found_in_dungeon': True}, crafted[0]: {'found_in_dungeon': False}, list(r0['item_map'].values())[0]: {'found_in_dungeon': True}}
end_p = o('end_nm.json'); json.dump(end, open(end_p, 'w'))
cli('extract', end_p, o('run_nm.json'), 'uid7', '1,1,1', dump(tags, 'loot.json'), '-', o('res.json'))
res = json.load(open(o('res.json')))
check('C extract: alive + dead, only dungeon loot', [u['status'] for u in res['units']] == ['alive', 'dead'] and list(res['items']) == [loot[0]])
# stackable loot merged into a stack (guid gone): back by key + amount, capped by what the hub holds of that key
gone = '000002b0-0000-0000-3930-00000053494d'
end2 = copy.deepcopy(end)                       # the picked 2 merged into the crafted key's stack: the hub gained 2 of that key
for comps in fe.ec_of(end2).values():
    for c_ in comps:
        sl_ = [x for x in (c_['Data'].get('ItemSlots') or []) if x['Guid'] == crafted[0]]
        for x in sl_: x['Amount'] += 2
end_p2 = o('end_nm2.json'); json.dump(end2, open(end_p2, 'w'))
tags2 = {gone: {'found_in_dungeon': True, 'key': crafted[1], 'amount': 2}, '000002b1-0000-0000-3930-00000053494d': {'found_in_dungeon': True, 'key': 'ITEM_NotInTheHub', 'amount': 5}}
cli('extract', end_p2, o('run_nm.json'), 'uid7', '1,1,1', dump(tags2, 'loot2.json'), '-', o('res2.json'))
cli('extract', end_p, o('run_nm.json'), 'uid7', '1,1,1', dump(tags2, 'loot2.json'), '-', o('res3.json'))
check('C extract: stacked loot the hub did not gain (lost with dead units / used up) does not come back', json.load(open(o('res3.json')))['items'] == {})
res2 = json.load(open(o('res2.json'))); it2 = res2['items'].get(gone) or {}
check('C extract: merged stack loot comes back by key and amount; a key the hub no longer holds does not',
      list(res2['items']) == [gone] and it2.get('amount') == 2 and it2['registry']['Key'] == crafted[1], json.dumps({k: v.get('amount') for k, v in res2['items'].items()}))
mans = {e['manifest']['export_id']: e['manifest'] for e in entries}
mp = dump(mans, 'manifests.json'); led = o('claims.json'); hd = o('home_saves')
r = cli('apply', HOME_P, o('res.json'), mp, led, hd)
path = r.stdout.splitlines()[0] if r.returncode == 0 else ''
check('C apply: new save', os.path.exists(path), r.stdout.strip()[:300])
new = fe.load(path)
H0, H1 = pick
p = fe.check_refs(new, gone=[H1] + list(entries[1]['closure']['items']))
check('C apply: Python reference check clean (dead unit + gear gone)', not p, '; '.join(p[:3]))
c0 = fe.comp(fe.ec_of(new)[H0], 0)
check('C apply: level +2, strength +3', c0['ProgressionStats']['Level'] == entries[0]['manifest']['level'] + 2
      and c0['GeneratedStats']['Strength'] == fe.comp(entries[0]['closure']['unit']['components'], 0)['GeneratedStats']['Strength'] + 3)
check('C apply: difficulty back to home (VeryHard) after a Normal hub', fe.difficulty_keys(new) == fe.difficulty_keys(home)
      and sorted(h['Key'] for h in fe.comp(fe.ec_of(new)[H0], 3)['AffecterHolders'] if fe.DIFF_RE.match(h['Key'])) == fe.difficulty_keys(home))
check('C apply: StateStats, equipment, quick slots = home', c0['StateStats'] == fe.comp(fe.ec_of(home)[H0], 0)['StateStats']
      and fe.comp(fe.ec_of(new)[H0], 6) == fe.comp(fe.ec_of(home)[H0], 6)
      and new['PlayerUnitsSaveData']['QuickSlots'][H0] == home['PlayerUnitsSaveData']['QuickSlots'][H0])
placed = [s_ for comps in fe.ec_of(new).values() for c in comps for s_ in (c['Data'].get('ItemSlots') or []) if s_['Key'] == loot[1] and s_['Guid'] not in fe.ec_of(home)]
check('C apply: loot placed in home storage', len(placed) == 1 and placed[0]['Amount'] == loot[2])
check('C apply: marker + header', new['ReportSaveData']['InGameRecordData']['Counts'].get(fe.MARK + res['run_id']) == 1
      and os.path.basename(path)[:-5] == new['CampaignSaveHeader']['SaveName'])
n_files = len(os.listdir(hd))
r2 = cli('apply', HOME_P, o('res.json'), mp, led, hd, ok=False)
check('C second apply refused (ledger)', r2.returncode == 2 and 'Already' in r2.stdout, r2.stdout.strip())
r3 = cli('apply', path, o('res.json'), mp, o('lost.json'), hd, ok=False)
check('C second apply refused (marker, ledger lost)', r3.returncode == 2 and 'marker' in r3.stdout, r3.stdout.strip())
check('no file written by refusals', len(os.listdir(hd)) == n_files)
bad = copy.deepcopy(res); bad['rules']['xp_counts'] = False
r4 = cli('apply', HOME_P, dump(bad, 'bad.json'), mp, o('c4.json'), hd, ok=False)
check('C tampered result refused', r4.returncode == 2 and 'damaged' in r4.stdout)
# conflict
home2 = copy.deepcopy(home); fe.comp(fe.ec_of(home2)[H0], 0)['ProgressionStats']['Level'] += 1
h2 = o('home2.json'); json.dump(home2, open(h2, 'w'))
r5 = cli('apply', h2, o('res.json'), mp, o('c5.json'), hd, ok=False)
check('C conflict asks for a choice', r5.returncode == 2 and 'choose' in r5.stdout)
r6 = cli('apply', h2, o('res.json'), mp, o('c5.json'), o('home_saves_conflict'), dump({entries[0]['manifest']['export_id']: 'home'}, 'ch.json'))
n6 = fe.load(r6.stdout.splitlines()[0]) if r6.returncode == 0 else None
check('C conflict "home" keeps the home level', n6 and fe.comp(fe.ec_of(n6)[H0], 0)['ProgressionStats']['Level'] == entries[0]['manifest']['level'] + 1)
# death not permanent + last-alive snapshot
la = {r1['hub_guid']: fe.export_unit(hub, r1['hub_guid'])}
fe.comp(la[r1['hub_guid']]['unit']['components'], 0)['ProgressionStats']['Level'] += 1
run_d = dict(run, run_id='d' * 32); json.dump(run_d, open(o('run_d.json'), 'w'))
cli('extract', end_p, o('run_d.json'), 'uid7', '0,1,0', '-', dump(la, 'la.json'), o('res_d.json'))
r7 = cli('apply', HOME_P, o('res_d.json'), mp, o('c7.json'), hd)
n7 = fe.load(r7.stdout.splitlines()[0]) if r7.returncode == 0 else None
check('C death not permanent: unit home at its last multiplayer state', n7 and fe.comp(fe.ec_of(n7)[H1], 0)['ProgressionStats']['Level'] == entries[1]['manifest']['level'] + 1
      and not fe.check_refs(n7), r7.stdout.strip()[:300])
check('fixtures unchanged on disk', all(sha(p_) == h for p_, h in FIX_SHA.items()))
print(f'\n{len(fails)} failed' if fails else '\nALL PASS', '| out:', OUT)
sys.exit(1 if fails else 0)
