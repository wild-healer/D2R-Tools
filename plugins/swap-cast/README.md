# Swap Cast

D2RLoader plugin: choose which weapons cast each skill.

- `M`: weapon set I (both hands)
- `MS`: the weapon of set I + the shield/off-hand of set II
- `SM`: the weapon of set II + the shield/off-hand of set I
- `S`: weapon set II

Hover a skill in the skill picker (or the left/right skill button, or the quick
cast bar) and press the
**Swap Cast: change skill weapons** key (default `F10`; rebind it in Options >
Controls, Extensions). Each press cycles M -> MS -> SM -> S. The letters show in
the tile's bottom corner.

When you cast a skill, its weapons are put in your hands for the cast, and set I
comes back as soon as no skill has been in use for `restore_delay_ms` (150 ms).
A skill with no mode casts with set I. Pressing W while the items are moved puts
them back where the game thinks they are (that is the swap you see when it
differs from what you hold). Leaving the game puts them back too, so the
character is saved normally.

- Install: get Swap Cast from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer) and add it through D2RMM like the other plugins.
- Modes are saved per character in `<game>\d2rloader\swap-cast\modes.txt`
  (outside the mod folder, so D2RMM reinstalls keep them).
- Config (`d2rloader/config/swap-cast.toml`): `enabled`, `show_m_label` (false), `allow_cross_cast` (true: MS and SM modes),
  `restore_delay_ms` (150), `log_swaps` (false), `show_swap_skills` (true).
- Works in single player and in games you host (for you), not in games you join.
- Skills your set II items give (Call to Arms' Battle Orders...) stay in the
  skill picker and on the skill buttons while you hold set I, and default to `S`.
  A skill an item grants as its own (charges) cycles through all four modes
  too; one that leaves the item out of your hands makes it red. The skill
  tooltip shows the mode, since a tile with charges has no room for the label.
  A set you haven't held yet this game is put in your hands for 0.3 s once, on
  joining or when you equip it, so the game adds its skills.
  `show_swap_skills = false` turns this off.
- A class skill only your set II gives (+2 Summon Tainted from a set II tome, no
  hard points) shows at its set II level while you hold set I, so it isn't red
  and casts (with `S` by default).
- A skill only your set II gives is red while its mode leaves the item that
  gives it out of your hands (`M`, or `MS`/`SM` on the wrong hand), and your
  character says "I can't" when you try to cast it. Other gear that gives the
  skill (+all skills on your armor...) still lets it cast.
- Item skills keep their own mode, separate from the same skill learned by your
  character (saved as `i<skill id>` in modes.txt).
- A combination the game refuses (a two-handed weapon with a shield...) casts with
  the weapons you hold; `log_swaps` logs it.
- Dying while the items are moved leaves the corpse's items where they were at
  that moment (softcore).

How it works:
- Casts: the server's packet dispatcher (D2R+0x4F2FA0) for the cast packets
  (0x05-0x11), before the game checks the skill, plus D2R+0x4FDB40 / 0x4F8DE0
  (skill on a location / unit) for any other cast. Set I/II: the game's weapon
  switch state (D2R+0x485200).
- Moves: D2R+0x471E90, the all-or-nothing item rearrange the game's own weapon
  swap uses (it checks every placement), then the same client updates the swap
  handler (D2R+0x4BEA50) sends. Only the hands that need it move (body slots 4/11
  for the weapon, 5/12 for the shield), so your skill bindings stay as they are.
- No move while a skill is in use (D2R+0x34BA40) or in the uninterruptable state.
- Labels: the tile's quantity text (used for scroll counts), set after the game
  fills a tile (D2R+0x239110 by skill id, D2R+0x239380 from the skill, which the
  skill picker uses).
- Other set's skills: D2R+0x33FE60 adds/removes the skills an item grants
  (charges) and D2R+0x33EA00 removes a "+skill" nobody gives a level to. The
  plugin skips those removals for you and does them itself once no item in your
  weapon slots gives the skill (checked on every packet you send and on the client
  every 250 ms). The skill buttons' level check (call at D2R+0x239BF4) sees such
  a skill as level 1, so it isn't replaced by Attack.
- Hovered tile: the UI's hovered widget, the same one the game checks to bind
  F1-F8 (skill picker, left/right skill buttons and the quick cast bar).

Every function's first 16 bytes are checked at load (Steam 3.3.93787 / 94995);
otherwise the plugin stays inactive. (SkillLevel D2R+0x33D1E0 and GetStat
D2R+0x2F5020 start with the game's own jump and are called, not checked.)

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
