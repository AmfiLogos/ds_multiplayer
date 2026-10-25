#!/usr/bin/env python3
"""One plain rig copy (no loader): its menu host port is the run's leased port (hostport_shim + DSRIG_HOST_PORT), not
47777 (the owner's game). A leased pair, one hidden copy at the title, mpm_status host_port, stop, release."""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'rig'))
import rig, rigports  # noqa: E402

pair = rig.lease('dsrigports-shim', os.getpid())
if not pair:
    raise SystemExit('no free pair')
try:
    port = rig.pair_port(pair[0])
    print('pair', pair, 'run port', port, 'lease', {p: e['owner'] for p, e in rigports.leases().items()}.get(str(port)), flush=True)
    rig.start(pair[0])
    rig.wait_for(pair[0], lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=600)
    m = rig.send(pair[0], {'cmd': 'mpm_status'})
    print('mpm_status host_port', m.get('host_port'), flush=True)
    print('RESULT shim', 'PASS' if m.get('host_port') == port and port != 47777 else 'FAIL', flush=True)
finally:
    print('released', rig.release(pair, 'dsrigports-shim'), flush=True)
    print('port lease gone after release:', str(port) not in rigports.leases(), flush=True)
