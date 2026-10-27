// Adds QuestFlag0..QuestFlag9 to the character select panel, one per visible
// row of the character list, next to the ladder/hardcore/expansion icons and
// D2RLoader's plugin/mod icons. They sit beside the list (its rows are copies
// of one template, so a widget inside it could not be told apart per
// character), so they draw over the list's gold selection frame: each row also
// has QuestFlagSel<n>, the same flag with that frame's top edge painted on, which
// the plugin shows instead on the selected row, and QuestFlagTip<n>, the tooltip
// area over both. The Quest Rewards plugin hides
// the flags of characters with nothing waiting and writes each tooltip
// ("@quest-rewards:Flag<n>").
//
// D2RLoader ships its own character select layout in d2rloader.mpq (its list
// with the plugin and mod icons). A mod's copy replaces it, and D2RMM can only
// read the game's original, so the layout below is D2RLoader 1.3.1-beta's,
// copied from d2rloader.mpq (data/global/ui/layouts/characterselectpanelhd.json).

const ROWS = 10; // the list shows 9.3 rows of 184 px in 1718 px
const layoutPath = 'global\\ui\\layouts\\characterselectpanelhd.json';
const layout = {"type":"CharacterSelectPanel","name":"CharacterSelectPanel","fields":{"priority":1,"anchor":{"x":1},"hardcoreCharacterNameColor":"$FontColorRed","deadHardcoreTint":{"r":1,"g":0.3,"b":0.3,"a":1},"noTitleCharacterNamePosition":{"x":25,"y":68},"noTitleCharacterLevelClassPosition":{"x":25,"y":120},"shiftStatusIconPosition1":{"x":545,"y":0},"shiftStatusIconPosition2":{"x":490,"y":0}},"children":[{"type":"ImageWidget","name":"Background","fields":{"rect":{"x":-801,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_RightPanel"},"children":[{"type":"ListWidget","name":"CharacterList","fields":{"rect":{"x":78,"y":72,"width":613,"height":1718},"scrollControllerName":"ScrollBar","selectorName":"Selection","direction":"vertical","itemSelectionMessage":"CharacterSelect:SelectCharacter","selectionChangedSound":"cursor_movement_hd","selectInactiveItems":true},"children":[{"type":"ListItemWidget","name":"CharacterTemplate","fields":{"rect":{"x":0,"y":0,"width":612,"height":184}},"children":[{"type":"ImageWidget","name":"Background","fields":{"rect":{"x":0,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_CharacterTile"}},{"type":"ImageWidget","name":"LadderIcon","fields":{"rect":{"x":431,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_LadderIconSmall"},"children":[{"type":"FocusableWidget","name":"LadderIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2r:CharacterSelectPanelLadderCharacter"}}]},{"type":"ImageWidget","name":"ClassicIcon","fields":{"rect":{"x":545,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_ClassicIconSmall"},"children":[{"type":"FocusableWidget","name":"ClassicIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2r:CharacterSelectPanelPreExpansionCharacter"}}]},{"type":"ImageWidget","name":"ExpansionIcon","fields":{"rect":{"x":545,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_ExpansionIconSmall"},"children":[{"type":"FocusableWidget","name":"ExpansionIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2r:Expansion"}}]},{"type":"ImageWidget","name":"HardcoreIcon","fields":{"rect":{"x":488,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_HardcoreIconSmall"},"children":[{"type":"FocusableWidget","name":"HardcoreIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2r:strHardcore"}}]},{"type":"ImageWidget","name":"ModIcon","fields":{"rect":{"x":374,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_ModdedIconSmall","visible":false},"children":[{"type":"FocusableWidget","name":"ModIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2rloader:D2RLoaderCharacterStatusModded"}}]},{"type":"ImageWidget","name":"PluginIcon","fields":{"rect":{"x":317,"y":0},"filename":"FrontEnd\\HD\\Final\\FrontEnd_PluginIconSmall","visible":false},"children":[{"type":"FocusableWidget","name":"PluginIconTooltip","fields":{"fitToParent":true,"tooltipString":"@d2rloader:D2RLoaderCharacterStatusPlugins"}}]},{"type":"TextBoxWidget","name":"Title","fields":{"rect":{"x":25,"y":11,"width":400,"height":42},"fontType":"16pt","style":{"fontColor":"$CharacterListDetailsColor","alignment":{"h":"left","v":"center"},"pointSize":"$XMediumFontSize","spacing":"$ReducedSpacing","dropShadow":"$DefaultDropShadow"}}},{"type":"TextBoxWidget","name":"Name","fields":{"rect":{"x":25,"y":68,"width":560,"height":55},"style":{"fontColor":"$LightButtonTextColor","alignment":{"h":"left","v":"center"},"pointSize":"$XMediumLargeFontSize","spacing":"$ReducedSpacing","dropShadow":"$DefaultDropShadow"},"useAltStyleIfDoesntFit":true,"altStyle":{"alignment":{"h":"left","v":"center"},"fontColor":"$LightButtonTextColor","pointSize":"$SmallFontSize","spacing":"$MinimumSpacing","dropShadow":"$DefaultDropShadow"}}},{"type":"TextBoxWidget","name":"Level&Class","fields":{"rect":{"x":25,"y":120,"width":400},"style":{"fontColor":"$CharacterListDetailsColor","pointSize":"$XMediumFontSize","spacing":"$ReducedSpacing","dropShadow":"$DefaultDropShadow"}}}]},{"type":"ImageWidget","name":"Selection","fields":{"filename":"FrontEnd\\HD\\Final\\FrontEnd_Selector"}}]},{"type":"ImageWidget","name":"BackgroundCover","fields":{"rect":{"x":-75,"y":40}},"children":[{"type":"ButtonWidget","name":"CreateCharacter","fields":{"rect":{"x":240,"y":1775},"filename":"FrontEnd\\HD\\Final\\FrontEnd_ButtonMed","textString":"@d2r:strLaunchCharSelectNewX","onClickMessage":"FrontEndNavigation:ToCharacterCreate","textColor":"$LightButtonTextColor","text/style":"$StyleFEButtonText","hoveredFrame":3,"disabledFrame":2}},{"type":"ButtonWidget","name":"DeleteCharacter","fields":{"rect":{"x":315,"y":1940},"filename":"FrontEnd\\HD\\Final\\FrontEnd_Delete","tooltipString":"@d2r:strDelete","onClickMessage":"CharacterSelect:ShowDeleteCharacterModal","hoveredFrame":2,"disabledFrame":3}},{"type":"ButtonWidget","name":"ConvertCharacter","fields":{"rect":{"x":539,"y":1940},"filename":"FrontEnd\\HD\\Final\\FrontEnd_Convert","tooltipString":"@d2r:CharacterSelectPanelConvertCharacter","disabledTooltipString":"@d2r:CharacterSelectPanelConvertCharacter","onClickMessage":"CharacterSelect:ShowConvertCharacterModal","hoveredFrame":2,"disabledFrame":3}},{"type":"TabBarWidget","name":"Tabs","fields":{"rect":{"x":145,"y":-300},"tabCount":2,"filename":"FrontEnd\\HD\\Final\\FrontEnd_Tabs","inactiveFrames":[1,1],"activeFrames":[0,0],"disabledFrames":[2,-1],"textStrings":["@d2r:OnlineButton","@d2r:Offline"],"textStyle":{"pointSize":"$MediumLargeFontSize"},"activeTextColor":"$TabsActiveTextColor","inactiveTextColor":"$TabsInactiveTextColor","disabledTextColor":"$FontColorGray","tabSize":{"x":332,"y":71},"tabPadding":{"x":-1,"y":0},"onSwitchTabMessage":"CharacterSelect:SelectTab","automaticallyPositionIndicators":true,"enableControllerTabSwitching":false,"enableControllerTabIndicators":false,"visible":false,"active":false}},{"type":"ImageWidget","name":"ScrollBarBackground","fields":{"rect":{"x":765,"y":31}},"children":[{"type":"ScrollControllerWidget","name":"ScrollBar","fields":{"rect":{"x":5,"y":31,"width":24,"height":1658},"upArrowFilepath":"FrontEnd\\HD\\Final\\FrontEnd_ScrollUpBtn","downArrowFilepath":"FrontEnd\\HD\\Final\\FrontEnd_ScrollDownBtn","barFilepath":"FrontEnd\\HD\\Final\\FrontEnd_ScrollIndicator","viewName":"CharacterList"}}]}]}]}]};

function find(widget, name) {
  if (widget.name === name) return widget;
  for (const child of widget.children || []) {
    const found = find(child, name);
    if (found) return found;
  }
  return null;
}

const list = find(layout, 'CharacterList');
const background = layout.children.find((child) => child.name === 'Background');
const template = find(list, 'CharacterTemplate');
const listRect = list.fields.rect;
const rowHeight = template.fields.rect.height;
// The game's icons fill 431, 488 and 545 from the right; D2RLoader's plugin and
// mod icons take the free ones. The flag goes in the next slot to the left.
const step = 57;
const flagX = Math.min(...['LadderIcon', 'HardcoreIcon', 'ExpansionIcon']
  .map((name) => find(template, name))
  .filter((icon) => icon && icon.fields && icon.fields.rect)
  .map((icon) => icon.fields.rect.x)) - step;
const flags = [];
for (let row = 0; row < ROWS; ++row) {
  const rect = { x: listRect.x + flagX, y: listRect.y + row * rowHeight };
  for (const [name, sprite] of [['QuestFlag', 'QuestRewards_Flag'], ['QuestFlagSel', 'QuestRewards_FlagSel']]) {
    flags.push({
      type: 'ImageWidget',
      name: name + row,
      fields: { rect: { x: rect.x, y: rect.y }, filename: 'FrontEnd\\HD\\Final\\' + sprite },
    });
  }
  // One tooltip area over both flags (a hidden widget on top would swallow the
  // tooltip of the one below it).
  flags.push({
    type: 'FocusableWidget',
    name: 'QuestFlagTip' + row,
    fields: { rect: { x: rect.x, y: rect.y, width: 58, height: 74 }, tooltipString: '@quest-rewards:Flag' + row },
  });
}
// After the list, so the flags draw over its rows.
background.children.splice(background.children.indexOf(list) + 1, 0, ...flags);
D2RMM.writeJson(layoutPath, layout);
D2RMM.copyFile('hd', 'hd', true);
