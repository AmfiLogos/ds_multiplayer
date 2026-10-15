#!/bin/zsh
# Assemble the merged source in ext/pvp/merge/full (src read only), after the FE merge (2026-10-15 11:26):
# current src + the files PvP changes (3-way results in merge/tree, conflicts kept both sides) + PvP's new files.
# Every other file is src's (PvP's copies of them are older; FE's transport and fe/ are already in src).
# wiki_test / build_test stay in ext/pvp (they need ext/pvp/tools).
set -e; R=~/ds_multiplayer; M=$R/ext/pvp/merge; P=$R/ext/pvp/src
[[ -d $M/full ]] && mkdir -p $M/old && mv $M/full $M/old/full_$(date +%H%M%S)
rsync -a --exclude old_versions --exclude '*.dSYM' --exclude __pycache__ $R/src/ $M/full/
for f in fe/fe_game.c mp_core.c mp_game.c mpmenu.c mp_test/test_mp.c ord_test/ord_smoke.c rig_cmd.c rig_main.c trn_game.c ui_game.c ui_teams.c; do
  grep -q '^<<<<<<<' $M/tree/$f && { echo "CONFLICT LEFT: $f"; exit 1; }
  cp $M/tree/$f $M/full/$f
done
cd $P
for f in pvp_*.c pvp_*.h pvp_ui.inc pvp_test/Makefile pvp_test/pvp_test.c; do mkdir -p $M/full/${f:h}; cp $f $M/full/$f; done
grep -q pvp_core.c $M/full/build.sh || sed -i '' -e 's|^\(  [^ ]*fe/fe_[a-z_]*\.c.*\) \\$|\1 \\\
  pvp_core.c pvp_modcheck.c pvp_mods.c pvp_game.c pvp_units.c pvp_wiki.c pvp_build.c \\|' $M/full/build.sh
grep -q -- '-lz' $M/full/build.sh || sed -i '' 's|libdsmath_arm64.a$|libdsmath_arm64.a -lz|' $M/full/build.sh   # pvp_wiki: inflate
grep -q '^# PvP' $M/full/build.sh || sed -i '' 's|^# Transitions (trn_\*): floor seeds, flow barrier, host-only autosave.$|&\
# PvP (pvp_*; ext/pvp/NOTES.md): rules, hooks, windows, own / wiki units, Vanilla check.|' $M/full/build.sh
echo assembled $M/full
