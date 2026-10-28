# Un-Anya-lyse

D2RLoader plugin: Anya gets a "Remove Personalization" line in her dialog. It
opens her personalize window; put a personalized item in it and confirm, and the
item loses its name. Free, as many times as you like, with or without her
Betrayal of Harrogath reward (the reward is not used up).

- Install: add `un-anya-lyse-1.0.0.zip` through D2RMM like the other plugins.
- Config (`d2rloader/config/un-anya-lyse.toml`): `enabled`.
- Texts in all 13 game languages.
- Works in single player and in games you host. The line doesn't show in games
  you join: the host decides, and an unmodded host refuses the request.

How it works (Steam 3.3.93787 / 94995 RVAs):
- Dialog: D2R+0x1E9030 adds a dialog line `(menu, utf8 text, int, int, callback,
  enabled)`; the dialog builder adds Cancel last from D2R+0x114D8B. Before that Cancel,
  for Anya (NPC 0x200, D2R+0x10D150), the plugin adds its line. Its callback turns
  remove mode on and calls Anya's Personalize line D2R+0x10DD30, which opens the
  window. A new dialog or the window closing (D2R+0x1E3DB0) turns it off.
- Window texts: the title getter D2R+0x1E3A90, and the text setter D2R+0x86DD50 when
  called for the instruction line (D2R+0x1E4BC4) and the confirm button (D2R+0x1E51A1).
- Which items the window accepts: D2R+0x36B1D0 (false for a personalized item), called
  at D2R+0x1E38B6 and D2R+0x1E59A7. In remove mode: personalized items only (not ears).
- Confirm: D2R+0x117510 sends packet 0x2E (type 3 = personalize, NPC, item). The
  plugin remembers the item.
- Server, D2R+0x4FC230 (packet 0x2E handler), for that item only: the reward-pending
  check D2R+0x325C50 (D2R+0x4FC7F3) passes, the item check D2R+0x36B1D0 (D2R+0x4FCB6E)
  wants a personalized item, the name set D2R+0x36DFA0 (D2R+0x4FD49F) becomes an empty
  name plus the personalized flag 0x01000000 cleared (D2R+0x36D8F0), and the quest
  completion D2R+0x547C60 (D2R+0x4FD4BC) is skipped. The game then puts the item back
  and sends it to the client as usual.
- Single player / host check: D2RLoader's thread service only has a game thread to
  queue work on when the game runs on this PC.

Every function's first 16 bytes and every call site above are checked at load;
otherwise the plugin stays inactive.
