// A legal team link for the C tests: builder_ref.js gen builds, run through the builder's own validate(), named,
// encoded by the builder (encodeTeam). Prints the ?team= link.  node make_team.js [UNITS=3] [SEED=5]
const path = require('path'), { execFileSync } = require('child_process');
const { load } = require(path.join(__dirname, 'builder_ref.js'));
const c = load(), K = +(process.argv[2] || 3), SEED = +(process.argv[3] || 5);
const ref = path.join(__dirname, 'builder_ref.js');
const gen = execFileSync('node', [ref, 'gen', '60', String(SEED)]).toString();
const val = execFileSync('node', [ref, 'validate'], { input: gen }).toString().trim().split('\n').map(l => JSON.parse(l));
// units with a race the game can build and a few learned skills
const ok = val.filter(b => ['AFFECTER_Human', 'AFFECTER_Elf', 'AFFECTER_LizardMan', 'AFFECTER_Lycan'].indexOf(b.race) >= 0 && (b.learned || []).length >= 2);
const team = ok.slice(0, K).map((b, i) => Object.assign(b, { name: 'Test ' + (i + 1) }));
c.encodeTeam(team).then(v1 => { process.stdout.write('https://dungeonsettlers.wiki/builder?team=' + v1); process.exit(0); });
