#!/usr/bin/env python3
"""kit_esc_test.py <ds_launcher tree> <out> [--copy FMP]

The multiplayer module under the LOADER (its view lookups go through the kit's view_find there; the rig's own dylib uses
the module's own search): the Esc menu's multiplayer clones must still be added when the game opens the Esc menu.
Break it catches: a player's Esc menu without the multiplayer rows (find_view_x's kit path returning nothing or a hidden
view). Runs ds_modloader loader/test/scan_rate_test.py's flow (its driver, a player-like start, --mods default) with short
phases, and opens the Esc menu the way the game does (PopUpViewContainer.ActivateView(1)) after the world loaded.
PASS: rig.log says "view lookups: the loader kit's view_find" and "Esc-menu clones added" after the popup."""
import os, sys, time
TREE = os.path.abspath(os.path.expanduser(sys.argv[1])); OUT = os.path.abspath(sys.argv[2])
COPY = sys.argv[sys.argv.index('--copy') + 1] if '--copy' in sys.argv else 'FMP'
T = os.path.join(TREE, 'loader', 'test')
sys.argv = ['scan_rate_test.py', OUT, '--mods', 'default', '--copy', COPY]
sys.path.insert(0, T)
import scan_rate_test as s                                   # noqa: E402  (its flow, env and copy handling)

s.TITLE_S, s.LIST_S, s.WORLD_S, s.KEYS = 5, 5, 5, 3
mark = {}
_phase = s.phase


def phase(name, secs):
    _phase(name, secs)
    if name == 'world':                                      # world loaded and running: the Esc menu as the game opens it
        log = os.path.join(s.I, 'rig.log'); mark['off'] = os.path.getsize(log) if os.path.exists(log) else 0
        mark['popup'] = s.cmd('popup 1'); time.sleep(3); mark['unpopup'] = s.cmd('unpopup 1'); time.sleep(1)


s.phase = phase
os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(s.I, 'rig.log'); off0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0   # the log keeps old runs
try:
    s.main()                                                 # exits: its RESULT is about SCAN-RULE lines (not judged here)
except SystemExit:
    pass
data = open(LOG, 'rb').read()                                # byte offsets: slice first, then decode
new = data[mark['off']:].decode(errors='replace') if 'off' in mark else ''
kit = "view lookups: the loader kit's view_find" in data[off0:].decode(errors='replace')
clones = 'Esc-menu clones added' in new
print('popup 1:', mark.get('popup'), '| kit path:', kit, '| Esc-menu clones after the popup:', clones)
ok = bool(mark.get('popup')) and kit and clones
print(f"RESULT kit_esc_test: {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
