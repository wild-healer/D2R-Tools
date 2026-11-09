// Every mercenary of an act draws its name from the strings NameFirst..NameLast
// of its hireling.txt rows (a range of string ids). A hired mercenary keeps the
// position of its name in that range, so the range is kept and its strings are
// rewritten instead: the slots are shared out between the ticked names (text in
// all languages). New hires and the mercenary you already have then all show
// a ticked name, and saves are left untouched.

const stringsPath = 'local\\lng\\strings\\mercenaries.json';
const strings = D2RMM.readJson(stringsPath);
const byKey = {};
strings.forEach((entry) => {
  byKey[String(entry.Key).toLowerCase()] = entry;
});

// One name range per act, as the game's hireling.txt has it.
const ranges = {};
D2RMM.readTsv('global\\excel\\hireling.txt').rows.forEach((row) => {
  const act = row.Act;
  const first = byKey[String(row.NameFirst || '').toLowerCase()];
  const last = byKey[String(row.NameLast || '').toLowerCase()];
  if (!act || !first || !last) return;
  const range = ranges[act] || { from: first.id, to: last.id };
  range.from = Math.min(range.from, first.id, last.id);
  range.to = Math.max(range.to, first.id, last.id);
  ranges[act] = range;
});

let changed = false;
['1', '2', '3', '5'].forEach((act) => {
  const range = ranges[act];
  if (!range) {
    console.warn(`Merc Names: act ${act}: no name range in hireling.txt`);
    return;
  }
  const slots = strings
    .filter((entry) => entry.id >= range.from && entry.id <= range.to)
    .sort((a, b) => a.id - b.id);
  // Read every name's text before any slot is overwritten.
  const ticked = slots
    .filter((entry) => config[`act${act}_${entry.Key}`] !== false)
    .map((entry) => {
      const text = {};
      Object.keys(entry).forEach((column) => {
        if (column !== 'id' && column !== 'Key') text[column] = entry[column];
      });
      return text;
    });
  // All ticked or none ticked: the game's names.
  if (ticked.length === 0 || ticked.length === slots.length) return;
  // The game picks a slot of the range at random; slot i shows the i-th
  // ticked name, cycling, so each ticked name gets an equal share of slots
  // (one more or less when they don't divide evenly).
  slots.forEach((entry, i) => {
    Object.assign(entry, ticked[i % ticked.length]);
  });
  changed = true;
});

if (changed) {
  D2RMM.writeJson(stringsPath, strings);
}
