// Every mercenary of an act draws its name from the strings NameFirst..NameLast
// of its hireling.txt rows (a range of string ids). A hired mercenary keeps the
// position of its name in that range, so instead of narrowing the range, every
// string in it is given the chosen name's text in all languages. New hires and
// the mercenary you already have then both show the chosen name, and saves are
// left untouched.

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
  const choice = config[`act${act}`];
  if (!choice) return; // Random (vanilla)
  const chosen = byKey[String(choice).toLowerCase()];
  const range = ranges[act];
  if (!chosen || !range) {
    console.warn(`Merc Names: act ${act}: name ${choice} or its range not found`);
    return;
  }
  // Copy the language columns first: the loop below overwrites the entry
  // itself when it falls inside the range.
  const text = {};
  Object.keys(chosen).forEach((column) => {
    if (column !== 'id' && column !== 'Key') text[column] = chosen[column];
  });
  strings.forEach((entry) => {
    if (entry.id >= range.from && entry.id <= range.to) {
      Object.assign(entry, text);
      changed = true;
    }
  });
});

if (changed) {
  D2RMM.writeJson(stringsPath, strings);
}
