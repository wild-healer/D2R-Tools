# Never Enough Storage: specification

Status: draft spec, no code yet. Feasibility is deliberately left for later: this document describes the most complete storage plugin we would want, and a later pass will mark what D2RLoader and the game allow today.

Short name: **NES**. Version 0.0.1 during development. Plugin id `never-enough-storage`, DLL `d2rl-never-enough-storage.dll`, zip `never-enough-storage.zip`, console command `nes`.

## 1. Goal

Unlimited item storage for D2R, kept in files outside the character and shared stash saves (the way Grail On! keeps its grail), with a window inside the game to browse it as a table, a search powerful enough to find "every ethereal elite polearm with 15% IAS or more", and close integration with the shared stash for materials (runes, gems, essences, keys and the rest).

It should fit every way people play: a collector who keeps everything, a trader who prices and sorts, a crafter who needs 40 Pul runes at hand, a hardcore player who never wants to lose an item, a modded player whose items the plugin has never heard of. Almost every behaviour is a setting, and every setting changes live.

### Non-goals

- Not a grail tracker. NES stores any kind of item, grail items included; Grail On! is an independent plugin (section 14).
- No trading with other players, no online services, no cloud sync of its own (a vault folder can sit in a synced folder if the player wants).
- No item creation or editing. Every item that comes out of a vault is an item that went in.

## 2. House rules this plugin follows

- **I Hate TOMLs**: NES has its own settings page inside its window (gear icon), so it joins IHT in "blue" mode (own window): IHT lists it with an Open button that opens NES on its settings page. Every setting **takes effect at once**, with no restart and no game rejoin.
- **Packaging**: the zip holds only the DLL, with no version in either name. The default TOML is embedded in the DLL and written on first launch. Art, icons and translations are embedded too.
- **No ReadProcessMemory** anywhere, and nothing else that scanners distrust (no WIC image decoding, no unpacking to disk).
- **Self-contained**: no companion D2RMM mod and no reliance on other installed mods. Names, pictures and stats come from the running game's own tables and files, so mods carry over.
- **Plugin defaults, not user configs**: changes in behaviour ship as new defaults; existing TOMLs are never rewritten by an update.
- **Versioning**: 0.0.1 during development, minor and patch raised freely until the first release, never the major. After release, dev builds are `1.x.y-dev-z` (z goes up with every build). The release number comes from Théo.
- **No item loss** is the first requirement over every other one (section 13).

## 3. Concepts

| Term | Meaning |
| --- | --- |
| **Vault** | One storage file holding any number of items. A player can have several vaults (section 4.2). |
| **Record** | One stored item, kept as the exact bytes the game's own save writer produces (socketed children included), plus an index of decoded fields for search and display. |
| **Stack record** | A count of identical simple items (runes, gems, essences, keys, potions...) kept as one row: base code + count, no bytes needed beyond one template. |
| **Shelf** | A user-made folder inside a vault (like a stash tab with no size limit). An item lives on exactly one shelf. Shelves nest. |
| **Smart shelf** | A saved search shown as a shelf. Its contents are whatever matches, live. |
| **Tag** | A free label on a record (`trade`, `merc`, `keep`, `mule-ama`...). Records carry any number of tags. |
| **Staging area** | The few custom page cells items pass through on the way in and out, as in Grail On!. Taken from Player Page Host when it is installed. |
| **Materials** | The stackable crafting items: runes, gems, essences, keys, organs, tokens, worldstone shards, statues and any misc type the player adds. |

## 4. Storage

### 4.1 Files

- Location: `Saved Games\Diablo II Resurrected\mods\<mod>\nes\` for the active mod (vanilla uses the base save folder), so a D2RMM reinstall never touches it.
- Vaults are always split by game version, because items of one can't exist in another: `classic`, `expansion` (Lord of Destruction) and `rotw` (Reign of the Warlock). Default vaults per version: `softcore-<version>.nes` and `hardcore-<version>.nes` (for example `hardcore-rotw.nes`). The game version is read from the character when it loads. There is no ladder or online split: D2RLoader runs neither.
- File layout: chunked binary, `NES1` magic, u32 version, then tagged chunks so new versions can add chunks that old ones skip:
  - `META` vault name, created time, game build, mod name, settings snapshot hash.
  - `SHLF` shelves: id, parent, name, icon, colour, sort order, view settings.
  - `RECS` item records: record id (u64, never reused), fingerprint, shelf id, flags (locked, favourite), added time, origin (character name, area, game number if known), note, tags, then the save writer bytes.
  - `STCK` stack records: base code, count, template bytes, shelf id.
  - `TOMB` tombstones: fingerprints taken out (as Grail On!), so a copy left on another character's page is never doubled.
  - `OUT1` taken-out journal: records withdrawn in a game that has not saved yet, with the character that took them; brought back if that game never saved (as Grail On! 1.1.2).
  - `HIST` history log (section 11.3), optional and capped.
  - `INDX` cached decoded fields so the vault opens fast; rebuilt from the bytes when missing or stale.
- Writes go to `.tmp`, are flushed, then replace the file with `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- A file that ends early is read as far as it goes and a copy is kept as `.damaged`. A file NES can't read at all switches the vault off for that session; it is never overwritten.
- Rolling backups: one per day and one per game session, the last N kept (`backups_daily`, `backups_sessions`), in `nes\backups\`.

### 4.2 Vault scopes

Each vault has a scope deciding which characters see it:

- `global` (default): all characters of that mode (SC or HC) and game version in that mod.
- `per-character`: a private vault per character name.
- `group`: a named vault shared by a chosen list of characters (for example "my sorceresses").
- `any-mode`: one vault shared by SC and HC of the same game version. Off by default, and HC items going to SC and back can be blocked by a setting (`hardcore_to_softcore`, default `false`).

A vault never mixes game versions, whatever its scope. A character can have several vaults open; the window has a vault picker. Items move between vaults with drag and drop or the Move action.

### 4.3 Identity

- Unique, set, rare, magic, crafted and runeword items: the 32-bit fingerprint read from the save bytes, as in Grail On! (the bits before the first ilvl + quality match).
- Two items sharing a fingerprint (item editor copies) are kept apart by code + ethereal + quality + quality row, like Grail On!'s `SameItem`; a real duplicate is refused and goes back to the inventory.
- Simple items have no fingerprint: they live in stack records, keyed by base code.

### 4.4 Capacity

No limit beyond disk space. The design target is 100,000 records per vault opening in under a second and searching in under 50 ms.

### 4.5 Import and export

- **Import** from: a shared stash `.d2i` (all pages or chosen ones), a character `.d2s` (inventory, stash, cube, mercenary, equipment as chosen), Grail On!'s `grail-*.dat` (copy, never move), another `.nes` file. Imports work from files of characters not currently loaded, so a mule's items can be pulled in without logging the mule. Imported items are removed from their source file only when the player picks "move", and only after a backup of the source.
- **Export**: a shelf, a search result or a whole vault to another `.nes`, to CSV and JSON (decoded fields, for spreadsheets and trade sites), and to a plain text list formatted for trade posts (name, key stats, sockets, eth).
- **Item share code**: a single item's decoded stats as a short text the player can paste in chat or Discord. It never carries the item's bytes.

## 5. Putting items in

Every way below can be switched on or off and most have their own key.

- **Drop**: drop the cursor item on the NES window (anywhere on it, or on a shelf in the shelf list to choose where it lands).
- **Ctrl+click** an item in the inventory, cube, personal or shared stash while the window is open: it goes to the current shelf.
- **Shift+Ctrl+click**: every item of the same kind (same base, or same unique/set row) in that container goes in.
- **Deposit all** button with a filter: everything in the inventory except locked cells, charms and the cube, or everything matching a chosen search. Respects the Inventory locked cells the player marks in NES (a grid of cells that are never deposited).
- **Deposit a stash page**: a button on each shared and personal stash page sends that page's items in.
- **Auto-deposit on pickup** (off by default): picked up items that match a rule go straight to the vault instead of the inventory, like Cube Overflow does for the cube. An option sends them only when the inventory has no room.
- **Auto-deposit on leaving the game**: items in chosen containers that match rules go in when the game ends.
- **Rules** (section 10) pick the shelf and tags for every item that comes in by any route.
- Items that can't go in (quest items, the cube itself, an item a rule forbids, a duplicate) go back where they were with a short on-screen message saying why.

## 6. Taking items out

- **Click** a record: it comes onto the cursor.
- **Ctrl+click**: to the inventory; if full, to the personal stash, then the shared stash (order is a setting).
- **Shift+click**: to the open stash page; with the cube open, into the cube.
- **Withdraw N**: on a stack record, a count box (and quick buttons 1, 5, 10, stack, all). Over-stack counts make several stacks.
- **Withdraw many**: a selection of records goes out in one action to a chosen container; what doesn't fit stays in the vault.
- **Withdraw for a recipe**: pick a cube recipe or a runeword and NES takes out the ingredients it has (section 9.4).
- **Withdraw to a mule** (offline): write chosen records into another character's `.d2s` stash while that character is not loaded, after a backup.
- Items taken out are journalled until the game saves (section 4.1, `OUT1`).

## 7. The window

Opened with a button on the inventory (its position is a setting), a Controls-menu keybind, the console, or IHT's Open button.

### 7.1 Layout

- Left: vault picker, shelf tree, smart shelves, tags list with counts.
- Centre: the item table.
- Right: detail pane with the selected item's full tooltip, its notes and tags, history, and buttons.
- Top: search bar with filter chips and result count.
- Bottom: status bar with the selection count, total value (section 11.3) and staging area state.
- Every pane can be hidden, resized and swapped left/right. Compact mode fits the window beside the inventory. A full-screen mode uses the whole screen with the game paused behind it in single player.
- Window position, size and scale are settings, saved per resolution.

### 7.2 Table view

- One row per record (stacks show as one row with a count).
- Columns chosen from every decoded field (section 8.2) plus computed ones: picture, name, base, type, quality, tier (normal/exceptional/elite), ilvl, required level/str/dex, class, sockets (filled/total), ethereal, runeword, roll percent, perfect, value, shelf, tags, note, added date, origin character, and any stat as its own column (for example "FCR", "All Res", "MF").
- Click a header to sort; Shift+click for multi-column sort. Drag headers to reorder, drag edges to resize. Right-click a header to hide or add columns.
- Column sets are saved per shelf or as named presets ("Trading", "Runes", "Caster gear").
- Group by any column (type, quality, tag, shelf...) with collapsible groups and per-group totals.
- Cell colouring: quality colours, stat values shaded from worst to best roll, conditional colour rules (`fcr >= 20` in green).
- Hovering a row shows the game's own tooltip; with Shift, the comparison against what the character wears.
- Virtual scrolling so 100k rows scroll smoothly; mouse wheel, drag on the scroll bar, page keys, Home/End.

### 7.3 Selection and actions

- Click, Ctrl+click and Shift+click select rows (with the modifier keys used for withdraw only when the setting says so; otherwise selection uses its own modifier).
- Actions on a selection: withdraw, move to shelf, move to vault, tag/untag, lock/unlock, favourite, add note, export, copy share code, delete (only with a confirm and only for items a rule marked junk; section 13).
- Undo for the last 50 vault-side actions (moves, tags, notes, shelf changes). Withdraw and deposit are undone by doing the reverse.

## 8. Search

### 8.1 Search bar

- Free text matches names, base names, affix names, stat lines, tags and notes, accent and case insensitive, in the game's current language and in English.
- Typing shows suggestions: field names, stat names, item names, tags, shelves, previous searches.
- Filter chips under the bar for the common filters (quality, type, tier, eth, sockets, class) toggle with a click and edit the query.
- Results update as you type. Matches can either filter the view or only highlight (dimming the rest, as Grail On! does), a setting and a toggle.
- History of the last 50 searches; pinned searches become smart shelves.
- **Global search**: the same query can also run over the shared stash pages, the personal stash, the cube, the inventory, and (offline) every character `.d2s` and `.d2i` in the save folder, so "where is my Shako" has one answer. Results outside the vault show where they are and can be jumped to (stash page switch as Stash Tab Dropdown does).

### 8.2 Query language

```
shako                          free text
name:"harlequin crest"         exact field
type:polearm tier:elite eth    type, tier, flag
quality:unique|set             either
ias>=15 ed>=200                stat comparisons
res:all>=30 fcr:20..40         ranges
sockets:4 sockets.empty>0      sub-fields
runeword:insight base:thresher
rolls>=90%  perfect  !anti     roll quality
class:sorceress  skill:"fire mastery">=1  skilltab:fire
req.level<=60  ilvl>=85
tag:trade  shelf:"Caster gear"  added>2026-10-01  from:Blizz
value>=5  (fcr>=20 or fhr>=24) and not eth
/^tal rasha/i                  regex
~enigma                        fuzzy
```

- Fields: everything the decoder reads (section 9.1) plus computed fields (roll percent, perfect, anti, value, total resists, total attributes, life+mana, max damage, defence, can-hold-runeword, wearable-by-current-character).
- Stats are named by their ItemStatCost row, by short aliases (`fcr`, `fhr`, `frw`, `ias`, `ed`, `mf`, `gf`, `res:all`, `ll`, `ml`, `cb`, `ds`, `ow`...) and by the game's own stat text in the current language. Mod stats are found by their row name.
- Operators: `= != > >= < <= ..`, `and or not` (also `& | !`), parentheses, `|` inside a value for alternatives.
- Sort and grouping can be in the query too: `sort:fcr desc group:type`.
- Saved searches have names and icons and can be shared as text.

### 8.3 Presets shipped by default

"Perfect items", "Near perfect (95%+)", "Ethereal bases for runewords", "Runeword bases by socket count", "Charms by stat", "Skillers (grand charms with +1 skill tab)", "Torches and Annis by roll", "Jewels 15 IAS / 40 ED", "Rares with 2+ open sockets", "Crafted casters", "Everything added this week". All editable and removable.

## 9. Item knowledge

### 9.1 Decoding

- Every record's bytes are decoded into the full stat list once (D2RCore wide format: 16-bit stat ids, 32-bit params; see the Grail On! byte notes), with names and lines from the running game's tables.
- Where the plugin can't decode an item with confidence (odd headers, personalized items, mod formats), it still stores and returns it; only its search fields are limited, and the item is marked "not fully read" in the detail pane.

### 9.2 Roll quality

- For each variable stat, its place between the minimum and maximum its source allows (unique/set row, runeword, affix row, base defence range).
- Roll percent per item (weighted average, weights a setting), perfect (every variable roll at max, or nothing varies), anti-perfect (as Perfect Frame), and per stat colours.
- Rares and magic items: affix tiers and their rolls, with the best possible affixes for that base and ilvl shown on hover.

### 9.3 Runewords and bases

- For a base: which runewords it can hold (socket count, type, mod runewords), and which of them the vault has the runes for.
- For a runeword: every base in the vault that can hold it, sorted by best (eth, defence, max sockets).

### 9.4 Recipes and crafting

- Reads cubemain.txt from the game (so mod recipes are there).
- "What can I make": recipes whose ingredients the vault and the character together hold, with counts ("Ber: 3 from 6 Lem? No: 2 Ber possible from Ist chain").
- Rune and gem upgrade planner: the cheapest chain to reach a wanted rune with what the vault has.
- Withdraw for a recipe puts the ingredients into the open cube in one action.
- Optional **virtual cubing** for rune and gem upgrades: performed inside the vault on stack records, only for recipes with a fixed output (no random rolls), and only when the setting is on (off by default, since it skips the game).

### 9.5 Wearability and upgrades

- Whether the current character (and its mercenary) can wear an item now, at which level, and what it is missing (str, dex, level, class).
- "Upgrade for": compares to what is equipped by chosen stat weights (presets per build, editable) and marks items that beat the current gear.

## 10. Rules and automation

A rule is a search query plus actions, checked in order, first match or all matches (a setting per rule):

- Triggers: item enters the vault, item picked up, leaving the game, opening the stash, opening NES, manual "Run rules".
- Actions: put on shelf, add tags, lock, favourite, deposit (on pickup triggers), keep out of the vault, send to the shared stash materials area, mark junk, colour, add a note.
- Rules can be enabled per vault and per character.
- A **dry run** shows what a rule would do on the current vault and inventory before it is saved.
- Shipped examples (off by default): "Runes and gems to the materials shelf", "Ethereal elite bases to Bases", "Charms to Charms by type", "Everything from mules tagged mule".

## 11. Materials and stash integration

### 11.1 Materials catalogue

- Built from the game's tables: runes, gems (by grade), essences, Token of Absolution, keys, organs, worldstone shards and the other RotW materials, plus any misc item type the player adds in settings.
- Shown as its own Materials table: one row per material with vault count, stash count, inventory count and total, grouped by kind; runes in their game order with the upgrade chain visible.

### 11.2 Vanilla materials stash, hard-wired

The game's own materials stash is wired straight to the vault, so the two act as one store:

- **Exact count**: each material's vanilla slot shows the true total it holds (slot plus vault), in the slot's own count text and its tooltip.
- **Slots always filled**: NES keeps every vanilla slot topped up from the vault (to its full stack, or to a lower keep level the player sets), so taking a material out, Ctrl+click, right-click, the cube and every keybind keep working exactly as in vanilla. As soon as a slot drops, it is refilled from the vault; when the vault has no more, the slot simply empties as it would without NES.
- **Overflow**: materials put into a slot that is already full (by hand, by Ctrl+click, by the game's own auto-deposit or by another plugin) go into the vault instead of bouncing, so the materials stash never says "full".
- **Keep levels** (optional): a number per material or per kind ("all gems: 3", "keys: 3") for how much stays in the vanilla slot; the rest lives in the vault. Defaults to a full slot.
- **Removing NES**: the vanilla slots hold real items, so the stash works without the plugin; what is in the vault stays in the vault file until NES is back.
- **Cube from vault**: with the cube open, a count clicked in the Materials table moves that many into the cube.
- **Stash search**: the NES search bar can also highlight matching items on the open stash page and list the pages that hold matches. Works with Expanded Stash, extra shared tabs (D2RLoader `add_shared_tabs`) and Stash Tab Dropdown page switching.

### 11.3 Value and history

- **Value**: each record and material can carry a value in a unit the player chooses (runes, "Ist", "HR", or plain numbers). A shipped default price list for runes and common items, editable, importable from CSV. Totals per shelf, per search, per vault.
- **History**: an optional log of everything in and out (time, character, item, route), searchable (`history:in date:today`), exportable. Capped by size and age (`history_days`, default 90).

### 11.4 Dashboard

An overview page: items per quality and type, materials totals, vault value, items added per day, top tags, largest shelves, "items you have not touched in 90 days", and a warning list (records not fully read, duplicates refused, journal entries pending).

## 12. Multiplayer

- Single player and games the player hosts: everything.
- TCP/IP games joined: items change only on the host. Through Player Page Host's host protocol (the Grail On! route), the joiner's NES asks the host's Player Page Host to make and take items; without it, the joiner can browse and search the vault, plan, and export, but not deposit or withdraw.

## 13. Safety and integrity

- An item is removed from the game only after its record is written and flushed. An item is made from a record before the record is marked out, and the record goes only after the made item is confirmed in a container.
- Taken-out journal and crash restore as Grail On! 1.1.2: if a game closes without saving, records withdrawn in it come back.
- Tombstones: the staging cells are saved in each character's save, so a character can leave the game with a vault item still sitting there. If another character then takes that item out of the vault, the first character's save still holds its old copy. The tombstone ("this fingerprint was taken out") lets NES recognise that copy when the first character loads and remove it, instead of storing it again as a new item, which would duplicate it.
- Duplicates by identity are refused and the item stays with the player.
- **Locked** records can't be withdrawn by bulk actions or rules, only by a direct click.
- Deleting a record needs the item to be marked junk and a confirm that names the item; deletion goes to a recycle shelf kept 7 days (setting) before it is really gone. Hardcore vaults can switch deletion off completely.
- `nes verify` checks every record (bytes decode, fingerprints unique, shelves valid, journal clean) and writes a report.
- `nes restore <backup>` puts a backup back after making a backup of the current file.
- Removing the plugin leaves no items on the staging cells, so characters load without it; the vault files stay for when it comes back.
- Hooks are checked byte-for-byte at load; on an unknown build the plugin switches the affected parts off and says so, and the vault stays readable (browse, search, export) even when nothing can be made in game.

## 14. Compatibility

- **Player Page Host**: NES takes a staging area from the host (3x4 by default, a setting); without the host it uses the custom page alone.
- **Grail On!**: independent. NES takes any item, grail items included, and neither plugin depends on the other. When Grail On! runs, NES can show grail status (found, perfect) as columns and filters. Import from the grail file is a copy. An item has one home at a time: if it sits in one plugin's storage, it is not in the other's.
- **Cube Overflow**: when both would take a pickup, the order is a setting (vault first, or cube first).
- **Charm Inventory, Inventory Slots, Stash Tab Dropdown, Visual Clutter, Compare Indicators, Perfect Frame**: no shared hooks; NES uses Perfect Frame's colours when it is present.
- **Loot filter**: an optional NES loot rule hides ground items the vault already has enough of (by count per base, per unique, or per search), with an "only unidentified" switch like Grail On!.
- **Mods**: all lists and names come from the running game. Items from a mod the vault no longer runs stay in the file, shown as "unknown item" with what can be read, and come back when the mod returns.

## 15. Settings

All are in IHT and on the NES settings page, grouped as below, and all take effect at once. File: `Saved Games\Diablo II Resurrected\nes\never-enough-storage.toml` (outside the mod, so reinstalls keep it), seeded from the embedded default.

### General

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin on or off without unloading it. |
| `language` | `game` | `game` or a game language code, as Grail On!. |
| `button_inventory` | `true` | Show the NES button on the inventory. |
| `button_inventory_pos` | built-in | Button position (position type, arrows with Shift/Ctrl steps, Reset). |
| `messages` | `short` | `none`, `short` or `full` on-screen messages for refused items and bulk actions. |

### Storage

| Key | Default | Meaning |
| --- | --- | --- |
| `default_scope` | `global` | Scope of new vaults: `global`, `per-character`, `group`, `any-mode`. |
| `hardcore_to_softcore` | `false` | Allow hardcore items into a vault softcore characters see. |
| `staging_size` | `[3, 4]` | Staging cells taken from the custom page or the host. |
| `backups_daily`, `backups_sessions` | `14`, `10` | Backups kept. |
| `history`, `history_days` | `true`, `90` | Keep the in/out history, and for how long. |
| `recycle_days` | `7` | Days a deleted record stays on the recycle shelf. `0` deletes at once. |
| `allow_delete` | `true` (SC) / `false` (HC) | Whether records can be deleted at all. |

### Window

| Key | Default | Meaning |
| --- | --- | --- |
| `layout` | `normal` | `normal`, `compact` or `fullscreen`. |
| `window_pos`, `window_size`, `scale` | built-in | Window placement, per resolution. |
| `panes` | `left,right` | Which side panes show; `swap_panes` swaps them. |
| `pause_in_fullscreen` | `true` | Single player pauses while the full-screen window is open. |
| `scroll_animations` | `true` | Smooth scrolling. |
| `perfect_marks` | `true` | Gold mark on perfect rows, red on anti-perfect. |
| `compare` | `shift` | `none`, `shift` or `always` comparison tooltip on hover. |

### Table

| Key | Default | Meaning |
| --- | --- | --- |
| `columns` | name, type, quality, ilvl, sockets, rolls, tags | Default column set. |
| `column_presets` | shipped presets | Named column sets. |
| `row_height`, `font_size` | built-in | Density. |
| `group_by` | none | Default grouping. |
| `shade_rolls` | `true` | Shade stat cells from worst to best roll. |
| `colour_rules` | empty | Conditional colours, one query + colour per line. |

### Search

| Key | Default | Meaning |
| --- | --- | --- |
| `search_mode` | `filter` | `filter` hides non-matches, `highlight` dims them. |
| `search_scope` | `vault` | `vault`, `vault+stash`, `vault+character`, `everything` (all saves, offline). |
| `search_languages` | `game,enUS` | Languages free text matches in. |
| `fuzzy` | `true` | Allow `~` fuzzy matches and typo-tolerant suggestions. |
| `saved_searches` | shipped presets | Named queries. |
| `roll_weights` | equal | Weights for roll percent per stat. |

### Input

| Key | Default | Meaning |
| --- | --- | --- |
| `ctrl_click_deposit`, `shift_ctrl_click_same` | `true`, `true` | Deposit shortcuts. |
| `click_withdraw` | `cursor` | What a click on a record does: `cursor`, `inventory`, `select`. |
| `ctrl_click_withdraw_order` | `inventory,personal,shared` | Where Ctrl+click withdraw tries first. |
| `middle_click` | `true` | Middle click an item anywhere to find it in the vault (or its twins). |
| `locked_cells` | none | Inventory cells Deposit all never takes. |

### Automation

| Key | Default | Meaning |
| --- | --- | --- |
| `auto_deposit_pickup` | `off` | `off`, `rules` (matching items) or `overflow` (matching items when the inventory is full). |
| `auto_deposit_leave` | `false` | Run deposit rules when leaving a game. |
| `cube_overflow_order` | `vault_first` | With Cube Overflow: `vault_first` or `cube_first`. |
| `rules` | shipped examples, off | Rule list (query, actions, trigger, enabled). |
| `virtual_cubing` | `false` | Rune and gem upgrades inside the vault. |

### Materials and stash

| Key | Default | Meaning |
| --- | --- | --- |
| `materials_extra` | empty | Extra misc item type codes counted as materials. |
| `materials_link` | `true` | Wire the vanilla materials stash to the vault (exact counts, slots kept filled, overflow). |
| `keep_levels` | full slots | Per material or kind, how many stay in the vanilla slot. |
| `keep_presets` | shipped presets | Named keep level sets. |
| `stash_search_highlight` | `true` | The NES search highlights the open stash page. |

### Value and loot

| Key | Default | Meaning |
| --- | --- | --- |
| `value_unit` | `ist` | Unit for values and totals. |
| `prices` | shipped list | Price list, editable. |
| `loot_enough` | `off` | `off`, or hide ground items the vault has at least N of. |
| `loot_enough_count`, `loot_unid_only` | `1`, `true` | Count needed, and only unidentified ground items. |

### Grail On!

| Key | Default | Meaning |
| --- | --- | --- |
| `grail_columns` | `true` | Show grail status columns and filters when Grail On! runs. |

## 16. Keybinds and console

Controls menu, section Never Enough Storage, unbound by default: open/close, focus search, deposit all, next/previous shelf, withdraw selection.

Console:

- `nes`: open or close.
- `nes find <query>`: run a search and print the hits.
- `nes deposit [query]`.
- `nes import <file> [copy|move]`, `nes export <shelf|query> <csv|json|nes>`.
- `nes verify`, `nes backup`, `nes restore <file>`.
- `nes stats`: counts and totals.

## 17. Performance

- Vault loaded on a background thread; the decoded index (`INDX`) means opening never decodes 100k items again.
- Search runs on the index off the game thread, with results streamed to the view.
- Only on-screen rows build widgets; pictures cached per base and quality, with a cap.
- Only staging cells hold real game items; everything else is data until withdrawn.
- Saves are batched (at most once per second, and always on leaving a game and on exit).

## 18. Localisation

All plugin texts in every game language, using the game's own strings first (item names, stat lines, quality names), with the plugin's own translations for the rest, as Grail On! does. Query field aliases stay English in every language; stat names also match the localised text.

## 19. Files the plugin writes

- `Saved Games\Diablo II Resurrected\nes\never-enough-storage.toml` settings.
- `Saved Games\Diablo II Resurrected\mods\<mod>\nes\*.nes` vaults, `backups\`, `exports\`, `verify-report.txt`.
- `<game>\d2rloader\logs\never-enough-storage.log` log.

## 20. Decisions

- Default vault scope stays `global` (one vault per mode and game version, per mod).
- Virtual cubing and deletion stay in, both off by default.
- NES takes any kind of item. Grail On! is independent.
