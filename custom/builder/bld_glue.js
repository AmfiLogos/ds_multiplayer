// Custom Modes - Unit Builder model (owner AFTER_MVP 4: "Unit Builder drawn in the game's menus"; 2026-10-15: "we want
// to allow people to create custom units inside of the multiplayer"). Runs in QuickJS (bld_js.c) AFTER the wiki's own
// data.js and builder.js, downloaded from dungeonsettlers.wiki and never shipped (the same cache as Test Fights).
// The game window (bld_ui.c) shows what state() returns and sends each click to act(). Nothing here computes a stat or
// decides what a build may hold: the wiki's own validate() (and the helpers it calls) is LIFTED from the builder.js source
// text (__BLD_BUILDER_SRC, set by the host) and evaluated here, never retyped, so every number and every rule equals the
// wiki. A click does what the wiki's click handler does to the build (S), then validate(), exactly like the wiki.
// The reference harness (test/ref_node.js) loads this same file in node, so the in-game model is compared with the wiki
// on V8.
var BLD = (function () {
  'use strict';
  // ---- lift top-level declarations out of the builder.js source (same reader as custom/fights/sg_glue.js)
  function lift(src, decl) {
    var at = src.indexOf('\n' + decl);
    if (at < 0) throw new Error('The wiki builder no longer has ' + decl.trim() + ' - the Unit Builder needs an update.');
    at += 1;
    if (decl.indexOf('var ') === 0) return src.slice(at, src.indexOf(';', at) + 1);
    var i = src.indexOf('{', at), depth = 0;
    for (; i < src.length; i++) {
      var ch = src.charAt(i);
      if (ch === '\\') i++;                                    // an escaped char (regex literals: /\//g)
      else if (ch === '"' || ch === "'") { var q = ch; for (i++; i < src.length && src.charAt(i) !== q; i++) if (src.charAt(i) === '\\') i++; }
      else if (ch === '/' && src.charAt(i + 1) === '/') i = src.indexOf('\n', i);
      else if (ch === '/' && src.charAt(i + 1) === '*') i = src.indexOf('*/', i) + 1;
      else if (ch === '{') depth++;
      else if (ch === '}' && --depth === 0) return src.slice(at, i + 1);
    }
    throw new Error('Could not read ' + decl.trim() + ' from the wiki builder.');
  }
  // the UI part's build rules and display helpers (builder.js after "UI (browser only)")
  var FUNCS = ['validate', 'treeOfSkill', 'raceOf', 'poolWeight', 'bgStarter', 'bgAvail', 'indAvail', 'bgCandidates',
    'firstPortrait', 'defaultBuild', 'raceShort', 'ammoOfPrimary', 'ammoFits', 'sanitizeEq', 'shelfOf', 'treeOfSkillAny',
    'shelfPut', 'shelfDrop', 'tagShelfOf', 'shelfTagsFor', 'unshelfTagsFor', 'setSlotItem', 'normTags', 'jewelStats',
    'jewelRolls', 'jewelPreset', 'jewelRetarget', 'blockReason', 'soloShareBuild', 'soloCompactUrl', 'b64enc',
    'fmtN', 'isPct', 'dispScale', 'statLabel', 'resistDisp', 'inscByKey', 'fillT'];
  var VARS = ['JEWEL', 'SOLO_PREFIX', 'DISP_SCALE', 'DISP_UNIT', 'EQ_ORDER', 'SLOT_LABEL'];
  var W = (function () {
    var src = typeof __BLD_BUILDER_SRC === 'string' ? __BLD_BUILDER_SRC : '';
    var body = 'var S = null, tourHoldBg = false;\n' +
      // defaultBuild seeds a recruit name from the race's pool (the wiki also skips names it has saved; the game
      // window names the unit on Save instead)
      'function defaultCharName(raceK){ var pn = D.playernames || {}; var pool = pn[raceShort(raceK)] || pn.Human || [];' +
      ' return pool.length ? pool[Math.floor(Math.random()*pool.length)] : "New Character"; }\n' +
      VARS.map(function (n) { return lift(src, 'var ' + n + ' '); }).join('\n') + '\n' +
      FUNCS.map(function (n) { return lift(src, 'function ' + n + '('); }).join('\n') + '\n' +
      'return { get: function(){ return S; }, set: function(b){ S = b; }, ' +
      FUNCS.map(function (n) { return n + ': ' + n; }).join(', ') + ', ' +
      VARS.map(function (n) { return n + ': ' + n; }).join(', ') + ' };';
    return (new Function('D', 'MAJ', body))(DSB, MAJ);
  })();

  var BASE = 'https://dungeonsettlers.wiki/builder';
  function clone(o) { return JSON.parse(JSON.stringify(o)); }
  // the wiki's load of a stored build (validatedSnapshot / loadCharacter): defaults under it, then validate
  function load(b) {
    W.set(b && typeof b === 'object' ? Object.assign(W.defaultBuild(), clone(b)) : W.defaultBuild());
    W.validate();
  }
  function S() { return W.get(); }

  // ---- what a click does (the wiki's handlers, minus the page repaint), then validate()
  function spentAndBudget(b, C) {
    var mainBud = mainSPBudget(b), subBud = subSPBudget(b), spentM = 0, spentS = 0;
    b.learned.forEach(function (k) { var t = W.treeOfSkill(k), sk = D().skills[k]; if (!t || !sk) return;
      if (treeIsWeapon(t)) spentM += skillCost(sk, t, C.st); else spentS += sk.sp; });
    return { mainBud: mainBud, subBud: subBud, mLeft: mainBud - spentM, sLeft: subBud - spentS };
  }
  function D() { return DSB; }
  // treeGraph's lock reason for a node (renderTrees budgetOf: what is LEFT in the tree's pool)
  function skillReason(b, C, sp, tree, k, active) {
    var sk = D().skills[k], learned = b.learned.indexOf(k) >= 0, isWeapon = treeIsWeapon(tree);
    var cost = isWeapon ? skillCost(sk, tree, C.st) : sk.sp, reason = '';
    if (!active) reason = 'this tree is not active';
    else if (!learned) {
      if (sk.lv > b.lvl) reason = W.fillT('requires level {#}', sk.lv);
      else { for (var i = 0; i < sk.pre.length; i++) if (b.learned.indexOf(sk.pre[i]) < 0)
               reason = W.fillT('requires {#}', (D().skills[sk.pre[i]] ? D().skills[sk.pre[i]].n : sk.pre[i]));
        if (!reason && cost > (isWeapon ? sp.mLeft : sp.sLeft)) reason = (isWeapon ? 'not enough main SP' : 'not enough sub SP'); }
    }
    return { cost: cost, reason: reason };
  }
  var ACT = {
    name: function (a) { S().name = String(a.v == null ? '' : a.v).slice(0, 48); },
    race: function (a) { if (D().races.some(function (r) { return r.k === a.k; })) S().race = a.k; },   // renderRace chip
    bg: function (a) {                                                                                     // bgPick
      var race = W.raceOf(S()); if (!D().bg[a.k] || !(W.bgAvail(race, a.k) || S().bg === a.k)) return;
      S().bg = a.k;
    },
    ind: function (a) {                                                                                    // renderInds card
      var b = S(), race = W.raceOf(b); if (!D().ind[a.k]) return;
      var was = b.inds.indexOf(a.k) >= 0;
      if (!(W.indAvail(race, a.k) || was)) return;
      if (was) b.inds = b.inds.filter(function (x) { return x !== a.k; });
      else if (b.inds.length < indCap()) b.inds.push(a.k);
    },
    lvl: function (a) { S().lvl = +a.v; },                                                                 // renderLevel range
    tree: function (a) {                                                                                   // renderTreePicker toggleTree
      var b = S(), i = (b.insctrees || []).indexOf(a.k);
      if (i >= 0) { b.insctrees.splice(i, 1); return; }
      if (!D().trees[a.k] || bgTreeSet(b)[a.k] || inscFree(b) <= 0) return;
      b.insctrees.push(a.k);
    },
    skill: function (a) {                                                                                  // treeGraph node click
      var b = S(), k = a.k, sk = D().skills[k]; if (!sk) return;
      var tree = W.treeOfSkill(k); if (!tree) return;
      if (b.learned.indexOf(k) >= 0) {
        var drop = {}; drop[k] = 1; var changed = true;
        while (changed) { changed = false;
          b.learned.forEach(function (x) { if (drop[x]) return; var sx = D().skills[x];
            for (var i = 0; i < sx.pre.length; i++) if (drop[sx.pre[i]]) { drop[x] = 1; changed = true; } }); }
        b.learned = b.learned.filter(function (x) { return !drop[x]; });
        Object.keys(drop).forEach(function (x) { if (x !== k) W.shelfPut(b, x); });
        W.shelfDrop(b, k);
      } else {
        var C = computeBuild(b);
        if (!skillReason(b, C, spentAndBudget(b, C), tree, k, true).reason) b.learned.push(k);
      }
    },
    insc: function (a) {                                                                                   // renderInscPicker toggle
      var b = S(), r = W.inscByKey(a.k); if (!r) return;
      var canAdd = inscFree(b) > 0, t = inscTree(r.k);
      if (t) {
        var ti = (b.insctrees || []).indexOf(t);
        if (ti >= 0) { b.insctrees.splice(ti, 1); return; }
        if (!canAdd) return;
        b.insctrees.push(t); return;
      }
      var i = b.insc.indexOf(r.k);
      if (i >= 0) { b.insc.splice(i, 1); return; }
      if (!canAdd) return;
      b.insc.push(r.k);
    },
    eq: function (a) {                                                                                     // slotBody onPick / clearSlot
      var b = S(), slot = a.slot, k = a.k || null;
      if (D().slots.indexOf(slot) < 0) return;
      if (k && (!D().items[k] || D().items[k].slot !== slot)) return;
      if (k && W.blockReason(slot)) return;
      if (k && slot === 'Ammo' && !W.ammoFits(k, W.ammoOfPrimary(b.eq))) return;
      var was = (slot === 'Primary') ? W.jewelStats(b) : null;
      W.setSlotItem(b, slot, k);
      W.jewelRetarget(b, was);
    },
    roll: function (a) {                                                                                   // slotBody roll select
      var b = S(), it = b.eq[a.slot] && D().items[b.eq[a.slot]]; if (!it || !it.tags || !(a.r >= 0 && a.r < (it.tn || 0))) return;
      if (!b.tags[a.slot]) b.tags[a.slot] = [];
      if (a.i == null || a.i < 0) b.tags[a.slot][a.r] = null;
      else { var tg = it.tags[a.i]; if (!tg) return; b.tags[a.slot][a.r] = { i: a.i, v: (tg[1] + (tg[2] - tg[1]) * 0.5) }; }
    }
  };
  function act(a) {
    if (typeof a === 'string') a = JSON.parse(a);
    var f = ACT[a && a.t];
    if (!f) throw new Error('unknown action ' + (a && a.t));
    W.set(S());          // the same object; validate() reads it through the lifted scope
    f(a);
    W.validate();
    return state();
  }

  // ---- what the window shows
  function nm(o, k) { return (o && o.n) || k; }
  function sheet(b, C) {
    // renderTopCard's grid and majors, as text (resistances as the damage they take off: resistDisp)
    var keys = [['MaxHealthTotal','HP',0],['PhysicalAttackPower','P.Atk',0],['MagicAttackPower','M.Atk',0],
      ['AttackSpeed','Atk Spd',2],['CriticalChance','Crit %',1],['CriticalDamageBonus','Crit Dmg',0],
      ['PhysicalResistance','Phys Res',0],['MagicResistance','Mag Res',0],['MovementSpeed','Move',0]];
    var out = keys.map(function (kv) {
      var v = C.st[kv[0]];
      var t = (kv[0] === 'PhysicalResistance' || kv[0] === 'MagicResistance') ? W.resistDisp(v) : W.fmtN(v, kv[2]) + (W.isPct(kv[0]) ? '%' : '');
      return { k: kv[0], n: kv[1], t: t };
    });
    var maj = MAJ.map(function (m) { return { k: m, n: W.statLabel(m), t: W.fmtN(C.majors[m]) }; });
    return { stats: out, majors: maj };
  }
  function state() {
    var b = S(), C = computeBuild(b), race = W.raceOf(b), sp = spentAndBudget(b, C), bgset = bgTreeSet(b);
    var o = { name: b.name || '', race: b.race, bg: b.bg, lvl: b.lvl, maxLvl: D().consts.maxLevel, portrait: b.portrait || null };
    o.races = D().races.filter(function (r) { return r.rel || r.k === b.race; })
      .map(function (r) { return { k: r.k, n: r.n, on: r.k === b.race }; });
    // backgrounds this race can have (bgCandidates, pickable ones; the current one always)
    o.bgs = W.bgCandidates(race).filter(function (k) { return W.bgAvail(race, k) || b.bg === k; })
      .map(function (k) { var x = D().bg[k]; return { k: k, n: nm(x, k), d: x.d || '', on: b.bg === k,
        trees: (x.mains || []).concat((x.subs || []).map(function (p) { return p[0]; })).filter(function (t) { return D().trees[t]; })
          .map(function (t) { return D().trees[t].n; }).join(', ') }; });
    o.indCap = indCap();
    o.inds = ((race && race.ind) || []).map(function (p) { return p[0]; })
      .filter(function (k) { return D().ind[k] && (W.indAvail(race, k) || b.inds.indexOf(k) >= 0); })
      .map(function (k) { var on = b.inds.indexOf(k) >= 0; return { k: k, n: nm(D().ind[k], k), d: D().ind[k].d || '', on: on,
        ok: on || b.inds.length < indCap() }; });
    o.sp = { main: sp.mLeft, mainMax: sp.mainBud, sub: sp.sLeft, subMax: sp.subBud };
    o.trees = activeTrees(b).map(function (t) {
      var td = D().trees[t];
      var rows = (td.rows || []).filter(function (row) { return row.some(function (k) { return k && D().skills[k]; }); })
        .map(function (row) { return row.map(function (k) {
          if (!k || !D().skills[k]) return null;
          var sk = D().skills[k], r = skillReason(b, C, sp, t, k, true);
          return { k: k, n: sk.n, lv: sk.lv, cost: r.cost, base: sk.sp, on: b.learned.indexOf(k) >= 0, why: r.reason,
            p: sk.ty === 'Passive' ? 1 : 0, d: sk.d || '' }; }); });
      return { k: t, n: td.n, w: treeIsWeapon(t) ? 1 : 0, ins: bgset[t] ? 0 : 1, rows: rows };
    });
    o.insc = { slots: inscSlots(b), trees: inscUsedByTrees(b), used: b.insc.length, free: inscFree(b) };
    o.treeCands = Object.keys(D().trees).filter(function (t) { return !bgset[t]; })
      .sort(function (x, y) { var kx = treeIsWeapon(x) ? 0 : 1, ky = treeIsWeapon(y) ? 0 : 1;
        return kx !== ky ? kx - ky : (D().trees[x].n < D().trees[y].n ? -1 : 1); })
      .map(function (t) { return { k: t, n: D().trees[t].n, w: treeIsWeapon(t) ? 1 : 0, on: (b.insctrees || []).indexOf(t) >= 0 }; });
    o.inscs = D().insc.filter(function (r) { return !inscTree(r.k); })
      .map(function (r) { return { k: r.k, n: r.n, r: r.r, d: r.d || '', on: b.insc.indexOf(r.k) >= 0 }; });
    var need = W.ammoOfPrimary(b.eq);
    o.eq = W.EQ_ORDER.map(function (slot) {
      var k = b.eq[slot] || null, it = k && D().items[k];
      var e = { slot: slot, label: W.SLOT_LABEL[slot] || slot, k: k, n: it ? it.n : '', blocked: W.blockReason(slot) || '' };
      e.items = Object.keys(D().items).filter(function (x) { var i = D().items[x];
          return i.slot === slot && (slot !== 'Ammo' || W.ammoFits(x, need)); })
        .sort(function (a2, b2) { var A = D().items[a2], B = D().items[b2], ao = A.ob ? 0 : 1, bo = B.ob ? 0 : 1;
          if (ao !== bo) return ao - bo; return A.sub === B.sub ? (A.n < B.n ? -1 : 1) : (A.sub < B.sub ? -1 : 1); })
        .map(function (x) { var i = D().items[x]; return { k: x, n: i.n, sub: i.sub, ob: i.ob ? 1 : 0, th: i.th ? 1 : 0, r: i.r || '' }; });
      if (it && it.tags && it.tn) {
        var cur = b.tags[slot] || [];
        e.rolls = [];
        for (var ri = 0; ri < it.tn; ri++) {
          var c = cur[ri], ci = (c && typeof c === 'object') ? c.i : (typeof c === 'number' ? c : -1);
          e.rolls.push({ i: ci == null ? -1 : ci, v: (c && typeof c === 'object' && c.v != null) ? W.fmtN(c.v * W.dispScale(it.tags[ci] ? it.tags[ci][0] : '')) : '' });
        }
        e.tags = it.tags.map(function (tg) { var tsc = W.dispScale(tg[0]); var u = (W.isPct(tg[0]) || tsc !== 1) ? '%' : '';
          return W.statLabel(tg[0]) + ' [' + W.fmtN(tg[1] * tsc) + '-' + W.fmtN(tg[2] * tsc) + u + ']'; });
      }
      return e;
    });
    o.sheet = sheet(b, C);
    return o;
  }

  return {
    load: function (json) { load(json ? (typeof json === 'string' ? JSON.parse(json) : json) : null); return state(); },
    act: act,
    state: state,
    // the build as the wiki stores and shares it (soloShareBuild: the whole validated build)
    build: function () { return W.soloShareBuild(S()); },
    // the wiki's offline unit link (soloCompactUrl: '#b=~' + the ?team= codec of this one build)
    link: function () { return W.soloCompactUrl(W.soloShareBuild(S()), BASE); },
    computed: function () { return computeBuild(S()); },
    tag: function () { return (DSB.savegen && DSB.savegen.tag) || ''; }
  };
})();
