#!/usr/bin/env python3
"""ws_check.py <log> [min_casts]: G3 verdict from weaponslot_pair.py's last RESULT line. Prints 'WS PASS ...' or
'WS FAIL ...'. PASS = no error, fp_equal, desyncs 0/0, no full-check fails, units equal at the paused tick, owner prefs
unchanged, and (min_casts > 0) at least that many weapon casts sent."""
import json, sys
lines = [l for l in open(sys.argv[1], errors='replace') if 'RESULT {' in l]
if not lines: print('WS FAIL no RESULT line'); sys.exit(1)
r = json.loads(lines[-1].split('RESULT ', 1)[1]); need = int(sys.argv[2]) if len(sys.argv) > 2 else 0
fc = r.get('full_checks_ok_fail') or [None] * 4
bad = [k for k, ok in (('error', 'error' not in r), ('fp_equal', r.get('fp_equal') is True),
       ('desyncs', list(r.get('desyncs') or []) == [0, 0]), ('full_fails', fc[1] in (0, None) and fc[3] in (0, None)), ('full_checks_ran', (fc[0] or 0) > 0 and (fc[2] or 0) > 0),
       ('units_equal', r.get('units_equal_at_paused_tick') is True), ('prefs', r.get('owner_prefs_unchanged') in (True, 'identical')),
       ('casts', need == 0 or (r.get('casts') or {}).get('sent', 0) >= need)) if not ok]
info = f"ticks {r.get('ticks_run')} desyncs {r.get('desyncs')} full {fc} casts {(r.get('casts') or {}).get('sent')} prefs {r.get('owner_prefs_unchanged')}"
print(('WS FAIL ' + ','.join(bad) if bad else 'WS PASS') + ' ' + info); sys.exit(1 if bad else 0)
