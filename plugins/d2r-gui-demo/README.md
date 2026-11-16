# D2R GUI Demo

Version 0.0.2 (test plugin, not released).

A window made only from the D2R GUI library (`lib/d2r-gui`), to try the
library in game and as an example of using it. It changes nothing in the game.

Open it with the console command `guidemo`, or bind a key in Controls
(D2R GUI Demo > Open D2R GUI Demo).

The whole window is one declared `App::Window` (lib/d2r-gui `app.h`): an
About page, settings pages with a check box, a drop-down, a number with
arrows and a button, a long drop-down that scrolls, filters as you type and
has section headers, and a searchable list of clickable rows.

The icons are from game-icons.net (CC BY 3.0, see
`lib/d2r-gui/ICONS-CREDITS.txt`).

Build: `sh build.sh` (MinGW-w64), which makes `d2r-gui-demo.zip`.
