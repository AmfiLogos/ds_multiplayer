#!/usr/bin/env python3
"""The chat's read state, Top / Bottom, "/clear" and the bin in a real 2-player session (owner 2026-10-25: "all of this goes
into the underlying chat feature, so it works for multiplayer chat too; /clear and the bin clear only the local view, never
the other players' chat").

ONE pair of hidden loader test copies (background-only, small window, muted, notch fix off; never the owner's game, saves or
screen), a pair lease from ~/ds_multiplayer/rig/rig.py, the host on the pair lease's own port (rig/rigports.py; never a
fixed or OS-chosen one), no player beyond the two copies in the session (players_guard). Host H loads the Moonlight test save
and hosts; guest G joins by mp.json. Both type chat lines (the real send path). The chat reads the mouse through Unity icalls,
so the rig control channel's `mpm_chat {drag ... button -1, roll}` plays the wheel / clicks through the chat's own pointer
code. Fails when (the breaks this catches):
  - in a session, the wheel over the chat lines does not open the box for reading, or the box takes the keyboard there
    (the game's text focus), or the entry field is missing in a session;
  - Top / Bottom do not reach the oldest / newest player line;
  - "/clear" does not empty the host's history, is sent to the guest (a line or a lost guest line), does not count in
    mp_api v4 chat_clears, or closes the box;
  - the bin on the guest does not empty the guest's history, or touches the host's chat;
  - lines after a clear do not arrive;
  - one Escape closes the chat box AND the Teams window under it (owner rule: one press, one close), or does not close it.
Usage: HM_BUILD=<kit: dsload.dylib (dev key, silent) + mods/multiplayer> LD_ROOT=<ds_modloader checkout> python3 chat_read_pair.py
       CP_OUT = output folder; CP_LOCK = a mkdir lock held while the pair runs."""
import json, os, shutil, sys, threading, time, traceback

LD = os.path.expanduser(os.environ.get('LD_ROOT', '~/ds_launcher'))
sys.path.insert(0, os.path.join(LD, 'loader', 'test'))
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, owner screen guard, rig leases)
from hostmods_pair import lrig, log, check, c  # noqa: E402

H, G = 'CPH', 'CPG'; HP.H, HP.G = H, G
OUT = os.path.abspath(os.environ.get('CP_OUT') or os.path.join(os.path.dirname(HP.BUILD), 'chatpair', time.strftime('%Y%m%d-%H%M%S')))
os.makedirs(OUT, exist_ok=True)
HP.SHOTS = OUT
LANE = 'dsdps-chat'
info = {}


def chat(x):
    return c(x, cmd='mpm_status').get('chat') or {}


def drag(x, x0, y0, x1, y1, button=0, frames=10, roll=0):
    a = c(x, cmd='mpm_chat', drag=1, x0=x0, y0=y0, x1=x1, y1=y1, button=button, frames=frames, roll=roll)
    if not a.get('ok'):
        raise RuntimeError(f'{x}: drag refused: {a}')
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(0.3)
        ch = chat(x)
        if not ch.get('fake') and not ch.get('drag'):
            time.sleep(0.4)
            return chat(x)
    raise RuntimeError(f'{x}: the scripted pointer did not finish')


def click(x, r):
    return drag(x, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2, frames=1)


def wheel_over_lines(x):                                        # the newest idle line must show (lines fade after 12 s)
    r = chat(x).get('rect')
    return drag(x, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2, button=-1, frames=3, roll=1)


def reading(ch):
    return ch.get('box') == 1 and ch.get('read') == 1 and ch.get('typing') == 0 and ch.get('hotkeys_blocked') == 0


def texts(ch):
    return [r.get('text', '') for r in ch.get('rows') or []]


def say(x, text):                                               # Enter, the text, Enter: the real send path
    c(x, cmd='mpm_chat', open=1); time.sleep(0.6)
    c(x, cmd='mpm_chat', text=text); time.sleep(0.6)
    c(x, cmd='mpm_chat', enter=1); time.sleep(0.8)


def has_line(x, part):
    return any(part in t for t in chat(x).get('last') or [])


def clears(x):
    return c(x, cmd='mpm_chat', clears=1)


def run(port, extra):
    for x in (H, G):
        HP.make_copy(x)
        HP.kit(x, {}, [], ['multiplayer'])
        HP.park(os.path.join(lrig.idir(x), 'mp_menu.json'))
    json.dump({'role': 'host', 'port': port, 'name': 'Hosta', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800},
              open(os.path.join(lrig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben', 'full_check_steps': 1800},
              open(os.path.join(lrig.idir(G), 'mp.json'), 'w'))
    HP.room_for(2)
    if os.getloadavg()[0] > 20:
        HP.wait('load under 20', lambda: os.getloadavg()[0] < 20, timeout=1200, every=15)
    HP.start(H, 'http://127.0.0.1:9', extra)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'chatpair_base.json'); shutil.copy2(HP.SAVE, src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    HP.wait('host locked', lambda: HP.st(H).get('det', {}).get('locked'), timeout=900, every=2)
    HP.start(G, 'http://127.0.0.1:9', extra)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    sg = HP.wait('guest joined and loaded', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                      and not s.get('mp', {}).get('needs_state') else None)(HP.st(G)), timeout=900, every=2)
    check('setup: the guest joined the host on the run\'s leased port and loaded its world', bool(sg), {'port': port, 'mp': (sg or {}).get('mp')})
    players('start')
    for x in (H, G):
        dl = HP.flog(x, 'dsload.log')
        check(f'{x}: the copy is silent (AudioListener held at 0)', 'AudioListener held at 0' in dl, [l for l in dl.splitlines() if 'AudioListener' in l][:1])
    time.sleep(3)

    # player lines on both screens (the real chat path: Enter, text, Enter)
    for i in range(6):
        say(H, f'host line {i + 1}'); say(G, f'guest line {i + 1}')
    HP.wait('every line on both', lambda: has_line(H, 'guest line 6') and has_line(G, 'guest line 6'), timeout=60, every=1)
    nh, ng = chat(H).get('lines', 0), chat(G).get('lines', 0)
    info['lines_before'] = {'host': nh, 'guest': ng}
    check('setup: 12 player lines on both screens', nh >= 12 and ng >= 12, info['lines_before'])

    # the host reads without typing: the wheel over the lines opens the box, the entry field is there, the keys are the game's
    ch = wheel_over_lines(H)
    sb = ch.get('sb') or {}
    check('session: the wheel over the chat lines opens the box for reading (no typing, the game keeps its keys), the entry field shows',
          reading(ch) and ch.get('ro') == 0 and sb.get('field') is not None and not (ch.get('field') or {}).get('focused') and ch.get('scroll', 0) >= 1,
          {k: ch.get(k) for k in ('box', 'read', 'typing', 'hotkeys_blocked', 'ro', 'scroll')} | {'field': sb.get('field')})
    HP.shot(H, 'pair_1_host_read.png')
    ch = click(H, sb['top'])
    check('session: Top shows the oldest player line', reading(ch) and ch.get('oldest') == 1 and any('host line 1' in t for t in texts(ch)),
          {'scroll': ch.get('scroll'), 'S': (ch.get('sb') or {}).get('S'), 'rows': texts(ch)[-2:]})
    ch = click(H, (ch.get('sb') or {})['bot'])
    check('session: Bottom shows the newest player line', reading(ch) and ch.get('scroll') == 0 and any('guest line 6' in t for t in texts(ch)[:2]),
          {'scroll': ch.get('scroll'), 'rows': texts(ch)[:2]})

    # "/clear" on the host: Enter from the read box, the command, Enter
    k0 = clears(H)
    say(H, '/clear')
    time.sleep(3)
    ch, cg = chat(H), chat(G)
    k1 = clears(H)
    check('session: "/clear" empties the host\'s history, the box stays open for reading, chat_clears (mp_api v4) counts it',
          ch.get('lines') == 0 and reading(ch) and k1.get('version', 0) >= 4 and k1.get('clears') == (k0.get('clears') or 0) + 1,
          {'lines': ch.get('lines'), 'read': ch.get('read'), 'typing': ch.get('typing'), 'api_before': k0, 'api_after': k1})
    check('session: "/clear" is never sent: the guest keeps every line, gets no "/clear" line, and counts no clear',
          cg.get('lines') == ng and not any('/clear' in t for t in cg.get('last') or []) and clears(G).get('clears') == 0,
          {'guest_lines': cg.get('lines'), 'before': ng, 'guest_last': (cg.get('last') or [])[-2:]})
    HP.shot(H, 'pair_2_host_cleared.png')

    # a line after the clear reaches both; then the guest's bin
    say(G, 'after the clear')
    HP.wait('the new line on the host', lambda: has_line(H, 'after the clear'), timeout=30, every=1)
    ch, cg = chat(H), chat(G)
    check('session: a line after the clear arrives (host: only that line; guest: its full history + 1)', ch.get('lines') == 1 and cg.get('lines') == ng + 1,
          {'host': ch.get('lines'), 'guest': cg.get('lines')})
    # the guest: the Teams window is open (a guest that arrives with no unit gets it: "Pick your units"; opened here so the
    # case always runs). Teams is modal (it holds the game's keys itself), so the chat checks here are: no typing, no focus.
    c(G, cmd='ui_teams', action='open'); time.sleep(1)
    tm = c(G, cmd='ui_teams')
    cg = wheel_over_lines(G)
    check('session: the guest opens its box for reading with the wheel over the Teams window (no typing, the field not focused)',
          tm.get('modal') == 1 and cg.get('box') == 1 and cg.get('read') == 1 and cg.get('typing') == 0 and not (cg.get('field') or {}).get('focused'),
          {'teams_modal': tm.get('modal')} | {k: cg.get(k) for k in ('box', 'read', 'typing')})
    cg = click(G, (cg.get('sb') or {})['bin'])
    time.sleep(2)
    ch = chat(H)
    check('session: the bin empties the guest\'s history only; the host\'s chat is untouched',
          cg.get('lines') == 0 and clears(G).get('clears') == 1 and ch.get('lines') == 1 and has_line(H, 'after the clear'),
          {'guest': cg.get('lines'), 'host': ch.get('lines'), 'host_last': ch.get('last')})
    HP.shot(G, 'pair_3_guest_bin.png')
    # Escape: one press closes the top window only (owner rule): the chat box, not the Teams window under it
    c(G, cmd='mpm_key', key='escape', via='app'); time.sleep(2)
    cg, tm = chat(G), c(G, cmd='ui_teams')
    check('session: one Escape closes only the chat box; the Teams window under it stays open', not cg.get('box') and cg.get('read') == 0 and tm.get('modal') == 1,
          {'chat_box': cg.get('box'), 'read': cg.get('read'), 'teams_modal': tm.get('modal')})
    c(G, cmd='mpm_key', key='escape', via='app'); time.sleep(2)
    check('session: the next Escape closes the Teams window', c(G, cmd='ui_teams').get('modal') == 0, c(G, cmd='ui_teams').get('modal'))
    c(H, cmd='mpm_key', key='escape', via='app'); time.sleep(2)
    check('session: Escape closes the host\'s read box', not chat(H).get('box') and chat(H).get('read') == 0, (chat(H).get('box'), chat(H).get('read')))
    players('end')


def players(phase):
    """another run's copy in this session = EXTRA PLAYER (rig/rigports.py), a FAIL"""
    ok, lines = HP.realrig.rigports.players_guard(lrig.send, [H, G], 2, phase)
    check(f'players: no extra player at the {phase}', ok, '; '.join(lines))


def main():
    res = {}; locked = False; pair = None
    lock = os.path.expanduser(os.environ.get('CP_LOCK', ''))
    g = threading.Thread(target=HP.guard, daemon=True); g.start()
    try:
        if os.path.exists(os.path.join(HP.RIGDIR, 'release.lock')):
            raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
        if lock:
            t0 = time.time()
            while True:
                try:
                    os.mkdir(lock); locked = True; break
                except FileExistsError:
                    if time.time() - t0 > 3600:
                        raise SystemExit('copy lock busy for an hour')
                    time.sleep(20)
        t0 = time.time(); pair = HP.realrig.lease(LANE, os.getpid())
        while not pair and time.time() - t0 < 1800:
            time.sleep(15); pair = HP.realrig.lease(LANE, os.getpid())
        if not pair:
            raise SystemExit('no free pair lease for 30 min')
        port = HP.realrig.PAIR_PORTS[pair]                       # the pair lease's own port (rigports.py): unique on the Mac
        log('lease', pair, 'port', port, '(leased)', 'build', HP.BUILD, 'out', OUT)
        crash = os.path.join(OUT, 'crash'); os.makedirs(crash, exist_ok=True); open(os.path.join(crash, 'answer'), 'w').write('no')
        wd = lrig.ctl_env()['DSRIG_WRITE_DIRS'] + ':' + OUT
        run(port, {'DSL_CRASHREP_TEST': crash, 'DSRIG_WRITE_DIRS': wd})
    except BaseException as e:
        log(traceback.format_exc())
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        for x in (G, H):
            try:
                HP.stop(x)
            except Exception as e:
                log('stop', x, repr(e))
        for x in (G, H):
            for f in ('dsload.log', 'rig.log', 'player.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        if pair:
            HP.realrig.release(pair, LANE)
        if locked:
            os.rmdir(lock)
        res.update({'checks': HP.checks, 'info': info, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks)})
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
