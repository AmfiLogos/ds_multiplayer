#!/usr/bin/env python3
"""Multiplayer world tag (lane mptag), one leased pair: host H, guest P.

  mptag_pair.py --dylib PATH [--tag NAME]

 1. Fresh mp_tags.txt + mp_history.json moved aside on both. H hosts the demo save alone: no tag (lobby, 5 s).
 2. P joins, H presses Start: within a few seconds both copies' mp_tags.txt have the world with both names. 0 desync (fp equal).
 3. H quits to the title, opens the load list: the world row reads "... Multiplayer  with <H> and <P>" + "Remove tag".
 4. A real mouse click on "Remove tag": the stock box asks, the row does not fold / unfold. Cancel: the tag stays.
    Click again + Remove: the row shows the stock name, no Remove text, the line is gone from mp_tags.txt.
 5. Restart H: the load list still has no tag (persisted). The demo save file is byte-identical to before the session and
    loads (scene 2): the tag never touched the save.
Writes ext/mptag/out/<tag>/test.log, result.json and frames."""
import hashlib, json, os, sys, time
ROOT = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(ROOT, 'rig'))
import rig

A = sys.argv
rig.DYLIB = os.path.abspath(A[A.index('--dylib') + 1])
TAG = A[A.index('--tag') + 1] if '--tag' in A else time.strftime('mptag_%H%M%S')
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', TAG); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, 'test.log'), 'a')
B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15)
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
        pair = rig.lease('mptag')
        if pair: break
        time.sleep(30)
    if not pair: log('no free pair'); return
    H, P = pair[0], pair[1]
    c = lambda inst, **k: rig.send(inst, k, timeout=120)
    st = lambda x: c(x, cmd='status')
    mp = lambda x: st(x).get('mp', {})
    press = lambda x, n: (c(x, cmd='mpm_press', button=B[n]), time.sleep(1.2))[0]

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

    def row(x, guid):
        r = c(x, cmd='mpm_tag')
        return r, next((w for w in r.get('rows', []) if w['guid'] == guid), None)

    busy = [x for x in (H, P) if rig.read_pid(x) and rig.alive(rig.read_pid(x))]
    if busy:
        log('copies already running outside this lease:', busy); rig.release(pair, 'mptag'); return
    stamp = time.strftime('%Y%m%d-%H%M%S')
    for x in (H, P):
        for f in ('mp.json', 'mp_tags.txt', 'mp_history.json'):
            p = os.path.join(rig.idir(x), f)
            if os.path.exists(p): os.replace(p, f'{p}.before_mptag_{stamp}')
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        for x in (H, P): rig.start(x, window=True, fps=30)
        for x in (H, P): rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'{x} title', timeout=1500)
        time.sleep(3)
        # 1. host alone
        press(H, 'MULTIPLAYER'); press(H, 'HOST'); press(H, 'LOAD_SAVE'); time.sleep(2)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        from demo import make_demo_save
        make_demo_save(src)
        import re
        m = re.search(rb'"SaveName"\s*:\s*"([^"]*)"', open(src, 'rb').read()); save_name = m.group(1).decode() if m else None
        c(H, cmd='load_save', file=src, det=0)
        wait('host session + lock', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)))
        demo_file = os.path.join(rig.idir(H), 'data', 'Saves', f'{save_name}.json') if save_name else None
        sha0 = hashlib.sha256(open(demo_file, 'rb').read()).hexdigest() if demo_file else None
        log('demo save', demo_file, sha0, 'header save name', save_name)
        time.sleep(5)
        t1 = tags(H)
        check('1. host alone in the lobby: no tag (file made, empty)', t1 == {}, f'H tags {t1}')
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
        time.sleep(40)                                       # play on: the tag code must not touch the sim
        a, b = fp(H, t_run), fp(P, t_run)
        common = sorted(set(a) & set(b)); diff = [t for t in common if a[t] != b[t]]
        sh, sp = mp(H), mp(P)
        check('2c. 0 desync while tagged (fingerprints equal)', len(common) >= 5 and not diff and not sh.get('desynced') and not sp.get('desynced'),
              f'{len(common)} common ticks, {len(diff)} differ; desync H {sh.get("desynced")} P {sp.get("desynced")}')
        # 3. load list on the title
        c(H, cmd='mpm_quit_title'); time.sleep(2)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'H title', timeout=600)
        time.sleep(4)
        c(H, cmd='mpm_open_load'); time.sleep(3)
        r, w = row(H, guid)
        log('rows', json.dumps(r))
        names = th[guid]
        check('3. load list world row: "Multiplayer" + both names + Remove tag', w and 'Multiplayer' in w['name'] and all(n in w['name'] for n in names) and w['remove'] == 1,
              f'{w}')
        shot(H, 'list_tagged')
        sl = c(H, cmd='mpm_slots')
        log('slots', json.dumps(sl))
        # 4. real click -> box -> Cancel; click -> Remove
        open0 = w['case_open'] if w else None
        c(H, cmd='mpm_tag', click=w['i'] if w else -1, real=1); time.sleep(1.5)
        r, w2 = row(H, guid)
        check('4a. real click on Remove tag: our box is up, the row did not fold / unfold', r.get('box') == 6 and r.get('pending') == guid and w2 and w2['case_open'] == open0,
              f'box {r.get("box")} pending {r.get("pending")} open {open0} -> {w2 and w2["case_open"]}')
        shot(H, 'box')
        c(H, cmd='mpm_box', action='cancel'); time.sleep(1.5)
        r, w3 = row(H, guid)
        check('4b. Cancel: tag stays (row + file)', r.get('box') == 0 and w3 and 'Multiplayer' in w3['name'] and tags(H) == th, f'{w3} file {tags(H)}')
        c(H, cmd='mpm_tag', click=w3['i'] if w3 else -1, real=1); time.sleep(1.5)
        c(H, cmd='mpm_box', action='confirm'); time.sleep(1.5)
        r, w4 = row(H, guid)
        check('4c. Remove: stock name back, no Remove text, line gone', w4 and 'Multiplayer' not in w4['name'] and w4['remove'] == 0 and tags(H) == {}, f'{w4} file {tags(H)}')
        shot(H, 'list_removed')
        # 5. persists + the save
        rig.stop(H); time.sleep(2)
        rig.start(H, window=True, fps=30)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'H title again', timeout=1500)
        time.sleep(4)
        c(H, cmd='mpm_open_load'); time.sleep(3)
        r, w5 = row(H, guid)
        check('5a. after a restart: still no tag', w5 and 'Multiplayer' not in w5['name'] and w5['remove'] == 0 and tags(H) == {}, f'{w5}')
        sha1 = hashlib.sha256(open(demo_file, 'rb').read()).hexdigest() if demo_file else None
        mt = open(demo_file, 'rb').read() if demo_file else b''
        check('5b. the save file is byte-identical, no tag text inside', sha0 and sha0 == sha1 and b'mp_tags' not in mt, f'{sha0} -> {sha1}')
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
        rig.release(pair, 'mptag')
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        n = sum(1 for k, v in results.items() if k != 'error' and v)
        log(f'RESULT {n}/{len([k for k in results if k != "error"])} pass', 'ERROR' if 'error' in results else '')


main()
