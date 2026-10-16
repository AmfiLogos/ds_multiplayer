#!/usr/bin/env python3
"""Lane fights game test on copy O (hidden, muted; cmrig.py): Custom Modes page > Gallery and Test Fights.

Checks: the page lists Gallery (30) and Test Fights (40); the Gallery loads live wiki builds; "Save to My Units" puts a
gallery team into My Units; "Test Fight" from the Gallery opens Test Fights with that squad; Test Fights with the My Units
team: Boss fight Golem -> Start -> the world loads (rig status campaign), the squad and the boss are there, units ordered
to the boss fight it (HP changes); back at the title the test world is absent from the Load list and the multiplayer host
list (Your save); Arena and DPS Check the same way. usage: tf_test.py <out dir> [phase ...] (phases: gallery boss arena dps)
"""
import json, os, sys, time, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmrig
from cmrig import Copy, Checks, log, I, X
r = cmrig.r

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(cmrig.WT, 'ext/fights13/out', time.strftime('tf_%Y%m%d-%H%M%S')))
PH = sys.argv[2:] or ['gallery', 'boss', 'arena', 'dps']
os.makedirs(OUT, exist_ok=True)
LIB = os.environ.get('FIGHTS_LIB') or os.path.join(OUT, 'library.json')   # My Units file of this run (DSMP_UNITS_LIB)
DATA = os.path.join(I, 'data')                     # with multiplayer on, the copy's game data folder
C = Checks(OUT)


def rig(cmd, **kw):
    return r.send(X, dict(kw, cmd=cmd), timeout=30, connect_wait=0)


def cm(c, **kw):
    """lane fights' own test call (cm_win.c cmf_test): state / open / set {name,index} / click {name} / type / close"""
    rc, out = c.call('cmf_test', json.dumps(kw, separators=(',', ':')))
    try:
        return json.loads(out)
    except Exception:
        return {'ok': False, 'raw': out[:300], 'rc': rc}


def fstate(c):
    return cm(c, cmd='state')


def wait(pred, what, limit=120, every=1.0):
    t0 = time.time(); last = None
    while time.time() - t0 < limit:
        try:
            last = pred()
            if last:
                return last
        except Exception as e:
            last = repr(e)
        time.sleep(every)
    log('timeout:', what, str(last)[:300])
    return None


def scene():
    try:
        return rig('status').get('scene')
    except Exception:
        return None


def go_home(c):
    c.click('title:Custom Modes', wait=1.5)
    return c.state().get('cm', {}).get('home', {})


def open_fights_from_home(c):
    back_to_title(c)
    st = go_home(c)
    c.click('home:Test Fights', wait=1.0)
    return wait(lambda: (lambda s: s if 'fights' in s.get('windows', []) else None)(fstate(c)), 'Test Fights window', 20)


def engine_ready(c, limit=240):
    return wait(lambda: (lambda s: s if s.get('tf', {}).get('engine') in (2, 3) else None)(fstate(c)), 'engine', limit, 2)


def start_and_load(c, name):
    t0 = time.time()
    c.click('fights:start', wait=0.5)
    s = wait(lambda: scene() == 2 or None, f'{name}: world loaded', 240, 2)
    C(f'{name}: Start loads the test world (scene campaign)', s, f'{time.time() - t0:.1f} s; tf {fstate(c).get("tf") if not s else ""}')
    if not s:
        return False
    time.sleep(6)
    return True


def world_units():
    u = rig('list_units')
    return u.get('units', []) if u.get('ok') else []


def fight_check(name, want_key=None, n_squad=None, order=True, combat=True):
    us = world_units()
    pl = [u for u in us if u['faction'] == 1 and not u['dead']]
    foes = [u for u in us if u['faction'] != 1 and not u['dead'] and u.get('x') is not None]
    C(f'{name}: squad in the world', n_squad is None or len(pl) >= n_squad, f'{len(pl)} player units: {[u["name"] for u in pl][:6]}')
    if want_key:
        b = [u for u in us if u['key'] == want_key]
        C(f'{name}: {want_key} present and alive', b and not b[0]['dead'], json.dumps(b[:1])[:300])
    hp0 = {u['guid']: u['hp'] for u in us}
    if order and pl and foes:
        tgt = min(foes, key=lambda f: min((f['x'] - p['x']) ** 2 + (f['y'] - p['y']) ** 2 for p in pl if p.get('x') is not None))
        for p in pl:
            rig('order_move', guid=p['guid'], x=tgt['cell'][0], y=tgt['cell'][1])
    rig('set_speed', speed=2)
    if not combat:
        time.sleep(10); rig('set_speed', speed=0)
        C(f'{name}: the world runs (squad still there after 10 s at speed 2)', len([u for u in world_units() if u['faction'] == 1 and not u['dead']]) >= len(pl), '')
        return
    changed = wait(lambda: [u for u in world_units() if u['guid'] in hp0 and abs(u['hp'] - hp0[u['guid']]) > 0.5] or None, f'{name}: combat', 90, 3)
    C(f'{name}: the fight starts (HP changes)', changed, json.dumps([(u['name'], round(hp0[u['guid']]), round(u['hp'])) for u in (changed or [])][:6]))
    rig('set_speed', speed=0)


def test_display_names():
    """DisplayName of every save of the listed test worlds (what the Load list shows)"""
    out = set()
    try:
        g = json.load(open(os.path.join(DATA, 'Mods/custom-modes/test_worlds.json')))['worlds']
    except Exception:
        return out
    for p in glob.glob(os.path.join(DATA, 'Saves', '*.json')):
        if any(x in os.path.basename(p) for x in g):
            h = json.load(open(p)).get('CampaignSaveHeader', {})
            out.add(h.get('DisplayName', ''))
    return out


LAST_DISP = set(); SEEN_G = set()
def files_of(gs):
    return [os.path.basename(p) for p in glob.glob(os.path.join(DATA, 'Saves', '*.json')) if any(x in p for x in gs)]
def slots_ok(name, where, sl, shown_min=1):
    disp = test_display_names() | LAST_DISP; titles = sl.get('slots', [])
    bad = [t for t in titles if t in disp]
    return C(f'{name}: test world absent from {where}', sl.get('ok') and sl.get('open') and len(titles) >= shown_min and not bad,
             f'test world shown as {sorted(disp)}; list: {titles[:8]}')


def back_to_title(c):
    if scene() != 1:
        rig('mpm_quit_title')
        wait(lambda: scene() == 1 or None, 'title', 120, 2)
    c.wait_title()


def load_list_check(c, name, test_names=None):
    rig('mpm_quit_title')
    ok = wait(lambda: scene() == 1 or None, 'title', 120, 2)
    C(f'{name}: back at the title', ok)
    LAST_DISP.update(test_display_names())               # read before the files go
    tn_before = test_names_now()[1]
    c.wait_title()
    left = wait(lambda: not files_of(SEEN_G) or None, 'test world removed', 20, 1)
    C(f'{name}: the test world saves are removed on the title', left and SEEN_G, f'guids {sorted(SEEN_G)}; left: {files_of(SEEN_G)}')
    cont = c.state().get('cm', {}).get('fights', {}).get('continue', '')
    g = []
    try: g = json.load(open(os.path.join(DATA, 'Mods/custom-modes/test_worlds.json')))['worlds']
    except Exception: pass
    C(f'{name}: the title Continue button does not point at the test world', cont and not any(x in cont for x in SEEN_G), f'continue = {cont}')
    rig('mpm_open_load'); time.sleep(4)
    slots_ok(name, 'the title Load list', rig('mpm_slots'))
    c.cap(f'{name.replace(" ", "_")}_loadlist')
    rig('mpm_list', close=1); time.sleep(1.5)


def mp_host_list_check(c, name, test_names=None):
    for b in (1, 121, 7):                                # Multiplayer > Host a game: Your save > Host from a save
        rig('mpm_press', button=b); time.sleep(2.5)
    l = rig('mpm_list')
    if l.get('open') and l.get('toggle') == 0 and l.get('toggle_text'):
        rig('mpm_list', flip=1); time.sleep(1.5)        # the multiplayer list hides single-player worlds by default: show them
    slots_ok(name, 'the multiplayer host list', rig('mpm_slots'), shown_min=0)   # main: this list shows multiplayer worlds only (no toggle)
    c.cap(f'{name.replace(" ", "_")}_hostlist')
    rig('mpm_list', close=1); time.sleep(1.5)
    for _ in range(3):
        if rig('mpm_status').get('state') == 'title_menu':
            break
        rig('mpm_press', button=3); time.sleep(1.5)      # Back


def test_names_now():
    try:
        g = json.load(open(os.path.join(DATA, 'Mods/custom-modes/test_worlds.json')))['worlds']
    except Exception:
        return [], []
    files = [os.path.basename(p) for p in glob.glob(os.path.join(DATA, 'Saves', '*.json')) if any(x in p for x in g)]
    return g, [f[:-5] for f in files]


def set_value(c, name, index):
    return cm(c, cmd='set', name=name, index=index)


def mode_index(st, mode):
    order = ['DPS Check', 'Boss fight', 'Custom fight']
    return order.index(mode)


def main():
    cmrig.install()
    seed = glob.glob(os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_*_0.json'))[0]
    os.makedirs(os.path.join(DATA, 'Saves'), exist_ok=True)
    import shutil; shutil.copy(seed, os.path.join(DATA, 'Saves', os.path.basename(seed)))   # one normal world in the lists
    extra = {'DSMP_UNITS_LIB': LIB}
    with Copy(['multiplayer', 'custom-modes', 'units-driver'], OUT, extra=extra, lane='fights') as c:
        st = c.wait_title()
        home = go_home(c)
        btns = home.get('buttons', '')
        C('home page lists Gallery then Test Fights', 'Gallery' in btns and 'Test Fights' in btns and btns.index('Gallery') < btns.index('Test Fights'), btns)
        c.cap('p1_home')
        if 'gallery' in PH:
            c.click('home:Gallery', wait=1.0)
            g = wait(lambda: (lambda s: s if s.get('gal', {}).get('items', 0) > 0 else None)(fstate(c)), 'gallery items', 60, 2)
            C('Gallery shows live wiki builds', g, json.dumps((g or fstate(c)).get('gal'))[:300])
            c.cap('p2_gallery_teams')
            c.click('gallery:c0.save', wait=0.5)
            s = wait(lambda: (lambda s: s if 'Saved to My Units' in s.get('gal', {}).get('status', '') or 'problem' in s.get('gal', {}).get('status', '') else None)(fstate(c)), 'save', 30)
            lib = json.load(open(LIB)) if os.path.exists(LIB) else {}
            C('Gallery team saved to My Units', s and 'Saved to My Units' in s['gal']['status'] and len(lib.get('teams', [])) >= 1,
              f"{(s or {}).get('gal', {}).get('status')} / teams {len(lib.get('teams', []))} units {len(lib.get('units', []))}")
            c.cap('p3_saved')
            c.click('gallery:f.kind1', wait=0.5)
            g = wait(lambda: (lambda s: s if s.get('gal', {}).get('items', 0) > 0 and not s['gal'].get('loading') else None)(fstate(c)), 'gallery units', 60, 2)
            C('Gallery Units tab loads', g, json.dumps((g or {}).get('gal'))[:200])
            c.click('gallery:c0.test', wait=0.5)
            f = wait(lambda: (lambda s: s if 'fights' in s.get('windows', '') and s.get('tf', {}).get('src') == 1 else None)(fstate(c)), 'fights from gallery', 120, 2)
            C('Test Fight from the Gallery opens Test Fights with that build', f, json.dumps((f or fstate(c)).get('tf'))[:300])
            e = engine_ready(c)
            C('Test Fights engine ready (wiki files downloaded / cached)', e and e['tf']['engine'] == 2, json.dumps((e or {}).get('tf'))[:300])
            c.cap('p4_fights_gallery')
            cm(c, cmd='close', win='fights'); cm(c, cmd='close', win='gallery'); time.sleep(1)
            c.req('key', code=27); time.sleep(1)
        if 'boss' in PH:
            f = open_fights_from_home(c)
            C('Test Fights opens from the page', f)
            e = engine_ready(c)
            C('engine ready', e and e['tf']['engine'] == 2, json.dumps((e or {}).get('tf'))[:300])
            c.click('fights:l.src0', wait=0.8)
            c.click('fights:l.team0', wait=0.8)
            s = fstate(c)
            C('My Units team picked as the squad', s.get('tf', {}).get('squad', 0) >= 1, json.dumps(s.get('tf')))
            set_value(c, 'fights:r.mode', 1); time.sleep(0.5)
            set_value(c, 'fights:r.boss', 0); time.sleep(0.5)
            c.cap('p5_boss_setup')
            n = fstate(c)['tf']['squad']
            if start_and_load(c, 'Boss Golem'):
                c.cap('p6_boss_world')
                fight_check('Boss Golem', 'UNIT_GiantStoneGolem', n)
                c.cap('p7_boss_fight')
                g, names = test_names_now(); SEEN_G.update(g); LAST_DISP.update(test_display_names())
                C('one test world kept, listed in test_worlds.json', len(g) == 1 and names, f'{g} {names}')
                load_list_check(c, 'Boss Golem', names + g)
                mp_host_list_check(c, 'Boss Golem', names + g)
        if 'arena' in PH:
            f = open_fights_from_home(c); engine_ready(c)
            c.click('fights:l.src0', wait=0.8); c.click('fights:l.team0', wait=0.8)
            set_value(c, 'fights:r.mode', 2); time.sleep(0.5)
            cm(c, cmd='type', name='fights:r.monq', text='Slogel'); time.sleep(0.5)
            c.click('fights:r.hit0', wait=0.8)
            s = fstate(c)
            C('Arena: a monster added', 'fights:r.mondel0' in json.dumps(cm(c, cmd='state').get('controls', {})), '')
            c.cap('p8_arena_setup')
            n = s['tf']['squad']
            if start_and_load(c, 'Arena'):
                fight_check('Arena', None, n)
                c.cap('p9_arena')
                g, names = test_names_now(); SEEN_G.update(g); LAST_DISP.update(test_display_names())
                load_list_check(c, 'Arena', names + g)
        if 'dps' in PH:
            f = open_fights_from_home(c); engine_ready(c)
            c.click('fights:l.src0', wait=0.8); c.click('fights:l.team0', wait=0.8)
            set_value(c, 'fights:r.mode', 0); time.sleep(0.5)
            n = fstate(c)['tf']['squad']
            if start_and_load(c, 'DPS Check'):
                fight_check('DPS Check', None, n, order=False, combat=False)
                c.cap('p10_dps')
                g, names = test_names_now(); SEEN_G.update(g); LAST_DISP.update(test_display_names())
                load_list_check(c, 'DPS Check', names + g)
        C('no guard violation (never on screen)', not c.violations, json.dumps(c.violations)[:300])
    return C.save()


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
