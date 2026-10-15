#!/usr/bin/env python3
"""Foreign Expedition save library (F1). Python first version; C port plan in NOTES.md.

All unit moves happen on the save JSON, outside the game, before a load (plan 1.2). Nothing here writes into a
file that exists already: every write is a NEW file (tmp + rename, refused if the target exists).

  export_units(save, guids)          -> list of (closure, manifest): a unit + its gear + every linked row
  build_hub(host_save, closures, run_id) -> (hub save, run manifest): copy with a new campaign identity, iron off,
                                        the host's clan removed, every closure injected with new guids
  extract_results(hub_end, run_manifest, rules, player_uid, item_guids) -> result for one player
  apply_results(home, result, export_manifests, ledger_path, ...) -> new home save (or a refusal)
  check_refs(save, gone=())          -> list of problems (empty = clean)
"""
import copy, datetime, hashlib, json, os, re, secrets, uuid

ZERO = '00000000-0000-0000-0000-000000000000'
GUID_RE = re.compile(r'^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$')
PUSD_TAKEN_ON_APPLY = ('SkillTrees', 'SkillLearnNotices')                   # plan 1.8: the progress rows (QuickSlots: merged)
DIFF_RE = re.compile(r'^AFFECTER_Player(Management)?Difficulty')            # campaign difficulty holders on each unit
MARK = 'mp.fe.'                                                              # ReportSaveData.InGameRecordData.Counts key prefix


class Refused(Exception):
    """A clear one-line reason; nothing was written."""


# ------------------------------------------------------------------ file io
def load(path):
    with open(path, 'rb') as f:
        return json.loads(f.read().decode('utf-8-sig'))


def write_new(save, folder, stem=None):
    """Write save as <folder>/<SaveName>.json. Never overwrites: refuses when the file exists."""
    stem = stem or save['CampaignSaveHeader']['SaveName']
    if save['CampaignSaveHeader']['SaveName'] != stem:
        raise Refused('file stem must equal SaveName (the loader checks it)')
    os.makedirs(folder, exist_ok=True)
    dst = os.path.join(folder, stem + '.json')
    if os.path.exists(dst):
        raise Refused(f'{dst} exists already; never overwritten')
    tmp = dst + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(save, f, ensure_ascii=False, indent=2)
    os.replace(tmp, dst)
    return dst


def _write_json_new(path, obj):
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)
    os.replace(tmp, path)


# ------------------------------------------------------------------ small helpers
def new_guid():
    return str(uuid.uuid4())


def comp(comps, t):
    for c in comps or []:
        if c and c.get('Type') == t:
            return c['Data']
    return None


def ec_of(save):
    return save['EntityComponentSaveData']['EntityComponents']


def sides(save):
    return list(save['EntityContainerSaveData']['UnitContainerSaveData'].keys())


def clan_units(save):
    """[(guid, side)] for every clan unit (PlayerGuidList, all sides)."""
    uc = save['EntityContainerSaveData']['UnitContainerSaveData']
    return [(g, sd) for sd in uc for g in (uc[sd].get('PlayerGuidList') or [])]


def side_of(save, g):
    for u, sd in clan_units(save):
        if u == g:
            return sd
    return None


def unit_info(save, g):
    c = ec_of(save).get(g) or []
    st, pr = comp(c, 0) or {}, comp(c, 10) or {}
    pg = st.get('ProgressionStats') or {}
    return {'guid': g, 'side': side_of(save, g), 'name_key': pr.get('UnitNameTextKey'),
            'level': pg.get('Level'), 'exp': pg.get('Exp')}


def item_guids_of(comps):
    """Every non-zero item guid an entity holds (ItemSlots + TempSlot of any inventory component)."""
    out = []
    for c in comps or []:
        d = c.get('Data') or {}
        slots = list(d.get('ItemSlots') or [])
        if isinstance(d.get('TempSlot'), dict):
            slots.append(d['TempSlot'])
        for s in slots:
            ig = s and s.get('Guid')
            if ig and ig != ZERO:
                out.append(ig)
    return out


def canon(o):
    return json.dumps(o, sort_keys=True, separators=(',', ':'), ensure_ascii=False)


def fnv64(s):
    h = 0xcbf29ce484222325
    for b in s.encode('utf-8'):
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f'{h:016x}'


def deep_replace(o, m):
    """Copy of o with every string key / value found in m replaced (guid remap)."""
    if isinstance(o, dict):
        return {m.get(k, k): deep_replace(v, m) for k, v in o.items()}
    if isinstance(o, list):
        return [deep_replace(v, m) for v in o]
    if isinstance(o, str):
        return m.get(o, o)
    return o


def reset_task(d):
    """savegen resetTask: guids -> zero, numbers -> 0, strings -> null, lists -> []."""
    for k, v in list(d.items()):
        if k == '$type':
            continue
        if isinstance(v, str) and GUID_RE.match(v):
            d[k] = ZERO
        elif isinstance(v, bool):
            d[k] = False
        elif isinstance(v, (int, float)):
            d[k] = 0
        elif isinstance(v, str):
            d[k] = None
        elif isinstance(v, list):
            d[k] = []


# ------------------------------------------------------------------ export (plan 1.3)
def export_unit(save, g):
    """Closure of one clan unit: components, registry, position, animation states, PlayerUnitsSaveData rows,
    iron-mode level-up roll index, exp stamp, and every item it holds (equipment + backpack)."""
    ec = ec_of(save)
    sd = side_of(save, g)
    if not sd or g not in ec:
        raise Refused(f'unit {g} is not a clan unit of this save')
    ecd = save['EntityContainerSaveData']
    comps = copy.deepcopy(ec[g])
    t = comp(comps, 7)
    if t:
        reset_task(t)                                          # a job in the home world means nothing elsewhere
    cy = comp(comps, 9)
    if cy:
        cy['CarriedGuid'] = cy['CarrierGuid'] = ZERO
    unit = {'guid': g, 'side': sd, 'components': comps,
            'registry': copy.deepcopy(ecd['UnitContainerSaveData'][sd]['UnitSaveDataDictionary'][g]),
            'position': copy.deepcopy((ecd.get('EntityPositions') or {}).get(sd, {}).get(g)),
            'anim': {s: copy.deepcopy(v[g]) for s, v in ((save.get('AnimationStateSaveData') or {}).get('States') or {}).items()
                     if isinstance(v, dict) and g in v},
            'pusd': {k: copy.deepcopy(v[g]) for k, v in (save.get('PlayerUnitsSaveData') or {}).items()
                     if isinstance(v, dict) and g in v},
            'prayer': ((save.get('CampaignRandomSaveData') or {}).get('LevelUpPrayerAttemptIndexByUnitGuid') or {}).get(g),
            'exp_stamp': (((save.get('ReportSaveData') or {}).get('LevelUpTimeData') or {}).get('UnitExpMaxStamp') or {}).get(g)}
    items, todo = {}, item_guids_of(comps)
    ic = (ecd.get('ItemContainerSaveData') or {}).get(sd) or {}
    while todo:                                                 # items can hold items: follow the chain
        ig = todo.pop()
        if ig in items:
            continue
        if ig not in ec:
            raise Refused(f'item {ig} of unit {g} has no entity in this save')
        ic_comps = copy.deepcopy(ec[ig])
        items[ig] = {'components': ic_comps,
                     'registry': copy.deepcopy((ic.get('ItemSaveData') or {}).get(ig)),
                     'autofix': (ic.get('ItemAutoFixable') or {}).get(ig)}
        todo += item_guids_of(ic_comps)
    return {'fe_closure': 1, 'unit': unit, 'items': items}


def progress_sig(closure):
    """What 'changed at home' means (plan 1.8): progression, attributes, skills, skill trees, equipment keys.
    Needs, moods, position and task change all the time while the home game runs; they are not progress."""
    c = closure['unit']['components']
    st, sk = comp(c, 0) or {}, comp(c, 8) or {}
    eq = comp(c, 6) or {}
    sig = {'prog': st.get('ProgressionStats'), 'gen': st.get('GeneratedStats'), 'perm': st.get('PermanentStats'),
           'skills': [sk.get('BasicSkills'), sk.get('ActiveSkills'), sk.get('PassiveSkills')],
           'trees': closure['unit']['pusd'].get('SkillTrees'),
           'equip': [s.get('Key') for s in eq.get('ItemSlots') or []]}
    return fnv64(canon(sig))


def export_units(save, guids, player_uid=0):
    hdr = save['CampaignSaveHeader']
    out = []
    for g in guids:
        cl = export_unit(save, g)
        info = unit_info(save, g)
        man = {'export_id': secrets.token_hex(16), 'player_uid': player_uid,
               'home_campaign_guid': hdr['CampaignGuid'], 'home_save_name': hdr['SaveName'],
               'home_save_version': hdr['Version'], 'unit_guid': g,
               'closure_hash': fnv64(canon(cl)), 'progress_sig': progress_sig(cl),
               'name_key': info['name_key'], 'level': info['level'], 'exp': info['exp']}
        out.append((cl, man))
    return out


# ------------------------------------------------------------------ removal (scrub every reference)
def remove_unit(save, g, with_gear=True):
    """Remove one clan unit and every reference to it (plan 1.2 table + the world records list)."""
    ec = ec_of(save)
    ecd = save['EntityContainerSaveData']
    sd = side_of(save, g)
    gear = item_guids_of(ec.get(g))
    if with_gear:
        todo = list(gear)
        while todo:
            ig = todo.pop()
            todo += item_guids_of(ec.get(ig))
            ec.pop(ig, None)
            for s in (ecd.get('ItemContainerSaveData') or {}).values():
                (s.get('ItemSaveData') or {}).pop(ig, None)
                (s.get('ItemAutoFixable') or {}).pop(ig, None)
    ec.pop(g, None)
    for s, uc in ecd['UnitContainerSaveData'].items():
        (uc.get('UnitSaveDataDictionary') or {}).pop(g, None)
        if g in (uc.get('PlayerGuidList') or []):
            uc['PlayerGuidList'] = [x for x in uc['PlayerGuidList'] if x != g]
        if g in (uc.get('AllyGuidList') or []):
            uc['AllyGuidList'] = [x for x in uc['AllyGuidList'] if x != g]
        sm = uc.get('UnitSummonerDictionary') or {}
        for k in [k for k, v in sm.items() if k == g or v == g or (isinstance(v, list) and g in v)]:
            summ = sm.pop(k)                                     # summons of the unit go with it
            for x in ([k] if k != g else []) + (summ if isinstance(summ, list) else [summ] if summ != g else []):
                if x != g and x in ec and (uc.get('UnitSaveDataDictionary') or {}).get(x, {}).get('Key') != 'UNIT_PlayerUnit':
                    ec.pop(x, None)
                    (uc.get('UnitSaveDataDictionary') or {}).pop(x, None)
                    for pm in (ecd.get('EntityPositions') or {}).values():
                        pm.pop(x, None)
    for pm in (ecd.get('EntityPositions') or {}).values():
        pm.pop(g, None)
    for st in ((save.get('AnimationStateSaveData') or {}).get('States') or {}).values():
        if isinstance(st, dict):
            st.pop(g, None)
    for v in (save.get('PlayerUnitsSaveData') or {}).values():
        if isinstance(v, dict):
            v.pop(g, None)
    ((save.get('CampaignRandomSaveData') or {}).get('LevelUpPrayerAttemptIndexByUnitGuid') or {}).pop(g, None)
    (((save.get('ReportSaveData') or {}).get('LevelUpTimeData') or {}).get('UnitExpMaxStamp') or {}).pop(g, None)
    bo = (save.get('BedOwnerSaveData') or {}).get('BedOwnerMap') or {}
    for k in [k for k, v in bo.items() if v == g]:
        del bo[k]
    rec = save.get('RecreationSaveData') or {}
    if isinstance(rec.get('Recreations'), list):
        rec['Recreations'] = [r for r in rec['Recreations'] if r.get('PerformerGuid') != g]
    evs = (((save.get('EventSaveData') or {}).get('EventSourceData') or {}).get('EventInstanceSaveData') or {})
    for e in evs.values():
        if isinstance(e, dict) and g in (e.get('TargetUnits') or []):
            e['TargetUnits'] = [x for x in e['TargetUnits'] if x != g]
    return gear


def remove_clan(save):
    gone = []
    for g, _ in clan_units(save):
        gone.append(g)
        gone += remove_unit(save, g)
    return gone


# ------------------------------------------------------------------ inject (plan 1.5)
def difficulty_keys(save):
    """The difficulty holder keys the save's own clan units carry (same keys on every unit)."""
    ec = ec_of(save)
    for g, _ in clan_units(save):
        ks = sorted(h['Key'] for h in (comp(ec.get(g), 3) or {}).get('AffecterHolders', []) if DIFF_RE.match(h['Key']))
        if ks:
            return ks
    return None


def retarget_difficulty(comps, want):
    """Swap the unit's Player(Management)Difficulty holders for the target world's (want = difficulty_keys)."""
    a = comp(comps, 3)
    if not a or not want:
        return False
    hs = a.get('AffecterHolders') or []
    old = [h for h in hs if DIFF_RE.match(h['Key'])]
    if sorted(h['Key'] for h in old) == want:
        return False
    tmpl = old[0] if old else {'Stack': 1, 'Duration': 0.0, 'InitialDuration': 0.0}
    rest = [h for h in hs if not DIFF_RE.match(h['Key'])]
    at = next((i for i, h in enumerate(hs) if DIFF_RE.match(h['Key'])), len(rest))
    new = [dict(tmpl, Key=k) for k in want]
    a['AffecterHolders'] = rest[:at] + new + rest[at:]
    return True


def next_free_pos(posmap, prefer):
    taken = {(p['x'], p['y']) for p in posmap.values() if isinstance(p, dict)}
    for p in prefer:
        if (p['x'], p['y']) not in taken:
            return dict(p)
    first = next((p for p in posmap.values() if isinstance(p, dict)), {'x': 100.5, 'y': 100.5})
    x, y = first['x'], first['y']
    while (x, y) in taken:
        x += 1.0
    return {'x': x, 'y': y}


def inject(save, closure, side='Base', prefer_pos=(), diff_keys=None):
    """Insert one closure with fresh guids for the unit and each item. Returns (new unit guid, item map)."""
    ec = ec_of(save)
    ecd = save['EntityContainerSaveData']
    m = {closure['unit']['guid']: new_guid()}
    for ig in closure['items']:
        m[ig] = new_guid()
    cl = deep_replace(closure, m)
    u = cl['unit']
    g = u['guid']
    comps = u['components']
    ei = comp(comps, 1)
    if ei:
        ei['Faction'], ei['IsDead'] = 1, False
    if diff_keys:
        retarget_difficulty(comps, diff_keys)
    ec[g] = comps
    uc = ecd['UnitContainerSaveData'][side]
    uc.setdefault('UnitSaveDataDictionary', {})[g] = dict(u['registry'], Guid=g)
    uc.setdefault('PlayerGuidList', []).append(g)
    pm = ecd.setdefault('EntityPositions', {}).setdefault(side, {})
    pm[g] = next_free_pos(pm, list(prefer_pos))
    st = (save.get('AnimationStateSaveData') or {}).get('States')
    if isinstance(st, dict):
        for s, a in u['anim'].items():                           # the game keeps a state per side; copy each
            if isinstance(st.get(s), dict):
                st[s][g] = a
    pu = save.get('PlayerUnitsSaveData') or {}
    for k, sub in pu.items():
        if not isinstance(sub, dict):
            continue
        if k in u['pusd']:
            sub[g] = u['pusd'][k]
        else:                                                    # shape of the world's other rows (savegen)
            any_ = next(iter(sub.values()), None)
            sub[g] = [] if isinstance(any_, list) else 0 if isinstance(any_, (int, float)) else {}
    if u.get('prayer') is not None:
        save['CampaignRandomSaveData'].setdefault('LevelUpPrayerAttemptIndexByUnitGuid', {})[g] = u['prayer']
    if u.get('exp_stamp') is not None:
        save['ReportSaveData'].setdefault('LevelUpTimeData', {}).setdefault('UnitExpMaxStamp', {})[g] = u['exp_stamp']
    ic = ecd.setdefault('ItemContainerSaveData', {}).setdefault(side, {})
    for ig, it in cl['items'].items():
        ec[ig] = it['components']
        ic.setdefault('ItemSaveData', {})[ig] = dict(it['registry'] or {}, Guid=ig)
        ic.setdefault('ItemAutoFixable', {})[ig] = bool(it['autofix'])
    return g, {k: v for k, v in m.items() if k != closure['unit']['guid']}


def population(save):
    return len(clan_units(save))


def key_totals(save):
    """amount of each item key in every inventory slot (stackable loot is counted by key)."""
    kt = {}
    for comps in ec_of(save).values():
        for c in comps or []:
            for s in (c.get('Data') or {}).get('ItemSlots') or []:
                if s and s.get('Guid') and s['Guid'] != ZERO and s.get('Key'):
                    kt[s['Key']] = kt.get(s['Key'], 0) + s.get('Amount', 1)
    return kt


def build_hub(host, entries, run_id=None, side='Base'):
    """host: the host's save (not modified). entries: [(closure, manifest)] from every player.
    Returns (hub save, run manifest)."""
    if host['CampaignSaveHeader'].get('CurrentMap', 0) != 0:
        raise Refused('the hub save must be at the settlement (CurrentMap 0)')
    hv = host['CampaignSaveHeader']['Version']
    for _, man in entries:
        if man['home_save_version'] != hv:
            raise Refused(f"unit {man['unit_guid'][:8]} comes from {man['home_save_version']}, the hub is {hv}")
    hub = copy.deepcopy(host)
    run_id = run_id or secrets.token_hex(16)
    diff = difficulty_keys(hub)
    old_pos = [p for g, sd in clan_units(hub) if sd == side
               for p in [hub['EntityContainerSaveData']['EntityPositions'][sd].get(g)] if p]
    removed = remove_clan(hub)
    cg = new_guid()
    stem = 'MPFE_' + run_id[:8]
    hdr = hub['CampaignSaveHeader']
    hdr.update(CampaignGuid=cg, SaveName=stem, IronModeEnabled=False, IsAutoSave=False,
               DisplayName='Foreign Expedition ' + datetime.date.today().isoformat(),
               SaveTime=datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
    hub['ClanSaveData']['CampaignGuid'] = cg
    css = (hub.get('CampaignSettingSaveData') or {}).get('CampaignStartSettingSaveData')
    if css is not None:
        css['IronModeEnabled'] = False
    rows = []
    for cl, man in entries:
        g, im = inject(hub, cl, side, old_pos, diff)
        rows.append({'hub_guid': g, 'export_id': man['export_id'], 'player_uid': man['player_uid'],
                     'home_guid': man['unit_guid'], 'item_map': im})
    hub['ClanSaveData']['CurrentPopulation'] = population(hub)
    run = {'run_id': run_id, 'hub_save_name': stem, 'hub_campaign_guid': cg, 'hub_version': hv,
           'hub_difficulty': diff, 'host_campaign_guid': host['CampaignSaveHeader']['CampaignGuid'],
           'removed_host_guids': removed, 'units': rows, 'key_totals_start': key_totals(hub)}
    return hub, run


# ------------------------------------------------------------------ results (plan 1.7)
def extract_results(hub_end, run, rules, player_uid, loot=None, snapshots=()):
    """Result for one player.
    loot: {item guid: {'found_in_dungeon': bool}} for the items tagged to that player in the hub (dsnet owner table +
          the found-in-dungeon tag). Owner 2026-10-14: only DUNGEON LOOT comes back; crafted / hub stock never does.
    snapshots: earlier hub saves (join-point / autosaves), newest first. Owner 2026-10-14: with death not permanent,
          a unit that died goes home in its state from the LATEST multiplayer save where it was still alive."""
    if hub_end['CampaignSaveHeader']['CampaignGuid'] != run['hub_campaign_guid']:
        raise Refused('this save is not the hub of that run')
    alive = {g for g, _ in clan_units(hub_end)}
    units, carried = [], set()
    for r in run['units']:
        if r['player_uid'] != player_uid:
            continue
        row = {'export_id': r['export_id'], 'home_guid': r['home_guid'], 'hub_guid': r['hub_guid'],
               'status': 'alive' if r['hub_guid'] in alive else 'dead'}
        src = hub_end if row['status'] == 'alive' else None
        if src is None and not rules.get('death_permanent'):
            for snap in snapshots:
                if snap['CampaignSaveHeader']['CampaignGuid'] == run['hub_campaign_guid'] and side_of(snap, r['hub_guid']):
                    src = snap
                    row['from_snapshot'] = snap['CampaignSaveHeader'].get('SaveTime')
                    break
        if src is not None:
            cl = export_unit(src, r['hub_guid'])
            row['closure_end'] = cl
            row.update(level_after=unit_info(src, r['hub_guid'])['level'], exp_after=unit_info(src, r['hub_guid'])['exp'])
            carried |= set(cl['items'])
        units.append(row)
    sent = {v for r in run['units'] for v in r['item_map'].values()}       # the copies that travelled never come back
    ec = ec_of(hub_end)
    amount = {}
    for comps in ec.values():
        for c in comps or []:
            d = c.get('Data') or {}
            for s in list(d.get('ItemSlots') or []) + ([d['TempSlot']] if isinstance(d.get('TempSlot'), dict) else []):
                if s and s.get('Guid') and s['Guid'] != ZERO:
                    amount[s['Guid']] = s.get('Amount', 1)
    items = {}
    for ig, tag in (loot or {}).items():
        if ig in sent or ig not in ec or not (tag or {}).get('found_in_dungeon'):
            continue
        reg = None
        for s in (hub_end['EntityContainerSaveData'].get('ItemContainerSaveData') or {}).values():
            reg = reg or (s.get('ItemSaveData') or {}).get(ig)
        auto = any((s.get('ItemAutoFixable') or {}).get(ig) for s in hub_end['EntityContainerSaveData']['ItemContainerSaveData'].values())
        items[ig] = {'components': copy.deepcopy(ec[ig]), 'registry': copy.deepcopy(reg), 'autofix': auto,
                     'amount': amount.get(ig, 1)}
    # stackable loot merged into a stack (its guid is gone): back by key + amount, capped by what the hub still holds of
    # that key; item data from any hub item of that key (same rule as fe_save.c)
    keytot = key_totals(hub_end)                    # what the hub gained of each key since the start (end - start)
    for k, v in (run.get('key_totals_start') or {}).items():
        keytot[k] = keytot.get(k, 0) - v
    icd = hub_end['EntityContainerSaveData'].get('ItemContainerSaveData') or {}
    for ig, tag in (loot or {}).items():
        tag = tag or {}
        if ig in items or ig in sent or ig in ec or not tag.get('key') or not tag.get('found_in_dungeon'):
            continue
        want = min(max(int(tag.get('amount', 1)), 1), keytot.get(tag['key'], 0))
        if want < 1:
            continue
        tg = next(((g, r) for s in icd.values() for g, r in (s.get('ItemSaveData') or {}).items() if r.get('Key') == tag['key'] and g in ec), None)
        if not tg:
            continue
        items[ig] = {'components': copy.deepcopy(ec[tg[0]]), 'registry': copy.deepcopy(tg[1]), 'autofix': False, 'amount': want, 'stacked': True}
        keytot[tag['key']] -= want
    res = {'fe_result': 1, 'run_id': run['run_id'], 'player_uid': player_uid, 'rules': dict(rules),
           'hub_version': run['hub_version'], 'hub_difficulty': run['hub_difficulty'],
           'units': units, 'items': items if rules.get('items_back') else {}}
    res['result_hash'] = fnv64(canon(res))
    return res


# ------------------------------------------------------------------ apply (plan 1.8)
def ledger_load(path):
    try:
        return load(path)
    except FileNotFoundError:
        return {'claims': {}}


def place_item(save, side, key, ig, unit_guids):
    """Owner decision 4 default: a storage building that accepts the key, else a unit's backpack. True if placed."""
    ec = ec_of(save)
    pol = (save.get('StoragePolicySaveData') or {}).get('StoragePolicies') or {}
    bsd = (save['EntityContainerSaveData'].get('BuildingContainerSaveData') or {}).get(side, {}).get('BuildingSaveData') or {}
    holders = [b for b in bsd if b in pol and (pol[b].get('AvailableItems') or {}).get(key)] + list(unit_guids)
    for h in holders:
        inv = comp(ec.get(h), 5)
        for s in (inv or {}).get('ItemSlots') or []:
            if s.get('Guid') == ZERO:
                return s
    return None


def apply_results(home, result, manifests, ledger_path, out_dir, choices=None, now=None):
    """Bring one result into a NEW save next to nothing (out_dir). manifests: {export_id: export manifest}.
    choices: {export_id: 'expedition'|'home'} for units that changed at home. Returns (path, report).
    Refusals raise Refused and write nothing."""
    choices = choices or {}
    now = now or datetime.datetime.now()
    rid = result['run_id']
    if result.get('result_hash') != fnv64(canon({k: v for k, v in result.items() if k != 'result_hash'})):
        raise Refused('these results are damaged (hash mismatch)')
    led = ledger_load(ledger_path)
    if rid in led['claims']:
        raise Refused(f"Already brought home on {led['claims'][rid]['date']}.")
    for u in result['units']:
        if u['export_id'] not in manifests:
            raise Refused('These results are not for your units.')
    hdr = home['CampaignSaveHeader']
    camp = {manifests[u['export_id']]['home_campaign_guid'] for u in result['units']}
    if camp and camp != {hdr['CampaignGuid']}:
        raise Refused('this save is not the campaign the units came from')
    if hdr['Version'] != result['hub_version']:
        raise Refused(f"Made with {hdr['Version']}; the expedition ran on {result['hub_version']}.")
    counts = home['ReportSaveData']['InGameRecordData'].setdefault('Counts', {})
    if MARK + rid in counts:
        raise Refused('Already brought home (marker in this save).')
    out = copy.deepcopy(home)
    ec = ec_of(out)
    rules = result['rules']
    home_diff = difficulty_keys(out)
    report, need = [], []
    for u in result['units']:
        man = manifests[u['export_id']]
        H = u['home_guid']
        if not side_of(out, H):
            report.append((H, 'not in this save anymore'))
            continue
        changed = progress_sig(export_unit(out, H)) != man['progress_sig']
        if changed and u['export_id'] not in choices:
            need.append(u['export_id'])
            continue
        if changed and choices[u['export_id']] == 'home':
            report.append((H, 'changed at home: home version kept'))
            continue
        if u['status'] == 'dead' and rules.get('death_permanent'):
            remove_unit(out, H, with_gear=True)                  # owner 2026-10-14: the unit and its home gear
            report.append((H, 'died: removed'))
            continue
        if u['status'] == 'dead' and 'closure_end' not in u:
            report.append((H, 'died: no multiplayer save with it alive, comes home as before'))
            continue
        tag = 'died: back to its last multiplayer save' if u['status'] == 'dead' else 'alive'
        if not rules.get('xp_counts'):
            report.append((H, tag + ': experience does not count'))
            continue
        x = deep_replace(u['closure_end'], {u['hub_guid']: H})['unit']
        cur = ec[H]
        old_active = set((comp(cur, 8) or {}).get('ActiveSkills') or [])
        for t in (0, 3, 8):
            xc, cc = comp(x['components'], t), comp(cur, t)
            if xc is None or cc is None:
                continue
            if t == 0:
                for k in ('SchemaVersion', 'ProgressionStats', 'GeneratedStats', 'PermanentStats', 'Stats'):
                    if k in xc:
                        cc[k] = xc[k]
            else:
                cc.clear(); cc.update(xc)
        retarget_difficulty(cur, home_diff)
        pu = out.get('PlayerUnitsSaveData') or {}
        for k in PUSD_TAKEN_ON_APPLY:
            if k in x['pusd'] and isinstance(pu.get(k), dict):
                pu[k][H] = x['pusd'][k]
        qs = ((pu.get('QuickSlots') or {}).get(H) or {}).get('QuickSlots')
        if isinstance(qs, list):
            # F2 finding: the hub world may have fewer quick slots (the game cut a 5-slot bar to 4 on its re-save).
            # Keep the home bar; a skill learned in the hub only fills an empty slot.
            for sk in (comp(cur, 8) or {}).get('ActiveSkills') or []:
                if sk not in old_active and sk not in qs and None in qs:
                    qs[qs.index(None)] = sk
        if x.get('prayer') is not None:
            out['CampaignRandomSaveData'].setdefault('LevelUpPrayerAttemptIndexByUnitGuid', {})[H] = x['prayer']
        if x.get('exp_stamp') is not None:
            out['ReportSaveData'].setdefault('LevelUpTimeData', {}).setdefault('UnitExpMaxStamp', {})[H] = x['exp_stamp']
        lv0, lv1 = man['level'], (comp(cur, 0) or {}).get('ProgressionStats', {}).get('Level')
        report.append((H, f'{tag}: level {lv0} -> {lv1}'))
    if need:
        raise Refused('changed at home since they left, choose per unit: ' + ', '.join(need))
    left = []
    alive_home = [g for g, sd in clan_units(out) if sd == 'Base']
    for ig, it in (result.get('items') or {}).items():
        reg = it['registry'] or {}
        slot = place_item(out, 'Base', reg.get('Key'), ig, alive_home)
        if not slot:
            left.append(ig)
            continue
        ng = new_guid()
        ec[ng] = copy.deepcopy(it['components'])
        ic = out['EntityContainerSaveData']['ItemContainerSaveData'].setdefault('Base', {})
        ic.setdefault('ItemSaveData', {})[ng] = dict(reg, Guid=ng)
        ic.setdefault('ItemAutoFixable', {})[ng] = bool(it['autofix'])
        slot.update(Guid=ng, Key=reg.get('Key'), Amount=it.get('amount', 1), AutoFixed=bool(it['autofix']))
    if left:
        report.append(('items', f'{len(left)} item(s) did not fit; kept in the result'))
    out['ReportSaveData']['InGameRecordData']['Counts'][MARK + rid] = 1
    oh = out['CampaignSaveHeader']
    base = re.sub(r'[^A-Za-z0-9+_-]+', '', oh.get('ClanName') or 'Clan')
    stem = f"{base}_BackFromExpedition_{rid[:8]}__AutoSave_{oh['CampaignGuid'].replace('-', '')}_0"
    oh.update(SaveName=stem, DisplayName=f"{oh.get('ClanName')} - back from expedition",
              IsAutoSave=bool(oh.get('IronModeEnabled')), SaveTime=now.strftime('%Y-%m-%d %H:%M:%S'))
    out['ClanSaveData']['CurrentPopulation'] = population(out)
    probs = check_refs(out)
    if probs:
        raise Refused('the merged save failed the reference check: ' + '; '.join(probs[:5]))
    path = write_new(out, out_dir, stem)
    led['claims'][rid] = {'date': now.strftime('%Y-%m-%d %H:%M'), 'file': path, 'left_items': left}
    _write_json_new(ledger_path, led)
    return path, report


# ------------------------------------------------------------------ integrity
def check_refs(save, gone=()):
    """savegen SG.validate + duplicate guid check + a whole-file scan for guids that must be gone."""
    p = []
    ec = ec_of(save)
    ecd = save['EntityContainerSaveData']
    pu = save.get('PlayerUnitsSaveData') or {}
    seen = {}
    for sd, uc in ecd['UnitContainerSaveData'].items():
        reg = uc.get('UnitSaveDataDictionary') or {}
        ic = (ecd.get('ItemContainerSaveData') or {}).get(sd) or {}
        for g in uc.get('PlayerGuidList') or []:
            if g in seen:
                p.append(f'{sd}: unit {g[:8]} listed twice')
            seen[g] = 1
            if g not in ec: p.append(f'{sd}: unit {g[:8]} has no components')
            if g not in reg: p.append(f'{sd}: unit {g[:8]} not in the unit registry')
            if g not in (ecd.get('EntityPositions') or {}).get(sd, {}): p.append(f'{sd}: unit {g[:8]} has no position')
            for k, sub in pu.items():
                if isinstance(sub, dict) and g not in sub:
                    p.append(f'{sd}: unit {g[:8]} misses its {k} row')
            for ig in item_guids_of(ec.get(g)):
                if ig not in ec: p.append(f'{sd}: item {ig[:8]} of unit {g[:8]} has no entity')
                if ig not in (ic.get('ItemSaveData') or {}): p.append(f'{sd}: item {ig[:8]} not registered')
                if ig not in (ic.get('ItemAutoFixable') or {}): p.append(f'{sd}: item {ig[:8]} has no AutoFixable row')
    holders = {}
    for g, comps in ec.items():
        for ig in item_guids_of(comps):
            if ig in holders:
                p.append(f'item {ig[:8]} held twice ({holders[ig][:8]}, {g[:8]})')
            holders[ig] = g
    if save['ClanSaveData'].get('CampaignGuid') != save['CampaignSaveHeader'].get('CampaignGuid'):
        p.append('clan record and header carry different campaign ids')
    if save['ClanSaveData'].get('CurrentPopulation') != population(save):
        p.append(f"population {save['ClanSaveData'].get('CurrentPopulation')} != roster {population(save)}")
    if gone:
        gs = set(gone)
        hits = []

        def walk(o, path):
            if isinstance(o, dict):
                for k, v in o.items():
                    if k in gs: hits.append(path + '/' + k)
                    walk(v, path + '/' + k)
            elif isinstance(o, list):
                for i, v in enumerate(o): walk(v, f'{path}[{i}]')
            elif isinstance(o, str) and o in gs:
                hits.append(path + '=' + o)
        walk(save, '')
        p += [f'dangling reference {h}' for h in hits[:20]]
    return p
