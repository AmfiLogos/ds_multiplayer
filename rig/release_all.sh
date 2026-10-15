#!/bin/zsh
# release_all.sh <expected sha256 prefix>
# Refuses unless release/gate_<sha16>.summary (release_gate.sh) ends "ALL GREEN <sha>" for this dylib.
# release_all.sh --owner-approved <sha256 prefix> <Mac gate summary> <Windows rerun dir> "<owner words (date)>"
#   For a gate that was green in two parts (Mac suites in one summary, Windows suites in a win_lane_only.sh rerun
#   folder after a win/-only fix) and that the OWNER approved to ship. Never edits a summary. Requires: the Mac
#   summary is for this dylib, its Mac lines have no FAIL (windows_* lines excepted) and include dylib/src unchanged;
#   every *.res in the rerun folder is PASS and its baselib.dll is the DLL that rerun reports. Writes
#   release/release_note_<sha16>.txt with both evidence paths, both hashes and the owner's words.
# Freezes build/dsrig.dylib (only if its sha matches the gated one), then builds from that exact file:
# the MP app (~/Applications), the friend kit zip (rig/) and the LAN portable zip (rig/lan/).
set -e; setopt pipefail; cd "${0:A:h}"
OWNER_OK=0; [[ $1 == --owner-approved ]] && { OWNER_OK=1; shift; }
WANT=$1; [[ -n $WANT ]] || { echo "usage: $0 [--owner-approved] <sha256 prefix of the gated dylib> ..." >&2; exit 1; }
if (( OWNER_OK )); then
  SUM=$2; WINDIR=${3%/}; OWNER_SAYS=$4
  [[ ${#WANT} -ge 16 && -f $SUM && -d $WINDIR && -n $OWNER_SAYS ]] || { echo "usage: $0 --owner-approved <sha> <Mac summary> <Windows rerun dir> \"<owner words (date)>\"" >&2; exit 1; }
  head -1 $SUM | grep -q "dylib sha256 $WANT" || { echo "$SUM is not for dylib $WANT" >&2; exit 1; }
  # Mac part = the result lines before the first GREEN verdict line
  MACL=$(awk '/^(ALL GREEN|NOT GREEN)/{exit} $2 ~ /^(PASS|FAIL)$/' $SUM)
  MACBAD=$(print -r -- "$MACL" | awk '$2 == "FAIL" && $1 !~ /^windows_/')
  [[ -z $MACBAD ]] || { echo "Mac part of $SUM has FAIL lines:" >&2; print -r -- "$MACBAD" >&2; exit 1; }
  for k in dylib_unchanged src_unchanged build_dylib_unchanged; do
    print -r -- "$MACL" | grep -q "^$k PASS" || { echo "$SUM: no '$k PASS'" >&2; exit 1; }; done
  MACN=$(print -r -- "$MACL" | awk '$2 == "PASS"' | wc -l | tr -d ' ')
  RES=($WINDIR/*.res(N)); (( ${#RES} >= 8 )) || { echo "$WINDIR: ${#RES} .res files (8 expected)" >&2; exit 1; }
  cat $RES | awk '$2 != "PASS"' | grep -q . && { echo "$WINDIR has non-PASS results" >&2; exit 1; }
  WDLL=$WINDIR/baselib.dll; [[ -f $WDLL ]] || { echo "no tested baselib.dll in $WINDIR" >&2; exit 1; }
  WSHA=$(shasum -a 256 $WDLL | cut -d' ' -f1)
  WTS=${WINDIR:t}; WTS=${WTS##*_}
  grep -q "rerun $WTS.*baselib.dll sha256 $WSHA" $SUM || { echo "$SUM does not report baselib.dll $WSHA for ${WINDIR:t}" >&2; exit 1; }
else
  # the full regression (release_gate.sh) must be green for this exact dylib
  SUM=release/gate_${WANT:0:16}.summary
  [[ ${#WANT} -ge 16 && -f $SUM ]] || { echo "no release gate summary $SUM: run release_gate.sh first" >&2; exit 1; }
  [[ $(tail -1 $SUM) == "ALL GREEN $WANT"* ]] || { echo "release gate not green for $WANT:" >&2; tail -1 $SUM >&2; exit 1; }
  awk '$2 == "FAIL"' $SUM | grep -q . && { echo "release gate summary has FAIL lines" >&2; exit 1; }
fi
mkdir -p release
LANDIR=${LANDIR:-$PWD/lan_latest}
if [[ ${LANDIR:t} == lan_latest && -e $LANDIR ]]; then   # owner: one release folder; the previous one goes to old_versions
  mkdir -p old_versions; mv $LANDIR old_versions/lan_latest-$(date +%Y-%m-%d-%H%M); mkdir -p $LANDIR
fi
FROZEN=$(print -l release/${WANT:0:16}*/dsrig.dylib(N) release/dsrig-${WANT:0:16}*.dylib(N) | head -1)
if [[ -n $FROZEN ]]; then                      # already frozen earlier: build/ may have moved on
  SHA=$(shasum -a 256 $FROZEN | cut -d' ' -f1)
  [[ $SHA == $WANT* ]] || { echo "$FROZEN is $SHA, not $WANT" >&2; exit 1; }
else
  SHA=$(shasum -a 256 ../build/dsrig.dylib | cut -d' ' -f1)
  [[ $SHA == $WANT* ]] || { echo "build/dsrig.dylib is $SHA, not $WANT: refusing" >&2; exit 1; }
  FROZEN=release/dsrig-${SHA:0:16}.dylib
  cp ../build/dsrig.dylib $FROZEN
  [[ $(shasum -a 256 $FROZEN | cut -d' ' -f1) == $SHA ]] || { echo "copy changed during freeze" >&2; exit 1; }
fi
echo "using $FROZEN"
RANGES=release/det_ranges-${WANT:0:16}.tsv; [[ -f $RANGES ]] || RANGES=${FROZEN:h}/det_ranges.tsv; [[ -f $RANGES ]] || RANGES=../build/det_ranges.tsv
STAGE=$(mktemp -d); cp $FROZEN $STAGE/dsrig.dylib; cp $RANGES $STAGE/det_ranges.tsv   # make_mp_app reads ranges next to the dylib
[[ $(shasum -a 256 $STAGE/dsrig.dylib | cut -d' ' -f1) == $SHA ]] || { echo "staged dylib differs" >&2; exit 1; }
echo "ranges $RANGES $(shasum -a 256 $RANGES | cut -c1-16)"
nice -n 5 ./make_mp_app.sh "$STAGE/dsrig.dylib" | tail -1
LANDIR=$LANDIR nice -n 5 ./make_mp_app.sh --portable "$STAGE/dsrig.dylib" | tail -1
K=$(mktemp -d)/DungeonSettlersMP; mkdir -p $K
cp $FROZEN $K/dsrig.dylib; cp $RANGES $K/det_ranges.tsv; cp make_mp_app.sh mp_launcher mp_launcher.c $K/; cp kit_README.txt $K/README.txt
mkdir -p old_versions; [[ -e DungeonSettlersMP_friend_kit.zip ]] && mv DungeonSettlersMP_friend_kit.zip old_versions/DungeonSettlersMP_friend_kit-$(date +%Y-%m-%d-%H%M%S).zip
RIG=$PWD; (cd ${K:h} && zip -qr "$RIG/DungeonSettlersMP_friend_kit.zip" DungeonSettlersMP)
echo "dylib sha256 $SHA"
for f in "$HOME/Applications/Dungeon Settlers MP.app/Contents/Frameworks/dsrig.dylib" "$LANDIR/Dungeon Settlers MP (LAN).app/Contents/Frameworks/dsrig.dylib" "$HOME/Applications/Dungeon Settlers MP.app/Contents/Frameworks/det_ranges.tsv" "$LANDIR/Dungeon Settlers MP (LAN).app/Contents/Frameworks/det_ranges.tsv"; do
  echo "$(shasum -a 256 "$f" | cut -c1-64)  $f"
done
unzip -p $LANDIR/DungeonSettlersMP_LAN.zip "Dungeon Settlers MP (LAN).app/Contents/Frameworks/dsrig.dylib" | shasum -a 256 | sed 's/-$/LAN zip dylib/'
unzip -p DungeonSettlersMP_friend_kit.zip DungeonSettlersMP/dsrig.dylib | shasum -a 256 | sed 's/^/kit dylib /'
unzip -p DungeonSettlersMP_friend_kit.zip DungeonSettlersMP/det_ranges.tsv | shasum -a 256 | sed "s/^/kit ranges /"
ls -la DungeonSettlersMP_friend_kit.zip $LANDIR/DungeonSettlersMP_LAN.zip
if (( OWNER_OK )); then
  N=release/release_note_${WANT:0:16}.txt
  { print "release $(date '+%Y-%m-%d %H:%M')  owner-approved (two-part gate, not one ALL GREEN summary)"
    print "owner: $OWNER_SAYS"
    print "Mac dylib sha256 $SHA  ($FROZEN)"
    print "Mac ranges sha256 $(shasum -a 256 $RANGES | cut -d' ' -f1)  ($RANGES)"
    print "Mac evidence: $MACN PASS, 0 Mac FAIL  ${SUM:A}"
    print "Windows baselib.dll sha256 $WSHA  (${WDLL:A})"
    print "Windows evidence: ${#RES}/${#RES} PASS  ${WINDIR:A}"
  } > $N
  cat $N
fi
