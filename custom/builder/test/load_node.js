// Loads the wiki's data.js + builder.js and the Unit Builder model (../bld_glue.js) into a node vm context, the way the
// wiki's own node harness does (custom/fights/test/ref_node.js). Without CompressionStream, like QuickJS: the wiki's
// encodeTeam then mints its raw (v1) link, which every wiki client reads.
'use strict';
const fs = require('fs'), vm = require('vm'), path = require('path'), zlib = require('zlib');
module.exports = function load(builderDir, opts) {
  opts = opts || {};
  const ctx = { console, setTimeout, clearTimeout, atob: global.atob, btoa: global.btoa };
  ctx.global = ctx;
  vm.createContext(ctx);
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
  const bsrc = fs.readFileSync(path.join(builderDir, 'builder.js'), 'utf8');
  vm.runInContext('var window = global;', ctx);
  vm.runInContext(fs.readFileSync(path.join(builderDir, 'data.js'), 'utf8'), ctx, { filename: 'data.js' });
  vm.runInContext('window = undefined;', ctx);
  vm.runInContext(bsrc, ctx, { filename: 'builder.js' });
  if (opts.glue !== false) {
    ctx.__BLD_BUILDER_SRC = bsrc;
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'bld_glue.js'), 'utf8'), ctx, { filename: 'bld_glue.js' });
  }
  return ctx;
};
