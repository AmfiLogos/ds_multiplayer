#!/bin/zsh
# diagnostic run: DLL load addresses (WINEDEBUG=+loaddll) of a title-only run with the given mods
cd ${0:A:h}; W=~/ds_multiplayer/win/instances/wloader_${(L)${:-WINE}}.out; N=$(wc -l < $W)
WINEDEBUG=+loaddll XM_ENABLED=$1 XM_TITLE_ONLY=1 python3 win_mods_test.py ~/ds_launcher_crossmods/loader/build/win/kit > diag.out 2>&1
tail -n +$((N+1)) $W | grep -iE "multiplayer|custom-modes|fault|dsload|baselib|gcd|hell" | cut -c1-220 | head -40 >> diag.out
