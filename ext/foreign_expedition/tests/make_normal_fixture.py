#!/usr/bin/env python3
"""fixtures/hub_Moonlight_normal.json: the MP Moonlight save at Normal / Normal difficulty (savegen setDifficulty,
values from ~/dungeonsettlersdb/site/builder/data.js "diff"), new campaign guid, iron off. Run once."""
import json, os, sys, uuid
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import fe_save as fe
src, dst = os.path.join(ROOT, 'fixtures', 'hub_Moonlight_mp.json'), os.path.join(ROOT, 'fixtures', 'hub_Moonlight_normal.json')
s = fe.load(src)
ORDER = ['Easy', 'Normal', 'Hard', 'VeryHard', 'Downfall']
TREAT, STAY = [7.0, 4.0, 0.0, -4.0, -7.0], [-3.0, -6.0, -9.0, -12.0]
ci = mi = ORDER.index('Normal')
h = s['CampaignSaveHeader']; h['CombatDifficulty'] = ci; h['ManagementDifficulty'] = mi
css = s['CampaignSettingSaveData']['CampaignStartSettingSaveData']; css['CombatDifficulty'] = ci; css['ManagementDifficulty'] = mi
bal = s['CampaignSettingSaveData'].get('CampaignBalanceSaveData')
if bal is not None:
    bal['ManagementDifficulty'] = mi; bal['TreatmentExpectationMoodValues'] = TREAT; bal['DungeonStayMoodPenaltyValues'] = STAY
players = {g for g, _ in fe.clan_units(s)}
ec = fe.ec_of(s)
for sd, uc in s['EntityContainerSaveData']['UnitContainerSaveData'].items():
    for g in uc.get('UnitSaveDataDictionary', {}):
        af = fe.comp(ec.get(g), 3)
        if not af: continue
        p = g in players
        kept = [x for x in af['AffecterHolders'] if not (x['Key'].startswith('AFFECTER_PlayerDifficulty') or x['Key'].startswith('AFFECTER_PlayerManagementDifficulty'))] if p \
            else [x for x in af['AffecterHolders'] if not x['Key'].startswith('AFFECTER_MonsterDifficulty')]
        for k in (['AFFECTER_PlayerDifficultyNormal', 'AFFECTER_PlayerManagementDifficultyNormal'] if p else ['AFFECTER_MonsterDifficultyNormal']):
            kept.append({'Key': k, 'Stack': 1, 'Duration': 0.0, 'InitialDuration': 0.0})
        af['AffecterHolders'] = kept
cg = str(uuid.uuid4())
h.update(CampaignGuid=cg, SaveName='MoonlightNormal__AutoSave_' + cg.replace('-', '') + '_0', IronModeEnabled=False, DisplayName='Moonlight Normal')
css['IronModeEnabled'] = False
s['ClanSaveData']['CampaignGuid'] = cg
if os.path.exists(dst): sys.exit('exists: ' + dst)
json.dump(s, open(dst, 'w'), indent=2)
print(dst, fe.difficulty_keys(s), fe.check_refs(s) or 'refs clean')
