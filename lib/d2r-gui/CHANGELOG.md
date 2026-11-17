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
* Declared windows take values through get and set, pages opened over others with Back, fixed headers and footers, windows on a Player Page Host area or over the open stash, item tile pages, ring charts, tree rows and an icon picker.
* D2R GUI Demo plugin 0.0.2 to try it, declared with app.h (console command guidemo).

**Fixed**
* Coloured text parts (App::Red and the others) show in colour instead of as raw codes.
* Table headers and cells are cut to their column with "...", and the full text shows on hover.
* Components inside a card stay inside its frame and clear of the scroll bar.
* Table and list rows have flat stripes behind them instead of a button sprite.
* Cards are a flat dark panel with a thin line around it instead of a bevelled box sprite.
* Long tooltips wrap over several lines instead of running off the screen.
* App windows can have a frame of the plugin's own art and size (Frame::Own), with the menus where the frame says.
* App::ItemGrid puts the page's real item grid on a page, with pictures over it, or sends a Player Page Host area its place.
* App windows can show the plugin's own player page in their item grid (window.page, window.pageGrid).
* ItemGrid can stay at a fixed place while its tiles scroll, and sends a command for clicks off its tiles.
* Tile labels can wrap over several lines, take a colour and a tooltip; tile row titles take a colour.
* Tabs can show item art, look see-through, leave a gap before a group and pad their badge.
* App::SetFrame moves or changes an own frame at runtime.
* window.wheelRight sets how far across the screen the wheel scrolls the window.
* Windows choose their tab bars' look (window.menuLook, Menu.pageLook); tabs can spread over a length with gaps between groups.
* Own frames can have a stone cover over their picture and a scroll bar of their own place and size (window.barLook for its art).
* Tiles take a look (Item.Looks / ItemGridSpec.look): label box, margin and font size, gaps, row title height, font, place and divider.
