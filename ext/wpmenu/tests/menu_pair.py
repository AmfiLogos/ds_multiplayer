#!/usr/bin/env python3
"""Work package M2 (menu): host choices, single / multiplayer load lists, Multiplayer world tag. One leased pair: host H, guest P.

  menu_pair.py --dylib PATH [--tag NAME] [--pair-wait N] [--lane NAME]

The dylib must be a fake-mouse build (ext/wpmenu/build_mouse.sh: -DTEAMS_MOUSE_TEST), so clicks on the toggle and on
"Remove tag" go through the game's own Input.GetMouseButtonDown / mousePosition.
 0. Multiplayer page: "Your save", "New save" and "Foreign Expedition" (registered by the built-in expedition code), with the
    stock title gap between every button frame from Your save down to Back (owner v12 item 4: measured on the stock title
    buttons of the same copy, +-1 px). Host page (Your save): the Rules panel is open beside the column with its 9 rows, no
    Rules cog button, no Done (owner v12 item 5).
 1. Fresh mp_tags.txt + mp_history.json on both. H: Your save > Host from a save: the load list is the multiplayer list
    (no tagged world yet: every world hidden, toggle "Show single-player worlds"). H hosts the demo save alone: no tag.
 2. P joins, H presses Start: both copies tag the world with both names; 0 desync (fingerprints equal for 40 s).
 3. H quits to the title, Load Campaign: the single-player list hides the tagged world; the toggle (real click) shows it
    with "Multiplayer with <H> and <P>" + "Remove tag".
 4. Real click on "Remove tag": the box; Cancel keeps it; again + Remove: tag gone. Toggle off: the world now shows
    (single-player again). Your save > Host from a save: the multiplayer list now hides it.
 5. Restart H: still no tag; the demo save file is byte-identical and loads.
Writes ext/wpmenu/out/<tag>/test.log, result.json and frames."""
import hashlib, json, os, re, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1])
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('menu_%H%M%S')
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, BACK=3, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15, HOST_SAVE=121, HOST_NEW=122, HOST_FE=123)
LANE = A[A.index('--lane') + 1] if '--lane' in A else 'm2menu'
results = {}


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOG.write(line + '\n'); LOG.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)


def tags(x):
    p = os.path.join(rig.idir(x), 'mp_tags.txt')
    if not os.path.exists(p): return None
    return {l.split('\t')[0]: l.rstrip('\n').split('\t')[1:] for l in open(p) if l.strip()}


def main():
    pair = None
    for _ in range(240):
        pair = rig.lease(LANE)
        if pair: break
        time.sleep(30)
    if not pair: log('no free pair'); return
    H, P = pair[0], pair[1]
    c = lambda inst, **k: rig.send(inst, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]
    mid = lambda r: ((r[0] + r[2]) / 2, (r[1] + r[3]) / 2)

    def mouse(x, r):
        px, py = mid(r); c(x, cmd='ui_teams', action='mouse', x=px, y=py, button=0); time.sleep(1.5)

    def wait(what, pred, timeout=600, every=2):
        t0 = time.time()
        while time.time() - t0 < timeout:
            r = pred()
            if r: return r
            time.sleep(every)
        raise RuntimeError('timeout: ' + what)

    def shot(x, name):
        p = os.path.join(OUT, f'{x}_{name}.png')
        log('frame', p, c(x, cmd='mpm_shot', path=p, how='backbuffer').get('ok'))

    def fp(x, after):
        out = {}
        for line in open(os.path.join(rig.idir(x), 'fp.log')):
            q = line.split()
            if len(q) >= 5 and int(q[0]) > after: out[int(q[0])] = q[1]
        return out

    def lst(x, **k):
        return c(x, cmd='mpm_list', **k)

    def case(l, guid):
        return next((w for w in l.get('cases', []) if w['guid'] == guid), None)

    def trow(x, guid):
        r = c(x, cmd='mpm_tag')
        return r, next((w for w in r.get('rows', []) if w['guid'] == guid), None)

    busy = [x for x in (H, P) if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
    if busy:
        log('copies already running outside this lease:', busy); rig.release(pair, LANE); return
    stamp = time.strftime('%Y%m%d-%H%M%S')
    for x in (H, P):
        for f in ('mp.json', 'mp_tags.txt', 'mp_history.json'):
            p = os.path.join(rig.idir(x), f)
            if os.path.exists(p): os.replace(p, f'{p}.before_wpmenu_{stamp}')
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        for x in (H, P): rig.start(x, window=True, fps=30)
        for x in (H, P): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(3)
        for _ in range(3):                                   # a copy with old expedition results: their window first ("Later")
            if not c(H, cmd='mpm_fewin').get('mode'): break
            c(H, cmd='mpm_press', button=110); time.sleep(1)
        sf = sorted(c(H, cmd='mpm_status').get('stock_frames', []), key=lambda r: -r[3])   # stock title buttons, top first
        sgaps = [round(sf[i][1] - sf[i + 1][3], 1) for i in range(len(sf) - 1)]
        stock_gap = min(sgaps) if sgaps else None                       # the gap between two adjacent stock buttons
        log('stock frames', sf, 'gaps', sgaps)
        # 0. host choices
        press(H, 'MULTIPLAYER'); time.sleep(1)
        tb = c(H, cmd='mpm_status').get('title', {})
        on = {n: tb.get(str(B[n]), {}) for n in ('HOST_SAVE', 'HOST_NEW', 'HOST_FE')}
        log('host choices', json.dumps(on))
        fr = c(H, cmd='mpm_status').get('frames', {})
        ids = (B['HOST_SAVE'], B['HOST_NEW'], B['HOST_FE'], 124, 125, 126, 14, 45, 28, B['BACK'])   # 123-126 host choices (FE, PvP, ... from Custom Modes), 14 Join by address, 45 Join a friend, 28 Expedition results
        col = sorted([fr[str(i)] for i in ids if str(i) in fr], key=lambda r: -r[3])
        gaps = [round(col[i][1] - col[i + 1][3], 1) for i in range(len(col) - 1)]   # Your save .. Join .. Back, top to bottom
        spaced = stock_gap is not None and stock_gap > 0 and len(col) >= 5 and all(abs(g - stock_gap) <= 1 for g in gaps)
        shot(H, '0_multiplayer_page')
        press(H, 'HOST_SAVE'); time.sleep(1)
        ms = c(H, cmd='mpm_status'); rp = ms.get('rules_panel', {})
        rules_open = ms.get('state') == 'title_host' and rp.get('title') == 1 and rp.get('title_rows') == 9 and rp.get('done') == 0 \
            and not ms.get('title', {}).get('21', {}).get('active')                     # 21 = MPB_SETTINGS, the old cog row
        shot(H, '0_host_page_rules')
        check('0. Multiplayer page: Your save / New save / Foreign Expedition shown, stock gap between the buttons; host page: Rules open beside it',
              all(v.get('active') for v in on.values()) and on['HOST_SAVE'].get('text', '').endswith('Your save')
              and 'New save' in on['HOST_NEW'].get('text', '') and 'Foreign Expedition' in on['HOST_FE'].get('text', '') and spaced and rules_open,
              f'{json.dumps(on)} stock gap {stock_gap} column gaps {gaps} rules {rp} settings row {ms.get("title", {}).get("21")}')
        # 1. Your save -> multiplayer list (nothing tagged yet), then host alone
        press(H, 'LOAD_SAVE'); time.sleep(3)
        l1 = lst(H); log('list', json.dumps(l1))
        check('1a. Your save list = multiplayer list: no tagged world, all hidden, toggle "Show single-player worlds"',
              l1.get('open') and l1['mode'] == 1 and l1['shown'] == 0 and l1['hidden'] >= 1 and l1['toggle'] == 1
              and l1['toggle_text'] == 'Show single-player worlds' and all(w['active'] == 0 for w in l1['cases']), '')
        shot(H, '1_host_list_mp')
        mouse(H, l1['toggle_rect']); l1b = lst(H)
        check('1b. toggle (real click): single-player worlds show too', l1b['other'] == 1 and l1b['hidden'] == 0 and l1b['shown'] == len(l1b['cases']),
              f"other {l1b['other']} shown {l1b['shown']} hidden {l1b['hidden']}")
        shot(H, '1_host_list_all')
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        m = re.search(rb'"SaveName"\s*:\s*"([^"]*)"', open(src, 'rb').read()); save_name = m.group(1).decode() if m else None
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        demo_file = os.path.join(rig.idir(H), 'data', 'Saves', f'{save_name}.json') if save_name else None
        sha0 = hashlib.sha256(open(demo_file, 'rb').read()).hexdigest() if demo_file else None
        log('demo save', demo_file, sha0)
        time.sleep(5)
        t1 = tags(H)
        check('1c. host alone in the lobby: no tag', not t1, f'H tags {t1}')
        # 2. P joins
        port = c(H, cmd='mpm_status').get('port')
        press(P, 'MULTIPLAYER'); press(P, 'JOIN_ADDR')
        c(P, cmd='mpm_type', text=f'127.0.0.1:{port}'); time.sleep(0.5)
        c(P, cmd='mpm_press', button=B['CONNECT'])
        wait('P loaded + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 and not s.get('mp', {}).get('needs_state'))(st(P)), timeout=900, every=1)
        press(H, 'START')
        wait('H + P running', lambda: not mp(H).get('paused') and not mp(P).get('paused'), timeout=180)
        t_run = max(st(H)['tick'], st(P)['tick'])
        th = wait('tag on H', lambda: tags(H) or None, timeout=30, every=1)
        tp = wait('tag on P', lambda: tags(P) or None, timeout=30, every=1)
        guid = next(iter(th))
        check('2a. friend joined: host tags the world with both names', len(th) == 1 and len(th[guid]) == 2 and len(set(th[guid])) == 2, f'H {th}')
        check('2b. guest tags the same world with the same names', tp == th, f'P {tp}')
        time.sleep(40)
        a, b = fp(H, t_run), fp(P, t_run)
        common = sorted(set(a) & set(b)); diff = [t for t in common if a[t] != b[t]]
        sh, sp = mp(H), mp(P)
        check('2c. 0 desync (fingerprints equal)', len(common) >= 5 and not diff and not sh.get('desynced') and not sp.get('desynced'),
              f'{len(common)} common ticks, {len(diff)} differ; desync H {sh.get("desynced")} P {sp.get("desynced")}')
        # 3. single-player load list
        c(H, cmd='mpm_quit_title'); time.sleep(2)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'H title', timeout=600)
        time.sleep(4)
        c(H, cmd='mpm_open_load'); time.sleep(3)
        l3 = lst(H); w = case(l3, guid); log('list', json.dumps(l3))
        check('3a. single-player list: the tagged world is hidden, toggle "Show multiplayer worlds"',
              l3['mode'] == 0 and w and w['active'] == 0 and w['tagged'] == 1 and l3['toggle_text'] == 'Show multiplayer worlds' and l3['other'] == 0, f'{w}')
        shot(H, '3_solo_list')
        mouse(H, l3['toggle_rect']); l3b = lst(H); w = case(l3b, guid)
        lst(H, scroll_to=guid); time.sleep(0.5)                # the row into view, as a player scrolls to it
        r, tr = trow(H, guid)
        check('3b. toggle (real click): the world shows with "Multiplayer with <H> and <P>" + Remove tag',
              l3b['other'] == 1 and w and w['active'] == 1 and tr and 'Multiplayer' in tr['name'] and all(n in tr['name'] for n in th[guid]) and tr['remove'] == 1, f'{tr}')
        shot(H, '3_solo_list_other')
        # 4. Remove tag (real clicks)
        open0 = tr['case_open'] if tr else None
        mouse(H, tr['rm_rect']); r, tr2 = trow(H, guid)
        check('4a. real click on Remove tag: the box is up, the row did not fold', r.get('box') == 6 and r.get('pending') == guid and tr2 and tr2['case_open'] == open0,
              f'box {r.get("box")} pending {r.get("pending")}')
        shot(H, '4_box')
        c(H, cmd='mpm_box', action='cancel'); time.sleep(1.5)
        r, tr3 = trow(H, guid)
        check('4b. Cancel: the tag stays', r.get('box') == 0 and tr3 and 'Multiplayer' in tr3['name'] and tags(H) == th, f'{tr3}')
        lst(H, scroll_to=guid); time.sleep(0.5); r, tr3 = trow(H, guid)
        mouse(H, tr3['rm_rect']); c(H, cmd='mpm_box', action='confirm'); time.sleep(1.5)
        r, tr4 = trow(H, guid)
        check('4c. Remove: stock name, no Remove text, line gone', tr4 and 'Multiplayer' not in tr4['name'] and tr4['remove'] == 0 and tags(H) == {}, f'{tr4} file {tags(H)}')
        l4 = lst(H); mouse(H, l4['toggle_rect']); l4b = lst(H); w = case(l4b, guid)
        check('4d. toggle off: the untagged world shows in the single-player list', l4b['other'] == 0 and w and w['active'] == 1 and w['tagged'] == 0, f'{w}')
        shot(H, '4_solo_list_untagged')
        lst(H, close=1); time.sleep(1.5)
        press(H, 'MULTIPLAYER'); press(H, 'HOST_SAVE'); press(H, 'LOAD_SAVE'); time.sleep(3)
        l4c = lst(H); w = case(l4c, guid)
        check('4e. Your save list now hides it (single-player again)', l4c['mode'] == 1 and w and w['active'] == 0, f'{w}')
        shot(H, '4_host_list_untagged')
        lst(H, close=1); time.sleep(1.5)
        # 5. persists + the save
        rig.stop(H); time.sleep(2)
        rig.start(H, window=True, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'H title again', timeout=1500)
        time.sleep(4)
        c(H, cmd='mpm_open_load'); time.sleep(3)
        l5 = lst(H); w = case(l5, guid)
        check('5a. after a restart: no tag, shown in the single-player list', tags(H) == {} and w and w['active'] == 1 and w['tagged'] == 0, f'{w}')
        sha1 = hashlib.sha256(open(demo_file, 'rb').read()).hexdigest() if demo_file else None
        mt = open(demo_file, 'rb').read() if demo_file else b''
        check('5b. the save file is byte-identical, no tag text inside', sha0 and sha0 == sha1 and b'mp_tags' not in mt, f'{sha0} -> {sha1}')
        lst(H, close=1); time.sleep(1)
        c(H, cmd='load_save', file=demo_file, det=0)
        ok = False
        try: ok = bool(rig.wait_for(H, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', 0) > 0, 'H loads the save', timeout=600))
        except Exception as e: log('load', e)
        check('5c. the save loads (no session)', ok)
    except Exception as e:
        log('ERROR', repr(e)); results['error'] = repr(e)
    finally:
        for x in (H, P):
            try: rig.stop(x)
            except Exception as e: log('stop', x, e)
        rig.release(pair, LANE)
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        n = sum(1 for k, v in results.items() if k != 'error' and v)
        log(f'RESULT {n}/{len([k for k in results if k != "error"])} pass', 'ERROR' if 'error' in results else '')


main()
