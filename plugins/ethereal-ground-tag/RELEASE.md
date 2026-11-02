# Ethereal Ground Tag 1.0.4: release texts

## Patch notes

```
**Ethereal Ground Tag 1.0.4**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
* The zip holds the DLL alone, named ethereal-ground-tag.zip. The default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads game memory through ReadProcessMemory, which made an antivirus flag the DLL.
* The plugin author now shows as wild-healer.

**Removed**

* versions setting: the plugin already checks the game code before it hooks and stays off when it doesn't match.
```

## GitHub release

- Tag: `ethereal-ground-tag-v1.0.4`
- Title: `Ethereal Ground Tag 1.0.4`
- Asset: `ethereal-ground-tag.zip`

Body:

```markdown
**Ethereal Ground Tag** 1.0.4: a tag such as `[Eth]` on the names of ethereal items on the ground.

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
- The zip holds the DLL alone, named `ethereal-ground-tag.zip`. The default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads game memory through ReadProcessMemory, which made an antivirus flag the DLL.
- The plugin author now shows as wild-healer.

### Removed
- `versions` setting: the plugin already checks the game code before it hooks and stays off when it doesn't match.

### Features
- Tags ethereal items on ground labels, with Alt or always shown, and on the label of the targeted item with a controller or the mouse.
- Tag text, before or after the name, brackets, parentheses or none, and the colors of the tag and brackets.
- Works with Chronicle Ground Flag, UnHoarder and Controller QOL loaded, in any order.
- `eth-tag` console command: whether the plugin is active and how many labels it tagged.

### Requirements
- D2RLoader 1.3.1 or later.
- D2R 3.3 (Steam and Battle.net). The plugin checks the game code when it loads; when it doesn't match it stays inactive and says so in its log.

### Install
Load `ethereal-ground-tag.zip` in D2RMM, or copy `d2rl-ethereal-ground-tag.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/ethereal-ground-tag.toml`, written on first launch, or in I Hate TOMLs. Existing settings are kept.
```

## D2RLoader Hub

Name: `Ethereal Ground Tag`

Summary:

> Adds a tag such as [Eth] to the names of ethereal items on the ground.

Description: the GitHub release body above.
