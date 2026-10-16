#!/bin/zsh
# Names check: gallery.c titles / member names / author names == the wiki's own site/assets/teamname.js on node,
# for live gallery pages (fetched here into build_c4/, not committed) and the live builds/words.json.
# usage: custom/gallery/test/gal_names.sh [wiki site dir]
set -e
H=${0:A:h}; R=${H:h:h:h}; B=$R/build_c4; SITE=${1:-$HOME/dungeonsettlersdb/site}
D=$B/gal_live; mkdir -p $D
W=https://dungeonsettlers.wiki
curl -sf -m 20 $W/builds/words.json -o $D/words.json
pages=()
for k in team char; do for s in popular recent; do for p in 0 1; do
  curl -sf -m 20 "$W/api/gallery?kind=$k&sort=$s&page=$p" -o $D/$k-$s-$p.json && pages+=($D/$k-$s-$p.json)
done; done; done
$B/gal_test --names $D/words.json $pages > $D/names_c.tsv
node -e '
const T = require(process.argv[1]); const fs = require("fs");
const W = JSON.parse(fs.readFileSync(process.argv[2], "utf8")).W; const out = [];
for (const f of process.argv.slice(3)) for (const it of JSON.parse(fs.readFileSync(f, "utf8")).items) {
  out.push(it.id + "\ttitle\t" + T.render(it.f.name, W));
  (it.u || []).slice(0, 8).forEach((u, i) => out.push(it.id + "\tmember" + i + "\t" + T.memberName((it.f.races || [])[i] || "", i, it.k === "char", W)));
  out.push(it.id + "\tauthor\t" + T.authorName(it.au, W));
}
process.stdout.write(out.join("\n") + "\n");' $SITE/assets/teamname.js $D/words.json $pages > $D/names_js.tsv
echo "items: $(grep -c $'\ttitle\t' $D/names_c.tsv), names: $(wc -l < $D/names_c.tsv | tr -d ' ')"
if cmp -s $D/names_c.tsv $D/names_js.tsv; then echo "names identical to teamname.js"; else diff $D/names_js.tsv $D/names_c.tsv | head -20; exit 1; fi
