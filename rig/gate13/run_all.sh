#!/bin/zsh
# run_all.sh - the v13 release gate, one pass on frozen inputs. Before: MP worktree = MP main (src/build.sh -> build/,
# fake-mouse build -> build/gate13_mouse), LD main built (build_mods.sh, app/build_app.sh, kit with MODS = mods_all/win/*).
# Writes ext/gate13/out/inputs.txt (sha256 of every shipped / tested binary) first and checks it is unchanged at the end.
# Summary: ext/gate13/out/summary.txt (one line per check); last line "ALL GREEN <inputs sha>" or "NOT GREEN".
source ${0:A:h}/lib.sh
H=${0:A:h}
[[ -d $HOME/ds_multiplayer/rig/release.lock ]] || { echo "take rig/release.lock first"; exit 1; }
[[ -e $SUM ]] && { mkdir -p $OUT/old; mv $SUM $OUT/old/summary-$(date +%Y%m%d-%H%M%S).txt; }
inputs > $OUT/inputs.txt; IN=$(shasum -a 256 $OUT/inputs.txt | cut -c1-64)
print "v13 gate $(date '+%F %T')  inputs $IN ($(head -1 $OUT/inputs.txt))" > $SUM
for g in g1 g2 g1ld g3 g4 g5 g6 g9 g7 g11 g12; do echo "== $g $(date +%T)"; zsh $H/$g.sh; done
print "inputs_unchanged $([[ $(inputs | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "prefs_final $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
NF=$(awk '$2 == "FAIL"' $SUM | wc -l | tr -d ' '); NP=$(awk '$2 == "PASS"' $SUM | wc -l | tr -d ' ')
(( NF == 0 && NP > 0 )) && print "ALL GREEN $IN ($NP checks)" | tee -a $SUM || print "NOT GREEN: $NF failed, $NP passed" | tee -a $SUM
