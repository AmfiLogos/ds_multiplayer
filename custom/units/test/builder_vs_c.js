// The C link reader (custom/units/wk_link.c + pvp_wiki.c) and the Compact Plus card (mu_card.c) against the wiki builder
// itself, on random builds (builder_ref.js gen: legal and illegal picks, every field the link carries).
//   node builder_vs_c.js CARD_TOOL [N=500] [SEED=11]
// Per build, every link form: ?team=<v1>, #team=<v2 deflate>, &team= %-encoded, #b=~<v1>, a bare code, and the
// one-unit JSON link #b=<base64 JSON>; plus teams of 2-8 units. Checks:
//   1. decode: the C units (wk_json) == the builder's own decode of the same code (_decodeBlob), field by field
//   2. card:   mu_card of each C unit == card_ref.js (builder.js functions) of the builder's decoded unit
//   3. every form of one build gives the same C units
// Exit 1 on any difference; the first ones are printed.
const path = require('path'), { execFileSync } = require('child_process'), zlib = require('zlib');
const { make } = require(path.join(__dirname, 'node', 'card_ref.js'));
const { c, card } = make(), D = c.D;
const tool = process.argv[2], N = +(process.argv[3] || 500), SEED = +(process.argv[4] || 11);
if (!tool) { console.error('usage: node builder_vs_c.js CARD_TOOL [N] [SEED]'); process.exit(2); }

// builder_ref.js gen (copied call: the generator lives in that file's main part)
const gen = JSON.parse(execFileSync('node', [path.join(__dirname, 'node', 'builder_ref.js'), 'gen', String(N), String(SEED)], { maxBuffer: 1 << 28 })
  .toString().trim().split('\n').map(l => l).join(',').replace(/^/, '[').replace(/$/, ']'));
const W = 'https://dungeonsettlers.wiki/builder';
const b64 = s => s.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
function v2of(v1) { const by = Buffer.from(v1.replace(/-/g, '+').replace(/_/g, '/'), 'base64'); by[0] = 2; return b64(zlib.deflateRawSync(by).toString('base64')); }
function decodeCode(code) { const by = c.dsbB64ToBytes(code); return by ? c._decodeBlob(by) : null; }
// canonical: numbers to 1e-4 (float32 in C vs double in JS), keys sorted, empty lists / false / null dropped
function canon(v) {
  if (Array.isArray(v)) return v.map(canon);
  if (v && typeof v === 'object') { const o = {}; Object.keys(v).sort().forEach(k => { const x = canon(v[k]);
    if (x === null || x === undefined || x === false || (Array.isArray(x) && !x.length) || (x && typeof x === 'object' && !Array.isArray(x) && !Object.keys(x).length)) return; o[k] = x; }); return o; }
  if (typeof v === 'number') return Math.round(v * 1e4) / 1e4;
  return v;
}
const same = (a, b) => JSON.stringify(canon(a)) === JSON.stringify(canon(b));
function firstDiff(a, b, p = '') {
  a = canon(a); b = canon(b);
  if (JSON.stringify(a) === JSON.stringify(b)) return null;
  if (a && b && typeof a === 'object' && typeof b === 'object') {
    for (const k of new Set(Object.keys(a).concat(Object.keys(b)))) { const d = firstDiff(a[k], b[k], p + '.' + k); if (d) return d; }
  }
  return p + ': C ' + JSON.stringify(a) + ' / builder ' + JSON.stringify(b);
}

(async () => {
  const cases = [];   // {what, link, ref: [builder units]}
  for (let i = 0; i < gen.length; i++) {
    const b = gen[i], v1 = await c.encodeTeam([b]), d = decodeCode(v1);
    if (!d || !d.units || !d.units.length) continue;
    const v2 = v2of(v1), g = 'b' + i;
    cases.push({ g, what: '?team=v1', link: W + '?team=' + v1, ref: d.units });
    cases.push({ g, what: '#team=v2', link: W + '#team=' + v2, ref: d.units });
    cases.push({ g, what: '&team=%', link: W + '?lang=fr&team=' + encodeURIComponent(v1).replace(/-/g, '%2D'), ref: d.units });
    cases.push({ g, what: '#b=~v1', link: W + '#b=~' + v1, ref: d.units });
    cases.push({ g, what: 'bare', link: '  ' + v2 + '  ', ref: d.units });
    // the full JSON one-unit link: the builder opens it as the JSON itself; the C packs it like the encoder (keys ->
    // link values), so the reference is the decode of the encoded team
    cases.push({ g, what: '#b=json', link: W + '#b=' + c.b64enc(b), ref: d.units, json: 1 });
  }
  for (let t = 0, s = SEED; t < Math.max(10, N / 10); t++) {
    s = (Math.imul(s, 1103515245) + 12345) >>> 0; const k = 2 + (s >>> 8) % 7, team = [];
    for (let j = 0; j < k; j++) { s = (Math.imul(s, 1103515245) + 12345) >>> 0; team.push(gen[(s >>> 8) % gen.length]); }
    const v1 = await c.encodeTeam(team), d = decodeCode(v1);
    if (d && d.units && d.units.length) cases.push({ g: 't' + t, what: 'team of ' + k, link: W + '?team=' + v1, ref: d.units });
  }
  const res = execFileSync(tool, { input: cases.map(x => x.link).join('\n') + '\n', maxBuffer: 1 << 28 }).toString().trim().split('\n').map(l => JSON.parse(l));
  let nDec = 0, nCard = 0, bad = 0, refused = 0, refusedJson = 0, units = 0; const shown = []; const byG = {}, cov = {};
  const show = s => { if (shown.length < 12) shown.push(s); };
  cases.forEach((x, i) => {
    const r = res[i];
    if (r.err) { if (x.json) { refusedJson++; return; } bad++; refused++; show(`${x.g} ${x.what}: C refused: ${r.err}`); return; }
    if (r.n !== x.ref.length) { bad++; show(`${x.g} ${x.what}: ${r.n} units, builder ${x.ref.length}`); return; }
    for (let u = 0; u < r.n; u++) {
      units++;
      const ref = x.ref[u];
      if (!x.json) { nDec++; const d = firstDiff(r.dec[u], ref); if (d) { bad++; show(`${x.g} ${x.what} unit ${u} decode ${d}`); } }
      const k = r.cards[u]; ['bar', 'stacks', 'insc', 'souls', 'acq', 'eq', 'active', 'passive'].forEach(f => { if (k[f].length) cov[f] = (cov[f] || 0) + 1; });
      if (k.passives.some(p => p[1])) cov.folded = (cov.folded || 0) + 1; if (k.weapon) cov.weapon = (cov.weapon || 0) + 1;
      nCard++; const cd = firstDiff(r.cards[u], card(ref)); if (cd) { bad++; show(`${x.g} ${x.what} unit ${u} card ${cd}`); }
    }
    if (x.g[0] === 'b') { const key = JSON.stringify(canon(r.dec)); (byG[x.g] = byG[x.g] || new Set()).add(key); }
  });
  const split = Object.keys(byG).filter(g => byG[g].size > 1);
  if (split.length) { bad += split.length; show(`forms disagree on ${split.length} builds, e.g. ${split[0]}`); }
  // folding: the unit writer (custom/units/pvp_build.c pb_folded_skill) folds exactly the builder's conditional passives
  console.log(`builder vs C: ${gen.length} builds, ${cases.length} links, ${units} units; decode ${nDec} compared, card ${nCard} compared; ` +
    `JSON links the C refuses (bad keys the builder drops): ${refusedJson}; differences ${bad}`);
  console.log('  cards with a non-empty field: ' + JSON.stringify(cov));
  shown.forEach(s => console.log('  DIFF ' + s));
  process.exit(bad ? 1 : 0);
})();
