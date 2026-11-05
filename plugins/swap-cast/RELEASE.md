# Swap Cast 1.1.2: release texts

## Patch notes

```
**Swap Cast 1.1.2**

**Fixed**

* A skill your set I weapon gives (Widowmaker's Guided Arrow) no longer turns into Attack on its skill button when a skill cast with set II moves that weapon away.
```

## GitHub release

- Tag: `swap-cast-v1.1.2`
- Title: `Swap Cast 1.1.2`
- Asset: `swap-cast.zip`

Body:

```markdown
**Swap Cast** 1.1.2

### Fixed
- A skill your set I weapon gives (Widowmaker's Guided Arrow) no longer turns into Attack on its skill button when a skill cast with set II moves that weapon away.

### Install
Load `swap-cast.zip` in D2RMM, or copy `d2rl-swap-cast.dll` to `<game>/d2rloader/plugins/`. Settings and modes.txt are unchanged.
```

# Swap Cast 1.1.1

## Patch notes

```
**Swap Cast 1.1.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled and show_swap_skills included.
* The download is now named swap-cast.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which antiviruses can flag.
```

## GitHub release

- Tag: `swap-cast-v1.1.1`
- Title: `Swap Cast 1.1.1`
- Asset: `swap-cast.zip`

Body:

```markdown
**Swap Cast** 1.1.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled and show_swap_skills included.
- The download is now named `swap-cast.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which antiviruses can flag.

### Install
Load `swap-cast.zip` in D2RMM, or copy `d2rl-swap-cast.dll` to `<game>/d2rloader/plugins/`. Settings and modes.txt are unchanged.
```

# Swap Cast 1.1.0

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

## D2RLoader Hub

Name: `Swap Cast`

Summary:

> Cast each skill with weapon set I, set II or one hand from each, without pressing W. Call to Arms skills stay available.

Description:

```markdown
**Swap Cast**: each skill casts with any combination of your two weapon sets, without pressing W.

### Features
- **Pick the weapons per skill.** Hover a skill in the skill picker, on the left/right skill button or on the quick cast bar and press F10 (rebind it in Options > Controls). Each press cycles M, MS, SM, S:
  - **M**: weapon set I
  - **MS**: set I weapon + set II shield/off-hand
  - **SM**: set II weapon + set I shield/off-hand
  - **S**: weapon set II
- **Swapped only for the cast.** The weapons go in your hands when you cast and set I comes back right after. Skills with no mode cast with set I.
- **Labels on the tiles.** The letters show in the corner of the skill tile (M is hidden unless you turn it on).
- **Swap skills always available.** Skills your set II gives (Call to Arms' Battle Orders and Battle Command, +class skills, charges...) stay in the skill picker and on the skill buttons while you hold set I, and cast with S by default.
- **Saved per character** in `<game>\d2rloader\swap-cast\modes.txt`, outside the mod folder.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `swap-cast.zip` in D2RMM, or copy `d2rl-swap-cast.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/swap-cast.toml`, written on first launch, or in I Hate TOMLs.

### Settings
- restore_delay_ms: 150 (default) = milliseconds without casting before set I comes back
- show_swap_skills: true (default) = keep set II skills (Call to Arms...) available while you hold set I
- show_m_label: false (default) = true also shows "M" on the tiles
- allow_cross_cast: true (default) = the MS and SM modes; false = only M and S
- log_swaps: false (default) = true logs every swap
- enabled: false turns the plugin off

### Notes
- Pressing W while the items are moved puts them back where the game expects them first. Leaving the game does too, so your character is saved normally.
- A set you haven't held yet this game is put in your hands for a moment when you join (or equip a new swap item), so the game adds its skills.
- A combination the game refuses (a two-handed weapon with a shield) casts with the weapons you hold.
```
