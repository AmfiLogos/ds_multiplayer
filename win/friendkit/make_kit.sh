#!/bin/zsh
# Builds the Windows friend kit zip: make_kit.sh <baselib.dll> <det_ranges_win.tsv> <version, e.g. v11>
# Output: out/DungeonSettlersMP_Windows_<version>.zip (our DLL, ranges file, bat files, README; no saves, no game files)
set -e
cd "${0:A:h}"
DLL=$1 RANGES=$2 V=$3
[[ -f $DLL && -f $RANGES && -n $V ]] || { print "usage: make_kit.sh <baselib.dll> <det_ranges_win.tsv> <version>"; exit 1; }
strings $DLL | grep -q "baselib proxy" || { print "ERROR: $DLL is not the DSMP baselib proxy"; exit 1; }
S=out/stage_$V/DungeonSettlersMP_Windows
[[ -e out/stage_$V ]] && mv out/stage_$V out/stage_$V.old_$(date +%H%M%S)
mkdir -p $S/files
crlf() { perl -pe 's/\r?\n/\r\n/' $1 > $2; }
crlf install.bat $S/install.bat
crlf uninstall.bat $S/uninstall.bat
crlf README.txt $S/README.txt
crlf DSMP_Play.bat $S/files/DSMP_Play.bat
crlf find_game.bat $S/files/find_game.bat
cp $DLL $S/files/dsmp_baselib.dll
cp $RANGES $S/files/det_ranges_win.tsv
Z=out/DungeonSettlersMP_Windows_$V.zip
[[ -e $Z ]] && mv $Z $Z.old_$(date +%H%M%S)
(cd out/stage_$V && zip -q -r -X ../${Z:t} DungeonSettlersMP_Windows)
unzip -l $Z
shasum -a 256 $Z $S/files/dsmp_baselib.dll
