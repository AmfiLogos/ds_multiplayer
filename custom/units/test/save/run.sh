#!/bin/zsh
# mu_save tests: real saves (read only copies) + the round trip through the save writer. ASan + UBSan.
#   custom/units/test/save/run.sh            (from anywhere; needs node + ~/dungeonsettlersdb for the round-trip builds)
set -e
H=${0:A:h}; R=${H:h:h:h:h}; T=$H/tmp
mkdir -p $T $H/old
[ ! -d $T/run ] || mv $T/run $H/old/run_$(date +%s)
mkdir -p $T/run
nice -n 19 cc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined \
  -I $R/custom/units -I $R/custom -I $R/custom/fe -o $T/save_test $H/save_test.c \
  $R/custom/units/{mu_save.c,pvp_wiki.c,pvp_build.c,mu_card.c} $R/custom/fe/{fe_json.c,fe_save.c} -lz -lm
export ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
B=~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves
I=~/ds_multiplayer/instances/A/data/Saves
st=0
$T/save_test list $B/*.json $I/WikiGolem__*.json $I/MoonlightBrigade__AutoSave_fe5ea00005a54b06a374b2810b9a732d_0.json \
  $I/MoonlightNormal__*.json $I/MPFE_0c0c23a7.json > $T/run/list.txt || st=1
# the builder's own import of each real save vs the reader
for f in $B/*.json $I/WikiGolem__*.json $I/MoonlightBrigade__AutoSave_fe5ea00005a54b06a374b2810b9a732d_0.json $I/MoonlightNormal__*.json $I/MPFE_0c0c23a7.json; do
  k=${${f:t}%.json}
  node $H/builder_import.js $f > $T/run/ref_$k.jsonl
  $T/save_test ref $f $T/run/ref_$k.jsonl > $T/run/refout_$k.txt || st=1
done
# round trip: random builds the builder itself made legal (validate), + the launcher's library fixture
N=${N:-120}
for seed in 5 11 424242; do
  (cd $R/custom/units/test/node && node builder_ref.js gen $N $seed | node builder_ref.js validate) > $T/run/builds_$seed.jsonl
  $T/save_test rt $T/run/builds_$seed.jsonl $T/run > $T/run/rt_$seed.txt || st=1
done
python3 -I -c 'import json,sys; [print(json.dumps(u["build"])) for u in json.load(open(sys.argv[1]))["units"]]' \
  $R/custom/units/test/fixtures/launcher_library.json > $T/run/builds_library.jsonl
$T/save_test rt $T/run/builds_library.jsonl $T/run > $T/run/rt_library.txt || st=1
grep -h "^==\|passed\|FAIL\|ERROR\|Sanitizer\|runtime error" $T/run/*.txt | head -60
exit $st
