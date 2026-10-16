#!/usr/bin/env python3
"""Weapon-slot mod, host only: does a weapon equip / swap keep the stock guest's SkillComponent equal?

  ws_equip_pair.py <PAIR> [--minutes 6] [--dylib PATH]      (env WEAPONSLOT_ROOT = launcher folder with test_env/pair)

Host (first letter, with the mod: 5 quick slots, slot 0 = weapon skill), guest (second letter, stock: 4 slots = the
host's first 4). Full-state check ON. Steps, all through the real order path (ord_inject -> ord capture -> lockstep):
 1. host fills its quick slots 1-4 of unit U with 4 non-weapon skills (SetQuickSlotRequested x4): the guest's bar is full
 2. host selects U and equips the WoodClub from U's inventory (EquipItemRequested; weapon skill SKILL_StunCharge) over
    the equipped CarapaceGreatSword (SKILL_WhirlwindSlash): a weapon swap
 3. the units fight; the host casts SKILL_StunCharge every --cast-every s (UseSkillTargetRequested)
 4. at half time the host swaps back to the greatsword
 5. pause, settle, both copies save; U's SkillComponent and every EntityComponents entry compared.
Reuses tests/weaponslot_pair.py (imported) for the rig setup. Result: <PAIRDIR>/equip_result.json."""
import importlib.util, json, os, sys, time

WS = os.path.expanduser('~/ds_launcher_weaponslot/tests/weaponslot_pair.py')
spec = importlib.util.spec_from_file_location('weaponslot_pair', WS)
w = importlib.util.module_from_spec(spec); spec.loader.exec_module(w)   # module level: rig on test_env/pair, dylib snapshot
rig, m, log = w.rig, w.m, w.m.log
H, G = w.H, w.G
MINUTES = w.arg('--minutes', 6.0)
CAST_EVERY = w.arg('--cast-every', 8.0)
U = '477ad25a-7c22-43f5-be28-db055895297e'          # greatsword unit of the demo save (host-owned)
NEW_W, NEW_SK = 'ITEM_WoodClub', 'SKILL_StunCharge'
OLD_W, OLD_SK = 'ITEM_CarapaceGreatSword', 'SKILL_WhirlwindSlash'


def comps_of(path):
    return json.load(open(path, encoding='utf-8-sig'))['EntityComponentSaveData']['EntityComponents']


def save_both(name):
    out = {}
    for x in 'AB':
        r = m.c(x, cmd='ord_save', name=name)
        p = os.path.join(rig.idir(H if x == 'A' else G), 'data', 'Saves', name + '.json')
        for _ in range(30):
            if os.path.exists(p):
                break
            time.sleep(1)
        out[x] = p if r.get('ok') and os.path.exists(p) else None   # the guest's save is blocked in multiplayer
    return out


def unit_view(path, g):
    c = {e['Type']: e['Data'] for e in comps_of(path)[g]}
    d = json.load(open(path, encoding='utf-8-sig'))
    return {'skills': {k: c[8].get(k) for k in ('BasicSkills', 'ActiveSkills', 'PassiveSkills', 'AutoUse') if k in c[8]},
            'equip': [s['Key'] for s in c[6]['ItemSlots']], 'inv': [s['Key'] for s in c[5]['ItemSlots']],
            'quick': d['PlayerUnitsSaveData']['QuickSlots'].get(g, {}).get('QuickSlots')}


def inv_index(path, g, key):
    return [s['Key'] for s in {e['Type']: e['Data'] for e in comps_of(path)[g]}[5]['ItemSlots']].index(key)


def ord_view(g):
    """live view on both copies (the guest cannot save: 'guest save blocked'): quick slots, auto-use keys (one per
    learned active skill: AddSkill adds the _autoUse entry), cooldown keys"""
    out = {}
    for x in 'AB':
        r = m.c(x, cmd='ord_read', guid=g)
        out[x] = {'quick': r.get('quick'), 'active_auto': sorted((r.get('auto') or {}).items()), 'cooldowns': sorted((r.get('cooldowns') or {}))}
    out['learned_equal'] = out['A']['active_auto'] == out['B']['active_auto']
    return out


def equip(idx):
    m.c('A', cmd='ord_select', guids=U, group=3003)
    return m.inject('A', 'EquipItemRequested', i32_16=m.st('A').get('map', 0), guid_20=U, i32_36=0, guid_40=U, i32_56=idx)


def main():
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    port = rig.pair_port(H)
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'partial_resync': False},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    res = {'pair': H + G, 'steps': {}}
    try:
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        w.make_demo_save(src)
        import subprocess
        r = subprocess.run([sys.executable, os.path.expanduser('~/ds_weaponslot/migrate_saves.py'), '--apply', src],
                           capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        log('migrate:', (r.stdout + r.stderr).strip()[-200:])
        rig.start('A', fps=10)
        rig.wait_for('A', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
        log('host load', m.c('A', cmd='load_save', file=src, det=1))
        m.wait('host locked', lambda: m.st('A').get('det', {}).get('locked'))
        rig.start('B', fps=30)
        rig.wait_for('B', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
        m.wait('guest joined', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(m.st('B')), timeout=600)
        time.sleep(5)
        us = m.units('A')
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        mine = {'A': [u for u in players if u['guid'] == U] + [u for u in players if u['guid'] != U][:1]}
        mine['B'] = [u for u in players if u not in mine['A']][:2]
        for x in 'AB':
            for u in mine[x]:
                m.c(x, cmd='mp_claim', guid=u['guid'])
            m.c(x, cmd='ord_hover', n=0)
        time.sleep(3)
        q = lambda x: w.cooldowns(x, U)[1]
        res['quick_start'] = (q('A'), q('B'))
        # 1. host fills slots 1-4 with non-weapon skills (quick-slot orders)
        fill = [k for k in q('A')[1:5] if k] or []
        for i, k in enumerate(['SKILL_Slay', 'SKILL_DestructiveSplit', 'SKILL_BladeGrinding', 'SKILL_EmergencyEscape']):
            m.inject('A', 'SetQuickSlotRequested', i32_16=m.st('A').get('map', 0), guid_20=U, str_40=k, i32_48=i + 1)
            time.sleep(1)
        time.sleep(3)
        res['steps']['1_live'] = ord_view(U)
        res['steps']['1_fill'] = {'host_quick': q('A'), 'guest_quick': q('B'), 'host_auto': w.auto_of('A', U), 'guest_auto': w.auto_of('B', U)}
        log('step 1', res['steps']['1_fill'])
        s0 = save_both('ws_eq_0')
        res['steps']['1_saved'] = {x: unit_view(p, U) for x, p in s0.items() if p}
        # 2. equip the club over the greatsword
        idx = inv_index(s0['A'], U, NEW_W)
        r = equip(idx); time.sleep(4)
        s1 = save_both('ws_eq_1')
        v1 = {x: unit_view(p, U) for x, p in s1.items() if p}
        res['steps']['2_equip_club'] = {'inject': r, 'inv_index': idx, 'view': v1, 'live': ord_view(U), 'skills_equal': v1.get('A', {}).get('skills') == v1.get('B', {}).get('skills')}
        log('step 2', json.dumps(res['steps']['2_equip_club'])[:1500])
        # 3. fight + casts; 4. swap back at half time
        t_start, t0, last_cast, phase, swapped = m.st('A')['tick'], time.time(), 0, 0, False
        casts = {'sent': 0, 'host_cd': 0, 'guest_cd': 0}
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        near_portal = lambda c_: 446 <= c_[0] <= 472 and 181 <= c_[1] <= 205
        stuck, popups, last = [0, time.time()], 0, 0
        while time.time() - t0 < MINUTES * 60:
            el = time.time() - t0
            if el - phase * 90 >= 0:
                try:
                    live = [e for e in m.units('A') if e['faction'] == 3 and not e['dead'] and 'cell' in e and not near_portal(e['cell'])]
                except KeyError:
                    live = []
                for x in 'AB':
                    for u in mine[x]:
                        tgt = (min(live, key=lambda e: d2(u, e))['cell'] if live else u['cell']) if phase % 2 == 0 else u['cell']
                        m.order_move(x, u['guid'], tgt)
                phase += 1
            if not swapped and el > MINUTES * 30:
                s2 = save_both('ws_eq_2')
                idx2 = inv_index(s2['A'], U, OLD_W)
                r2 = equip(idx2); time.sleep(4)
                s3 = save_both('ws_eq_3')
                v3 = {x: unit_view(p, U) for x, p in s3.items() if p}
                res['steps']['4_swap_back'] = {'inject': r2, 'inv_index': idx2, 'view': v3, 'live': ord_view(U), 'skills_equal': v3.get('A', {}).get('skills') == v3.get('B', {}).get('skills')}
                log('step 4', json.dumps(res['steps']['4_swap_back'])[:1500])
                swapped = True
            if time.time() - last_cast >= CAST_EVERY:
                last_cast = time.time()
                try:
                    us_now = m.units('A')
                except KeyError:
                    us_now = []
                live = [e for e in us_now if e['faction'] == 3 and not e['dead'] and 'cell' in e]
                me = [u for u in us_now if u['guid'] == U and not u['dead']]
                sk = OLD_SK if swapped else NEW_SK
                if live and me:
                    tgt = min(live, key=lambda e: d2(me[0], e))
                    w.cast(U, sk, tgt['guid']); casts['sent'] += 1
                    time.sleep(2)
                    ca, cb = w.cooldowns('A', U)[0].get(sk), w.cooldowns('B', U)[0].get(sk)
                    casts['host_cd'] += bool(ca and ca > 0); casts['guest_cd'] += bool(cb and cb > 0)
            sa = m.st('A')
            if time.time() - last > 30:
                log(f'host tick {sa["tick"]} | desync host {sa.get("mp", {}).get("desyncs")} guest {m.st("B").get("mp", {}).get("desyncs")}'); last = time.time()
            if m.handle_portal():
                stuck[:] = [m.st('A')['tick'], time.time()]; continue
            if sa['tick'] != stuck[0]:
                stuck[:] = [sa['tick'], time.time()]
            elif time.time() - stuck[1] > 15 and popups < 5:
                for x in 'AB':
                    m.c(x, cmd='mp_popup', reason=4, add=0)
                popups += 1; stuck[1] = time.time()
            time.sleep(2)
        res['casts'] = casts
        ma, mb = m.mp('A'), m.mp('B')
        n, first = m.fp_equal_from(t_start)
        m.c('B', cmd='mp_pause', on=1)
        same = m.settle_both()
        res.update({'desyncs': (ma.get('desyncs'), mb.get('desyncs')),
                    'full_checks_ok_fail': (ma.get('full_ok'), ma.get('full_fails'), mb.get('full_ok'), mb.get('full_fails')),
                    'orders_applied': (ma.get('orders'), mb.get('orders')), 'fp_common_ticks': n, 'fp_first_diff_tick': first,
                    'paused_tick': same, 'wall_minutes': round((time.time() - t0) / 60, 1), 'end_live': ord_view(U)})
    except Exception as e:
        import traceback
        res['error'] = repr(e); res['trace'] = traceback.format_exc()[-1500:]
    finally:
        for x in 'AB':
            try:
                rig.stop(x)
            except Exception as e:
                log('stop', x, e)
        res['owner_prefs_unchanged'] = rig.check_prefs()
        json.dump(res, open(os.path.join(w.PAIRDIR, 'equip_result.json'), 'w'), indent=1)
        log('RESULT', json.dumps(res)[:4000])


if __name__ == '__main__':
    main()
