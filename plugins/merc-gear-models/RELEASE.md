# Merc Gear Models 1.0.0: release texts

## Patch notes

```
**Merc Gear Models 1.0.0**

**New**

* Mercs wear the real HD models of their helm and weapon: base items, uniques and set items.
* Mercs' body armor follows their armor: light and medium looks, and a class's heavy armor for heavy armor.
* Weapon colors such as Enchant's red show on the merc's weapon.
* The Iron Wolf's cap is hidden while he wears a helm.
* Settings table: weapon, helm, armor and enchant switches for each merc.
```

## GitHub release

- Tag: `merc-gear-models-v1.0.0`
- Title: `Merc Gear Models 1.0.0`
- Asset: `merc-gear-models.zip`

Body:

```markdown
**Merc Gear Models** 1.0.0

### New
- Mercs wear the real HD models of their helm and weapon: base items, uniques and set items.
- Body armor follows their armor: light and medium looks, and a class's heavy armor for heavy armor.
- Weapon colors such as Enchant's red show on the merc's weapon.
- The Iron Wolf's cap is hidden while he wears a helm.
- Weapon, helm, armor and enchant switches for each merc.

### Install
Load `merc-gear-models.zip` in D2RMM, or copy `d2rl-merc-gear-models.dll` to `<game>/d2rloader/plugins/`.
```

## D2RLoader Hub

Name: `Merc Gear Models`

Summary:

> Your mercenary wears what you give it: real helm and weapon models, armor looks and weapon colors.

Description:

```markdown
**Merc Gear Models**: your merc finally looks like its gear.

### Features
- **Real item models.** Helms and weapons are drawn with the same HD models your character uses: base items, uniques and set items.
- **Armor looks.** The body follows its armor: light and medium looks, and heavy armor borrows a class's heavy armor (Amazon for the Rogue, Barbarian for the others).
- **Weapon colors.** Enchant's red and other item colors show on the merc's weapon.
- **No double hats.** The Iron Wolf's own cap is hidden while he wears a helm.
- **Per merc switches.** Weapon, helm, armor and enchant can be turned off for each merc.
- **Only visual.** Nothing about the merc's items or stats changes.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. On another build it stays inactive and says so in its log.

### Install
Load `merc-gear-models.zip` in D2RMM, or copy `d2rl-merc-gear-models.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/merc-gear-models.toml`.

### Settings
- `enabled`: false gives every merc its normal look back.
- For each merc (`rogue`, `desert`, `iron_wolf`, `barbarian`): `weapon`, `helm`, `armor` and `enchant` (item colors on the weapon, needs weapon), all on by default.
```
