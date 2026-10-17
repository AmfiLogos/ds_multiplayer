#!/bin/zsh
# gate_clean.sh <gate output dir>: what a release gate run leaves behind and nobody needs after it (2026-10-17: these
# copies filled the disk). Run by lib.sh verdict() at the end of every gate run, passed or failed; safe to run by hand.
#   bottle DSMP: the install copies the gate's Windows tests moved aside ("Dungeon Settlers Modded.gate*", ".oldupd*"),
#                and the contents of the test folders old_pathwarn, DSW/old_gate13, AppData/Local/dsml_update_test_old
#   <out>:       the game / app / kit copies inside each check's folder (root, run, apps, Applications, kit, releases)
#                and the earlier tries' check folders in <out>/old (*-dir-*)
# Kept: summary.txt, inputs.txt, every *.log, the checks' summary / json / png files. Never touches the default install
# folder "Dungeon Settlers Modded" (a test may be using it), the owner's game or anything outside these places.
OUT=${1:?gate output dir}; setopt NULL_GLOB
DRV="$HOME/Library/Application Support/CrossOver/Bottles/DSMP/drive_c"; LOC="$DRV/users/crossover/AppData/Local"
items=("$LOC"/"Dungeon Settlers Modded".gate* "$LOC"/"Dungeon Settlers Modded".oldupd* "$DRV"/old_pathwarn/* "$DRV"/DSW/old_gate13/*
       "$LOC"/dsml_update_test_old/*)
items+=($OUT/*/(root|run|apps|Applications|kit|releases)(/) $OUT/old/*-dir-*(/))
kb=0; n=0
for p in $items; do
  [[ -e $p ]] || continue
  kb=$(( kb + $(du -sk "$p" 2>/dev/null | cut -f1) )); rm -rf "$p" && n=$(( n + 1 ))
done
print "gate_clean $(date '+%F %T'): $n item(s), $(( kb / 1024 )) MB freed; $(df -g "$HOME" | awk 'NR==2 {print $4}') GB free" | tee -a $OUT/cleanup.log
