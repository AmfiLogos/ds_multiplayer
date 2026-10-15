# Multiplayer tooltips, lan_v9 (lane uxdesign, 2026-10-14)

Owner correction (PLAN.md, 2026-10-14 00:45): every multiplayer feature, button and setting we added gets OUR OWN explanatory tooltip in the SAME FORMAT as the game's own tooltips (title + body, the game's tooltip window). "Show tooltips on hover" (panel cog) turns all of ours off.

## 1. How the game shows its tooltips (Mac IL2CPP dump ~/ds_skill_truth/dump_25846389/dump.cs, build 25846389; the win/ dump has Windows addresses)

| Piece | What it is | RVA / offset |
|---|---|---|
| `Refactor.UI.TooltipUI` (StaticUI) | the one tooltip layer; pools one tooltip prefab per type | class |
| `TooltipUI.ShowTooltip(Guid id, TooltipType type, string key, RectTransform callerRect, TalentType, TooltipContext)` | shows a tooltip placed next to the hovered element (how the stock buttons do it) | 0xF4A244 |
| `TooltipUI.ShowTooltipAtScreenPosition(Guid, TooltipType, string key, Vector2 screenPos, TalentType, TooltipContext)` | the same at a screen point | 0xF4A648 |
| `TooltipUI.HideToolTip(Guid)` | hides it | 0xF4AA04 |
| `TooltipType.ETC = 0` | the title + description tooltip (HUD chips, window info icons, building options) | enum |
| `Tooltip_ETC` | `_titleText` +0x180, `_subtitleText` +0x188, `_subtitleLayout` +0x190, `_desText` +0x198, `_flavorText` +0x1A0 | class |
| `Tooltip_ETC.Show(string key)` | reads `TEXTKEY_<key>_NAME` (title) and `_DESC` (body) from the language table | 0xF3D8B8 |
| `Tooltip_Base.HideLastDivider` | hides the divider under the last section | 0xF379A8 |
| `TooltipContext.HideHoldLayout` | struct field +0xA0: no "hold the key" line | struct |
| (other types) `SimpleText = 12` (one line, used by `TooltipFeedback.SetSimpleText`, e.g. the stock Save button), `TextFit = 1000` | single text | |

Stock tone, from the language table (`TEXTKEY_*_DESC`), for our writing:
- Population: "Represents the number of expedition members. As [Shared_ClanRank] rises, it can ..."
- Game Option Panel: "You can adjust in game settings like language and sound settings in this window."
- Hold Position: "Holds position and waits for orders. Outside combat, idle without moving."
- Recruitment Reset Timer: "Time remaining until reset. The current recruit list will change after this time."

So: a short noun-phrase title, then 1-2 plain sentences in the present tense that say what it is, then what it does or how it works. No exclamation marks.

## 2. The shared helper (built): `src/ui_tip.c` + `src/ui_tip.h`

```c
void game_tip_show(const char *title, const char *body);                      /* next to the mouse */
void game_tip_show_rect(const char *title, const char *body, void *rect);     /* next to an element (stock placement) */
void game_tip_hide(void);
void game_tip_frame(void);                 /* once per frame: hides 0.2 s after the last show call */
char *ui_tip_exec(const char *cmd, const char *line);   /* test socket "ui_tip" */
```
- How it works: it asks the stock `TooltipUI.ShowTooltipAtScreenPosition` (0xF4A648) for an ETC tooltip with a marker key. One hook on `Tooltip_ETC.Show` (0xF3D8B8) runs the stock `Show` with a real key (so every stock layout step runs: width per language, dividers, sections), then writes our title and body into `_titleText` / `_desText`, and turns off the subtitle, the flavour line and the ";; Pin" hold chip (our tooltips hide when the pointer leaves, so they cannot be pinned).
- Placement: for `game_tip_show_rect` the point is computed here: the element's bottom-left corner, 2 px right and 2 px down (the stock `OFFSET_X` / `OFFSET_Y`), mapped to the screen through the element's own canvas. The stock callerRect overload (0xF4A244) maps through the TOOLTIP's canvas, which put tooltips for our HUD and Teams elements in the wrong place, so it is not used.
- A new title or body replaces the shown tooltip at once: the old stock session is hidden first, because the stock session otherwise keeps its first text.
- Layering: the TooltipUI canvas gets override sorting 32050, above our windows (Teams 32000), so lanes need not raise it themselves.
- Build guard: `int game_tip_ready(void)` = 1 only when the first instruction of each game function matches build 25846389 (Mac) and the scene has its TooltipUI. Every call checks the same guard, so a wrong build means no tooltip, never a call into the wrong code. (The first version used Windows addresses from win/25846389/dump and crashed; fixed 01:16.)
- Call `game_tip_show_rect` every frame while the pointer is over the element (same title and body = no work). Pass the element's RectTransform for the stock placement.
- It honours "Show tooltips on hover": it uses an exported `int ui_tooltips_enabled(void)` from ui_game.c when present (dlsym). Otherwise it reads `mp_ui.json` once a second, the same way mpmenu does today.
- Registered in `src/build.sh` (windows2 told). Not wired yet: the file owners wire their calls (section 4).
- Owners must do two things:
  - **uxgame (ui_game.c):** add `game_tip_frame()` to `ui_frame()`; export `int ui_tooltips_enabled(void) { settings_load(); return uiset.tooltips; }`; add `ui_tip_exec` to the `ui_exec` chain for the `ui_tip` test command.
  - **Everyone:** replace the custom boxes (`ui_tooltip` in ui_game.c, the Rules `tip_box` in mpmenu.c, the Teams rules box in ui_teams.c) with `game_tip_show_rect(title, body, rect)`.
- Proof frames: section 5.

## 3. Inventory: every multiplayer element, with its tooltip

Facts come from the code (mpmenu*.c, ui_game.c, ui_model.c, ui_teams*.c, mp_game.c, net/) and PLAN.md. "Owner" = the lane that wires the call. Hover target = the element the tooltip is attached to.

### 3.1 Title screen and Multiplayer page (mpmenu.c, uxmenu)
| Element | Title | Body |
|---|---|---|
| Title button "Multiplayer" | Multiplayer | Play one world together with friends. Every player runs the game; only the orders travel, so all screens stay the same. |
| "Host a game" | Host a Game | Open one of your worlds, or a new campaign, to other players. The world is saved on your computer. |
| "Join by address" | Join by Address | Join a friend's world with their address (ip:port). Use it when Steam is not available, or on the same home network. |
| "Join a friend on Steam" | Join a Friend on Steam | Lists the Steam friends who are hosting a world right now. Click one to join. Needs the Steam version of the game. |
| A friend row "Join the owner's world" | Join the owner's World | Joins this friend's world through Steam. The world loads from the host; your own saves are not touched. |
| "Recent Worlds" panel (title) | Recent Worlds | Worlds you played in multiplayer, newest first, with the day, the players and your units. |
| A Recent world row | Reconnect | Joins this world again the same way as last time (you hosted it, Steam, or the IP address). Your units come back to you. |
| Row "x" | Remove from List | Removes this world from Recent Worlds. The world itself and its save are not deleted. |
| Row "Your units" | Your Units | The units you controlled in this world when you last left. They stay reserved for you until you come back. |
| Newer / older arrows | More Worlds | Shows the next page of Recent Worlds. |
| "Back" | Back | Returns to the previous menu. |

### 3.2 Host page (mpmenu.c, uxmenu)
| Element | Title | Body |
|---|---|---|
| "Your name" field | Your Name | The name other players see: in the player list, above your units and in chat. |
| "Rules" box (cog) | Rules | How this multiplayer world plays: pause, speed, gear, units and events. Saved with the world; new worlds start with your last rules. |
| "Host from a save" | Host from a Save | Pick one of your saves to open to other players. The world stays on your computer, and only you save it. |
| "Host a new campaign" | Host a New Campaign | Start a new campaign and open it to other players as soon as it is created. |

### 3.3 Rules panel rows (host page and lobby; mpmenu.c, uxmenu). Values as in `mpm_rule_value`.
| Row | Title | Body |
|---|---|---|
| Pause | Pause | Who can pause the game. Anyone can pause: one player pauses it for everyone. Pause by vote: it pauses only when every player votes for it. No pause: nobody can pause it. Events, the Portal screen and a lost player still pause in every mode. |
| Speed-up | Speed-up | On: players can play at Fast speed when every player asks for it. Off: the game stays at normal speed for everyone. |
| Gear | Gear | Who may change gear. Shared, own units: everyone uses the settlement storage, and each player equips only their own units. Shared, any unit: anyone equips any unit. Own storage: each player has their own items. |
| Units per player | Units per Player | The most units each player can control at the same time (1, 2, 3, 4 or 6). The host can change it during play. |
| Population | Population | How the clan's population limit is shared. Shared: one limit for everyone, first come. Per player: each player has the full limit. Split equally: the limit is divided between players. Multiplied: limit times players, shared. A hire needs room here and under Units per Player. |
| Event choices | Event Choices | Who answers an event. Owner of the unit: the player who controls the unit in the event. Host only. First to answer. Majority vote: the host breaks a tie. |
| Admin mode | Admin Mode | On: the host can order every unit and release the units of absent players. Off: the host is a normal player for other players' units. |
| "Done" | Done | Closes the Rules. |
| Note "Saved with this world..." | (no tooltip: it is already an explanation) | |

### 3.4 Join page (mpmenu.c, uxmenu)
| Element | Title | Body |
|---|---|---|
| "Host address (ip:port)" field | Host Address | The address the host shares from their lobby, for example 192.168.1.20:47777. Outside your home network, the host must open the port on their router. |
| "Your name" field | Your Name | The name other players see: in the player list, above your units and in chat. |
| "Connect" | Connect | Connects to the host and loads their world. It stops trying after 8 seconds without an answer. |
| "Cancel" (while joining) | Cancel | Stops joining and returns to the title. |

### 3.5 Lobby window (mpmenu.c, uxmenu)
| Element | Title | Body |
|---|---|---|
| "Players (n)" | Players | Everyone in this world. The bars show each player's connection. |
| A player row | (name) | The colour is this player's colour everywhere: names above units, portraits, Teams and chat. |
| "Steam invite" | Steam Invite | Opens Steam's invite window so you can invite friends without sharing your address. Needs the Steam version. |
| Address + "Copy" | Copy Address | Copies your address (ip:port) so you can send it to a friend. They use Join by Address. |
| "Rules" (cog) | Rules | Shows how this world plays. Only the host can change them. |
| "Start" (host) | Start | Starts the game for everyone in the lobby. Players who join later catch up with the running game. |
| "Leave multiplayer" / "Leave" | Leave Multiplayer | Host: closes the world for everyone, and the world stays saved on your computer. Guest: you leave and return to the title; your units stay reserved for you. |

### 3.6 Players panel and its cog (ui_game.c, uxgame)
| Element | Title | Body |
|---|---|---|
| Header "Players (n)" | Players | Everyone in this world and their connection. Right-click and drag to move this panel. |
| Collapse arrow | Show / Hide | Shows or hides the player list. The header stays. |
| Cog | Settings | Your own display settings for multiplayer. The host also sets Units per Player and Admin Mode here. |
| Other units see-through | Other Units See-through | Makes the units you do not control see-through (0 to 100%, in steps of 25%), so your own units stand out. Only on your screen. |
| Names above units | Names Above Units | Shows the owner's name above units. Others: only other players' units. All: every unit. Off: none. |
| Show tooltips on hover | Show Tooltips on Hover | Shows these explanations when you point at a multiplayer button or setting. |
| Panel size | Panel Size | Normal or Compact. Compact makes the Players panel and the Teams button smaller. |
| Chat messages fade after | Chat Fade Time | How long a chat message stays on screen (3 to 120 seconds). Press Enter to see the recent messages again. |
| Show join / leave lines | Join and Leave Lines | Shows a chat line when a player joins, leaves or reconnects. |
| Chat | Chat | Turns the chat on or off on your screen. |
| Units per player (host) | Units per Player | The most units each player can control (1, 2, 3, 4 or 6). Changes apply to everyone at once. |
| Admin mode (host) | Admin Mode | On: you can order every unit and release the units of absent players. |
| Status line "Paused by Ben" / "Waiting for ..." | (no tooltip: the line is the explanation) | |
| "You control Ben's units until they return" | Temporary Control | Ben disconnected and you chose Keep playing. You order Ben's units until Ben comes back; they stay Ben's. |

### 3.7 Connection bars (ui_game.c, uxgame; also lobby rows: mpmenu.c, Teams rows: ui_teams.c)
The title is the player's name. The body is the level, with no milliseconds (owner rule, layout v2 §1). `ui_conn_text` today prints "42 ms"; it must not be used for the body.
| Level | Body |
|---|---|
| 3 green | Good connection. In sync. |
| 2 orange | Slow connection, or catching up with the game. |
| 1 red | Poor connection, or loading, or resyncing. |
| X | Disconnected. The game waits, or the host keeps playing their units. |
| Header bars (collapsed panel) | One line per player, worst first, with the same words. |

### 3.8 Teams button and Teams window (ui_teams.c, teamsfinish)
| Element | Title | Body |
|---|---|---|
| HUD "Teams" button | Teams | Who controls which units. Take free units, and lend yours to another player. Right-click and drag to move this button. |
| Info icon (i) | How Teams Work | Left-click a free unit to take it, or your unit to free it. You can control up to the host's Units per Player. To start an expedition, use the game's Go to dungeon; every player approves it. |
| "Your units" strip | Your Units | The units you control. Only you can give them orders. |
| "Free units" | Free Units | Nobody controls these units. Left-click one to take it. The host orders them until someone takes them. |
| Another player's row | Ben's Units | Units Ben controls. You cannot order them unless Ben lends you one. |
| "Reserved for Ben (away)" | Reserved | Ben is not here. These units wait for Ben and go back to Ben when Ben rejoins. With Admin Mode, the host can release them. |
| "In the dungeon" highlight (in a dungeon) | On the Run | This unit is in the dungeon now. The others stay at the settlement. |
| Unit card (yours) | (unit name) | Left-click: free this unit. Use Lend to give control to another player. |
| Unit card (free) | (unit name) | Left-click: take this unit. |
| Chip "Lend" | Lend Control | Lets another player order this unit, in the dungeon too. It stays your unit; take it back any time. |
| Chip "Lent: Ben" | Lent to Ben | Ben orders this unit now. Click to take back control. |
| Chip "From the owner" | Lent by the owner | The owner lent you this unit. You order it fully (move and skills). Click to give it back. |
| Host: section name (Admin Mode) | Move Units | Click a section name, then units: they go to that player, or back to Free units. |
| "Go" / "Approve" / "Decline" / "Cancel Ready" | (as the proposal rows below) | |

### 3.9 Lend menu (portrait right-click; ui_game.c, uxgame)
| Element | Title | Body |
|---|---|---|
| "Lend control to Ben" | Lend Control to Ben | Ben orders this unit until you take it back. It stays your unit. |
| "Take back control" | Take Back Control | You order this unit again. |
| "Give back" | Give Back | Returns control of this unit to its owner. |
| "Release (free unit)" | Release | Frees this unit: nobody controls it, and any player can take it. |
| "Not your unit (owned by X)" (skill bar greyed) | Not Your Unit | X controls this unit. You can look at its details, but only X gives it orders. Ask X to lend it to you. |

### 3.10 Viewer banner (ui_game.c, uxgame)
| Element | Title | Body |
|---|---|---|
| Banner "Watching the owner's expedition" | Watching | You have no units on this run. You can watch and chat; the owner gives the orders. |
| "Follow the owner's camera" | Follow Camera | Your camera follows what the owner looks at. |
| "Free camera" | Free Camera | Move your camera yourself. |
| "Ask the owner to lend a unit" | Ask for a Unit | Sends the owner a request to lend you one of their units on this run. |

### 3.11 Expedition proposal (ui_teams.c, teamsfinish)
| Element | Title | Body |
|---|---|---|
| Expedition plan "Send to friends" (step 1) | Send to Friends | Sends this region and these units to every player as a proposal. The expedition starts only when everyone approves. |
| Step 2 supplies window "Expedition Proposal" | Pack Supplies | Pack supplies for your own units on this run, from the settlement storage. Other players pack for their own units. |
| Summary: region | Region | Where the expedition goes. Chosen by the player who proposed it. |
| Summary: who goes / stays | Who Goes | The units on this run, by player. The other units stay at the settlement. |
| "Approve" | Approve | You agree to go. When every player approves, everyone leaves together. |
| "Decline" | Decline | Cancels the expedition for everyone. |
| "Cancel" / "Cancel proposal" (proposer) | Cancel Proposal | Withdraws your proposal. Nobody leaves. |
| Line "Waiting for Ben" | Waiting | Players who have not answered yet. With no answer in 60 seconds, the expedition is cancelled. |

### 3.12 Votes and boxes (ui_game.c, uxgame)
| Element | Title | Body |
|---|---|---|
| Map vote prompt (Yes / No) | Change Map | The owner wants to go down a floor (or return to the settlement). Everyone goes together, so every player votes; the host breaks a tie. With no answer in 60 seconds, the vote is cancelled. |
| Proposer line "Waiting for Ben to agree (1 of 2)" | Map Vote | Your proposal waits for the other players. |
| Speed line "Ben wants Fast: press >> to agree" | Speed Request | Fast speed needs every player. Press >> to agree. |
| Story popup line "Ben chooses" / "Vote 1 of 2" | Event Choice | Who answers this event, from the host's Event Choices rule. In a vote, the host breaks a tie. |
| Disconnect box "Wait for Ben" | Wait for Ben | The game stays paused until Ben comes back. Ben's units stay Ben's. |
| Disconnect box "Keep playing" | Keep Playing | The game goes on. You order Ben's units until Ben comes back. |
| Portal, read-only "Ben is choosing who returns" | Portal | The player whose unit used the portal chooses who returns. Everyone else watches; when Ben confirms, everyone returns together. |

### 3.13 Chat (mpmenu.c, uxmenu)
| Element | Title | Body |
|---|---|---|
| Chat box (when open) | Chat | Type and press Enter to send, Escape to close. Messages fade after the time set in the Players panel. Right-click and drag to move. |
| "Ben is typing..." | (no tooltip: a status line) | |

### 3.14 Esc menu (mpmenu.c, uxmenu; Save greyed: ui_game.c, uxgame)
| Element | Title | Body |
|---|---|---|
| "Steam invite" (host) | Steam Invite | Opens Steam's invite window so you can invite friends into this world. |
| "Leave multiplayer" | Leave Multiplayer | Host: closes the world for everyone; it stays saved on your computer. Guest: you return to the title; your units stay reserved for you. |
| "Save Campaign" (guest, greyed) | Save Campaign | Only the host saves this world. It is saved on the host's computer. (Stock `TooltipFeedback.SetSimpleText` is used today: change it to the ETC format.) |
| "Load Campaign" (greyed) | Load Campaign | Leave multiplayer to load another world. |

### 3.15 Other places (one tooltip each)
| Element | Owner | Title | Body |
|---|---|---|---|
| Portrait "Free" tag | ui_game.c | Free Unit | Nobody controls this unit. Take it in the Teams window. |
| Portrait colour dot | ui_game.c | (player name) | This player controls the unit. Grey: the player is away. |
| Recruitment line "Units you recruit are yours" | ui_game.c | Recruiting | Units you recruit are yours. The clan limit and Units per Player both apply (host's Population rule). |
| Name above a unit (world) | ui_game.c | no tooltip (world hover is the game's own) | |

## 4. Wiring per file
- **uxmenu (mpmenu.c):** 3.1, 3.2, 3.3 (replace `rp_tooltip`'s box: `game_tip_show_rect(mpm_rule_label(row), <body>, u_tr(row))`), 3.4, 3.5, the lobby rows' bars (3.7), 3.13, 3.14 (Steam invite, Leave, Load).
- **uxgame (ui_game.c):** `game_tip_frame` + `ui_tooltips_enabled` + `ui_tip_exec`; 3.6, 3.7, 3.9, 3.10, 3.12, 3.14 (Save), 3.15; replace `ui_tooltip` (keep its name as a wrapper if other lanes call it: `ui_tooltip(text)` -> `game_tip_show("", text)` is not enough, because every call needs a title).
- **teamsfinish (ui_teams.c):** 3.7 (Teams rows), 3.8, 3.11; replace the rules box under the info icon.
- The bodies above follow the code at 00:50. If a rule's values change, update the body with them.

## 5. Proof (hidden copy A, scratch dylib = src + scratch-only wiring, 1512x982, 2026-10-14 01:20-02:10)
- `research/ux_shots/v9_tip_stock_title_Shared_Gold.png`: the stock ETC tooltip "Gold" (reference; the stock one shows ";; Pin").
- `research/ux_shots/v9_tip_rules_population_title.png`: host page, Rules panel, the Population row hovered, with our title and body in the game's window. The old custom box is off.
- `research/ux_shots/v9_tip_mouse_variant.png`: `game_tip_show` at a screen point (earlier build, still with the Pin chip).
- `research/ux_shots/v9_tip_rect_portraitbar.png`: `game_tip_show_rect` on the portrait bar: under its bottom-left corner, no Pin chip.
- `research/ux_shots/v9_tip_rect_switch.png`: a second element with another title replaces the first at once.
- `research/ux_shots/v9_tip_rect_hidden.png`: 0.2 s after the last call, `game_tip_frame` hid it (probe: shown 0).
- `rig.py check-prefs`: identical after each run.
