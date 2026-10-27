# Swap Cast 1.1.0: release texts

## Patch notes

```
**Swap Cast 1.1.0**

**New**

* allow_cross_cast setting: true (default) keeps the MS and SM modes. false makes the key switch only between M and S, and skills saved as MS or SM cast with M or S, by where their weapon comes from.
```

## GitHub release

- Tag: `swap-cast-v1.1.0`
- Title: `Swap Cast 1.1.0`
- Asset: `swap-cast-1.1.0.zip`

Body:

```markdown
**Swap Cast** 1.1.0: a setting to turn off the one-hand-from-each-set modes.

### New
- allow_cross_cast setting: true (default) keeps the MS and SM modes. false makes the key switch only between M and S, and skills saved as MS or SM cast with M or S, by where their weapon comes from.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `swap-cast-1.1.0.zip` in D2RMM, or copy `d2rl-swap-cast.dll` to `<game>/d2rloader/plugins/`. An existing config and modes.txt keep working; add `allow_cross_cast = false` to the config to turn the MS and SM modes off.
```

## Nexus Mods

Version: `1.1.0`

Changelog: the patch notes above. The summary and description below are the 1.0.0 texts with the swap skills line updated.

Name: `Swap Cast (D2RLoader plugin)`

Summary (short description):

> Cast each skill with weapon set I, set II or one hand from each, without pressing W. Call to Arms skills stay available.

Description (BBCode):

```bbcode
[size=5][b]Swap Cast[/b][/size]
A D2RLoader plugin that lets each skill cast with any combination of your two weapon sets, without pressing W.

[size=4][b]Features[/b][/size]
[list]
[*][b]Pick the weapons per skill.[/b] Hover a skill in the skill picker, on the left/right skill button or on the quick cast bar and press F10 (rebind it in Options > Controls). Each press cycles M, MS, SM, S:
[list]
[*][b]M[/b]: weapon set I
[*][b]MS[/b]: set I weapon + set II shield/off-hand
[*][b]SM[/b]: set II weapon + set I shield/off-hand
[*][b]S[/b]: weapon set II
[/list]
[*][b]Swapped only for the cast.[/b] The weapons go in your hands when you cast and set I comes back right after. Skills with no mode cast with set I.
[*][b]Labels on the tiles.[/b] The letters show in the corner of the skill tile (M is hidden unless you turn it on).
[*][b]Swap skills always available.[/b] Skills your set II gives (Call to Arms' Battle Orders and Battle Command, +class skills, charges...) stay in the skill picker and on the skill buttons while you hold set I, and cast with S by default.
[*][b]Saved per character[/b] in [i]<game>\d2rloader\swap-cast\modes.txt[/i], outside the mod folder.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Single player, or a TCP/IP game you host
[*]Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and stays inactive on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-swap-cast.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/swap-cast.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]restore_delay_ms[/b]: 150 (default) = milliseconds without casting before set I comes back
[*][b]show_swap_skills[/b]: true (default) = keep set II skills (Call to Arms...) available while you hold set I
[*][b]show_m_label[/b]: false (default) = true also shows "M" on the tiles
[*][b]allow_cross_cast[/b]: true (default) = the MS and SM modes; false = only M and S
[*][b]log_swaps[/b]: false (default) = true logs every swap
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Notes[/b][/size]
[list]
[*]Pressing W while the items are moved puts them back where the game expects them first. Leaving the game does too, so your character is saved normally.
[*]A set you haven't held yet this game is put in your hands for a moment when you join (or equip a new swap item), so the game adds its skills.
[*]A combination the game refuses (a two-handed weapon with a shield) casts with the weapons you hold.
[/list]
```
