#!/usr/bin/env python3
"""Single-player frame-cost probe (no session): title 60 s, load the ElvenExpedition save, 6 x 15 s after the load,
then load it again (like a portal / new load) and 4 x 15 s. Prints the rig "perf" answer per phase.
  fps_probe.py <dylib> <label> [X] [fps]     env DSRIG_MINIMAL=1 = baseline without the module's frame work"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
dylib, label = sys.argv[1], sys.argv[2]; x = sys.argv[3] if len(sys.argv) > 3 else 'C'; fps = int(sys.argv[4]) if len(sys.argv) > 4 else 240
rig.DYLIB = dylib
out = []
def ph(name):
    p = rig.send(x, {'cmd': 'perf', 'reset': 1}); p['phase'] = name; out.append(p)
    a = p['parts']
    print(f"{label} {name}: frames {p['frames']} all avg {a['all']['avg_ms']:.3f} max {a['all']['max_ms']:.1f} | mpmenu avg {a['mpmenu']['avg_ms']:.3f} max {a['mpmenu']['max_ms']:.1f} | ui avg {a['ui']['avg_ms']:.3f} max {a['ui']['max_ms']:.1f} | cm max {a['custom_modes']['max_ms']:.1f} | dt {p['dt_hist']} max {p['dt_max_ms']} | scans {p['mpm_scans']}", flush=True)
assert rig.check_prefs()
mpj = os.path.join(rig.idir(x), 'mp.json')
if os.path.exists(mpj): os.replace(mpj, mpj + '.fpsfix_aside')   # no mp.json = no auto host: single player
try:
    rig.start(x, fps=fps)
    rig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500)
    ph('boot'); time.sleep(60); ph('title60s')
    work = os.path.join(rig.idir(x), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(rig.SAVE)); shutil.copy2(rig.SAVE, src)
    for rnd in (1, 2):
        rig.send(x, {'cmd': 'load_save', 'file': src, 'det': 0})
        time.sleep(3)
        rig.wait_for(x, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'loaded', timeout=600, every=1)
        ph(f'load{rnd}')
        rig.send(x, {'cmd': 'set_speed', 'speed': 1})
        for i in range(6 if rnd == 1 else 4):
            time.sleep(15); ph(f'after_load{rnd}_{(i + 1) * 15}s')
finally:
    if os.path.exists(mpj + '.fpsfix_aside'): os.replace(mpj + '.fpsfix_aside', mpj)
    rig.stop(x)
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), f'probe_{label}.json'), 'w'), indent=1)
    print('prefs ok' if rig.check_prefs() else 'PREFS CHANGED')
