#!/bin/zsh
# Unit Builder equality proof (PLAN_V13 BLD): N random builds made through the window's model in QuickJS (bld_cli: only
# clicks the state offers) == the wiki's builder.js on node (ref_node.js): validate keeps each build, computeBuild equal,
# the unit link equal and decodable, the sheet equal; plus My Units takes every build (mu_add_build) and the link decodes
# to the same unit in C (wk_link). Needs: build.sh run first; the wiki source (default ~/dungeonsettlersdb/site/builder).
# usage: custom/builder/test/run_eq.sh [count] [seed] [wiki builder dir]
set -e
H=${0:A:h}; R=${H:h:h:h}; B=$R/build_bld
N=${1:-300}; SEED=${2:-13}; BUILDER=${3:-$HOME/dungeonsettlersdb/site/builder}
O=$B/eq; [[ -d $O ]] && mkdir -p $B/old && mv $O $B/old/eq_$(date +%s); mkdir -p $O
$B/bld_cli $BUILDER $O $N $SEED
node $H/ref_node.js $BUILDER $O
