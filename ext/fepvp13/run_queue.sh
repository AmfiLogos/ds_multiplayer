#!/bin/zsh
# Lane fepvp: pair tests one after the other (each test leases its own pair and waits for a free one).
W=${0:A:h:h:h}; cd $W; O=$W/ext/fepvp13/out; P=~/ds_multiplayer/ext/pvp/tests
export PVP_DYLIB=$W/build/dsrig.dylib
for t in "$@"; do
  echo "$(date +%H:%M:%S) START $t"
  case $t in
    v1_*) python3 ext/fepvp13/tests/pvp_v1_pair.py --tag $t > $O/$t.out 2>&1 ;;
    m4_van) python3 $P/pvp_vanilla.py --tag fepvp_m4_van > $O/$t.out 2>&1 ;;
    m4_bring) python3 $P/pvp_bring_ui.py --tag fepvp_m4_bring > $O/$t.out 2>&1 ;;
    m4_ui) python3 $P/pvp_ui.py --tag fepvp_m4_ui > $O/$t.out 2>&1 ;;
    hubdive) python3 ext/fepvp13/tests/hub_dive_test.py --death 0 --feed > $O/$t.out 2>&1 ;;
  esac
  echo "$(date +%H:%M:%S) END $t rc $? | $(grep -E 'RESULT|PASS, |[0-9]+/[0-9]+ PASS' $O/$t.out | tail -1)"
done
