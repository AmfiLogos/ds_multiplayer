#!/usr/bin/env python3
"""make_hub_template.py - turn a player's base save into the Foreign Expedition hub template (the wiki file
/assets/saves/hub_<build>.json that src/fe_hub.c downloads; owner 2026-10-21: "Remove all units from the save and keep
the base with everything unlocked").

  python3 tools/make_hub_template.py <base save.json> <out hub_<build>.json> [--stock-template base_<build>.json]

Never changes the input file. What it does:
  - every player unit goes (the clan's units; NPCs such as the Guild Merchant stay with their shop: owner 2026-10-21
    "you could keep the guild merchant"), with the gear they hold (item slots, carried items, recursively), their positions, animation states, skill trees /
    quick slots / task priorities / ..., bed and grave owners, recreation slots, event targets, prayer / exp stamps;
  - leftovers of units that are no longer in the save (animation states, recreation performers: the stock load logs
    "RecreationContainer Deserialize Failed: Performer Not Found" for those) go too;
  - top-level sections the stock template does not have (other mods' data, e.g. "DelversCore", "...10Slots...") go;
  - the player's crafting orders (they would use up the dungeon loot in the hub);
  - every research done, every region open (fe_hub.c does it again at hub build);
  - clan / save names neutral (fe_hub.c rewrites them anyway; the player's names never reach the published file);
  - adds "MPHubTemplate" {spawn: the cells where the clan's units stood, difficulty: their difficulty affecter keys}.
    fe_hub.c takes it out of the world it writes and uses it when the hub is started with brought units (the hub has
    no units of its own to take the place and the difficulty from).
Buildings, floors, storage, decorations, farms, time of day stay as they are. Exit 0 = written and checked."""
import json, re, sys

GUID = re.compile(r'^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$')
ZERO = '00000000-0000-0000-0000-000000000000'
HUB_NAME = 'Foreign Expedition Hub'
STOCK_SECTIONS = ['EntityContainerSaveData', 'EntityComponentSaveData', 'CampaignSaveHeader', 'CampaignSettingSaveData',
                  'CampaignRandomSaveData', 'AnimationStateSaveData', 'RawMapContainerSaveData', 'RawBaseSaveData',
                  'RawDungeonSaveData', 'PlayerUnitsSaveData', 'ResearchSaveData', 'ClanSaveData', 'EnvironmentSaveData',
                  'TimeSaveData', 'TaskContainerSaveData', 'QuestContainerSaveData', 'FarmingCropSaveData', 'BedOwnerSaveData',
                  'GraveOwnerSave', 'EventSaveData', 'ReportSaveData', 'RecreationSaveData', 'DecisionSaveData',
                  'StoragePolicySaveData']                     # the top-level keys of base_25846389.json (stock 0.4.29 save)


def comp(comps, t):
    for c in comps or []:
        if isinstance(c, dict) and c.get('Type') == t: return c.get('Data')
    return None


def item_guids(comps):
    out = []
    for c in comps or []:
        d = c.get('Data') if isinstance(c, dict) else None
        if not isinstance(d, dict): continue
        for s in d.get('ItemSlots') or []:
            g = s.get('Guid') if isinstance(s, dict) else None
            if g and g != ZERO: out.append(g)
        t = d.get('TempSlot')
        if isinstance(t, dict) and t.get('Guid') and t['Guid'] != ZERO: out.append(t['Guid'])
    return out


def main():
    args = sys.argv[1:]
    stock = None
    if '--stock-template' in args:
        i = args.index('--stock-template'); stock = args[i + 1]; del args[i:i + 2]
    if len(args) != 2: print(__doc__); sys.exit(1)
    src, out = args
    s = json.load(open(src, encoding='utf-8'))
    st0 = json.load(open(stock, encoding='utf-8')) if stock else None
    sections = list(st0.keys()) if st0 else STOCK_SECTIONS
    ec = s['EntityComponentSaveData']['EntityComponents']
    ecd = s['EntityContainerSaveData']
    ucs = ecd['UnitContainerSaveData']

    # what the hub takes from the clan before it goes: where the units stood, their difficulty keys
    spawn, diff, names = [], None, set()
    for side, uc in ucs.items():
        for g in uc.get('PlayerGuidList') or []:
            p = ecd['EntityPositions'].get(side, {}).get(g)
            if side == 'Base' and p: spawn.append({'x': p['x'], 'y': p['y']})
            ah = (comp(ec.get(g), 3) or {}).get('AffecterHolders') or []
            k = sorted(h['Key'] for h in ah if str(h.get('Key', '')).startswith(('AFFECTER_PlayerDifficulty', 'AFFECTER_PlayerManagementDifficulty')))
            if k and diff is None: diff = k
    for side, uc in ucs.items():                               # every player unit's own name (checked below: none may stay)
        for g in uc.get('PlayerGuidList') or []:
            pr = comp(ec.get(g), 10) or {}
            for f in ('UnitNameTextKey', 'DefaultUnitNameTextKey'):
                v = pr.get(f)
                if isinstance(v, str) and v and not v.startswith('TEXTKEY_'): names.add(v)
    clan_name = s.get('ClanSaveData', {}).get('ClanName')
    if clan_name: names.add(clan_name)

    # 1. the units and their gear
    gone, unit_set = set(), set()
    for side, uc in ucs.items():
        reg = uc.get('UnitSaveDataDictionary') or {}
        npc = {g for g, r in reg.items() if r.get('Key') != 'UNIT_PlayerUnit' and g not in (uc.get('PlayerGuidList') or []) and g not in (uc.get('AllyGuidList') or [])}
        for g in [g for g in reg if g not in npc] + list(uc.get('PlayerGuidList') or []) + list(uc.get('AllyGuidList') or []):
            if g in gone: continue
            gone.add(g); unit_set.add(g)
            todo = item_guids(ec.get(g))
            while todo:
                ig = todo.pop()
                if ig in gone: continue
                gone.add(ig); todo += item_guids(ec.get(ig))
        uc['UnitSaveDataDictionary'] = {g: r for g, r in reg.items() if g in npc}   # NPCs stay (the Guild Merchant: owner 2026-10-21)
        uc['PlayerGuidList'] = []; uc['AllyGuidList'] = []; uc['UnitSummonerDictionary'] = {}
    n_units = len(unit_set)
    for g in gone: ec.pop(g, None)
    for ic in ecd['ItemContainerSaveData'].values():
        for k in ('ItemSaveData', 'ItemAutoFixable', 'OnMapItems'):
            for g in list((ic.get(k) or {}).keys()):
                if g in gone: del ic[k][g]
    for pm in ecd['EntityPositions'].values():
        for g in list(pm.keys()):
            if g in gone: del pm[g]
    alive = set(ec.keys())                                     # every entity that stays (buildings, items, farms, ...)

    # 2. records keyed by unit guid: removed units and units no longer in the save
    st = s.get('AnimationStateSaveData', {}).get('States') or {}
    for side in st.values():
        for g in list(side.keys()):
            if g not in alive: del side[g]
    for sub in (s.get('PlayerUnitsSaveData') or {}).values():
        if isinstance(sub, dict):
            for g in list(sub.keys()):
                if GUID.match(g): del sub[g]
    s.get('CampaignRandomSaveData', {}).get('LevelUpPrayerAttemptIndexByUnitGuid', {}).clear()
    s.get('ReportSaveData', {}).get('LevelUpTimeData', {}).get('UnitExpMaxStamp', {}).clear()
    s['BedOwnerSaveData']['BedOwnerMap'] = {}
    s['GraveOwnerSave']['GraveOwnerMap'] = {}                 # a grave's owner is a unit (dead); the grave itself stays
    rec = s.get('RecreationSaveData', {}).get('Recreations') or []
    s['RecreationSaveData']['Recreations'] = [r for r in rec if r.get('PerformerGuid') in alive and r.get('TargetBuilding') in alive]
    for ev in (s.get('EventSaveData', {}).get('EventSourceData', {}).get('EventInstanceSaveData') or {}).values():
        if isinstance(ev, dict) and isinstance(ev.get('TargetUnits'), list):
            ev['TargetUnits'] = [g for g in ev['TargetUnits'] if g in alive]

    # 2b. the player's crafting orders go: in the hub the brought units would carry out the repeat orders and use up the
    #     dungeon loot (Leather from RawHide, powder from mana stones: pair test 2026-10-21, the loot never came home)
    tc = s.get('TaskContainerSaveData', {})
    if isinstance(tc.get('AllTasks'), list):
        tc['AllTasks'] = [t for t in tc['AllTasks'] if not str(t.get('$type', '')).startswith('Refactor.CraftingTaskSaveData')]

    # 3. other mods' sections
    extra = [k for k in s if k not in sections]
    for k in extra: del s[k]

    # 4. everything unlocked
    rd = s['ResearchSaveData']['ResearchDatas']
    for r in rd.values():
        r['Workload'] = r['RequiredWorkload']; r['IsLocked'] = False
    s['ResearchSaveData']['ResearchKeyQueue'] = []
    for r in s['ClanSaveData']['ExpeditionMapData']:
        if r.get('Region', -1) >= 0: r['IsLocked'] = False

    # 5. neutral names, no population
    h = s['CampaignSaveHeader']
    for k in ('ClanName', 'DisplayName', 'SaveName'): h[k] = HUB_NAME if k != 'SaveName' else 'MPHubTemplate'
    s['ClanSaveData']['ClanName'] = HUB_NAME; s['ClanSaveData']['CurrentPopulation'] = 0
    css = s.get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData')
    if isinstance(css, dict):
        css['CampaignName'] = HUB_NAME; css['IronModeEnabled'] = False
        # the new-campaign unit picks hold the starting units' names (OverriddenNameTextKey): the stock template's picks, or none
        pl = (st0 or {}).get('CampaignSettingSaveData', {}).get('CampaignStartSettingSaveData', {}).get('EstablishSelectionPayload')
        if isinstance(pl, str): css['EstablishSelectionPayload'] = pl
        elif isinstance(css.get('EstablishSelectionPayload'), str):
            try:
                o = json.loads(css['EstablishSelectionPayload'])
                for t in o.get('Tokens') or []: t['OverriddenNameTextKey'] = ''
                css['EstablishSelectionPayload'] = json.dumps(o, separators=(',', ':'))
            except ValueError: css['EstablishSelectionPayload'] = ''
    h['IronModeEnabled'] = False; h['IsAutoSave'] = False

    s['MPHubTemplate'] = {'version': 1, 'spawn': spawn, 'difficulty': diff or []}

    # checks: no removed guid left anywhere, no unit / clan name left
    text = json.dumps(s, ensure_ascii=False, separators=(',', ':'))
    left = [g for g in gone if g in text]
    bad = [n for n in names if json.dumps(n, ensure_ascii=False) in text or json.dumps(json.dumps(n))[1:-1] in text
           or (len(n) >= 6 and n in text)]   # as a JSON value, inside an embedded JSON string, or anywhere (longer names)
    if left or bad:
        print('FAIL: left in the file:', len(left), 'removed guids,', len(bad), 'names'); sys.exit(2)
    if not spawn: print('FAIL: no spawn cells (the save has no clan unit at the settlement)'); sys.exit(2)
    open(out, 'w', encoding='utf-8').write(text)
    print(f'hub template written: {out} ({len(text)} bytes); removed {n_units} unit(s) and {len(gone) - n_units} item(s) they held; '
          f'sections dropped: {extra}; research {len(rd)} done; {len(spawn)} spawn cells; difficulty {diff}; '
          f'buildings {len(ecd["BuildingContainerSaveData"]["Base"]["BuildingSaveData"])}, items {len(ecd["ItemContainerSaveData"]["Base"]["ItemSaveData"])}')


if __name__ == '__main__':
    main()
