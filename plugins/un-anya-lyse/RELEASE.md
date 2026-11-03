# Un-Anya-lyse 1.0.1: release texts

## Patch notes

```
**Un-Anya-lyse 1.0.1**

**New**

* The enabled setting can be changed in game with I Hate TOMLs and applies at once: the line shows or hides the next time you talk to Anya.
* The download is now named un-anya-lyse.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.
```

## GitHub release

- Tag: `un-anya-lyse-v1.0.1`
- Title: `Un-Anya-lyse 1.0.1`
- Asset: `un-anya-lyse.zip`

Body:

```markdown
**Un-Anya-lyse** 1.0.1

### New
- The enabled setting can be changed in game with I Hate TOMLs and applies at once: the line shows or hides the next time you talk to Anya.
- The download is now named `un-anya-lyse.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.

### Install
Load `un-anya-lyse.zip` in D2RMM, or copy `d2rl-un-anya-lyse.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

# Un-Anya-lyse 1.0.0

## Patch notes

```
**Un-Anya-lyse 1.0.0**

**New**

* Anya has a "Remove Personalization" line in her dialog, just above Cancel.
* It opens her personalize window, which then only takes personalized items; confirming clears the item's name.
* Free and unlimited, with or without her quest reward, which is not used up.
* Texts in all 13 game languages.
* Works in single player and in games you host; the line is hidden in games you join.
```

## D2RLoader Hub

Name: `Un-Anya-lyse`

Summary:

> Anya can remove the personalization from an item: a "Remove Personalization" line in her dialog clears its name.

Description:

```markdown
**Un-Anya-lyse**: Anya can remove the personalization from an item.

### Features
- A "Remove Personalization" line in Anya's dialog, just above Cancel.
- It opens her personalize window with its own title and button. The window only takes personalized items; confirm and the item's name is gone.
- Free, as many times as you like, with or without her Betrayal of Harrogath reward. The reward is not used up.
- Texts in all 13 game languages.
- **I Hate TOMLs support.** Turn it on or off in game; it applies at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.
- Single player or a game you host. In games you join the line is hidden, since the host would refuse the request.

### Install
Load `un-anya-lyse.zip` in D2RMM, or copy `d2rl-un-anya-lyse.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/un-anya-lyse.toml`, written on first launch, or in I Hate TOMLs.
```
