#!/bin/sh
# wait until fewer than 2 pairs are leased machine-wide (NIGHT_RULES 4), then run the given command
while [ "$(python3 ~/ds_multiplayer/rig/rig.py leases | grep -vc ' free')" -ge 2 ]; do sleep 60; done
exec "$@"
