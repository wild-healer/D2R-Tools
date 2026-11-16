**D2R GUI 0.0.1**

**New**
* Header-only library of the window parts from Grail On! and I Hate TOMLs.
* Pictures, game sprites, the game's own art at any size, and art of its own when that is missing.
* Layout pieces: text, images, buttons, check boxes, text boxes, drop-downs, clip views, scroll bars.
* A panel pool for windows that change while open, with fixed probe names.
* Typing into text boxes, the mouse wheel, scroll bar dragging.
* Font sizes read from the game, Low Vision Mode included.
* Left panel behaviour: closes the game's left panels and moves the view like they do.
* Character-screen window, drop-down, number stepper and font wrapping (from I Hate TOMLs).
* Inventory button, item grid with half-cell tiles, view panel cache, item and stash art, strings tables, HUD life number (from Grail On!).
* An icon set: 120 game-icons.net icons at any size and colour (opt-in icons.h).
* A window in the stash's look (opt-in stash_window.h), and showing or hiding the inventory button live.
* Icon tabs (opt-in tabs.h), and item cells that take one item code each, on Player Page Host's page or the plugin's own (opt-in item_cells.h).
* Long drop-down lists that scroll, take a filter box and have section headers.
* Menus and pages as icon tabs (opt-in menus.h).
* Declared windows (opt-in app.h): a plugin lists its menus, pages and components bound to its data, and the library does the rest.
* Declared windows take values through get and set, pages opened over others with Back, fixed headers and footers, and windows on a Player Page Host area.
* D2R GUI Demo plugin 0.0.2 to try it, declared with app.h (console command guidemo).
