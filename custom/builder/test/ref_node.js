// The REFERENCE for the Unit Builder equality test: the wiki's own builder.js on node (V8) checks every build the game's
// model made (bld_cli, QuickJS):
//   1. the wiki's validate() keeps the build exactly as it is (a build the wiki itself would hold),
//   2. the wiki's computeBuild() gives the same numbers (every stat, byte for byte as JSON),
//   3. the wiki's own unit link (soloCompactUrl's offline form '#b=~' + encodeTeam) is the same text, and the wiki decodes it,
//   4. the model on V8 (bld_glue.js in node) shows the same sheet.
// usage: node ref_node.js <wiki builder dir> <dir with b*.json>
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm');
const load = require('./load_node.js');
const [dir, outDir] = process.argv.slice(2);
const W = load(dir, { glue: false });          // the wiki alone
const M = load(dir);                           // the model on V8
// The wiki's UI part (builder.js after "UI (browser only)") runs only in a browser. Its function declarations are taken
// as they are written, without our lifter: the part's body goes into a function that returns before any of its
// statements runs, so only the hoisted declarations exist (validate, defaultBuild, soloShareBuild, ... over their own S).
const UI = (function () {
  const src = fs.readFileSync(path.join(dir, 'builder.js'), 'utf8');
  const head = "if (typeof document !== 'undefined') (function(){";
  const a = src.indexOf(head), z = src.lastIndexOf('})();');
  if (a < 0 || z < a) throw new Error('builder.js: UI part not found');
  return vm.runInContext('(function(){ return { validate: validate, defaultBuild: defaultBuild, soloShareBuild: soloShareBuild,' +
    ' set: function (b) { S = b; }, get: function () { return S; } };\n' + src.slice(a + head.length, z) + '\n})()', W);
})();
const BASE = 'https://dungeonsettlers.wiki/builder';
(async () => {
  const files = fs.readdirSync(outDir).filter((f) => /^b\d+\.json$/.test(f)).sort();
  let ok = 0, bad = 0;
  const fail = (f, what) => { bad++; if (bad <= 20) console.log('DIFFERENT ' + f + ': ' + what); };
  for (const f of files) {
    const r = JSON.parse(fs.readFileSync(path.join(outDir, f), 'utf8'));
    const b = r.build;
    let n0 = bad;
    // 1. the wiki's validate keeps it
    UI.set(Object.assign(UI.defaultBuild(), JSON.parse(JSON.stringify(b))));
    UI.validate();
    const v = UI.soloShareBuild(UI.get());
    if (JSON.stringify(v) !== JSON.stringify(b)) fail(f, 'the wiki validate() changes the build');
    // 2. numbers
    if (JSON.stringify(W.computeBuild(JSON.parse(JSON.stringify(b)))) !== JSON.stringify(r.computed)) fail(f, 'computeBuild');
    // 3. link
    // soloCompactUrl: base + '#b=~' + encodeTeam([build]) (the core's own encoder)
    const link = BASE + '#b=~' + await W.encodeTeam([UI.soloShareBuild(JSON.parse(JSON.stringify(b)))]);
    if (link !== r.link) fail(f, 'link ' + link + ' vs ' + r.link);
    const code = link.split('#b=~')[1];
    const dec = code ? await W.decodeTeam(code) : null;
    if (!dec || dec.units.length !== 1 || dec.warn) fail(f, 'the wiki does not decode the link');
    // 4. the model on V8: same sheet, same stored build
    const st = M.BLD.load(b);
    if (JSON.stringify(st.sheet) !== JSON.stringify(r.sheet)) fail(f, 'sheet');
    if (JSON.stringify(M.BLD.build()) !== JSON.stringify(b)) fail(f, 'model on V8 stores another build');
    if (bad === n0) ok++;
  }
  console.log('node reference: ' + files.length + ' builds, ' + ok + ' equal, ' + (files.length - ok) + ' different');
  process.exit(ok === files.length && files.length > 0 ? 0 : 1);
})();
