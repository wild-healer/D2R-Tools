# HUD Cast

D2RLoader plugin: the bottom bar (the HUD with the orbs, belt and skill buttons)
normally stops your clicks when the cursor is over it, even over its empty art, and
a held skill stops as soon as the cursor reaches it. With this plugin, clicking or
holding a skill over the bar's art casts it (or walks) at the spot under the cursor,
as anywhere else in the game. The bar's buttons, belt, orbs and skill buttons still
work as usual.

- Install: load `hud-cast.zip` with D2RMM, or copy `d2rl-hud-cast.dll` to
  `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/hud-cast.toml`, written on first launch): `enabled`,
  `log_parts`. Both can be changed in game with I Hate TOMLs and apply at once.
- Log: `d2rloader/logs/hud-cast.log` names each part of the bar the cursor stopped
  on, once, and whether casts go through it.
- Mouse and keyboard only; controller play is left alone.

## How it works

Two things stop those casts (Steam 3.3.93787 / 3.3.94995):

1. **Clicks.** A click goes to the UI first (D2R+0x840120 right, D2R+0x83FCC0 left).
   The HUD panel (HUDPanelHD, vtable D2R+0x1FDF990) holds click catchers
   (ClickCatcherWidget, vtable D2R+0x1D80390). Their message handler D2R+0x8E1920
   marks a mouse message handled (D2R+0x843FC0) when the catcher is the hovered
   widget (D2R+0x876180, called at D2R+0x8E1B34), and a handled click never reaches
   the world handlers (D2R+0x83B00 right, D2R+0x839E0 left). That call is pointed at
   a small stub in int3 padding that answers "not hovered" for catchers inside the
   HUD panel.
2. **Held casts and the unit under the cursor.** Each frame D2R+0xF2670
   `int (int* x, int* y)` returns 1 when the cursor is over the UI: any hovered
   widget (UI manager D2R+0x846020, +0x178) or the bottom 49 pixels. The hover
   search (D2R+0xF1250) and held clicks (D2R+0xF1DB2) stop then. The plugin hooks
   it: over a HUD click catcher, the cursor counts as over the world and the game's
   own tail runs (D2R+0x846210, D2R+0x8F270).

Widget fields: +0x08 name, +0x30 parent. Every function's code is checked at load;
on another build the plugin stays inactive and says so in its log.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64 win32 thread model,
SDK pinned to 03a5af9).
