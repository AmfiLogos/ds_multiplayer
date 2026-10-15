#!/usr/bin/env python3
"""Owner rule (2026-10-13): the multiplayer world's save lives only on the host.
Runs mp2_test.py (guest joins from the title via the join point, map changes by vote, forced desync -> resync)
on a leased pair and checks the GUEST's data/Saves folder (every file, mp_sync included) is unchanged afterwards,
plus the guest's SaveHeaderCache.json has no mp_sync entry. Leases a free pair (lane "mpglue")."""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig


def snap(x):
    d = os.path.join(rig.idir(x), 'data', 'Saves')
    return {f: (os.path.getsize(os.path.join(d, f)), os.path.getmtime(os.path.join(d, f))) for f in os.listdir(d)} if os.path.isdir(d) else {}


def main():
    pair = rig.lease('mpglue'); assert pair, 'no free pair'
    H, G = pair[0], pair[1]
    print('pair', pair, flush=True)
    try:
        g0, h0 = snap(G), snap(H)
        env = dict(os.environ, MP2_PAIR=pair)
        r = subprocess.run([sys.executable, 'mp2_test.py', '--ticks', sys.argv[1] if len(sys.argv) > 1 else '3000', '--keep'],
                           cwd=os.path.dirname(os.path.abspath(__file__)), env=env, capture_output=True, text=True)
        out = r.stdout + r.stderr
        open(os.path.join('det_out', f'mp_saves_{pair}_mp2.log'), 'w').write(out)
        for line in out.splitlines():
            if ' PASS ' in line or ' FAIL ' in line or 'RESULTS' in line or 'Traceback' in line:
                print(line[:260])
        stg = rig.send(G, {'cmd': 'status'}, timeout=60).get('mp', {})
        sth = rig.send(H, {'cmd': 'status'}, timeout=60).get('mp', {})
        print('guest mp status', {k: stg.get(k) for k in ('loads', 'guest_world', 'saves_blocked', 'sync_files_deleted', 'sync_file_pending', 'cache_refreshes')})
        print('host  mp status', {k: sth.get(k) for k in ('saves', 'sync_files_deleted', 'sync_file_pending')})
        time.sleep(3)
        g1, h1 = snap(G), snap(H)
        new_g = sorted(f for f in g1 if g1[f] != g0.get(f))
        gone_g = sorted(f for f in g0 if f not in g1)
        hc = os.path.join(rig.idir(G), 'data', 'SaveHeaderCache.json')
        cache_mp = os.path.exists(hc) and 'mp_sync' in open(hc, errors='replace').read()
        print('guest Saves new/changed:', new_g, 'removed:', gone_g, '| guest header cache lists mp_sync:', cache_mp)
        print('host Saves new/changed:', sorted(f for f in h1 if h1[f] != h0.get(f)), '| host mp_sync left:', any(f.startswith('mp_sync') for f in h1))
        ok = not new_g and not any(f.startswith('mp_sync') for f in g1) and not cache_mp and (stg.get('loads') or 0) >= 1
        print('RESULT', 'PASS' if ok else 'FAIL', 'guest Saves folder unchanged and header cache without mp_sync after join + map change + resync')
    finally:
        rig.stop(H); rig.stop(G)
        for x in pair:
            p = os.path.join(rig.idir(x), 'mp.json')
            if os.path.exists(p):
                os.replace(p, p + '.last')
        rig.release(pair)
        print('prefs identical:', rig.check_prefs())


if __name__ == '__main__':
    main()
