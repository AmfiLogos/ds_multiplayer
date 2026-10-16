// The REFERENCE: the wiki's own builder scripts on node (V8), loaded the way the wiki's gate harness loads them
// (~/dungeonsettlersdb/pipeline/savegen/holders_harness.js), with the browser pieces they use given by node itself
// (atob, zlib inflateRaw for DecompressionStream). Writes the same outputs as sg_cli, for a byte compare.
//
// usage: node ref_node.js <builder dir> <saves dir> <out dir> <cases.json> <builds.json> [blobs.json]
//   cases.json : [{ id, builds: "<set>" | "<set>:i,j", opts: {...} }]   builds.json: { "<set>": [builds] }
//   blobs.json : { "<name>": "<gallery blob>" }  -> <out>/decode_<name>.json
//   env DET_SEED, DET_NOW (both engines get the same test/det_prelude.js)
'use strict';
const fs = require('fs'), vm = require('vm'), path = require('path'), zlib = require('zlib');
const [dir, saves, out, casesPath, buildsPath, blobsPath] = process.argv.slice(2);
const here = __dirname;

const ctx = { console, setTimeout, clearTimeout, atob: global.atob, btoa: global.btoa };
ctx.global = ctx;
vm.createContext(ctx);
ctx.__DET_SEED = +(process.env.DET_SEED || 1);
ctx.__DET_NOW = +(process.env.DET_NOW || 1760000000000);
vm.runInContext(fs.readFileSync(path.join(here, 'det_prelude.js'), 'utf8'), ctx, { filename: 'det_prelude.js' });
// fetch of a template: from the saves folder (what the wiki serves at /assets/saves/)
ctx.fetch = function (url) {
  const f = path.join(saves, String(url).split('/').pop().split('?')[0]);
  if (!fs.existsSync(f)) return Promise.resolve({ ok: false, status: 404 });
  const text = fs.readFileSync(f, 'utf8');
  return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(JSON.parse(text)) });
};
// builder.js _dsbInflate: Response(Blob.stream().pipeThrough(DecompressionStream('deflate-raw'))).arrayBuffer()
ctx.Blob = function (parts) { this._b = Buffer.from(parts[0]); };
ctx.Blob.prototype.stream = function () { const b = this._b; return { pipeThrough: (t) => t._run(b) }; };
ctx.DecompressionStream = function (fmt) { if (fmt !== 'deflate-raw') throw new TypeError(fmt); };
ctx.DecompressionStream.prototype._run = function (b) { try { return { out: zlib.inflateRawSync(b) }; } catch (e) { return { err: e }; } };
ctx.Response = function (body) { this._b = body; };
ctx.Response.prototype.arrayBuffer = function () {
  const b = this._b;
  if (b.err) return Promise.reject(b.err);
  return Promise.resolve(b.out.buffer.slice(b.out.byteOffset, b.out.byteOffset + b.out.length));
};
ctx.module = { exports: {} };
vm.runInContext('var window = global;', ctx);
vm.runInContext(fs.readFileSync(path.join(dir, 'data.js'), 'utf8'), ctx, { filename: 'data.js' });
vm.runInContext('window = undefined;', ctx);
vm.runInContext(fs.readFileSync(path.join(dir, 'builder.js'), 'utf8'), ctx, { filename: 'builder.js' });
vm.runInContext(fs.readFileSync(path.join(dir, 'savegen.js'), 'utf8'), ctx, { filename: 'savegen.js' });
ctx.__CM_BUILDER_SRC = fs.readFileSync(path.join(dir, 'builder.js'), 'utf8');
vm.runInContext(fs.readFileSync(path.join(here, '..', 'sg_glue.js'), 'utf8'), ctx, { filename: 'sg_glue.js' });
const CMF = ctx.CMF;

function pick(sets, spec) {
  const [name, idx] = String(spec).split(':');
  const all = sets[name];
  if (!all) throw new Error('no build set ' + name);
  return idx ? idx.split(',').map((i) => all[+i]) : all;
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, 'panel.json'), JSON.stringify(CMF.panel()));
  if (blobsPath) {
    const blobs = JSON.parse(fs.readFileSync(blobsPath, 'utf8'));
    for (const k of Object.keys(blobs)) {
      const b = await CMF.decode(blobs[k]);
      fs.writeFileSync(path.join(out, 'decode_' + k + '.json'), JSON.stringify(b));
    }
  }
  const cases = JSON.parse(fs.readFileSync(casesPath, 'utf8'));
  const sets = JSON.parse(fs.readFileSync(buildsPath, 'utf8'));
  for (const c of cases) {
    const opts = Object.assign({}, c.opts, { builds: pick(sets, c.builds) });
    try {
      const r = await CMF.generate(JSON.parse(JSON.stringify(opts)));
      fs.writeFileSync(path.join(out, c.id + '.save.json'), r.file);
      const meta = Object.assign({}, r); delete meta.file;
      fs.writeFileSync(path.join(out, c.id + '.meta.json'), JSON.stringify(meta));
    } catch (e) {
      fs.writeFileSync(path.join(out, c.id + '.error.txt'), String(e && e.message || e));
    }
  }
})().catch((e) => { console.error(e && e.stack || e); process.exit(1); });
