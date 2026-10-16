// Test only: makes the wiki's generator deterministic so QuickJS and node can be compared byte for byte.
// Loaded by BOTH engines before the wiki scripts (sg_js.c with an sg_det, and ref_node.js). __DET_SEED and
// __DET_NOW are set by the caller. savegen.js uses crypto.randomUUID when it exists, else Math.random (newGuid),
// and new Date() for the save's time stamp; builder.js uses Math.random for default names.
(function (G) {
  'use strict';
  var s = (G.__DET_SEED >>> 0) || 1;
  Math.random = function () {                      // mulberry32
    s = (s + 0x6D2B79F5) >>> 0;
    var t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  var Real = Date, NOW = G.__DET_NOW;
  function FixedDate(a, b, c, d, e, f, g) {
    var n = arguments.length;
    if (!(this instanceof FixedDate)) return new Real(NOW).toString();
    if (n === 0) return new Real(NOW);
    if (n === 1) return new Real(a);
    return new Real(a, b, c === undefined ? 1 : c, d || 0, e || 0, f || 0, g || 0);
  }
  FixedDate.now = function () { return NOW; };
  FixedDate.UTC = Real.UTC;
  FixedDate.parse = Real.parse;
  FixedDate.prototype = Real.prototype;
  G.Date = FixedDate;
  try { delete G.crypto; } catch (e) { }
  G.crypto = undefined;
})(globalThis);
