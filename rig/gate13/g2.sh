#!/bin/zsh
# g2.sh - G2 rig pair checks on the frozen build ($D, mouse variant $M): smoke, mp2, left_prompt, menu_pair, tip_pos,
# choice2, ctl_owner, prefs identical. Each check leases its own pair (one at a time).
source ${0:A:h}/lib.sh
G=$HOME/ds_multiplayer/ext/merger13/tools/gate.sh
chk g2_smoke 'smoke 3/3' $WT zsh $G $D gate13 smoke
chk g2_mp2 ' 19 PASS, 0 FAIL, tb 0' $WT zsh $G $D gate13 mp2
chk g2_left 'left_prompt 16 PASS, 0 FAIL, tb 0' $WT zsh $G $D gate13 left
chk g2_menu_pair 'RESULT 17/17 pass' $WT python3 ext/wpmenu/tests/menu_pair.py --dylib $M --lane gate13 --tag gate13
chk g2_tip_pos 'RESULT 8/8 pass' $WT python3 ext/mpfix/tests/tip_pos.py --dylib $M --lane gate13
chk g2_choice2 'RESULT 8/8 pass' $WT python3 ext/mpfix/tests/choice2_pair.py --dylib $D --lane gate13
chk g2_ctl_owner 'RESULT 6/6 pass' $WT python3 ext/mpfix/tests/ctl_owner.py --dylib $D --lane gate13
print "g2_prefs $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
