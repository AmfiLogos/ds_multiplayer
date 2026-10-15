# Ping and camera awareness: design spec for lan_v10 (2026-10-14)

Owner request (PLAN.md, lan_v10): "show where each friend's camera is (in their colour), and a ping (mark a spot on
the map, seen by all, in your colour)". "Do it good": research what good co-op games do. A ping disappears when a unit
reaches the spot, plus a timeout. Research and design only: no code, no game copy, no pair was used.

Owner rules that bind this design (PLAN.md): game style (stock fonts, colours, frames); your OWN units keep the game's
default look, other players get a colour that each viewer can change for their own screen; tooltips in the game's
tooltip format, and "Show tooltips on hover" turns them off; everything we add can be moved (right-click + drag);
Escape closes our windows; no desync: camera and ping travel OUTSIDE the simulation, like chat (`P_CHAT` / `P_TYPING`
in `src/net/dsnet_internal.h`).

## TLDR
| Change | Impact | Cost | Failure point |
|---|---|---|---|
| Ping: middle-click tap or `B` at the mouse, ring in the player's colour, edge arrow when off screen, soft sound, chat line | Players can say "go here" without typing | 1 new packet, 1 overlay, 1 sound | unit-arrival test needs a unit grid-position hook (to find) |
| Ping clears when a unit arrives (radius 1 cell), else after 45 s of play | Matches the owner rule; no stale marks | per-frame check of a few units | ping on a wall cell: radius grows to 2 cells |
| Spam limit: 3 pings in 5 s, then 4 s wait; max 3 live pings per player; mute per player | No ping spam (top complaint in LoL threads) | small counters, server checks too | none |
| Friend's view: corner brackets in their colour + name, on screen only | You see where friends look without clutter | 1 packet, at most 5 per second, only when the view moved | the view size needs the friend's screen aspect (sent in the packet) |
| Players panel: click a friend's row = jump to their view; "Follow" toggle | Fast "where is Ben?" | uses the existing camera setter | today's follow camera travels as sim orders; move it out of the sim |
| Settings in the Players panel cog | Each player can turn each part off | 6 rows | none |

## 1. What good games do (research)

### 1.1 Pings
- **RimWorld Multiplayer (closest match: colony sim, top-down, lockstep).** Read in the local source copy
  `research/rwmt_Multiplayer`:
  - Ping at the mouse position: keybinding `MpPingKey`, default `Keypad0` (`Defs/KeyBindings.xml`), plus mouse buttons
    in the settings, default send = Mouse4, jump = Mouse3 (`Source/Client/Settings/MpSettings.cs:26-29`). A middle
    click counts only when it was NOT a drag (`LocationPings.cs`, `KeyDown`: `KeyCode.Mouse2` -> `MpInput.Mouse2UpWithoutDrag`),
    because middle-drag pans the camera. Same conflict as Dungeon Settlers (`WheelDragStart/Dragging/End`).
  - ONE ping per player: a new ping replaces that player's old one (`ReceivePing`: `pings.RemoveAll(p => p.player == ...)`).
  - Lifetime 10 s, fade during the last second (`PingInfo.cs:24,40`); the marker bounces again every 2 s (`:62-66`).
  - Sound: a small bell (`SoundDefOf.TinyBell`), also for your own ping (feedback that it was sent).
  - A red "pinged" alert lists who pinged; clicking it jumps the camera to the ping; the jump key cycles through live
    pings; Shift + jump key hides the alert (`AlertPing.cs`, `LocationPings.cs`).
  - Setting "Enable pings" (`MpSettingsUI.cs:76`). Pings are not drawn while the game catches up (`TickPatch.Simulating`).
  - Other players' cursors are sent 20 times a second, selections 5 times a second, unreliable (`PlayerCursors.cs`).
- **Factorio**: "Ping a map location" = Ctrl + Alt + left click; it creates a map ping AND a clickable link in the chat
  ([Factorio wiki, Controls](https://wiki.factorio.com/Controls)). The chat link is the useful part: you can click it
  later to jump there.
- **StarCraft II**: Ctrl + Alt + click (or Alt + G + click) pings on the minimap ([Liquipedia](https://liquipedia.net/starcraft2/Minimap)).
- **Age of Empires II**: "Signal Allies" flare button by the minimap; the flare shows on allies' minimaps for about
  3 seconds ([ageofempires.com, Controls](https://ageofempires.com/learn-to-play/control-resources-aoe2)).
- **Valheim**: middle click on the map pings; it lasts "a few seconds" in the base game; mods raise it to 10 s or more
  ([I'm Right Here mod](https://thunderstore.io/c/valheim/p/BloodRaven/ImRightHere/)). Players install mods to make
  pings last longer: a 3 s ping is too short for a "go here" mark.
- **Deep Rock Galactic**: a quick click pings, a held click leaves a waypoint that stays until you place another; one
  waypoint at a time ([Steam discussion](https://steamcommunity.com/app/548430/discussions/1/3758852249530917623);
  [wiki](https://deeprockgalactic.wiki.gg/wiki/Laser_Pointer)). The marked spot is visible through terrain.
- **Apex Legends**: context-sensitive ping (location, enemy, loot "dibs", agree / cancel); Respawn tested it with voice
  chat banned ([PC Gamer](https://www.pcgamer.com/au/apex-legends-ping-system-is-a-tiny-miracle-for-fps-teamwork-and-communication/);
  [GameRevolution](https://www.gamerevolution.com/?p=493241)). Lesson for us: a ping must work with no voice and no
  typing. The rich context wheel is not needed in a slow colony game.
- **League of Legends** (spam limits): 2 to 7 pings in a 5 s window by honor level, then a lockout of 6 s that grows;
  some ping types have their own 3-in-6 s cap ([LoL wiki, Ping](https://wiki.leagueoflegends.com/en-us/Ping)). Riot
  added a per-player "mute signals" button; players also ask for a "reduce" option (first ping shown, the rest hidden)
  ([RiftFeed](https://riftfeed.gg/more/lol-players-wish-riot-would-make-one-specific-change-pings);
  [mein-mmo](https://mein-mmo.de/en/lol-feature-gegen-nervige-mates,156093)). Valorant had a ping-spam macro that
  dropped frame rates; Riot fixed it with a rate limit
  ([devtrackers](https://devtrackers.gg/valorant/p/9ef1ee6f-player-abusing-ping-fps)).

What players like: one key, instant, seen by all, a sound, a direction when off screen, a chat line to click later.
What annoys them: spam (sound most of all), pings that vanish before you look, no way to mute one person.

### 1.2 Seeing a friend's view
- A SpringRTS developer: allies' cursors move too fast to follow and "may just end up confusing", while their camera
  views are useful ([SpringRTS forum](https://springrts.com/phpbb/viewtopic.php?p=165254)).
- RTS minimaps draw the own view as a white rectangle (StarCraft, 0 A.D.)
  ([Liquipedia](https://liquipedia.net/starcraft/Minimap); [0 A.D. manual](https://trac.wildfiregames.com/wiki/Manual_GUI_Overview)).
  Dungeon Settlers has no minimap, so the friend's rectangle goes on the world itself.
- RimWorld Multiplayer shows cursors (20 Hz) and selections (5 Hz), not views (`PlayerCursors.cs`).
- Locking your view to another player's camera is a known request (Capture Age); our viewer mode already has
  "Follow the owner's camera" (`src/ui_game.c` section 8h).
- Off-screen arrows: pin to the screen edge in the target's direction (angle from the screen centre, `atan2`), clamp
  inside a border, and keep them simple with good contrast
  ([Tuts+ gamedev](https://code.tutsplus.com/positioning-on-screen-indicators-to-point-to-off-screen-targets--gamedev-6644t)).

## 2. Recommended design

### 2.1 Input (checked against the game's key bindings)
The game's bindings, read from the owner's `UserSetting.json` (`KeySettingData.Bindings`) and named with
`enum KeyInputType` from the IL2CPP dump (`win/25846389/dump/dump.cs:191312`):
W A S D camera, 1-0 select unit, Shift + 1-0 add to selection, Tab and ` select all, Space pause, F1 / F2 / Left /
Right / Down / F12 speed, Q E R T (and 0) skills, Q / E also rotate a building, Y quick-slot item, K U I P summary
tabs, Z gather, X cancel assign, C multiple pick-up, V demolish, F / G / H behaviour options, F5 guide, comma tooltip
hold, Page Up console, Esc. Left Shift, Left Alt, Left Ctrl are bound as modifier actions. Mouse: left = select /
click, right = interact / move, wheel = zoom, middle-drag = camera pan (`MouseInputType.WheelDrag*`). Enter = our chat.
(Correction to the brief: Z X C V are work modes, not skills; skills are Q E R T.)

Free letters today: **B, J, L, M, N, O**. The player can rebind game keys, so the code must read the live bindings
(`KeySettingData`, to find in memory) and warn if our key collides.

| Action | Default | Why |
|---|---|---|
| Ping at the mouse | **Middle-click tap** (press and release, mouse moved under 6 px, under 300 ms) | RimWorld MP's own choice for the same pan conflict; one hand, no aim change. A middle drag stays the game's pan. |
| Ping at the mouse (keyboard, trackpad) | **B** ("beacon") | Mac trackpads have no middle button. B is free and next to the V/C work keys. |
| Jump to the newest ping | **Shift + B**; press again to cycle through live pings (newest first) | RimWorld MP has the same cycle key. Shift + B is free (Shift is only used with digits). |
| Remove your own ping | Ping again on it (within 1 cell), or right-click its marker | Undo for a misclick. |
| Jump to a ping | Left-click its off-screen arrow or its chat line | Factorio chat link and RimWorld alert behaviour. |

Not chosen: Alt + left click (Dota / LoL style, trackpad-friendly) because the game binds `LeftAlt` to an action whose
use is not known yet (to find). If Left Alt does nothing with a left click on the map, add Alt + left click as a second
default. Keypad 0 (RimWorld) is missing on Mac laptops.

Keys are blocked while the chat field is focused (`KeyInputDispatcher.IsTextInputFocused` hook, already in
`src/mpmenu.c`) and while any game popup or our window has focus. Clicks on UI never ping (the ping needs a world cell
under the mouse, the same test the game uses before a right-click order: to find).

### 2.2 Ping types
**One type in v10: "Look here / go here".** The owner rule (it clears when a unit arrives) makes it a movement mark.
Context types (enemy, item, "danger") need more art and more rules; leave them for later, and only if players ask.

### 2.3 Ping visuals (pixel art, game palette)
- **Colour**: a friend's ping uses that friend's colour as THIS viewer set it (the v10 per-viewer colours). Your own
  ping uses the stock accent pale gold `#d9be8f` (your own things keep the game's look; this is the game's own
  "selected" accent, ux_design_2026-10-13.md 2.2).
- **On the map** (world space, under units and over the ground, so it never covers a unit portrait or UI):
  - A ring 1.5 cells wide, 2 px line at screen scale, centred on the CELL (snap to the grid cell under the mouse,
    `RefreshMouseGridPos` gives the cell: to find).
  - Arrival animation: 3 ripples grow from 0.5 to 2 cells and fade, 0.6 s each.
  - While live: a small marker pin (12 x 16 px, the player's colour, 1 px dark outline `#222025`) above the cell that
    bobs 2 px every 2 s (RimWorld bounce rhythm). A soft ring pulse every 2 s (alpha 0.9 -> 0.4).
  - Name tag: the player's name in Lora 14 above the pin, in their colour, for the first 4 s, then only on hover.
  - Last 3 s of the timeout: fade to 0. On arrival clear: one ripple shrinks into the cell (0.4 s), so players see
    WHY it went away.
- **Off screen**: an arrow at the screen edge, pointing from the screen centre to the ping.
  - 28 px chevron in the player's colour, 2 px dark outline, 24 px inside the edge.
  - Kept out of HUD rectangles: the portrait bar, our Players panel, chat box, Teams button, top-right stock buttons
    (slide along the edge to the nearest free spot).
  - First 2 s: grows from 1.0 to 1.3 and back twice. Then still, alpha 0.85.
  - Several arrows on one side: stack with 4 px gap, never overlap.
  - Hover: game-format tooltip (see section 6). Left-click: jump the camera to the ping (keep zoom).
  - No distance number: tiles mean little here; the arrow size stays fixed.
- **Paused game**: pings still work (they are outside the sim). Ripples and pulses run on real time.

### 2.4 Sound
- One soft UI sound from the game's own set (to find: a stock notice or button sound; never a new loud sound), at the
  UI volume. Plays for every ping, also your own (send feedback).
- Per sender at most one sound per 1.5 s, even inside the spam limit.
- Setting: "Ping sound" on / off.

### 2.5 Chat line
- A system line in the chat: "**Ben** pinged a spot." (name in Ben's colour, rest `#a69085`). Clicking the line jumps
  the camera there while the ping is still live; after that the line is plain text. Your own pings write "You pinged a
  spot."
- The line counts as a chat line for the idle fade (12 s) like the others.
- Setting: "Ping lines in chat" on / off.

### 2.6 Lifetime and the owner's arrival rule
- **Clear on arrival**: a ping clears when a PLAYER unit (any owner, also free units; never enemies, animals or
  visitors) stands on the ping's cell or one of the 8 cells around it.
  - Only units that were OUTSIDE that area when the ping was placed count. Else a ping placed next to a unit would
    vanish at once.
  - If the ping's cell is not walkable (wall, water, building): the area grows to 2 cells (to find: a walkability test
    on the grid).
  - Each peer checks with its own copy of the game state (identical in lockstep). The check only READS unit grid
    positions once per frame and changes nothing in the sim. Two peers may clear a frame apart; that is fine.
- **Timeout**: 45 s of RUNNING game time (the timer stops while the game is paused, since no unit can arrive), with a
  hard cap of 3 minutes of real time. RimWorld uses 10 s and Valheim a few seconds, but those pings mean "look"; ours
  also means "go", and a walk across the base takes longer. Players in Valheim install mods to make pings last longer.
- **Max 3 live pings per player**: a 4th replaces that player's oldest one (RimWorld keeps only 1; 3 lets a player
  mark a short route or several targets).
- Other clears: the map changes (base <-> dungeon, a new dungeon floor), the sender leaves the game (fade 1 s), the
  sender removes it.

### 2.7 Spam limit
- Sender side: up to 3 pings in any 5 s. The 4th is refused: no sound, and a short local notice "Wait a moment before
  you ping again." (stock `HelpNoticeView` toast, the same as our other toasts). After 4 s it works again.
- Server side: the server drops pings from a sender above 4 in 5 s (a modified client cannot flood others).
- Receiver side: "Mute pings" per player (Players panel row, right-click menu). A muted player's pings, arrows, sounds
  and chat lines are hidden for this viewer only. (Riot added this after years of complaints.)

### 2.8 Friends' views (camera awareness)
- **Draw**: for each friend whose view overlaps yours, draw 4 corner brackets of THEIR view rectangle in their colour
  (each bracket 16 px long, 2 px thick, alpha 0.7), plus their name in Lora 14 just inside the top-left bracket.
  Corner brackets, not a full frame: a full rectangle across the map looks like a selection box or a building zone,
  and 3 friends would draw 12 long lines over the playfield.
- **Not drawn**: the friend's view is the same as yours (follow mode, or nearly the same: centres within 2 cells and
  same zoom) -> draw nothing; their view is fully off your screen -> no edge marker by default (it would sit on the
  edge all the time). Option "Show off-screen friends" adds a small eye mark at the edge (16 px, alpha 0.6, their
  colour), clickable to jump.
- **No cursors**: friends' mouse cursors are not shown. The SpringRTS developer and our own goal (no clutter) agree;
  the ping covers "this exact spot".
- **Jump**: left-click a friend's row in the Players panel = jump your camera to their current view (keep your zoom).
  Tooltip on the row says so.
- **Follow**: in the same row's right-click menu: "Follow Ben's view" / "Stop following". Any own camera input (W A S D,
  middle-drag, edge scroll, zoom, a unit hotkey that moves the camera) stops following, with the toast "Stopped
  following Ben." Viewer mode keeps its "Follow the owner's camera" button and uses the same code.
- **Smoothing**: the friend's rectangle moves to each new value over 0.2 s (it arrives at 5 per second at most).

### 2.9 Settings (Players panel cog, section "Co-op markers")
Each row is a game-style check box with a game-format tooltip. Local only, saved with our other panel settings.
| Setting | Default |
|---|---|
| Show pings | on |
| Ping sound | on |
| Ping lines in chat | on |
| Show friends' views | on |
| Show off-screen friends | off |
| Ping key | B (click, then press a key; refuses a key the game already uses, with the reason in the tooltip) |

"Mute pings" is per player in the Players panel, not in the cog.

## 3. Network (outside the simulation, like chat)
Model: `P_TYPING` in `src/net/dsnet_internal.h` and its relay in `src/net/dsnet_server.c`. Never stamped, never in
saves, hashes or replays. Relayed to every connected player except the sender. Not sent to a peer that is loading
(drop, do not queue: a stale view or ping is useless).

| Packet | Client -> server | Server -> clients | Rate |
|---|---|---|---|
| `P_VIEW` | u8 map_key, f32 cx, f32 cy, f32 half_w, f32 half_h (world units) | u8 from + the same | At most 5 per second, only when it moved by more than 0.25 cell or the zoom changed; one more 1 s after the last move; and once every 5 s as a keep-alive. |
| `P_PING` (name clash: today `P_PING` / `P_PONG` are the latency probe; call these `P_MARK` / `P_MARK_DEL`) | `P_MARK`: u8 map_key, u8 id (sender-local 0-255), i16 cell_x, i16 cell_y | u8 from + the same | Spam limit 2.7 |
| `P_MARK_DEL` | u8 id | u8 from, u8 id | when the sender removes it |

- `map_key` = map type (base / dungeon) plus the dungeon floor number, from SIM state, so every peer computes the same
  key (to find: floor index). The receiver ignores a view or mark whose key is not its current map.
- The view sends half_w and half_h, not the zoom alone: friends have different screen sizes (ortho size x aspect).
  Source: `CameraSetting` position and ortho size, already used in `src/ui_game.c` (`cam_setting()`, getters
  `0xD23584`, `0xD23590`).
- Arrival clear and timeout are computed by each peer; no packet. Peers agree because the sim is in lockstep.
- **Change needed**: today's viewer follow sends the camera as stamped zero-guid ui orders (`UI_ORDER_FOLLOW`,
  `UI_ORDER_CAMERA`, `src/ui_game.c` section 8h, every 0.5 s while it moves). They enter the command stream. Move
  them to `P_VIEW`: a follower just copies the leader's last `P_VIEW`. Follow state is local, no packet.
- Bytes: `P_VIEW` 1 + 1 + 1 + 16 = 19 bytes; 3 friends x 5 per second = 285 bytes/s in, trivial on TCP. On Steam P2P
  later, send `P_VIEW` unreliable (newest wins, like RimWorld's cursors) and marks reliable.

## 4. Edge cases
- **Base vs dungeon / floors**: all players are always on the same map (owner rule). During a transition (barrier,
  loading) hide all views and arrows; clear all pings on the map change; drop any packet with the old key.
- **Viewer mode** (no units on the run): a viewer CAN ping (it is talk, not an order) and is drawn like any player.
  A viewer's ping clears on arrival of any player unit, like all pings.
- **Disconnected player**: on leave, remove their view at once and fade their pings in 1 s. On "Keep playing" the host
  controls their units, but the away player has no view. Grey `#8a8088` is NOT used for pings (away means gone).
- **Rejoin**: nothing to restore; the next `P_VIEW` brings the view back.
- **Catching up / resync** (`dsnet_busy_mask`, loading): the peer that catches up hides its own overlays and sends
  nothing; others hide that player's view (its camera may jump during the load).
- **Game paused / story popup / Esc menu open**: pings and views keep working; the ping timeout stops while paused.
- **Over UI**: a middle tap or B with the mouse over any UI does nothing.
- **Escape**: nothing to close (no window). Escape does not clear pings.
- **Your own view**: never drawn.
- **Colour clash**: friends' colours come from the colour-blind-safe palette that avoids game status colours (v10);
  the pin's dark outline keeps it readable on any ground.
- **Screenshot / "hide UI"** (the game's `TEST_TurnOnOffUI` or a photo mode, if any): pings and views hide with the UI.

## 5. Hooks to find (do not guess addresses)
- Mouse world position -> grid cell (the game's `RefreshMouseGridPos` path).
- "Is the mouse over UI" test the game uses before a world click.
- Middle button down / up with no drag: the game's `WheelDragStart` threshold, so our tap and its pan never both fire.
- Live key bindings (`KeySettingData`) for the collision check; what `LeftAlt` does.
- Player unit list with grid positions; enemy / animal / visitor test; walkable test for a cell.
- Current map type and dungeon floor index from sim state.
- World -> screen point (Camera.main `WorldToScreenPoint`) for arrows and brackets; the HUD rectangles to avoid.
- A stock UI sound to reuse; a world-space sprite layer under units for the ring.
- Known already (in `src/ui_game.c`): `CameraSetting` get position `0xD23584`, get ortho `0xD23590`, set position
  `0xD22C54`, set ortho `0xD22DB0`.

## 6. Tooltip texts (game format: title + body)
| Element | Title | Body |
|---|---|---|
| Ping marker (hover) | Ben's Ping | Ben marked this spot. It goes away when a unit reaches it, or after 45 seconds. |
| Your own ping marker | Your Ping | Ping here again or right-click to remove it. It goes away when a unit reaches it. |
| Off-screen arrow | Ben's Ping | Click to move your camera there. |
| Friend's view brackets (name hover) | Ben's View | This is what Ben sees now. |
| Players panel row (friend) | Ben | Click to look where Ben looks. Right-click for more: Follow Ben's view, Mute Ben's pings. |
| Follow (menu item) | Follow Ben's View | Your camera follows what Ben looks at. Move your camera to stop. |
| Mute pings (menu item) | Mute Ben's Pings | Hide Ben's pings and their sound on your screen. Ben is not told. |
| Cog: Show pings | Show Pings | Show the spots other players mark. Mark a spot yourself: click the middle mouse button, or press B. |
| Cog: Ping sound | Ping Sound | Play a short sound when someone pings. |
| Cog: Ping lines in chat | Ping Lines in Chat | Write "Ben pinged a spot." in the chat. Click the line to look there. |
| Cog: Show friends' views | Show Friends' Views | Show the corners of what each friend sees, in their colour. |
| Cog: Show off-screen friends | Show Off-screen Friends | Show a small mark at the edge of the screen toward friends who look somewhere else. |
| Cog: Ping key | Ping Key | The key that marks the spot under the mouse. Shift + this key moves your camera to the newest ping. |
| Spam toast | (toast, no title) | Wait a moment before you ping again. |
| Follow stopped toast | (toast) | Stopped following Ben. |

## 7. Test plan (for the build lane)
- Unit: `P_VIEW` / `P_MARK` relay, server spam drop, map-key filter, leave clears (dsnet unit tests, no game).
- Pair: A pings, B sees ring + arrow + chat line; a unit walks in -> both clear; a unit already next to the spot does not
  clear it; timeout stops while paused; middle-drag still pans with no ping; B in the chat field types "b".
- Determinism gate: a run with heavy pinging and view traffic gives the same fingerprints as a run with none.

## Owner override (2026-10-14 01:55)
- No 45 s / 3 min timeout. A ping stays until a player unit reaches it or a player deletes it.
- B over an existing ping deletes it; B elsewhere places one (toggle). Middle-click still places.
