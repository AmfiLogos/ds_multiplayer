# Roadmap: Multiplayer mod

What we are working on, what comes next, and where you can help.

Dungeon Settlers Modded is made of three repos. Each item below is listed once, in the repo that does the work:

- **Multiplayer mod** (this repo): playing together, the Multiplayer page, the release downloads.
- **Mod loader** ([ds_modloader](https://github.com/AmfiLogos/ds_modloader), [its roadmap](https://github.com/AmfiLogos/ds_modloader/blob/main/ROADMAP.md)): the app, the Mods window, which mods each world uses, the installers.
- **Custom Modes** ([ds_custommodes](https://github.com/AmfiLogos/ds_custommodes), [its roadmap](https://github.com/AmfiLogos/ds_custommodes/blob/main/ROADMAP.md)): My Units, Unit Builder, Gallery, Test Fights, the Foreign Expedition hub, PvP.

## Now

- **v13.1.** PvP between a Mac host and a Windows player will stay in sync for the whole match. In v13 the Windows game could lose sync a few times per match. The fix is in Custom Modes, and the download comes from this repo's Releases page.
- **Multiplayer page in two groups.** "Host a game" comes first, with Your save, New save and Foreign Expedition. Then a line, then "Join a game", with Join by address and Join a friend on Steam. You can tell what each button does at a glance.
- **Plainer text.** We are rewriting the multiplayer menus and hover cards in plain words. Some of the current text reads like a programmer's notes.

## Next

- Nothing new is planned for the multiplayer mod yet. The Now list comes first.

## Later / ideas

- PvP v2 (fog of war, units that come back after they die) is a Custom Modes item. See the [Custom Modes roadmap](https://github.com/AmfiLogos/ds_custommodes/blob/main/ROADMAP.md).

## Needs help / testing

Have you tried one of these? Please open an issue and tell us what happened.

- **A real Windows PC.** We have only tested Windows in a test setup on a Mac.
- **Steam on Windows.** Nobody has tested the Windows version with a real Steam install yet.
- **Playing over the internet.** We have not yet tested two players on different networks.

## Known issues

- **The weapon-skill slot mod is left out of the download.** It changes gameplay, so every player in the game would need it. Details are on the [mod loader roadmap](https://github.com/AmfiLogos/ds_modloader/blob/main/ROADMAP.md).
