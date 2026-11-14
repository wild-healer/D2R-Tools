# D2R GUI: what it can do

D2R GUI gives a D2RLoader plugin an in-game window that looks and behaves
like the game's own, without writing any window code. It is the window code
of Grail On! and I Hate TOMLs, made reusable: both plugins now draw
everything through it, and the D2R GUI Demo plugin is built from nothing else.

It is header-only. Include `d2r_gui.h`, and the library compiles into your
plugin's DLL. No extra DLL, no companion D2RMM mod, nothing shared between
plugins at run time.

Version 0.0.1, not released yet.

---

## A window in a few lines

```cpp
#include <d2r_gui.h>
namespace Gui = D2RGui;

Gui::PanelPool Window;

std::string Layout(const std::string& panel, const std::string& probe) {
	std::string c = Gui::LeftPanelFrame(1162, 1507, "My Plugin", "close", 1075);
	c += Gui::Probe(probe);
	c += Gui::TypedBox("Search", "search", 80, 160, 1000, 60, Search, "Search", "Type to filter");
	c += Gui::Text("OddLabel", 80, 260, 500, 60, "Show odd rows", Gui::Style::Label);
	c += Gui::Check("Odd", 1020, 267, ShowOdd, "odd", "Shows or hides the odd rows");
	c += Gui::Stepper("Number", 600, 340, 400, 60, std::to_string(Number), "number-", "number+");
	c += Gui::DropDown::Box("Choice", 600, 420, 420, 60, Choices[Choice], "drop");
	return Gui::Panel(panel, 236, -651, 1162, 1507, c, 10, Gui::LeftAnchor);
}

void OnCommand(const std::string& command) { /* "odd", "number+", "drop", "close"... */ }

// While the plugin loads:
Gui::Init(context, "my-plugin");
Gui::Commands::Listen(OnCommand);
```

Every widget function returns a piece of the game's layout JSON. You add
the pieces together, and the game draws them with its own widgets, fonts and
sounds. Button clicks come back to your plugin as short command names, on
the UI thread.

---

## Windows

| You get | What it does |
| --- | --- |
| **Left panel frame** | The game's left-panel frame, title band and close button. |
| **Character-screen window** | A window drawn like the character screen, using the game's own background art. I Hate TOMLs uses this one. |
| **Stash-look window** | Slot stone, gold rims and the stash's art at any size. Grail On! uses this one. |
| **Behaves like a game left panel** | Opening it closes the character screen, quests, waypoints and stash, and opening one of those closes it. The view shifts right while it is open, as it does for theirs. |
| **Windows that change while open** | A pool of panels takes turns, so every rebuild shows the new layout. Rebuilds are combined into one per frame, and the pool recovers on its own if the game keeps an old layout. |
| **Fast repeat views** | Many panels each keep one view built. A view you saw before opens at once, and the view unused for longest is replaced first. |
| **One-off panels** | A panel with a fixed layout, or one that gets a new layout each time it is shown. |
| **Stays on screen** | Its anchor is written out in full, so a mod that moves `$LeftPanelAnchor` (ESP, for example) can't push the window off screen. |

## Controls

| Control | Notes |
| --- | --- |
| Text | The game's styles (title, label, note, gold, grey…), wrapped to a width, CJK included. |
| Buttons | Sprite buttons with normal, hovered and pressed frames, text buttons, invisible click areas, tooltips, the game's click sounds. |
| Check boxes | Ticked, unticked and greyed out. |
| Text boxes | Real typing: keyboard layouts, Shift, AltGr, copy, paste, undo, select all. A caret while typing, a grey hint while empty. Enter, Esc and click-away each call your code. |
| Drop-downs | The game's own drop-down box and list. |
| Number steppers | A number between two arrows. |
| Scroll bars | A recessed track with a bronze thumb sized to the share shown. Click a step to jump, drag the thumb, or use the mouse wheel over the window. |
| Clip views | Content taller than its room, cut to the room. |
| Section titles, dividers, table rows | For settings pages and long lists. |
| Inventory button | A button on the inventory's left edge in the stone and gold mold that Charm Inventory uses. You can move it while the game runs. |

## Items

| You get | What it does |
| --- | --- |
| **Item grids of your own** | An item grid bound to one of the plugin's player pages. |
| **Half-cell tiles** | A 1-wide item sits centred in a 2-wide slot, for both drawing and the mouse. |
| **Tooltips past the grid** | An item can show its tooltip on a picture tile outside the grid's own cells. |
| **No "cannot wear" colour** | Items in your grid aren't coloured red, neither by the game nor by Item Quality Backgrounds. |
| **Item pictures** | Item art recoloured like the game does it, greyed out for items not found yet, with sockets, runes and Item Quality Backgrounds' quality colours. |

## Art

- **The game's own art, at any size.** The library reads drop-down boxes, loot filter rows and arrows, close and refresh buttons, slots, the scroll track and the character screen from the game's files, mods included. It resizes them on demand.
- **Its own drawing when that art is missing.** Every part falls back to art in the game's look, so a window never needs a companion mod.
- **Low-end copies.** Half-size `.lowend` sprites are made for the game's low graphics setting.
- **Your own PNGs.** They are compiled into the DLL's code, with no resources and no Windows image codecs. Antivirus scanners flagged the usual method.
- **Two scaling styles.** Bilinear, as I Hate TOMLs uses, and area average, as Grail On! uses.
- **Picture tools.** Icons from masks, glows, ring charts, gems and gold rules.

## Text and languages

- **Translated strings** in all 13 of the game's languages, named from layouts as `@my-plugin:Key`.
- **Text the game's font can draw.** Characters the font lacks are replaced.
- **The game's real font sizes**, read from the fonts and layout profiles it loads (a font mod's when one is installed). Rows and wrapping fit before the game draws them, and Low Vision Mode is followed.

## Mouse and game screen

- The mouse wheel scrolls the window while the cursor is over it.
- Any other mouse message can go to your plugin first. Middle click is one example.
- The library can tell whether the stash is open, so a window can sit over it like the cube does.
- It hides the life number on the HUD while your window is open, as the game does for its own left panels.

---

## Safe by design

- **Version-checked.** Every call into the game checks the code bytes first and does nothing on a build it doesn't recognise. D2R 3.3.94995 and 3.4.95041 match.
- **Fixed names only.** D2RLoader gives each plugin about 255 widget lookups and never takes them back, so the library looks up only fixed names.
- **Chains with other plugins.** Hooks are added on top of whatever is already there. Inventory Slots wraps the same grid entries.
- **Self-contained.** Each plugin carries its own copy of the library, so two plugins on different library versions never affect each other.

## Try it

Install D2R GUI Demo (`plugins/d2r-gui-demo`) and type `guidemo` in the
console. It opens a window with a search box, a check box, a drop-down, a
number with arrows, a reset button and a long scrolling list.

For the full list of headers, see [README.md](README.md).
