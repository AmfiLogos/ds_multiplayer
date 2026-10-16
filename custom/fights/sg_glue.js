// Custom Modes - Test Fights glue (owner 2026-10-15: "custom fights and boss fights should follow the way it's done
// in the wiki"). Runs in QuickJS (sg_js.c) AFTER the wiki's own data.js, builder.js and savegen.js, downloaded from
// dungeonsettlers.wiki and never shipped. Nothing here computes a unit or a save: every call hands the wiki's own
// functions the same arguments its "Test this team in-game" panel (builder.js renderSaveGen / sgBuild) hands them.
// The reference harness (test/ref_node.js) loads this same file in node, so the in-game result is compared with
// the wiki's code on V8 byte for byte.
//
// Headless, builder.js runs only its pure core (everything after "UI (browser only)" needs a document). The panel's
// own small helpers (sgBossList, sgDummy, sgEnemies, sgKitDefaults, sgMgmtFor, sgDiffTiers, b64dec, SG_MAX_SQUAD,
// SG_SHOW_BASE) live in that UI part, so they are LIFTED from the builder.js source text (__CM_BUILDER_SRC, set by
// the host) and evaluated here, never retyped: when the wiki changes one, the game follows.
var CMF = (function () {
  'use strict';
  var SG = module.exports;
  // the deps builder.js loadSaveGen() passes to SG.init, name for name
  SG.init({ D: DSB, computeBuild: computeBuild, skillCost: skillCost,
    activeTrees: activeTrees, learnedByTree: learnedByTree, treeOfSkillFor: treeOfSkillFor,
    treeInscKey: treeInscKey, mainSPBudget: mainSPBudget, subSPBudget: subSPBudget,
    unitSkillSets: unitSkillSets, quickFor: quickFor,
    stackPassives: stackPassives, stackCountFor: stackCountFor, stackLvFor: stackLvFor,
    slugTeam: slugifyTeamName, diffLabel: diffLabel });

  // ---- lift top-level declarations out of the builder.js source
  function lift(src, decl) {
    var at = src.indexOf('\n' + decl);
    if (at < 0) throw new Error('The wiki builder no longer has ' + decl.trim() + ' - Test Fights needs an update.');
    at += 1;
    if (decl.indexOf('var ') === 0) return src.slice(at, src.indexOf(';', at) + 1);
    // a function: match its braces, skipping strings and comments
    var i = src.indexOf('{', at), depth = 0;
    for (; i < src.length; i++) {
      var ch = src.charAt(i);
      if (ch === '"' || ch === "'") { var q = ch; for (i++; i < src.length && src.charAt(i) !== q; i++) if (src.charAt(i) === '\\') i++; }
      else if (ch === '/' && src.charAt(i + 1) === '/') i = src.indexOf('\n', i);
      else if (ch === '/' && src.charAt(i + 1) === '*') i = src.indexOf('*/', i) + 1;
      else if (ch === '{') depth++;
      else if (ch === '}' && --depth === 0) return src.slice(at, i + 1);
    }
    throw new Error('Could not read ' + decl.trim() + ' from the wiki builder.');
  }
  var UI = (function () {
    var src = typeof __CM_BUILDER_SRC === 'string' ? __CM_BUILDER_SRC : '';
    var names = ['sgDiffTiers', 'sgBossList', 'sgDummy', 'sgEnemies', 'sgKitDefaults', 'sgMgmtFor', 'b64dec'];
    var body = names.map(function (n) { return lift(src, 'function ' + n + '('); }).join('\n') + '\n' +
      lift(src, 'var SG_MAX_SQUAD ') + '\n' + lift(src, 'var SG_SHOW_BASE ') + '\n' +
      'return { sgDiffTiers: sgDiffTiers, sgBossList: sgBossList, sgDummy: sgDummy, sgEnemies: sgEnemies,' +
      ' sgKitDefaults: sgKitDefaults, sgMgmtFor: sgMgmtFor, b64dec: b64dec,' +
      ' SG_MAX_SQUAD: SG_MAX_SQUAD, SG_SHOW_BASE: SG_SHOW_BASE };';
    // the UI part's own `D` is the page's DSB payload
    return (new Function('D', 'diffOrder', body))(DSB, diffOrder);
  })();
  if (typeof __CM_BUILDER_SRC !== 'undefined') __CM_BUILDER_SRC = null;   // 700 KB not kept

  // Magic Saturation (site assets/app.js dsMsat): Abyss rooms only, default day 100, keystones 0/20/40/60/100
  var MSAT_DEFAULT = 100, MSAT_KEYS = [0, 20, 40, 60, 100];
  // builder.js sgRoomIsAbyss: the payload's own flag, else dsMsat.isAbyss (key starts with UNIT_Abyss)
  function isAbyssBoss(id) {
    var bl = UI.sgBossList().filter(function (x) { return x.id === id; })[0];
    if (!bl) return false;
    var u = (DSB.units || []).filter(function (x) { return x.k === bl.key; })[0];
    return !!((u && u.abyss) || String(bl.key).indexOf('UNIT_Abyss') === 0);
  }

  // Everything the Test Fights tab shows, with the wiki panel's defaults (renderSaveGen `st`; combat = the wiki's
  // globalDiff() with nothing stored = Normal).
  function panel() {
    var S = DSB.savegen || {}, K = S.kits || {}, dummy = UI.sgDummy();
    var bosses = UI.sgBossList().map(function (b) {
      return { id: b.id, key: b.key, n: b.n, ic: b.ic, abyss: isAbyssBoss(b.id) };
    });
    var modes = [];
    if (dummy) modes.push('dps');
    modes.push('boss', 'arena');
    if (UI.SG_SHOW_BASE) modes.push('base');   // withheld on the wiki while the base template is a day-0 start
    var mgmtFor = {}, labels = {};
    diffOrder().forEach(function (t) { mgmtFor[t] = UI.sgMgmtFor(t); labels[t] = diffLabel(t); });
    return {
      tag: S.tag, siteVer: S.siteVer, tpl: S.tpl || {}, tplNames: S.tplNames || [],
      maxSquad: UI.SG_MAX_SQUAD, modes: modes, baseWithheld: !UI.SG_SHOW_BASE,
      defaults: { mode: dummy ? 'dps' : 'boss', boss: (bosses[0] || {}).id || null, combat: 'Normal',
        mgmt: UI.sgMgmtFor('Normal'), kits: UI.sgKitDefaults(), kitInsc: true, supplies: {}, msat: MSAT_DEFAULT },
      bosses: bosses, dummy: dummy,
      combatTiers: diffOrder(), mgmtTiers: UI.sgDiffTiers(), mgmtFor: mgmtFor, diffLabels: labels,
      supplies: S.supplies || [], kits: K.kits || [], kitInsc: K.insc || null, kitMiss: K.miss || [],
      enemies: UI.sgEnemies().map(function (u) { return { k: u.k, n: u.n, ic: u.ic || null, abyss: !!u.abyss }; }),
      msat: { def: MSAT_DEFAULT, keys: MSAT_KEYS }
    };
  }

  // opts = the tab's choices: { mode, boss, builds:[stored builds], combat, mgmt, monsters:[{k,n}],
  //   supplies:{key:amount} | [{k,amount}], kits:{id:true} | [id], kitInsc, msat, team }
  // Builds go in as stored (the builder's own build objects, as the wiki's roster keeps them).
  // -> the wiki's generate() result with the save as the text its download writes (JSON.stringify(save, null, 2))
  function generate(o) {
    var builds = (o.builds || []).slice(0, UI.SG_MAX_SQUAD);   // the panel never sends more than an expedition holds
    var sup = Array.isArray(o.supplies) ? o.supplies
      : Object.keys(o.supplies || {}).map(function (k) { return { k: k, amount: o.supplies[k] }; });
    // kit ids in the payload's own order, as sgBuild passes them
    var kon = Array.isArray(o.kits) ? o.kits.reduce(function (m, k) { m[k] = true; return m; }, {})
      : (o.kits || UI.sgKitDefaults());
    var kits = ((DSB.savegen.kits || {}).kits || []).filter(function (k) { return kon[k.id]; }).map(function (k) { return k.id; });
    var abyss = (o.mode === 'boss' || o.mode === 'arena') && isAbyssBoss(o.boss);
    var combat = o.combat || 'Normal';
    return SG.generate({ mode: o.mode, boss: o.boss, builds: builds, combat: combat,
      mgmt: o.mgmt || UI.sgMgmtFor(combat), monsters: o.monsters || [], supplies: sup, kits: kits,
      msat: abyss ? (o.msat == null ? MSAT_DEFAULT : o.msat) : null,
      kitInsc: o.kitInsc !== false, raw: null, team: o.team == null ? '' : o.team,
      applyDifficulty: false
    }).then(function (r) {
      return { file: JSON.stringify(r.save, null, 2), filename: r.filename, problems: r.problems,
        notes: r.notes, warnings: r.warnings, added: r.added, roster: r.roster, room: r.room || null,
        clan: r.clan };
    });
  }

  // a gallery item's stored build (`blob` of /api/gallery?id=<id>&full=1: a ?team= code, or 'b:' + the character
  // JSON) -> builds, by builder.js's own decoders (the steps of its DSB_EMBED_API.decode, without the UI-side
  // validatedSnapshot pass, which needs the page)
  function decode(blob) {
    if (typeof blob !== 'string') return Promise.resolve([]);
    if (blob.indexOf('b:') === 0) {
      var b = UI.b64dec(blob.slice(2));
      return Promise.resolve(b && typeof b === 'object' ? [b] : []);
    }
    return decodeTeam(blob).then(function (d) { return (d && d.units) ? d.units : []; });
  }

  // the template a fight needs (builder.js sgTemplateFor)
  function templateFor(mode, boss) {
    if (mode === 'base') return 'base';
    if (mode === 'dps') return 'boss_' + (((DSB.savegen || {}).dummy || {}).boss || 'golem');
    return 'boss_' + (boss || 'golem');
  }

  return { panel: panel, generate: generate, decode: decode, templateFor: templateFor,
    templateUrl: SG.templateUrl, tag: SG.TAG };
})();
