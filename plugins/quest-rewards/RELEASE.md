# Quest Rewards 1.0.0: release texts

## GitHub release

- Tag: `quest-rewards-v1.0.0`
- Title: `Quest Rewards 1.0.0`
- Assets: `quest-rewards-1.0.0.zip` (plugin), `QuestRewardsMod-1.0.0.zip` (mod)

Body:

```markdown
**Quest Rewards comes in two parts, and you need both:** a D2RLoader plugin (`quest-rewards-1.0.0.zip`) and a D2RMM mod (`QuestRewardsMod-1.0.0.zip`). The mod adds the flags to the character select screen; the plugin reads your saves and fills them in.

First release of **Quest Rewards**: see at a glance which characters still have a quest reward waiting.

### Features
- **A flag on the character button.** On the character select screen, a blue flag next to the hardcore/ladder icons marks every character with a reward still unclaimed.
- **Details in the tooltip.** Hover the flag to see which reward and in which difficulty:
  - Charsi (imbue): Tools of the Trade, Act I
  - Larzuk (socket): Siege on Harrogath, Act V
  - Anya (personalize): Betrayal of Harrogath, Act V
- **Per-quest settings** (`charsi`, `larzuk`, `anya`): turn off the rewards you don't care about.
- **Keeps D2RLoader's character list**, and the flag sits under the gold selection frame like the other icons.
- **Read only.** Your save files are read, never written. They are read again each time you leave a game.

### Requirements
- D2RLoader 1.3.1 or later, and D2RMM.
- Offline characters (online characters have no save file, so they never get a flag).

### Install
1. Load `quest-rewards-1.0.0.zip` in D2RMM, or copy `d2rl-quest-rewards.dll` to `<game>/d2rloader/plugins/`.
2. Extract `QuestRewardsMod-1.0.0.zip` into D2RMM's `mods` folder (it holds a `Quest Rewards` folder), enable it and apply.

Settings are in `d2rloader/config/quest-rewards.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Known limits
- Up to 10 characters get a flag. With more than 9, scrolling the list moves the characters but not the flags.
- The mod contains D2RLoader 1.3.1's character select layout. A D2RLoader update that changes that screen needs a mod update.

### Log
`d2rloader/logs/quest-rewards.log` lists every character with its row and raw quest flags each time the tooltips are written.
```

## Nexus Mods

Name: `Quest Rewards (D2RLoader plugin + D2RMM mod)`

Summary (short description):

> Plugin + mod: a flag on the character select screen shows which characters still have Charsi's imbue, Larzuk's socket or Anya's personalization waiting.

Description (BBCode):

```bbcode
[size=5][b]Quest Rewards[/b][/size]
[b]Two parts, both needed:[/b] a D2RLoader plugin ([i]quest-rewards-1.0.0.zip[/i]) and a D2RMM mod ([i]QuestRewardsMod-1.0.0.zip[/i]). The mod adds the flags to the character select screen; the plugin reads your saves and fills them in.

See at a glance which characters still have a quest reward waiting.

[size=4][b]Features[/b][/size]
[list]
[*][b]A flag on the character button.[/b] On the character select screen, a blue flag next to the hardcore/ladder icons marks every character with a reward still unclaimed.
[*][b]Details in the tooltip:[/b] which reward and in which difficulty. Charsi (imbue, Tools of the Trade), Larzuk (socket, Siege on Harrogath), Anya (personalize, Betrayal of Harrogath).
[*][b]Per-quest settings:[/b] turn off the rewards you don't care about.
[*][b]Keeps D2RLoader's character list[/b], and the flag sits under the gold selection frame like the other icons.
[*][b]Read only:[/b] save files are read, never written.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later, and D2RMM
[*]Offline characters (online characters have no save file)
[/list]

[size=4][b]Installation[/b][/size]
[list=1]
[*]Load [i]quest-rewards-1.0.0.zip[/i] in D2RMM, or copy [i]d2rl-quest-rewards.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
[*]Extract [i]QuestRewardsMod-1.0.0.zip[/i] into D2RMM's [i]mods[/i] folder, enable [i]Quest Rewards[/i] and apply.
[/list]
Settings are in [i]d2rloader/config/quest-rewards.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]charsi[/b], [b]larzuk[/b], [b]anya[/b]: true (default) = that reward raises the flag and shows in the tooltip; false = ignored
[*][b]log_ui[/b]: true writes the menus' UI messages to a log (troubleshooting)
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Known limits[/b][/size]
[list]
[*]Up to 10 characters get a flag; with more than 9, scrolling the list moves the characters but not the flags.
[*]The mod contains D2RLoader 1.3.1's character select layout; a D2RLoader update that changes that screen needs a mod update.
[/list]

[size=4][b]Log[/b][/size]
[i]d2rloader/logs/quest-rewards.log[/i] lists every character and its quest flags each time the tooltips are written.
```
