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
[[ -n ${GATE_ONLY:-} ]] && { echo "run_all is a full pass: unset GATE_ONLY (reruns: rerun.sh, same summary)"; exit 1; }
G=(g1 g2 g1ld g3 g4 g5 g6 g9 g7 g11 g12)
for g in $G; do echo "== $g $(date +%T)"; zsh $H/$g.sh; rc=$?; (( rc )) && print "group_$g FAIL exit $rc (a check after the exit point is missing)" | tee -a $SUM; done
print "inputs_unchanged $([[ $(inputs | shasum -a 256 | cut -c1-64) == $IN ]] && echo PASS || echo FAIL)" | tee -a $SUM
print "prefs_final $([[ $(prefs) == identical ]] && echo PASS || echo FAIL)" | tee -a $SUM
# expect: inputs_unchanged prefs_final
verdict $0 ${^G/#/$H/}.sh
