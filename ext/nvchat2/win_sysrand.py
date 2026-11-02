#!/usr/bin/env python3
"""Item 23 check on the Windows VM (PLAN_v013; ext/sharedaudit/REPORT.md row 9). rig_det.c h_hc_seed (HashCode's global
seed -> a constant) shares its code with System.Random.GenerateGlobalSeed on Windows, so every unseeded new
System.Random() repeated its numbers on every launch. Fix: not installed on Windows (rig_main_win.c WIN_SKIP_SHARED);
hashcode_seed_fix runs HashCode's cctor, then pins its seed (det on, the lock, each frame).

Needs: the VM running (started visibly by the caller, VMWARE.md) and the winrig agent (winrig.py up). The modded test
profile's release multiplayer.dll + mod.json + det_ranges_win.tsv are moved aside for each case and moved back after.
  probe  --dll NEW --old-dll OLD : 2 launches of each build to the title, det off; the test command sysrand_probe calls
         System.Random.GenerateGlobalSeed and reads s_globalRandom's state. FAIL when OLD (the old code + the probe) does
         not show the constant seed (the probe would not catch the bug), or NEW still gives the constant, or NEW's
         s_globalRandom is the same in both launches.
  pair   --dll NEW --mac-build DIR [--fight-secs 60] : the Mac test copy (MAC_LOADER / MAC_COPY: one hidden copy; the
         release Mac mod set with this build's multiplayer) hosts a dungeon save, the VM joins, Start, both in the dungeon,
         the host's units attack the nearest enemy (target choice = HashCode.Combine ties, DETERMINISM_NOTES 6), the sim
         runs on. FAIL on a desync on either side or a dead game.
  NEW / OLD = a module folder of win/build_win.sh (DSMP_TARGET=module): multiplayer.dll, mod.json, det_ranges_win.tsv.
Output: <out>/summary.json, RESULT.txt (RESULT line on stdout)."""
import argparse, contextlib, json, os, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
WB = os.path.join(HERE, '..', '..', 'win', 'winbot')
sys.path.insert(0, WB)
import winrig as W  # noqa: E402
import win_checklist as WC  # noqa: E402

FILES = ('multiplayer.dll', 'mod.json', 'det_ranges_win.tsv')
MODS = ['multiplayer']        # the pair: only this mod on both sides (the Mac release build's gcd-cdr files differ from the VM's
                              # kit, and hell-difficulty is not in the host's set for a non-Hell world: the join was refused)
log = W.log


@contextlib.contextmanager
def module(w, src):
    """this build's multiplayer files in the test profile's mods folder; the release files moved aside and back"""
    md = W.ROOT + r'\mods\multiplayer'
    keep = W.WORK + r'\to_delete\mp_release_' + time.strftime('%Y%m%d-%H%M%S')
    w.kill_all()
    for f in FILES:
        w.move(md + '\\' + f, keep + '\\' + f)
    try:
        for f in FILES:
            w.upload(os.path.join(src, f), md + '\\' + f)
        yield
    finally:
        try:
            w.stop()
        except Exception as e:
            log('stop', repr(e))
        for f in FILES:
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + r'\nvchat2_' + f)
            w.move(keep + '\\' + f, md + '\\' + f)
        log('release multiplayer files back in place')


def probe_launches(w, rep, label, n=2):
    out = []
    for i in range(n):
        w.prepare(lang=0)
        w.start()
        r = w.cmd(cmd='sysrand_probe')
        log(label, 'launch', i + 1, json.dumps(r))
        rep.check('probe', f'{label} launch {i + 1}: the probe ran with det off (3 values)', r.get('ok') and r.get('det_on') == 0 and r.get('n') == 3, r)
        out.append(r)
        w.stop()
    return out


def probe(w, rep, a):
    """gseed = System.Random.GenerateGlobalSeed (seeds s_globalRandom, which seeds every thread's Random): the bug makes it
    9e3779b9 every time. g_state = s_globalRandom's state at the title: with a constant seed it repeats on every launch.
    v = new Random().Next() x 3 at the title: these differ on both builds (how many Randoms the title made before
    varies with timing), so they are logged only."""
    with module(w, a.old_dll):
        old = probe_launches(w, rep, 'old code')
    with module(w, a.dll):
        new = probe_launches(w, rep, 'new code')
    C = '9e3779b9'
    rep.check('probe', 'old code: System.Random\'s global seed is the constant 9e3779b9 in both launches (the bug; the probe catches it)',
              all(r.get('gseed') == [C, C] for r in old), [r.get('gseed') for r in old])
    rep.check('probe', 'new code: System.Random\'s global seed is random (not the constant, other values each call and each launch)',
              all(C not in r.get('gseed', [C]) for r in new) and len({g for r in new for g in r.get('gseed', [])}) == 4, [r.get('gseed') for r in new])
    rep.check('probe', 'new code: s_globalRandom (the root of every unseeded Random) differs between launches',
              new[0].get('g_state') not in (None, '00000000') and new[0].get('g_state') != new[1].get('g_state'), [r.get('g_state') for r in new])
    log('old code s_globalRandom state per launch (same = repeats every launch):', [r.get('g_state') for r in old])
    log('new Random().Next() at the title: old', [r.get('v') for r in old], 'new', [r.get('v') for r in new])
    rep.items['probe']['values'] = {'old': old, 'new': new}


def pair(w, rep, a):
    I = 'pair'
    copy = os.environ.get('MAC_COPY', 'CLN')
    loader = os.path.expanduser(os.environ.get('MAC_LOADER', '~/ds_launcher_wt_nvchat2/loader'))
    os.environ.update(MAC_COPY=copy, MAC_LOADER=loader, MAC_LANE='nvchat2-pair')
    WC.MAC_CTL = os.path.join(loader, 'test_env', 'instances', copy, 'ctl.sock')
    mac = WC.Mac(a.mac_build)
    with module(w, a.dll):
        try:
            mac.start(MODS, {})
            work = os.path.join(os.path.dirname(WC.MAC_CTL), 'save_in_dungeon.json')
            import shutil; shutil.copy2(WC.fresh_world(a.save_dungeon, 'Dungeon'), work)
            try:
                mac.host(work)
            except W.GameError as e:                                  # mac.sh host waits 85 s; a loaded Mac needs longer
                if 'timeout' not in str(e):
                    raise
                log('Mac host slow, waiting on:', str(e)[:120])
                def hosted():
                    try:
                        s = mac.status()
                    except OSError:
                        return None
                    return s if (s.get('det') or {}).get('locked') and (s.get('mp') or {}).get('started') else None
                w.wait(hosted, 'the Mac host session + lockstep (slow load)', 300, 2)
                mac.answer_boxes()
            w.prepare(lang=0, active=MODS, settings={})
            w.start()
            w.join(f'{W.hub_host()}:{mac.port()}', wait=None)
            w.wait(lambda: (mac.answer_boxes(), w.mpm().get('state') in ('guest_lobby', 'guest_loading', 'playing'))[1], 'the VM in the host lobby', 300, 1)
            mac.cmd(cmd='mpm_press', button=W.B['START'])
            S = {'mac': mac, 'vm': w}
            st = lambda side: S[side].cmd(cmd='status')

            def land(side):
                s = st(side)
                return s.get('map') == 1 and not s.get('busy') and (s.get('det') or {}).get('locked')
            w.wait(lambda: (mac.answer_boxes(), land('mac') and land('vm'))[1], 'both in the dungeon', 600, 1)
            hs = {k: S[k].cmd(cmd='sysrand_probe').get('hc_seed') for k in S}   # det on: no Random made, the pinned HashCode seed only
            log('det', json.dumps({k: st(k).get('det') for k in S})[:600])
            units = mac.cmd(cmd='list_units')['units']
            us = [u for u in units if 'cell' in u and not u.get('dead')]
            pl = [u for u in us if u.get('faction') == 1]
            en = [u for u in us if u.get('faction') == 3]
            fought = False
            if pl and en:
                for u in pl:                                          # the host's own (or free) units: claim, then move
                    if mac.cmd(cmd='mp_owner', guid=u['guid']).get('owner') == -1:
                        mac.cmd(cmd='mp_claim', guid=u['guid'])
                time.sleep(3)
                mine = [u for u in pl if mac.cmd(cmd='mp_owner', guid=u['guid']).get('owner') == 0]
                p = (mine or pl)[0]['cell']; en.sort(key=lambda u: (u['cell'][0] - p[0]) ** 2 + (u['cell'][1] - p[1]) ** 2)
                for u in mine:
                    mac.cmd(cmd='mp_move', guid=u['guid'], x=en[0]['cell'][0], y=en[0]['cell'][1])
                fought = bool(mine)
            rep.check(I, 'det on: HashCode seed pinned to 9e3779b9 on both sides', all(v == '9e3779b9' for v in hs.values()), hs)
            rep.check(I, 'the host sent its units at the nearest enemy', fought, {'player_units': len(pl), 'enemies': len(en)})
            t0 = time.time()
            while time.time() - t0 < a.fight_secs:
                time.sleep(5)
                ok_vm, ok_mac = w.alive(), 'scene' in mac.status()
                if not (ok_vm and ok_mac):
                    break
            rep.check(I, 'both games alive after the fight', w.alive() and 'scene' in mac.status())
            for side in S:
                s = st(side); m = s.get('mp') or {}
                rep.check(I, f'{side}: 0 desyncs, sim ran ({s.get("tick")} ticks)', not m.get('desynced') and not m.get('desyncs') and (s.get('tick') or 0) > 0,
                          {'desyncs': m.get('desyncs'), 'desynced': m.get('desynced'), 'tick': s.get('tick'), 'hc_seed': hs.get(side)})
            after = [u for u in mac.cmd(cmd='list_units')['units'] if u.get('faction') == 3 and 'cell' in u]
            log('enemies dead after the fight:', sum(1 for u in after if u.get('dead')), 'of', len(after))
        finally:
            for side, obj in (('vm', w), ('mac', mac)):
                try:
                    if side == 'vm':
                        WC.fresh_logs(w, rep, 'pair')
                    obj.stop()
                except Exception as e:
                    log('stop', side, repr(e))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('what', choices=['probe', 'pair', 'both']); p.add_argument('--out', required=True)
    p.add_argument('--dll', required=True); p.add_argument('--old-dll'); p.add_argument('--mac-build')
    p.add_argument('--save-dungeon', default=os.path.join(WC.SAVES, 'boss_golem_25846389.json'))
    p.add_argument('--fight-secs', type=int, default=60)
    a = p.parse_args()
    rep = WC.Report(os.path.abspath(a.out))
    w = W.Win(); w.up()
    for k in (['probe', 'pair'] if a.what == 'both' else [a.what]):
        t = time.time()
        try:
            (probe if k == 'probe' else pair)(w, rep, a)
        except Exception as e:
            rep.check(k, 'ran to the end', False, f'{type(e).__name__}: {e}')
            traceback.print_exc()
        rep.items.setdefault(k, {'checks': [], 'secs': 0})['secs'] = time.time() - t
        rep.save()
    s = rep.save()
    n = sum(len(i['checks']) for i in s['items'].values()); ok = sum(c['ok'] for i in s['items'].values() for c in i['checks'])
    log('RESULT', 'PASS' if s['pass'] else 'FAIL', f'{ok}/{n}', os.path.join(rep.out, 'RESULT.txt'))


if __name__ == '__main__':
    main()
