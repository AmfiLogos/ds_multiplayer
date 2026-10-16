#!/bin/zsh
# g12.sh - G12 public repos + release zips: key grep of both repos' diffs since v12-mvp, name/game-file scan of the two
# release zips and both published .NET packages (sha256 = the installers' pins), loader's own scan_names.py too.
source ${0:A:h}/lib.sh
keys() {
  local bad=0
  for r in $HOME/ds_multiplayer $LD; do
    local h=$(git -C $r diff v12-mvp..main | command grep -nE '^\+.*(PRIVATE KEY-----|ghp_[A-Za-z0-9]{20}|github_pat_|sk-[A-Za-z0-9]{20}|AKIA[0-9A-Z]{16}|xox[bp]-)' | head -3)
    [[ -n $h ]] && { echo "key-like lines in $r: $h"; bad=1; }
  done
  (( bad )) || echo "KEYS PASS 0 hits"
}
pkgs() {
  local d=$OUT/g12_pkgs; mkdir -p $d; local ok=1
  for v in melon:melonloader-mac-0.1.0:melonloader-mac-arm64-0.1.0.zip bepinex:bepinex-mac-0.1.2:bepinex-mac-arm64-0.1.2.zip; do
    local k=${v%%:*} t=${${v#*:}%%:*} z=${v##*:}
    curl -sfL -o $d/$z https://github.com/AmfiLogos/ds_multiplayer/releases/download/$t/$z || { echo "download $z failed"; ok=0; continue; }
    local pin=$(sed -n 's/^SHA=//p' $LD/app/$k/install_$k.sh) got=$(shasum -a 256 $d/$z | cut -d' ' -f1)
    [[ $pin == $got ]] && echo "$z sha256 = pin $pin" || { echo "$z sha256 $got != pin $pin"; ok=0; }
  done
  zsh ${ZS} $d/*.zip || ok=0
  (( ok )) && echo "PKGS PASS"
}
ZS=${0:A:h}/zipscan.sh
chk g12_keys 'KEYS PASS' $WT keys
zips() { zsh $ZS "$LD/app/build/Dungeon Settlers Modded.zip" "$LD/loader/build/win/Dungeon Settlers Modded - Windows.zip" && echo ZIPS_OK; }
chk g12_zips 'ZIPS_OK' $WT zips
scan() { python3 modules/scan_names.py "$USER" "$LD/app/build/Dungeon Settlers Modded.zip" "$LD/loader/build/win/Dungeon Settlers Modded - Windows.zip" build/mods_all/mac build/mods_all/win && echo SCAN_OK; }
chk g12_scan_names 'SCAN_OK' $LD/loader scan
chk g12_packages 'PKGS PASS' $WT pkgs
