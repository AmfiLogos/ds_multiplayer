// The wiki builder's OWN save import (builder.js dsbExtractSave + dsbSaveUnitToBuild), run in node on a save file:
// one line per clan unit: {"guid":..., "build":{...}}. The C reader (mu_save.c) is checked against it (save_test ref).
//   node builder_import.js SAVE.json
const path = require('path'), fs = require('fs');
const { load } = require(path.join(__dirname, '..', 'node', 'builder_ref.js'));
const c = load(), save = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
for (const u of c.dsbExtractSave(save, null).units) process.stdout.write(JSON.stringify({ guid: u.guid, build: c.dsbSaveUnitToBuild(u) }) + '\n');
