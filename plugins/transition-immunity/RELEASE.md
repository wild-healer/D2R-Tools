# Transition Immunity 1.0.0: release texts

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

## Nexus Mods

Name: `Transition Immunity (D2RLoader plugin)`

Summary (short description):

> After every loading screen, monsters can't hurt or curse you until you move or for a minute.

Description (BBCode):

```bbcode
[size=5][b]Transition Immunity[/b][/size]
A D2RLoader plugin that keeps you safe after every loading screen.

[size=4][b]Features[/b][/size]
[list]
[*][b]Safe arrivals.[/b] After a town portal, waypoint, cave or dungeon entrance, stairs, or joining a game, monsters can't hurt or curse you.
[*][b]Loading screen covered.[/b] You're protected while the new area loads, not just after.
[*][b]Everything blocked.[/b] Melee, missiles, spells, traps, aura damage (Holy Fire, Holy Shock, Holy Freeze, Sanctuary), poison, burning, freeze, chill, stun, knockback and curses.
[*][b]Ends when you move.[/b] Walking, running or teleporting ends it, as does the time limit (60 s by default). Attacking or casting in place does not.
[*][b]Indicator.[/b] The armor shrine's shield floats over your head while you're protected, above any skill effect.
[/list]

[size=4][b]Not blocked[/b][/size]
[list]
[*]Aura effects that do no damage, such as the Holy Freeze slow or Conviction
[*]Poison that was already ticking before the loading screen
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Single player, or a TCP/IP game you host (protects you, the host); does nothing in a game you joined
[*]Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and stays inactive on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-transition-immunity.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/transition-immunity.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]duration_seconds[/b]: how long the protection lasts at most if you don't move (60)
[*][b]indicator[/b]: false hides the indicator
[*][b]indicator_overlay[/b]: the overlay.txt Filename drawn as the indicator (ShrineArmor; also ShrineSkill, ShrineCombat, ManaShieldBall)
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Log[/b][/size]
[i]d2rloader/logs/transition-immunity.log[/i] has one line per protection: why it ended and how many hits and curses it blocked.
```
