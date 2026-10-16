#!/bin/zsh
# g4.sh - G4 custom multiplayer on the rig pair (frozen build $D + custom-modes next to it): hub smoke, FE dive in the
# neutral hub (save units, Death Off), PvP from the hub, PvP v1 on the base (checks 0-7 incl. round end), PvP m4 tests.
# (The dive with a My Units unit runs in loader copies: mu_dive_mac.py / mu_dive_win.py.)
source ${0:A:h}/lib.sh
export PVP_DYLIB=$D
chk g4_hub_smoke '5/5 PASS' $WT python3 ext/fepvp13/tests/hub_smoke.py --dylib $D
chk g4_hub_dive '25/25 PASS' $WT python3 ext/fepvp13/tests/hub_dive_test.py --death 0 --feed --dylib $D
chk g4_hub_pvp '13/13 PASS' $WT python3 ext/fepvp13/tests/hub_pvp_test.py --death 0 --feed --dylib $D
chk g4_pvp_v1 'RESULT PASS 11/11' $WT python3 ext/fepvp13/tests/pvp_v1_pair.py --dylib $D --tag gate13_v1
chk g4_m4_van 'RESULT PASS' $WT python3 ext/pvp/tests/pvp_vanilla.py --tag gate13_van
chk g4_m4_bring 'RESULT PASS' $WT python3 ext/pvp/tests/pvp_bring_ui.py --tag gate13_bring
chk g4_m4_ui 'RESULT PASS' $WT python3 ext/pvp/tests/pvp_ui.py --tag gate13_ui
chk g4_mu_dive_mac 'RESULT ([0-9]+)/\1 pass' $WT python3 rig/gate13/mu_dive_mac.py
print "g4_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
