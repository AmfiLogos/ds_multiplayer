// The builder's own sheet for builds given on stdin (JSON array), with extra stat adjustments that apply to every unit
// in the test world (the host's difficulty affecter: same AdjustStats the game reads), via the builder's b.extraAdj.
// A build may carry _worldAdj (its own world effects, e.g. a Lycan's daytime affecter).
// Usage: echo '[builds]' | node builder_sheet.js '[["CriticalChance",10]]'
const c = require('./wiki_codec_probe.js').load(), D = c.D;
const extra = JSON.parse(process.argv[2] || '[]');
let s = ''; process.stdin.on('data', d => s += d).on('end', () => {
  const out = JSON.parse(s).map(b => { b = Object.assign({}, b, { extraAdj: extra.concat(b._worldAdj || []) }); const r = c.computeBuild(b);
    const o = Object.assign({}, r.st); D.majors.forEach(m => o[m] = r.majors[m]); return o; });
  process.stdout.write(JSON.stringify(out));
});
