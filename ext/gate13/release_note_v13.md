Dungeon Settlers with mods and multiplayer, for Mac and Windows. Made for Steam build 25846389.

Steam's game, your normal saves and your settings do not change. The modded game uses its own copy of the game and a copy of your saves.

The mod loader is at [github.com/AmfiLogos/ds_modloader](https://github.com/AmfiLogos/ds_modloader).

## Mac (Apple chip)

1. Download `Dungeon-Settlers-Modded-Mac.zip`.
2. Double-click the zip. You get "Dungeon Settlers Modded".
3. Move "Dungeon Settlers Modded" into your Applications folder.
4. The first time only: right-click the app and click Open. If macOS blocks it, open System Settings, click Privacy & Security, scroll down and click "Open Anyway". (Or in Terminal: `xattr -dr com.apple.quarantine "/Applications/Dungeon Settlers Modded.app"`.)
5. Keep Steam open and signed in.
6. The first start makes the modded game copy (about 10 seconds). On the title screen, click Mods to turn mods on or off.

## Windows

1. Download `Dungeon-Settlers-Modded-Windows.zip`.
2. Right-click the zip. Click "Extract All", then "Extract".
3. Open the folder and double-click `install.bat`. If Windows says "Windows protected your PC", click "More info", then "Run anyway".
4. When it says "Done", start "Dungeon Settlers Modded" from your desktop or the Start menu.

The `README.txt` in the zip has the full steps and the uninstall steps.

## What is new in v13

- Title screen: Multiplayer, then Mods, then a new button "Custom Modes".
- Hover cards open to the right of the panel. Mods change at the title screen only; Apply there is faster.
- Each world in the world lists shows its mods on the right of its row.
- Custom Modes: My Units, Unit Builder, Gallery and Test Fights.
- Bring Units (expedition and PvP): tabs "From a save" and "My Units", cards with icons, "New unit" opens the Unit Builder.
- Foreign Expedition plays in a neutral hub. Rules are on the host page.
- PvP v1: Multiplayer > Host a game > PvP. Everyone starts on the base map with their units.
- Mods shipped on Mac and Windows: Multiplayer, Custom Modes, Global cooldown, Hell difficulty (Hell is off until you turn it on).
- MelonLoader mods load on Mac (Apple chip) and Windows, like BepInEx mods. A mod set cannot hold both kinds.
- A first start turns on every shipped mod except Hell, on Mac and on Windows, so both have the same mod set.

## Known issues

- PvP with a Mac host and a Windows player: the Windows game can lose sync a few times per match and resyncs briefly. It fixes itself and the match ends the same on both.
- On Windows, an expedition with a My Units unit was tested on Mac only (the Windows test could open Bring Units, not tick a unit).
- The weapon-skill slot mod changes gameplay: every player needs it. It is not in this release.
- Not tested: a real Windows PC, Steam on Windows, playing over the internet. Windows was tested in a test setup only.

## Files

| File | SHA-256 |
|---|---|
| Dungeon-Settlers-Modded-Mac.zip | c1db5ddce7d4f3836fb9c74d8c1c8184d2150d609f650a21483b0c7d63578035 |
| Dungeon-Settlers-Modded-Windows.zip | 104e3022a514d22a865b1fcb12c104f9e133e8e14a365fc60f3ebf10bb6e0211 |
