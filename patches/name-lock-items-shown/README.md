# Name Lock With Items Shown

A D2RLoader memory patch (no plugin, no DLL) that fixes a vanilla D2R behaviour: the hold-click target lock ("name lock") stops working while item labels are shown.

Normally, when you click a monster and keep the button held, your character keeps attacking or casting at it even after the cursor leaves it. Melee follows the monster, ranged moves into range, spells stay on target. With Show Items toggled on, this lock never holds, so you end up attacking wherever the cursor is.

## Cause

While you hold the button, the game repeats the action on the locked unit. Each time, it checks "are item labels shown, and is the locked unit not an item?" When both are true, it clears the lock. The likely intent is to let a held click move on to item labels while you hold the Show Items key. With Show Items toggled on, that check is always true, so the lock is cleared on every repeat.

The patch changes one conditional jump into a plain jump, so that branch is always skipped. Clicking item labels still picks items up as before.

## Install

Get Name Lock With Items Shown from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer) and copy `name-lock-items-shown.json` to `<game>\d2rloader\patches\`. That folder is global, so D2RMM's mod installs don't wipe it. You can also put it in a mod's `mods\<mod>\d2rloader\patches\` folder.

On the next launch, `d2rloader\logs\d2rloader.log` lists it as `[PATCH] Name Lock With Items Shown — 1 change`. The `patches` console command shows its status too.

To remove the fix, delete the file.

## Compatibility

- Tested on Steam D2R 3.3.93787 with D2RLoader 1.3.1-beta.
- The patch carries the original bytes it expects. On any other build, D2RLoader finds different bytes and skips the patch, leaving the game unchanged. Battle.net 3.3.93847 is not checked yet.
- It touches the client action handler at RVA `0xFE3B0`. Some plugins hook that function's entry, which is fine because the patch is further inside it at `0xFE4FB`.

## Technical notes (3.3.93787)

| RVA | What |
| --- | --- |
| `0xFE3B0` | Client action handler (click / held-click repeat) |
| `0xF1900` | Get the slot's locked unit (returns null when not locked) |
| `0xF15C0` | Clear the slot's lock |
| `0xF1A60` | Lock the slot to a unit |
| `0x8AFC0` | "Item labels shown" flag (set by `0xCE450` when Show Items changes) |
| `0x77E10` | Controller mode (the branch is skipped in controller mode anyway) |

Original code at `0xFE4ED`:

```
call  0x77E10            ; controller mode?
test  al, al
jne   skip
call  0x8AFC0            ; item labels shown?
test  eax, eax
je    skip               ; 74 16  ->  EB 16 (always skip)
mov   rdx, [rsp+0x68]    ; locked unit
test  rdx, rdx
je    skip
cmp   dword [rdx], 4     ; unit type 4 = item
je    skip
mov   ecx, ebx
call  0xF15C0            ; clear the lock
skip:
```
