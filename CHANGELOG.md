# Changelog

What changed in each version, newest first. Downloads: the GitHub releases of this repo.

## Multiplayer (multiplayer)

### 0.1.27 (2026-10-25)

Chat:
- You can use the chat without typing. The mouse wheel over the chat lines, a click on a chat line, or Enter when you
  cannot send opens it to read. The game keeps its keys, so Space still pauses. Escape or the red X closes it.
- The chat has a scrollbar, with Top and Bottom buttons that jump to the oldest and the newest messages.
- "/clear" or the bin button empties the chat on your screen only. Nothing is sent to the other players.
- Without a multiplayer session, the chat is read only: it has no text field.
- Drag the chat by its title or frame with the left mouse button to move it (the right-click drag still works). Drag its
  bottom-right corner or its bottom edge to change its size. The chat keeps its place and size.
- The chat keeps 3000 lines (before: 100).
- In single player, the chat shows lines from mods, for example the DPS log of the Units Laboratory Test Fights. These
  lines stay on your screen, and clicks go through them.
Escape:
- Escape closes the multiplayer windows: PvP Match, Bring Units, the PvP result, Expedition Results and the multiplayer
  message boxes. One press does the window's own Close, Back, Cancel or OK, and the game does not also act on it.
- Escape closes the spectator's mini overview, as its X does.
- When a loader message or another mod's window is over a multiplayer page, one Escape closes only that window. The page
  does not also go back.
Players and joining:
- Your player name is never the name of your computer account. Without Steam, the name is the one you typed, else
  "Player" and four random digits. An old name that was filled in from your account is removed once.
- Get the host's mods: a mod that is on but did not start on your computer (for example after a crash at start) gets its
  own line with the reason. The "Get the host's mods" button does not show for it, because a restart cannot fix it.
  Before, you restarted and the host refused you again. Texts in 10 languages.
Windows and boxes:
- The multiplayer windows close with the game's red X: PvP Match, its Bring Units list, the PvP result, Bring Units,
  the Teams window, the Steam friends picker and Expedition Results at the title.
- The PvP windows and the Teams window move with a right-click drag and keep their place.
- The multiplayer windows, the Players panel, the Teams button and the other multiplayer boxes follow the game's UI
  Scale. A window that would not fit is drawn smaller, so it stays on the screen.
- The Bring Units window shows above the Teams button. The multiplayer panels under it take no clicks.
- The "Keep a single-player copy of this world?" box waits until the world shows, so it is never on a black screen.
- In the Foreign Expedition hub lobby, the rules cog is hidden: the hub rules are set on the hub host page.
- Box buttons grow to fit a long label, for example "Get the host's mods".
- "Remove Tag" on a multiplayer world in Load Campaign is never cut: a long world name is cut with "..." instead.
- Host from a Save: a first-time host (no world with the Multiplayer tag yet) sees every world.
- The host's Esc menu button says "Turn Off Multiplayer". A guest keeps "Leave Multiplayer".
- English buttons, rows and box buttons use Title Case.
- Error lines start with a capital letter and end with a full stop, in every language.
- Each language uses its own word for the hub.
PvP:
- Start Match with a team that has no unit or no player keeps the window open and says why, in red.
- A guest's PvP window shows the host's rules and teams.
- The match messages (countdown, "Fight!", preparation) show on a dark plate under the score panel.
For mods:
- Chat lines that show only on your own screen, also in single player (multiplayer API 3 and 4).

### 0.1.26 (2026-10-25)

- Foreign Expedition hub: the hub no longer leaves a save, a tag or a world line behind.
- A world that is new to multiplayer asks once "Keep a single-player copy of this world?" before the session. Yes makes
  a new world "<name> (solo)" from its newest save, with the same mods.
- A guest with Hell difficulty joins a host on a Hell world with no refusal and no restart. A guest with other Hell files
  is refused as before.
- Clicking units no longer forces a full resync of the game.

### 0.1.25 (2026-10-25)

- Bring Units (hub lobby, the text window): skill trees show in the game's own words and in your language (for example
  "Water" for Water Magic), as in the character window's skill tab, not as raw text keys.

### 0.1.24 (2026-10-24)

- Bring Units (hub lobby, the text window): a unit's gear and skill trees, and a save's note, stay inside their card in
  every language. A line that is too long ends in "..."; the card's tooltip shows it in full.

### 0.1.23 (2026-10-24)

- The texts of the multiplayer windows now look like the game's own menus: the same text style as the title menu,
  Settings and Load Campaign, in every language (Multiplayer page, host page and rules, lobby, Players panel, chat, Teams,
  Esc menu additions, PvP Match).
- Window titles and text fields keep the plain style, as in the game.

### 0.1.22 (2026-10-24)

- The "Show multiplayer worlds" box under the Load list is gone. Dungeon Settlers Modded v0.1.23 adds a filter panel to
  the Load Campaign list: tick Multiplayer there to see your multiplayer worlds.
- With an older Dungeon Settlers Modded (v0.1.22 or before), the box stays where it was.
- Multiplayer > Host a world keeps its "Show single-player worlds" box.

### 0.1.21 (2026-10-21)

- One multiplayer mode: "Hub: Co-op or PvP". The separate PvP entry and its "PvP Base" world are gone.
- The hub is one shared map: a big ready base with everything unlocked. Go on co-op expeditions together, or start PvP
  matches from the Esc menu (Esc > PvP Match).
- The PvP Match window now has every PvP rule: Floor, the six enemy visibility rules and the presets (Save as preset...,
  Delete preset). The PvP map "This settlement" means a fight on the hub base.
- Bring Units from the PvP bar during the preparation works in the hub too.
- After a match, "Back to the Hub" keeps everyone in the hub, as it was before the match.
- All texts in the 10 languages of the mod.

### 0.1.20 (2026-10-21)

- The Foreign Expedition hub keeps the Guild Merchant: you can trade with it in the hub again.

### 0.1.19 (2026-10-21)

- The Foreign Expedition hub is now a full, finished base: every building, storage and decoration, every research done
  and every region open. It has no units of its own: the units the players bring arrive at the start.
- The hub map and the PvP map now come inside the mod. Nothing is downloaded any more. If the mod has no map for your
  game version, you see a message that asks you to update the mod.

### 0.1.18 (2026-10-21)

After a review of every multiplayer window in English, German, Russian and Japanese, at five window sizes and UI scale 1.5:
- The Teams window, lobby, chat, Players panel and Esc menu fit inside the game window at every size and UI scale.
- Teams: the "take a free unit" hint is in the empty area, not on the unit slots. Unit names under the portraits show.
- Chat: one plain background; long lines wrap inside the box.
- Lobby and rules: texts no longer run edge to edge in buttons or touch the arrows.
- Recent worlds: rows grow with their players line; the players line no longer runs under your units.
- PvP: score panel labels inside the panel, vote box and check boxes lined up, a clearer message when a team has no units
  yet.
- PvP: a player who joins goes to the smaller team (before, a friend could land on Blue too, and every Start was refused).
- Chinese, Japanese and Korean text is never smaller than 14 pt (design size), other languages never smaller than 12 pt.
- Better German, Russian and Japanese wording.

### 0.1.17 (2026-10-21)

- If your connection to the host drops for a short time, you stay in the world and the game joins again by itself. You
  get your units back. Click Cancel to go to the title.
- Chat: long messages wrap inside the chat box and no longer overlap. Accents, Cyrillic, Chinese, Japanese and Korean
  letters work. The mouse wheel over the chat scrolls the chat, not the map. You can chat in the lobby before the game
  starts. The chat shows above the game's windows.
- You see the units your friends select: they glow in the colour of the friend's name.
- When a unit goes to another player in Teams, it leaves the selection of the player who lost it.
- A player who joins with no unit sees the Teams window with a short hint.
- When one player pauses, the others can still give orders to their own units. The orders run when the game continues.
- Return scroll: every player votes first (60 seconds, no answer = yes). The game pauses while any vote is open, also in
  "No pause" mode.
- Clicking a friend's unit to look at it no longer blocks his orders.
- The lobby has Quit for guests, and Quit or "Turn off multiplayer" for the host.
- Steam lobby: the address block is hidden behind a "Show address for friends without Steam" button. It shows one
  internet address and one home network address.
- Bring Units window: about 8 units fit on a page, and a click anywhere on a card selects it.

- Bring World lists your normal game saves too.
- You can bring units in the PvP lobby and during the preparation (Bring Units button).
- With Units Laboratory installed, you can bring units from My Units or build a new one. Other players do not need Units
  Laboratory.
- Nobody starts the fight alone: a player proposes, everyone agrees, then a short countdown starts. The game pauses while
  a proposal is open.
- Each team sees only its own units and what they see. Enemy health bars, portraits, status and windows are hidden.
  Clicking an enemy only shows its info, like a monster.
- PvP rules: the host can show or hide enemy vision, windows, portraits, health bars, status and view range (all hidden
  by default). Rules are kept between games and can be saved as presets.
- Maps: random maps with random start points, and dungeon maps (dungeon 1 or 2, a floor, or the boss room).
- Your orders win: hold position (G) stops a unit at once and it holds into the fight (a unit outside its team's half
  first walks into it), and an enemy attack no longer replaces your attack order.
- When everyone agrees to fight, the game waits until both teams are in place ("Taking places...", at most 20 seconds),
  then counts down. A timer shows the preparation and the countdown. A round end screen shows who won the round and when
  the next round starts.

### 0.1.16 (2026-10-20)

- Windows: Steam multiplayer. Host and join through Steam, invite Steam friends from the game and accept their invites.
  Mac and Windows players play together through Steam invites.
- PvP and Foreign Expedition are now part of Multiplayer. Units Laboratory is not needed to host or join them. You bring
  units from your own saves.
- Units Laboratory never blocks a join.
- The lobby and the Players list show how each player is connected: Steam or Direct. A guest sees how they are connected
  to the host.
- When you host by address, the game asks your router to open the host port (UPnP). If the router does not, the lobby
  tells you to open the port on your router to this computer (port forwarding).
- A friend can join while the host has a game window open. The friend waits until the host closes it.
- The Bring Units window shows your units as cards.
- Foreign Expedition: the loot you find comes home with you.
- A host who is alone in the lobby can bring units before friends join.
- Windows: the lobby line about the Windows network question is no longer cut off.

### 0.1.15 (2026-10-20)

- New setting "Host port" (in the Mods window settings of Multiplayer): the port your computer listens on when you host.
  Type the port that your router sends to this computer. Default: 47777. Joining is not changed.
- The lobby now shows your internet address, the one friends outside your home type to join. Copy copies it. A "Same
  home" line under it shows the address for players on your own network. If your internet provider shares one address
  between many homes, the lobby says so.
- Mac: Invite opens a list of your Steam friends in the game. Pick a friend to send the invite. Your friend must open
  Dungeon Settlers Modded first.
- Cancel on the multiplayer new-campaign screen really cancels: a single-player game you load after it is no longer
  hosted.
- The tutorial cannot be hosted in multiplayer. A message says that the tutorial is single-player only.
- When the host leaves a world that nobody else is in, the world keeps running.

### 0.1.14 (2026-10-20)

- Listed as a launcher feature in the Mods window: it is no longer part of a world's mod set (needs the v0.1.17 mod
  loader for the new grouping).

### 0.1.13 (2026-10-19)

- Mods can keep their own data; it is synced when you join a host.
- A host whose game asks a question when the world loads (Lock Consecration) can now let players join.
- For mods: a random that is the same for every player, and chat lines (multiplayer API 2).
- Stricter test-only tools.

### 0.1.12 (2026-10-18)

- If your mods differ from the host's, "Get the host's mods" downloads the host's official mods and settings for you,
  then you join again. The box names each setting that differs.
- Mod settings are part of the join check.
- Messages about lent units name the unit, even when it is on another map.
- The expedition party size follows the game's own limit (needed for ZET-U's 8 units).
- Windows: the Foreign Expedition hub now hides the Guild Contact button when recruiting is off, as on Mac.
- Test tools are now only active in test builds.

### 0.1.11 (2026-10-18)

- Fixed: a crash in multiplayer after entering the dungeon more than once.

### 0.1.10 (2026-10-18)

- The Players panel opens above the Teams window.
- The "Watching" box has a Hide button.
- Hosts see their address in the Players panel, with a Copy button.
- Players watching an expedition now see it.
- Fixed: Windows PCs with ARM processors went out of sync with other players in the same game.
- On Mac, joining a host on the local network works again. If macOS blocks it, the game says so and opens the right
  setting.
- Multiplayer page: a short second line under the Foreign Expedition and PvP buttons.
- The title menu no longer jumps after Multiplayer > Back.

### 0.1.9 (2026-10-17)

- All texts of the mod follow the game's language: English, French, German, Spanish, Portuguese (Brazil), Russian,
  Japanese, Korean, Chinese (Simplified) and Chinese (Traditional). Chinese, Japanese and Korean use the game's own
  fonts. Players with different languages can play together.
- Player names in Chinese, Japanese, Korean or Cyrillic letters show everywhere (lobby, player list, chat).
- On the multiplayer page, all the buttons have the same size.

### 0.1.8 (2026-10-17)

- Joining a game that is already running is a little faster (with Fast join on: about 3.6-4.1 seconds instead of 4.4-4.6
  on our test Macs).
- A full resync after a desync is about 1 second faster.
- Less work every frame while a multiplayer game runs.

### 0.1.7 (2026-10-17)

- Fast join is on by default.
- More network fixes from a code review.
- When two players have different versions, the join message says which versions they are.

### 0.1.6 (2026-10-16)

Changes since bundle-v0.1.16:
- A player who joins your game can no longer crash the host by sending an oversized command.
- A player who never finishes joining, or a connection that stops responding, is dropped after a time limit instead of
  stalling the game.
- If the host's save can't be sent while you join, you go back to the menu instead of waiting.
- Hosts of big worlds no longer drop out of their own game at the regular sync check.
- A player with a long mod list doesn't crash the join anymore.

## DSMP Bridge (dsmp-bridge)

### 1.1.0 (2026-10-19)

- A library for BepInEx mods (Mac and Windows, one DLL): the multiplayer session, actions that run on every player's
  game, a random that is the same for every player, and chat lines. It is hidden in the Mods window and installed with
  the mods that need it. Every player in a game needs the same version. It needs Multiplayer 0.1.13.

## BepInEx for Mac (bepinex-mac)

### 0.1.3 (2026-10-16)

- BepInEx 6 (IL2CPP) for Mac with an Apple chip, packaged for Dungeon Settlers Modded. The app downloads it by itself
  the first time you turn on a BepInEx mod. Same as 0.1.2 with a licenses folder added (the license text of BepInEx and
  every library it ships).

### 0.1.2 (2026-10-16)

- Same as 0.1.1, with one library rebuilt so that no file holds local build paths. Licenses are in the licenses folder
  inside the zip.

### 0.1.1 (2026-10-15)

- Removed. Use bepinex-mac 0.1.2 or newer, or the bundle-v0.1.13 release.

### 0.1.0 (2026-10-15)

- Removed. Use bepinex-mac 0.1.2 or newer, or the bundle-v0.1.13 release.

## MelonLoader for Mac (melonloader-mac)

### 0.1.1 (2026-10-16)

- MelonLoader for Mac with an Apple chip, packaged for Dungeon Settlers Modded. The app downloads it by itself the first
  time you turn on a MelonLoader mod. Same as 0.1.0 with a licenses folder added (the license text of MelonLoader and
  every library it ships).

### 0.1.0 (2026-10-16)

- MelonLoader for Dungeon Settlers Modded on Mac (Apple chips). The Mac app downloads it the first time a MelonLoader mod
  is turned on, checks it and installs it in its own folder.

## Old bundles (bundle)

### 0.1.16 (2026-10-16)

- You can now add a mod while the game is running. Click "Open mods folder" in the Mods window and drop the zip in. Then
  open the Mods window again, or click Refresh, and the mod is in the list. Click Apply to turn it on.
- If the zip isn't a mod, the game moves it aside and the Mods window says so.

### 0.1.15 (2026-10-16)

- You install it with a disk image (.dmg) on Mac or a Setup.exe on Windows. The zip and install.bat are gone.
- The Multiplayer page shows "Host a game" and its three choices at the top, then a line, then "Join a game".
- Mods belong to a world now, not to one save file. Every save of that world uses them, and the Mods window shows the
  world's name.
- The hover card on a world lists all of its mods.
- We rewrote the menu text, hover cards and mod descriptions in plainer words.
- Every mod also comes as its own zip. Click "Open mods folder" in the Mods window and drop the zip in, and the game
  picks it up the next time it starts.

### 0.1.14 (2026-10-16)

- PvP with a Mac host and a Windows player no longer loses sync during the match.
- New app ID (com.amfilogos.ds-modded). The first start of bundle-v0.1.14 on Mac asks again for the permissions the app
  needs. Your mods, groups and saves stay as they are.
- Mods list their author as AmfiLogos.
- Everything from bundle-v0.1.13 is in it: Custom Modes (My Units, Unit Builder, Gallery, Test Fights), Bring Units, the
  Foreign Expedition neutral hub, PvP v1, Global cooldown and Hell difficulty on Mac and Windows (Hell is off until you
  turn it on), MelonLoader and BepInEx mods.

### 0.1.13 (2026-10-16)

- Replaced by bundle-v0.1.14.

### 0.1.11 (2026-10-15)

- Spectator mode.
- Shared fog of war rule.
- Mod check when you join.
- Foreign Expedition.
- PvP match.
- Faster late join.

### 0.1.10 (2026-10-15)

- Multiplayer for Dungeon Settlers on Mac (Apple Silicon). The Steam version of the game is required.
