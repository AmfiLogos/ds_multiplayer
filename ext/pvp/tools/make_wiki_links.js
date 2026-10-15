// Test vectors for the C decoder: random teams encoded by the wiki's own encoder (v1) and deflated (v2), with the wiki's
// own decode as the expected value. Output: JSON lines {code_v1, code_v2, expect}. Usage: node make_wiki_links.js N SEED
const zlib = require('zlib'), c = require('./wiki_codec_probe.js').load(), D = c.D, X = c.dsbIdx();
let seed = +(process.argv[3] || 1); const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
const pick = a => a[rnd(a.length)], some = (a, k) => { const o = []; for (let i = 0; i < k; i++) { const v = pick(a); if (o.indexOf(v) < 0) o.push(v); } return o; };
const N = +(process.argv[2] || 50);
(async () => {
  for (let t = 0; t < N; t++) {
    const team = [];
    for (let u = 0; u < 1 + rnd(4); u++) {
      const race = pick(X.RACE), b = { race, bg: rnd(5) ? pick(X.BG) : null, lvl: 1 + rnd(30), tal: {}, offer: pick(['Least', 'Ordinary', 'Plentiful']), roll: pick(['min', 'avg', 'max']),
        advanced: !!rnd(2), addShield: !!rnd(2), hideHead: !!rnd(2), inds: some(X.IND, rnd(4)), insctrees: some(X.TREE, rnd(4)), learned: some(X.SKILL, rnd(7)), insc: some(X.INSC, rnd(4)), eq: {}, tags: {}, manual: {} };
      D.majors.forEach(m => b.tal[m] = pick(D.tiers.order));
      X.SLOT.forEach(s => { if (rnd(2)) { const k = pick(X.ITEM); b.eq[s] = k; const it = D.items[k]; if (it.tags && it.tags.length && rnd(2)) b.tags[s] = [{ i: 0, v: it.tags[0][1] + (it.tags[0][2] - it.tags[0][1]) * (rnd(256) / 255) }]; } });
      if (rnd(2)) b.name = 'Unit ' + t + '-' + u;
      const pool = (D.portraits || {})[race] || []; if (pool.length && rnd(2)) b.portrait = pick(pool);
      if (rnd(3) === 0) { b.manual[pick(X.STAT)] = rnd(500) / 10; }
      if (rnd(3) === 0) { b.majorBase = {}; D.majors.forEach(m => b.majorBase[m] = rnd(300) / 10); }
      if (rnd(3) === 0) { b.passiveStacks = {}; b.passiveStacks[pick(X.SKILL)] = rnd(5); }
      if (rnd(3) === 0) b.souls = some(X.SOUL, 2);
      if (rnd(3) === 0) b.savedInds = some(X.IND, 3);
      if (rnd(3) === 0) b.savedInsc = some(X.INSC, 2);
      if (rnd(3) === 0) b.acqInds = some(X.ACQ, 2);
      team.push(b);
    }
    const v1 = await c.encodeTeam(team);
    const bytes = c.dsbB64ToBytes(v1); bytes[0] = 2;
    const v2 = c.dsbBytesToB64(Array.from(zlib.deflateRawSync(Buffer.from(bytes))));
    const expect = c._decodeBlob(c.dsbB64ToBytes(v1));
    process.stdout.write(JSON.stringify({ v1, v2, expect: expect.units, warn: expect.warn }) + '\n');
  }
})();
