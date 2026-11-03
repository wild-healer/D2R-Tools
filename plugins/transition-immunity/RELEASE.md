# Transition Immunity 1.0.1: release texts

## Patch notes

```
**Transition Immunity 1.0.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once: enabled, duration_seconds, indicator and indicator_overlay.
* indicator_overlay setting: the menu offers a list of symbols to pick from (the shrine symbols and the Energy Shield orb); the change shows over your head at once.
* The download is now named transition-immunity.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which can make an antivirus flag the DLL.
```

## GitHub release

- Tag: `transition-immunity-v1.0.1`
- Title: `Transition Immunity 1.0.1`
- Asset: `transition-immunity.zip`

Body:

```markdown
**Transition Immunity** 1.0.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once: `enabled`, `duration_seconds`, `indicator` and `indicator_overlay`.
- `indicator_overlay`: the menu offers a list of symbols to pick from (the shrine symbols and the Energy Shield orb); the change shows over your head at once.
- The download is now named `transition-immunity.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which can make an antivirus flag the DLL.

### Install
Load `transition-immunity.zip` in D2RMM, or copy `d2rl-transition-immunity.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

## D2RLoader Hub

Name: `Transition Immunity`

Summary:

> After every loading screen, monsters can't hurt or curse you until you move or for a minute.

Description:

```markdown
**Transition Immunity**: safe arrivals after every loading screen.

### Features
- **Safe arrivals.** After a town portal, waypoint, cave or dungeon entrance, stairs, or joining a game, monsters can't hurt or curse you.
- **Loading screen covered.** You're protected while the new area loads, not just after.
- **Everything blocked.** Melee, missiles, spells, traps, aura damage (Holy Fire, Holy Shock, Holy Freeze, Sanctuary), poison, burning, freeze, chill, stun, knockback and curses.
- **Ends when you move.** Walking, running or teleporting ends it, as does the time limit (60 s by default). Attacking or casting in place does not.
- **Indicator.** The armor shrine's shield floats over your head while you're protected, above any skill effect. Pick another symbol in the settings.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Not blocked
- Aura effects that do no damage, such as the Holy Freeze slow or Conviction.
- Poison that was already ticking before the loading screen.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host (it protects you, the host). It does nothing in a game you joined.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `transition-immunity.zip` in D2RMM, or copy `d2rl-transition-immunity.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/transition-immunity.toml`, written on first launch, or in I Hate TOMLs.
```

# Transition Immunity 1.0.0

## GitHub release

- Tag: `transition-immunity-v1.0.0`
- Title: `Transition Immunity 1.0.0`
- Asset: `transition-immunity-1.0.0.zip`

Body:

```markdown
First release of **Transition Immunity**, a D2RLoader plugin that keeps you safe after every loading screen.

### Features
- **Safe arrivals.** After a town portal, waypoint, cave or dungeon entrance, stairs, or joining a game, monsters can't hurt or curse you.
- **Loading screen covered.** You're protected while the new area loads, not just after.
- **Everything blocked.** Melee, missiles, spells, traps, aura damage (Holy Fire, Holy Shock, Holy Freeze, Sanctuary), poison, burning, freeze, chill, stun, knockback and curses.
- **Ends when you move.** Walking, running or teleporting ends it, as does the time limit (60 s by default). Attacking or casting in place does not.
- **Indicator.** The armor shrine's shield floats over your head while you're protected, above any skill effect.

### Not blocked
- Aura effects that do no damage, such as the Holy Freeze slow or Conviction.
- Poison that was already ticking before the loading screen.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host (it protects you, the host). It does nothing in a game you joined.
- Steam 3.3.93787 or 3.3.94995. It uses the game's own code, checked when the plugin loads; on another build it stays inactive and says so in its log.

### Install
Load `transition-immunity-1.0.0.zip` in D2RMM, or copy `d2rl-transition-immunity.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/transition-immunity.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Settings
- `duration_seconds`: how long the protection lasts at most if you don't move (60).
- `indicator`: false hides the indicator.
- `indicator_overlay`: the overlay.txt Filename drawn as the indicator (`ShrineArmor`; also `ShrineSkill`, `ShrineCombat`, `ManaShieldBall`).
- `enabled`: false turns the plugin off.

### Log
`d2rloader/logs/transition-immunity.log` has one line per protection: why it ended and how many hits and curses it blocked.
```
