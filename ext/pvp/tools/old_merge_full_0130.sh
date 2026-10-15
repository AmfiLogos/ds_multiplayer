#!/bin/zsh
# Assemble the merged source in ext/pvp/merge/full (src read only): src + merged files (merge/tree, conflicts resolved
# by hand) + PvP's new files. Taken from src as they are (PvP did not change them; its copies are older): ui_tip.c,
# mpmenu_test/mpmenu_test.c, ui_test/test_ui (binary). build.sh: src's + the PvP sources. wiki_test / build_test stay in
# ext/pvp (they need ext/pvp/tools).
set -e; R=~/ds_multiplayer; M=$R/ext/pvp/merge; P=$R/ext/pvp/src
[[ -d $M/full ]] && mv $M/full $M/old_full_$(date +%H%M%S)
rsync -a --exclude old_versions --exclude '*.dSYM' --exclude __pycache__ $R/src/ $M/full/
cd $M/tree
for f in $(find . -type f ! -name build.sh ! -path ./ui_tip.c ! -path ./mpmenu_test/mpmenu_test.c ! -path ./ui_test/test_ui | sed 's|^\./||'); do
  grep -q '^<<<<<<<' $f && { echo "CONFLICT LEFT: $f"; exit 1; }
  cp $f $M/full/$f
done
cd $P
for f in pvp_*.c pvp_*.h pvp_ui.inc fe/fe_json.c fe/fe_json.h fe/fe_save.c fe/fe_save.h pvp_test/Makefile pvp_test/pvp_test.c; do
  mkdir -p $M/full/${f:h}; cp $f $M/full/$f
done
sed -i '' -e 's|^  trn_logic.c trn_game.c trn_mp.c \\$|  trn_logic.c trn_game.c trn_mp.c \\\
  pvp_core.c pvp_modcheck.c pvp_mods.c pvp_game.c pvp_units.c pvp_wiki.c pvp_build.c fe/fe_json.c fe/fe_save.c \\|' \
  -e 's|libdsmath_arm64.a$|libdsmath_arm64.a -lz|' \
  -e 's|^# Transitions (trn_\*): floor seeds, flow barrier, host-only autosave.$|&\
# PvP (pvp_*, ext/pvp/NOTES.md): rules, hooks, windows, own / wiki units, Vanilla check; fe/ = Foreign Expedition save library.|' $M/full/build.sh
echo assembled $M/full
