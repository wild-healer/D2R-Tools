# Quest Rewards

**Two parts, both needed:** a D2RLoader plugin and
a D2RMM mod, both on the [D2RLoader Hub](https://d2rloader.net/hub/plugins/quest-rewards). The mod adds the flags to the
character select screen; the plugin reads your saves and fills them.

On the character select screen, a blue
flag on a character's button means a quest reward is still waiting. Its tooltip
says which, per difficulty:

| Line | Quest | Reward |
|------|-------|--------|
| Charsi (imbue) | Act I, Tools of the Trade | imbue an item |
| Larzuk (socket) | Act V, Siege on Harrogath | socket an item |
| Anya (personalize) | Act V, Betrayal of Harrogath | personalize an item |

- Install: load the plugin in D2RMM like the other plugins, and
  extract the mod (the `Quest Rewards` folder) into D2RMM's
  `mods` folder and enable it.
- Config (`d2rloader/config/quest-rewards.toml`): `charsi`, `larzuk`, `anya`
  turn each quest on or off (an off quest never raises the flag nor shows in the
  tooltip); `log_ui` writes the menus' UI
  messages to `d2rloader/logs/quest-rewards-ui.log`.
- Log: `d2rloader/logs/quest-rewards.log` lists every character, its row and the
  raw quest flags each time the tooltips are written.

How it works:
- Saves (`Saved Games\Diablo II Resurrected\mods\<mod>\*.d2s`, or the folder
  itself without a mod) are read, never written. Quest data starts 10 bytes after
  `Woo!` (offset 403 in v105 saves), 96 bytes per difficulty, one 16-bit flag word
  per quest: +6 Tools of the Trade, +70 Siege on Harrogath, +76 Betrayal of
  Harrogath. Bit 0 = reward granted, bit 1 = reward pending (D2MOO's
  QFLAG_REWARDGRANTED / QFLAG_REWARDPENDING).
- Plugins can't change a game layout, so the mod adds `QuestFlag0`..`QuestFlag9`
  (ImageWidget + FocusableWidget tooltip `@quest-rewards:Flag<n>`) beside the
  character list, one per visible row. The list's rows are copies of one
  template, so a flag inside it could not be set per character. The mod starts
  from D2RLoader 1.3.1's own character select layout (copied from d2rloader.mpq
  into mod.js), since a mod's layout replaces D2RLoader's. Flags sit in the
  icon slot left of 431/488/545. Being outside the list they draw over its gold
  selection frame, so each row also has `QuestFlagSel<n>` (the flag with the
  frame's top edge painted on) that the plugin shows on the selected row, and
  one tooltip area `QuestFlagTip<n>` over both (a hidden widget on top swallows
  the tooltip below). The list sends no selection message plugins see: the plugin
  polls the `Selection` image's rect every 100 ms (y / 184 = row).
- The plugin registers the `Flag<n>` strings at load and when a game is left
  (the save is written just before), and on `PanelManager:OpenPanel:CharacterSelectPanel`
  / `CharacterSelect:SelectTab` hides the flags of rows with nothing waiting
  (all of them on the Online tab). Rows are most recently played first, the
  game's order.

Limits: 10 flags; with more than 9 characters, scrolling the list moves the rows
but not the flags. Online characters have no save file.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
The flag sprite is the hardcore icon's banner, recolored blue with a gold "!".
