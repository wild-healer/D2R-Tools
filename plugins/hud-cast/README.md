# HUD Cast

D2RLoader plugin: with keyboard quick cast on, a skill key does nothing while the
cursor is over the bottom bar (the HUD with the orbs, belt and skill buttons), even
over its empty art. With this plugin, a quick cast over the bar's art casts at the
spot (or the monster) under the cursor, as anywhere else in the game. The bar's
buttons, belt, orbs and skill buttons, and mouse clicks, work as usual.

- Install: load `hud-cast.zip` with D2RMM, or copy `d2rl-hud-cast.dll` to
  `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/hud-cast.toml`, written on first launch): `enabled`,
  `log_parts`. Both can be changed in game with I Hate TOMLs and apply at once.
- Log: `d2rloader/logs/hud-cast.log` names each part of the bar a quick cast was
  let through over, once.
- Mouse and keyboard only; controller play is left alone.

## How it works

Steam 3.3.93787 / 3.3.94995:

1. **The key.** A key goes to the UI first. With keyboard quick cast on, the click
   catchers of the HUD panel (HUDPanelHD, vtable D2R+0x1FDF990; ClickCatcherWidget,
   vtable D2R+0x1D80390, message handler D2R+0x8E1920) mark a skill key handled
   (D2R+0x843FC0) when the cursor is inside them. The handler asks the catcher's
   "point inside" method (vtable slot 8, calls returning to D2R+0x8E1ADB and
   D2R+0x8E1B02). The key binding (D2R+0x1203A0) then sees the key handled
   (D2R+0x8460D0) and never starts the cast. The plugin swaps that vtable slot and
   answers "outside" to those two calls, for catchers inside the HUD panel only.
2. **The target.** Each frame D2R+0xF2670 `int (int* x, int* y)` returns 1 when the
   cursor is over the UI (a hovered widget, UI manager D2R+0x846020 +0x178), and the
   hover search (D2R+0xF1250) then finds no monster. The plugin hooks it for that
   caller only (return D2R+0xF1285): over a HUD click catcher the cursor counts as
   over the world and the game's own tail runs (D2R+0x846210, D2R+0x8F270). Held
   clicks (D2R+0xF1DB2) still stop at the bar.

Widget fields: +0x08 name, +0x30 parent. Every function's code is checked at load;
on another build the plugin stays inactive and says so in its log.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64 win32 thread model,
SDK pinned to 03a5af9).
