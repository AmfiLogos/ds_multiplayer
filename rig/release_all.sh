#!/bin/zsh
# release_all.sh <expected sha256 prefix>
# Refuses unless release/gate_<sha16>.summary (release_gate.sh) ends "ALL GREEN <sha>" for this dylib.
# Freezes build/dsrig.dylib (only if its sha matches the gated one), then builds from that exact file:
# the MP app (~/Applications), the friend kit zip (rig/) and the LAN portable zip (rig/lan/).
set -e; setopt pipefail; cd "${0:A:h}"
WANT=$1; [[ -n $WANT ]] || { echo "usage: $0 <sha256 prefix of the gated dylib>" >&2; exit 1; }
# the full regression (release_gate.sh) must be green for this exact dylib
SUM=release/gate_${WANT:0:16}.summary
[[ ${#WANT} -ge 16 && -f $SUM ]] || { echo "no release gate summary $SUM: run release_gate.sh first" >&2; exit 1; }
[[ $(tail -1 $SUM) == "ALL GREEN $WANT"* ]] || { echo "release gate not green for $WANT:" >&2; tail -1 $SUM >&2; exit 1; }
awk '$2 == "FAIL"' $SUM | grep -q . && { echo "release gate summary has FAIL lines" >&2; exit 1; }
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
nice -n 5 ./make_mp_app.sh "${FROZEN:A}" | tail -1
LANDIR=$LANDIR nice -n 5 ./make_mp_app.sh --portable "${FROZEN:A}" | tail -1
K=$(mktemp -d)/DungeonSettlersMP; mkdir -p $K
cp $FROZEN $K/dsrig.dylib; cp ${FROZEN:h}/det_ranges.tsv $K/ 2>/dev/null || cp ../build/det_ranges.tsv $K/; cp make_mp_app.sh mp_launcher mp_launcher.c $K/; cp kit_README.txt $K/README.txt
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
