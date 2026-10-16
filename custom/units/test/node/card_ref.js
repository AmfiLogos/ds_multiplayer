// The Compact Plus card of a build, computed by the wiki builder's OWN functions (builder.js unitCompactCard with
// plus = true, branchIconRow, compactInscHtml, compactEqHtml) plus the skill bar (quickFor) and the passive skills
// (unitSkillSets), as data instead of HTML. Reference for custom/units/mu_card.c (test/builder_vs_c.js).
const path = require('path');
const { load } = require(path.join(__dirname, 'builder_ref.js'));
function make() {
  const c = load(), D = c.D;
  const ws = c.__src.match(/\nvar WSUBLABEL = (\{[\s\S]*?\});/);
  c.__lift(['displayInscriptions', 'acquiredTraits', 'investedTrees', 'bankedStacks', 'indAvail', 'inscEntry', 'firstPortrait', 'equippedWeaponLabel'],
    ws ? 'var WSUBLABEL = ' + ws[1] + ';' : '');
  function card(b) {
    // branchIconRow: background trees, then other learned trees that are not inscription trees; ACTIVE = main kind
    const invested = {}; c.investedTrees(b).forEach(t => { invested[t] = 1; });
    const inscTree = {}; (b.insctrees || []).forEach(t => { inscTree[t] = 1; });
    const active = [], passive = [], seen = {};
    const push = t => { if (!D.trees[t] || seen[t]) return; seen[t] = 1; (D.trees[t].kind === 'main' ? active : passive).push([t, invested[t] ? 1 : 0]); };
    c.bgWeaponTrees(b).forEach(push); c.bgSubTrees(b).forEach(push);
    c.investedTrees(b).forEach(t => { if (!inscTree[t]) push(t); });
    const stacks = c.bankedStacks(b).map(sp => [sp.k, sp.lv, sp.max, sp.learned ? 1 : 0]);
    const ss = c.unitSkillSets(b);
    const pas = ss.passives.map(k => { const pg = D.passiveGrants[k]; const thr = pg && pg.thr && pg.thr.length;
      return [k, pg && pg.cond && !thr ? 1 : 0, thr ? c.stackLvFor(b, k) : -1]; });
    return {
      name: b.name || '', lvl: b.lvl || 1, race: b.race || null, bg: (b.bg && D.bg[b.bg]) ? b.bg : null,
      weapon: c.equippedWeaponLabel(b) || null,
      portrait: b.portrait || null, avatar: b.portrait || c.firstPortrait(b.race) || null,
      active, passive, stacks,
      insc: c.displayInscriptions(b).map(e => e.k),
      souls: (b.souls || []).filter(k => D.souls && D.souls[k]),
      acq: c.acquiredTraits(b),
      eq: (D.slots || []).map(s => [s, b.eq && b.eq[s]]).filter(x => x[1] && D.items[x[1]]),
      bar: c.quickFor(b),
      passives: pas,
    };
  }
  return { c, card };
}
module.exports = { make };
