// Custom Modes - Test Fights: the browser pieces the wiki's builder scripts use, for QuickJS (sg_js.c). Loaded first.
// Only what the scripts reach on the paths the game uses: fetch (templates), atob (links), and the
// Blob -> DecompressionStream('deflate-raw') -> Response chain of builder.js _dsbInflate (v2 team links).
// Each one is defined only when the engine lacks it. Natives from sg_js.c: __cm_template(file) -> text | throws,
// __cm_inflate(latin1) -> latin1 | throws.
var global = globalThis;
(function (G) {
  'use strict';
  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  if (typeof G.atob === 'undefined') {
    G.atob = function (s) {
      s = String(s).replace(/[\t\n\f\r ]+/g, '');
      if (s.length % 4 === 0) s = s.replace(/==?$/, '');
      if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new Error('InvalidCharacterError');
      var out = '', buf = 0, bits = 0;
      for (var i = 0; i < s.length; i++) {
        buf = (buf << 6) | B64.indexOf(s.charAt(i)); bits += 6;
        if (bits >= 8) { bits -= 8; out += String.fromCharCode((buf >> bits) & 0xff); }
      }
      return out;
    };
  }
  if (typeof G.btoa === 'undefined') {
    G.btoa = function (s) {
      s = String(s); var out = '';
      for (var i = 0; i < s.length; i += 3) {
        var a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
        if (a > 255 || b > 255 || c > 255) throw new Error('InvalidCharacterError');
        var n = (a << 16) | ((b || 0) << 8) | (c || 0);
        out += B64.charAt(n >> 18) + B64.charAt((n >> 12) & 63) +
          (i + 1 < s.length ? B64.charAt((n >> 6) & 63) : '=') + (i + 2 < s.length ? B64.charAt(n & 63) : '=');
      }
      return out;
    };
  }
  // savegen.js SG.loadTemplate: fetch(SG.templateUrl(name)).then(r => r.ok ? r.json() : throw)
  if (typeof G.fetch === 'undefined') {
    G.fetch = function (url) {
      var file = String(url).split('?')[0].split('/').pop();
      return new Promise(function (res) {
        var text = null;
        try { text = __cm_template(file); } catch (e) { text = null; }
        if (text == null) { res({ ok: false, status: 404 }); return; }
        res({ ok: true, status: 200, json: function () { return Promise.resolve(JSON.parse(text)); },
          text: function () { return Promise.resolve(text); } });
      });
    };
  }
  function toLatin1(u8) {
    var s = '';
    for (var i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, Array.prototype.slice.call(u8, i, i + 4096));
    return s;
  }
  function fromLatin1(s) {
    var u = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) u[i] = s.charCodeAt(i);
    return u;
  }
  if (typeof G.Blob === 'undefined') {
    G.Blob = function (parts) {
      var all = [];
      (parts || []).forEach(function (p) { for (var i = 0; i < p.length; i++) all.push(p[i] & 0xff); });
      this._u8 = new Uint8Array(all);
      this.size = all.length;
    };
    G.Blob.prototype.stream = function () {
      var u8 = this._u8;
      return { pipeThrough: function (t) { return t._run(u8); } };
    };
  }
  if (typeof G.DecompressionStream === 'undefined') {
    G.DecompressionStream = function (format) {
      if (format !== 'deflate-raw') throw new TypeError('Unsupported compression format: ' + format);
    };
    G.DecompressionStream.prototype._run = function (u8) {
      try { return { _out: fromLatin1(__cm_inflate(toLatin1(u8))) }; } catch (e) { return { _err: e }; }
    };
  }
  if (typeof G.Response === 'undefined') {
    G.Response = function (body) { this._b = body; };
    G.Response.prototype.arrayBuffer = function () {
      var b = this._b;
      if (!b || b._err) return Promise.reject(b && b._err || new TypeError('Failed to fetch'));
      return Promise.resolve(b._out.buffer);
    };
  }
  if (typeof G.console === 'undefined') G.console = { log: function () { }, warn: function () { }, error: function () { } };
  // the scripts' module hook (savegen.js ends with `if (typeof module ...) module.exports = SG`)
  G.module = { exports: {} };
})(globalThis);
