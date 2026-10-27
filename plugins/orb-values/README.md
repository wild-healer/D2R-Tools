# Orb Values

D2RLoader plugin that shows the life and mana numbers all the time, without their "Life:" and "Mana:" prefixes, in the middle of each orb.

- **Always shown**: the numbers no longer need the orb to be clicked or hovered. The game still hides them under its own panels, as before.
- **No toggle**: clicking an orb does nothing anymore.
- **No prefix**: only `123 / 456` is left, in every language.
- **Centered**: each number sits in the middle of its orb, drawn over the liquid, and stays centered whatever its length.

Patches and hooks the game's orb widget code; it checks that code when it loads and stays inactive on other builds. Tested on Steam 3.3.94995 with the mouse and keyboard HUD (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/orb-values). Load `orb-values-<version>.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `d2rl-orb-values.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/orb-values.toml`, written on first launch:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `offset_x`, `offset_y` | `0`, `0` | Moves both numbers from the middle of their orb, in HUD units (an orb is about 350 units tall). |
