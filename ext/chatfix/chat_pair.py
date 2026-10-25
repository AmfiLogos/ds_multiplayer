#!/usr/bin/env python3
"""chatfix lane (owner feedback 2026-10-20, v0.1.21 A1-A7): the multiplayer chat on a leased hidden pair.
  chat_pair.py --dylib PATH (a fake-mouse build: -DTEAMS_MOUSE_TEST) --out DIR
Host H goes Multiplayer > Host > Load (the lobby), guest G joins by address. Before Start:
  A6  the host opens the chat in its lobby (the Esc menu) and both players see the line; the guest answers while it
      waits for the host to start.
  A5  accents / Cyrillic / Japanese arrive as sent and are drawn in a game font that has them (no '?'); an emoji
      becomes '?' (no game font has it); a 300-byte Cyrillic text is cut in the field at 200 bytes, whole letters.
  A1/A2 a long and a two-line message wrap inside the chat width; each row is as high as its text; rows never overlap.
  A7  the chat canvas sorts above the game's windows (lobby / Esc menu, Teams 32000), under the tooltips (32040+).
After Start: A4 the wheel over the open chat scrolls the chat and leaves the camera zoom alone; over the world it zooms.
Frames (mpm_shot) in DIR. Fails on the old code (box never opens in the lobby, rows overlap, the camera zooms)."""
import json, os, shutil, subprocess, sys, time, traceback
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig

B = dict(MULTIPLAYER=1, HOST=2, LOAD_SAVE=7, START=10, JOIN_ADDR=14, CONNECT=15)
results = {}


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


OUT = arg('--out', '/tmp/chatfix'); os.makedirs(OUT, exist_ok=True)
LOGF = open(os.path.join(OUT, 'run.log'), 'a')


def log(*a):
    line = time.strftime('%H:%M:%S') + ' ' + ' '.join(str(x) for x in a)
    print(line, flush=True); LOGF.write(line + '\n'); LOGF.flush()


def check(name, ok, info=''):
    results[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, str(info)[:900])


def c(x, **k): return rig.send(x, k, timeout=120)
def ms(x): return c(x, cmd='mpm_status')
def chat(x): return ms(x).get('chat', {})
def press(x, n): r = c(x, cmd='mpm_press', button=B[n]); time.sleep(1.2); return r


def wait(what, pred, timeout=600, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = pred()
            if r: return r
        except Exception as e:
            last = e
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def shot(x, tag):
    p = os.path.join(rig.idir(x), f'shot_{tag}.png')
    r = c(x, cmd='mpm_shot', how='backbuffer', path=p); time.sleep(1.5)
    if os.path.exists(p): shutil.copy2(p, os.path.join(OUT, f'{tag}.png'))
    log('shot', tag, r.get('ok'))


def owner_playing():                                     # the owner's game (launcher copy or Modded app); shells that only name the path do not count
    out = subprocess.run(['pgrep', '-fl', 'DSModLauncher/games/|Dungeon Settlers Modded.app'], capture_output=True, text=True).stdout
    return any(l.split(None, 1)[1].split()[0].split('/')[-1] not in ('zsh', 'bash', 'sh', 'python3', 'Python', 'pgrep', 'grep')
               for l in out.splitlines() if len(l.split(None, 1)) == 2)


def overlap(rows):                                       # screen rects [x0, y0, x1, y1] (y up): any two overlap vertically
    rs = sorted((r['rect'] for r in rows), key=lambda r: r[1])
    return any(rs[i + 1][1] < rs[i][3] - 1 for i in range(len(rs) - 1))


def say(x, text, via_keys=False):
    c(x, cmd='mpm_chat', open=1); time.sleep(0.6)
    if via_keys: c(x, cmd='mpm_key', chars=text)       # Unity key events into the field (one UTF-8 character each)
    else: c(x, cmd='mpm_chat', text=text)
    time.sleep(0.8); buf = chat(x).get('buf', '')
    c(x, cmd='mpm_chat', enter=1); time.sleep(1.5)
    return buf


ACC = 'Salut à tous, ça va ? Où est le coffre ? Übermäßig schön, ñandú.'
CYR = 'Привет всем, где сундук?'
JA = 'こんにちは、宝箱はどこ？'
EMO = 'gg 🎉 well played'
LONG = ('This is a long message that has to wrap inside the chat width instead of running over the other lines, '
        'so here are more words to make it three lines long at least.')
TWO = 'Two-line message: the first line is long enough to wrap onto a second line here.'
CYR300 = 'Ж' * 150                                         # 300 bytes: cut at 200 (100 letters)


def main():
    rig.DYLIB = os.path.abspath(arg('--dylib', os.path.expanduser('~/ds_mp_chatfix/build/new/dsrig.dylib')))
    while owner_playing():
        log('owner is playing: waiting 60 s'); time.sleep(60)
    pair = None
    for _ in range(120):
        pair = rig.lease('chatfix', os.getpid())
        if pair: break
        time.sleep(10)
    if not pair: raise SystemExit('no free pair')
    H, G = pair[0], pair[1]; log('pair', pair, 'dylib', rig.DYLIB)
    os.environ['DSL_NOTCH'] = '0'
    try:
        for x in (H, G):
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p): os.replace(p, p + '.last')
        assert rig.check_prefs()
        rig.start(H, fps=30, window=True); rig.start(G, fps=30, window=True)
        rig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=1500)
        time.sleep(3)
        for b in ('MULTIPLAYER', 'HOST', 'LOAD_SAVE'): press(H, b)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
        log('host load', c(H, cmd='load_save', file=src, det=0))
        wait('host lobby', lambda: ms(H).get('state') == 'lobby', timeout=900, every=3)
        port = ms(H).get('port'); log('lobby port', port)
        rig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'guest title', timeout=1500)
        time.sleep(2)
        press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR'); c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
        # A6 while the guest connects / loads (title or loading scene): the host's line reaches it and shows
        time.sleep(1.5)
        say(H, 'Hello while you connect')
        gs = ms(G); log('guest while connecting: state', gs.get('state'), 'chat', json.dumps(gs.get('chat', {}).get('last'))[:200])
        wait('guest in the lobby', lambda: ms(G).get('state') == 'guest_lobby' and ms(H).get('state') == 'lobby', timeout=900, every=3)
        time.sleep(3)
        g0 = chat(G)
        check('A6 a line sent while the guest connected reaches it', any('Hello while you connect' in t for t in g0.get('last', [])), g0.get('last'))
        # A6 lobby (before Start): the host opens the chat in its lobby
        c(H, cmd='mpm_chat', open=1); time.sleep(1)
        h1 = chat(H)
        check('A6 host: Enter opens the chat box in the lobby (before Start)', h1.get('box') == 1 and h1.get('typing') == 1, {k: h1.get(k) for k in ('box', 'typing')})
        c(H, cmd='mpm_chat', escape=1); time.sleep(1)
        check('A6 Escape closes the chat, the lobby stays', chat(H).get('box') == 0 and ms(H).get('state') == 'lobby', ms(H).get('state'))
        buf = say(H, ACC, via_keys=True)
        check('A5 accents typed as key events arrive in the field as typed (test key events carry no Shift)', buf.lower() == ACC.lower(), buf)
        say(H, CYR); say(H, JA); say(H, EMO)
        time.sleep(1)
        g1 = chat(G); last = g1.get('last', [])
        log('guest rows', json.dumps([{k: r[k] for k in ('text', 'font', 'missing')} for r in g1.get('rows', [])], ensure_ascii=False))
        check('A6 guest (waiting for Start) shows the host lines', g1.get('idle_shown', 0) >= 1 and g1.get('canvas', {}).get('alive') == 1, (g1.get('idle_shown'), g1.get('canvas')))
        for name, t in (('accents', ACC), ('Cyrillic', CYR), ('Japanese', JA)):
            row = next((r for r in g1.get('rows', []) if t.lower() in r['text'].lower()), None)
            check(f'A5 guest: {name} arrive as sent and draw in a game font that has them', row and row['missing'] == 0 and row['font'] >= 0, row and {k: row[k] for k in ('font', 'missing')})
        erow = next((r for r in g1.get('rows', []) if 'well played' in r['text']), None)
        check('A5 an emoji (no game font has it) shows as ?, never an empty box', erow and erow['missing'] >= 1 and '?' in erow['text'] and '🎉' not in erow['text'], erow and erow['text'])
        check('A5 guest fonts: the game\'s Japanese / Chinese fonts are candidates', len(g1.get('fonts', [])) >= 2, g1.get('fonts'))
        shot(G, 'lobby_guest_utf8'); shot(H, 'lobby_host_utf8')
        # A1 / A2 from the guest, while it waits
        say(G, LONG); say(G, TWO); say(G, 'short after')
        time.sleep(2)
        h2 = chat(H); idle = [r for r in h2.get('rows', []) if r['idle']]
        lr = next((r for r in idle if 'three lines long' in r['text']), None)
        sr = next((r for r in idle if 'short after' in r['text']), None)
        log('host idle rows', json.dumps([{k: r[k] for k in ('rect', 'plate', 'h', 'pref_h')} for r in idle]))
        check('A1 a long message wraps (its row is 3+ text lines high, inside the chat width)', lr and sr and lr['h'] >= 2.5 * sr['h'] and lr['rect'][2] <= lr['plate'][2] + 1, lr and sr and (lr['h'], sr['h']))
        check('A2 idle rows: every text fits its row and its plate, no two plates overlap', idle and all(r['h'] >= r['pref_h'] - 1 for r in idle)
              and all(r['plate'][1] <= r['rect'][1] + 1 and r['rect'][3] <= r['plate'][3] + 1 for r in idle) and not overlap([{'rect': r['plate']} for r in idle]), len(idle))
        check('A3 the text starts at the same padding inside every plate', idle and max(r['rect'][0] - r['plate'][0] for r in idle) - min(r['rect'][0] - r['plate'][0] for r in idle) < 1.5,
              [round(r['rect'][0] - r['plate'][0], 1) for r in idle])
        shot(H, 'lobby_host_long_idle')
        c(H, cmd='mpm_chat', open=1); time.sleep(1.5)
        h3 = chat(H); box = [r for r in h3.get('rows', []) if not r['idle']]
        log('host box rows', json.dumps([{k: r[k] for k in ('rect', 'h', 'pref_h')} for r in box]))
        check('A2 open box: rows as high as their text, never overlapping, inside the box', box and all(r['h'] >= r['pref_h'] - 1 for r in box) and not overlap(box)
              and all(r['rect'][1] >= h3['rect'][1] - 1 and r['rect'][3] <= h3['rect'][3] + 1 for r in box), len(box))
        check('A7 chat canvas sorts above the game windows (lobby / Esc menu, Teams 32000) and under the tips (32040)', 32000 < h3.get('canvas', {}).get('order', 0) < 32040, h3.get('canvas'))
        shot(H, 'lobby_host_box_open')
        c(H, cmd='mpm_chat', text=CYR300); time.sleep(1)
        fb = chat(H).get('buf', '')
        check('A5 a 300-byte Cyrillic text is cut in the field at 200 bytes, on a whole letter', len(fb.encode()) <= 200 and fb == 'Ж' * 100, len(fb.encode()))
        c(H, cmd='mpm_chat', escape=1); time.sleep(1)
        # Start, then A4 in the world
        press(H, 'START')
        wait('playing', lambda: ms(H).get('state') == 'playing' and ms(G).get('state') == 'playing', timeout=120)
        time.sleep(3)
        for i in range(14): c(H, cmd='mpm_chat_sys', text=f'history line {i}', link=0)
        time.sleep(1)
        c(H, cmd='mpm_chat', open=1); time.sleep(1.5)
        h4 = chat(H); r = h4['rect']; s0 = h4.get('scroll'); z0 = h4.get('cam_ortho')
        def mouse(px, py): return rig.send(H, {'cmd': 'ui_teams', 'action': 'mouse', 'x': px, 'y': py, 'button': 0}, timeout=60)
        mouse((r[0] + r[2]) / 2, (r[1] + r[3]) / 2); time.sleep(1)
        c(H, cmd='mpm_chat', wheel=1, frames=20); time.sleep(2.5)
        h5 = chat(H)
        log('wheel over the box', {k: h5.get(k) for k in ('scroll', 'wheel_blocked', 'over_now', 'cam_ortho')}, 'before', s0, z0)
        check('A4 wheel over the open chat scrolls the chat history', h5.get('scroll', 0) > (s0 or 0), (s0, h5.get('scroll')))
        check('A4 wheel over the chat does not zoom the camera', h5.get('wheel_blocked', 0) > 0 and abs(h5.get('cam_ortho', 0) - (z0 or 0)) < 0.01, (z0, h5.get('cam_ortho'), h5.get('wheel_blocked')))
        shot(H, 'world_box_scrolled')
        c(H, cmd='mpm_chat', escape=1); time.sleep(4)
        z1 = chat(H).get('cam_ortho')
        mouse(700, 330); time.sleep(1)                     # the world, right of the chat
        c(H, cmd='mpm_chat', wheel=1, frames=20); time.sleep(3)
        z2 = chat(H).get('cam_ortho')
        check('A4 baseline: the same wheel over the world zooms the camera', abs(z2 - z1) > 0.01, (z1, z2))
        # A7 with a game window open: Teams (32000) + a fresh idle line from the guest
        c(H, cmd='ui_teams', action='open'); time.sleep(2)
        say(G, 'Visible above the Teams window'); time.sleep(2)
        h6 = chat(H); t6 = c(H, cmd='ui_teams')
        check('A7 a line shows while the Teams window is open (chat above it)', t6.get('open') and any('Teams window' in rr['text'] for rr in h6.get('rows', [])), (t6.get('open'), h6.get('idle_shown')))
        shot(H, 'world_teams_open_chat_on_top')
        c(H, cmd='ui_teams', action='close'); time.sleep(1)
    except Exception:
        check('run reached the end', False, traceback.format_exc()[-900:])
    finally:
        for i in pair:
            try: rig.stop(i)
            except Exception as e: log('stop', i, e)
        rig.release(pair, 'chatfix')
        log('check-prefs', 'identical' if rig.check_prefs() else 'DIFFERENT')
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
