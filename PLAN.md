# Dungeon Settlers lockstep multiplayer - plan (2026-10-12)

Owner decision: lockstep (every peer runs the game, only orders travel), each player picks 1-3 units,
pause by vote (one player pauses -> all pause). Mac (Apple Silicon) only for now.
Research: research/rimworld_mp.md (copy its clock design), research/determinism_audit.md (what breaks).

## TLDR
| Step | What it does | Cost | Main failure point |
|---|---|---|---|
| 0. Test rig (lane harness) | 2 isolated copies of the game (own saves, own prefs, no Steam, silent, hidden window, nice 19), each driven by a local control socket | 2 x 3.7 GB disk; 1 lane | Unity may refuse -batchmode; window hiding; save path redirect |
| 1. Net core (lane net) | host sequencer + client in pure C: stamps tick, tickUntil window, sentCmds counter, pause vote, fingerprint compare; TCP localhost now, Steam P2P later | 1 lane, no game needed | protocol bugs (unit tests catch them) |
| 2. Determinism (after 0) | take over MainFlow.Update (1 tick per cycle), sync A* paths, own Random.state, Guid counter, seed System.Random sites | hardest code | hidden frame-time paths -> desync |
| 3. Orders (after 2) | capture UI-origin events, serialize by Guid, run at tick T on both | most code | a missed action path |
| 4. Ownership + pick screen + pause vote UI | ownerId per unit, block foreign orders | medium | UI hooks |

Rules for every lane: never touch the real install or his saves/prefs; never show a window on his screen;
nice -n 19, max 4 threads, only one heavy job (game copies) at a time; game copies muted.

## Owner rules for popups (2026-10-12)
- Level-up popup: opens only for the unit's owner; never blocks or pauses other players.
- Story popups: pause the game for everyone (stock behaviour); only the owner of the unit involved can choose; others see it but cannot click. If no unit is involved: players VOTE, host breaks a tie (owner 2026-10-12).
- Trade: opens only for the owner; owner prefers NO block, if the game re-checks range while trading (being checked in code). Selling items of units out of range must stay impossible (stock range rule).
- Trade code truth (2026-10-12): range 10.0 checked at open AND again at commit (TradeHelper.TryCommit 0xCB2D14 -> TryResolveOwner 0xCB38A8); a unit that walked away/died/changed map makes the commit fail with no item moved. So NO block is safe: final rule = trade does not block others. Whether TradeUIView pauses depends on prefab _timeLock (unread); if it pauses, make it local-only.
- Unclaimed units: host only may order them (owner). After auto pause: resume by itself when all in sync (owner).
- Integration pending: mpglue needs rig_det_* functions from the determinism lane (list in src/mp_game.h, MP_NOTES.md); lead hands it over after determinism finishes.

## Game copies (owner 2026-10-12): up to 5 pairs at once (AB, CD, EF, GH, IJ). Take a pair with `rig/rig.py lease <lane>`, free it with `release`. Never more than 5 pairs. Host TCP port per pair: AB 7791, CD 7811, EF 7831, GH 7851, IJ 7871 (`rig/rig.py port <pair>`).

## Demo (owner 2026-10-12)
When host + guest play a dungeon together end to end: show him TWO visible game windows side by side (rig `--window` mode made visible, each half the screen), one per player, so he can watch and judge. Only then, announced first; never before (no other windows on his screen).
- Story popups pause everyone even when "Allow pause" is off (owner 2026-10-12). Prefab truth: LevelUp and TradeUI _timeLock=1 (pause today) -> in MP they open only for the owner and do not pause; EventNode = essential popup.
- Map changes (owner 2026-10-12): start by VOTE (propose/accept, host breaks tie); speed resets to 1x after; everyone waits while a slower peer loads ("waiting for X"); autosave host only. Design: research/transitions_design.md (floor gen seed is clock-based + Edgar time limit: must fix).
- Return-to-base screen (DungeonResultView, popup 3; opened by DungeonPortalHandler.Process 0xF9E298 when a unit uses the exit portal out of combat, or by ForcedBaseReturnView's button) (owner 2026-10-12): PAUSE everyone; only the opener (owner of the unit that used the portal) picks; others see "X is choosing" read-only; when X confirms, everyone returns to base (no vote). Forced return with no unit: host picks (lead default, tell owner).
- Teams (owner 2026-10-12, "like Super Smash Bros"): each player has a micro team (max 3 units). A "Teams" screen in the base, open any time (locked in a dungeon); portrait right-click stays as a shortcut. Expedition: each player marks his micro team ready; it starts when all are ready. Base work (build, recruit, craft, research): everyone, shared base and shared resources.
## Demo v2 feedback (owner 2026-10-12)
- Pause display: use the stock big "Pause" panel in the middle; add who paused ("Paused by <name>").
- Bug: on the guest, units kept moving while paused (pause looked on/off). Part was demo.py auto-releasing pauses every 5 s; check the rest.
- Pause modes (host lobby setting, replaces allow_pause on/off): "Anyone can pause" (default, lead choice), "All players must vote pause", "No pause". Story / return screens still pause in every mode.
- Show the connection: player list with names, colour, ping/lag, in-sync state.
- Units you do not control: greyed / toned down (world + portraits); your own stay normal.
- Local numbering: your own units are always 1, 2, 3 on your bar and hotkeys (local view only; orders carry Guids).
- Demo: buff units more (x4 not enough, one died).

## Live version + base (owner 2026-10-12)
Goal: a version the owner can play live with a friend on a Mac.
- Platform first: Mac with Mac. Install: a separate app "Dungeon Settlers MP" next to the normal game (own saves folder; normal game untouched). Friend builds it from HIS own Steam install (we never ship game files).
- Connect: Steam invite (friends; host clicks Multiplayer > Host, invites via Steam; friend accepts the invite).
- Save: host picks any save (or a new campaign) in the MP app, then "Multiplayer" + invite. Players must be able to choose multiplayer and invite from inside the game.
- Recruits: owned by the player who recruited them.
- Team size: bench + 3 per run. A player can own any number of units in base; at most 3 of his units go into a dungeon.
- Gear / storage: host lobby option: (a) shared storage, each equips only own units (host: unclaimed); (b) shared storage, anyone equips any unit; (c) own storage per player (harder: game has one storage; items tagged by owner).
- Earlier: Teams screen in base any time; expedition starts when every player marks his team ready; base work (build, recruit, craft, research) open to everyone, shared resources.
- Connect (owner 2026-10-12): button text "Steam invite" (never "Invite"). Direct IP join as backup: host shows its address:port; "Join by address" field on the title Multiplayer menu; host listens on Steam and TCP at once.
- Expedition (owner 2026-10-13): the stock "Go to dungeon" button marks you Ready (with your marked units) and opens the Teams screen. Region: each player picks one on the Teams screen; vote decides, host breaks a tie.
- Chat (owner 2026-10-13, LATER, after the first live test): Enter opens a line, type, Enter sends. Messages show in a corner and fade after 10-15 s; pressing Enter shows the recent messages again. Sent outside the sim (no desync risk). Reuse a stock text field (unit rename).
- Live test feedback (owner 2026-10-13): lobby settings saved per world; a new world inherits the last used settings; multiplayer worlds get a "Multiplayer" tag in the load list; "Join by address" needs a real text field; host must count as a player; layout of the lobby text fixed.
- Live bugs (owner 2026-10-13, lan_v5): HOST animations do not play (units slide, dead monsters stay standing, host laggier) — guest fine; GUEST units jumpy; pick-up order hits all players' units; host portraits of others not greyed; guest keeps a copy of the mp save (must live on host only); "you cannot modify this unit" banner needed on others' character screen.
- RELEASE RULE (owner 2026-10-13): no partial versions to the owner. lan_v7 ships only when ALL open feedback items are built + gated together, with an exact "what is in it" list checked against the build.
- lan_v6 feedback (owner 2026-10-13): player panel at TOP of screen, numbered rows + Wi-Fi signal icon instead of raw ping; name entry; host "Admin mode" setting (off by default: host is a normal player for others' units); Teams = standalone window in the game font, own units on top, others below greyed + not clickable, clear Free group; host animations STILL broken in v6 (speed fix was not the cause).
- Owner 2026-10-13: Esc menu seed label overlaps our buttons (offset it). Players remembered across sessions: stable player id; a returning player gets his previous units back; absent players' units stay reserved; only the host with Admin mode can release them. Check MP adds no extra autosaves vs stock.
- Disconnects (owner 2026-10-13): any guest exit sends a proper leave at once; game auto-pauses; host popup "<name> disconnected": Wait / Keep playing (host temporarily controls his units, still reserved for him); the player can rejoin mid-game and gets his units back.
- No loading another save during a multiplayer session (owner 2026-10-13): stock Load hidden/disabled with "Leave multiplayer to load another world".
- Disconnect pauses in EVERY pause mode, including "No pause" (owner 2026-10-13).
- Pairs: max 3 at once again (owner 2026-10-13, after two GUI session crashes under heavy load).
- Esc menu in MP does NOT pause anyone (owner 2026-10-13). Other units: see-through stays 0% by default (owner said transparency off by default); panel stays at the top (owner).
- Owner 2026-10-13 (UX): settings behind the GAME gear icon (lobby, host page, in-game panel); lobby reorganized in clear sections; connection = 3 bars only (1 red / 2 orange / 3 green), never milliseconds. Layout spec: research/ux_layout_v2.md.
- Settings button = the game Cog Wheel item icon (ITEM_CogWheel) in the small framed button (owner 2026-10-13). Build ux_layout_v2.md now (owner).
- Owner 2026-10-13 (after mockups): cog wheel settings button everywhere. Unit limit per player = host rule 1/2/3/4/6, changeable by the host during play. Expedition: no region vote; the player who presses Go sends an "Expedition proposal" window to everyone (region, who goes with which units, who stays); ALL must approve; Decline or no answer in 60 s = cancelled. Game party cap stays PlayerPartyMax = 4 (stock).
- Research 2026-10-13: the stock game simulates BOTH maps (base + dungeon) every frame (MainFlow.Update 0xd1b648 -> TickFlow per MapType); only one dungeon can exist. Players on different maps (one in dungeon, one at base) is possible in principle; blocker = the "in view" flag (CurrentMap == mapType) changes simulation work (BehaviorManager.Tick, chatter, DecisionSystem) -> must be made identical on all peers and the camera split from CurrentMap.
- Owner 2026-10-13: NO solo/split missions: all players always on the same map. If only one player's units go to the dungeon, that player controls the expedition; the others are viewers (spectate, cannot order). Players can LEND control of their units to another player at any time (dungeon too) and take it back. Mockups first (artifact Q2ujjne83LAdsZDwSWp426).
- Owner 2026-10-13 approved mockups (artifact Q2ujjne83LAdsZDwSWp426, "it's good"): build Lobby A (host+guest), Host page A with B as the opened Rules state, Player panel board, Teams A, Proposal (with per-player supplies: each player packs only his own units from settlement storage, gear rule respected; receiver packs before approving), Lend control, Viewer. All messages (lend/take back/give back, proposal results) use the game's big centred message (the goddess statue text). Cog = ITEM_CogWheel sprite.
- Proposal flow (owner 2026-10-13, final): step 1 region+units (stock panel/Teams), step 2 the stock supplies window for your own units, then the proposal summary (no storage grid) with Approve/Decline; receivers get "Pack supplies" opening the stock step-2 for their own units.
- Proposal flow CLARIFIED (owner 2026-10-13): receivers automatically get the stock step-2 supplies window titled "Expedition Proposal" (own going units + side list of who goes from other players) -> Next -> final "Expedition Proposal" summary with Approve/Decline.
- Owner 2026-10-13 evening: "it all looks good": fix the black text outline in custom windows (use stock text materials), then ship the finished lan_v7 for his test.
- RELEASED lan_v7 2026-10-13 19:11: dylib 35d104c04b619e1b (gate 20261013-1902 green), MP app + friend kit same file; Windows real-PC kit win/kit/DSMP_WindowsTestKit_25846389.zip (baselib 2a21ca30, xplay 19/19, kit dry run 6000/6000 identical). Frozen source rig/release/v7_src. Checklist rig/lan_v7/WHATS_IN_V7.txt.
## lan_v7 owner test feedback (2026-10-13 evening) -> lan_v8
- Join page: Back button and Escape do not work. Guest "Leave multiplayer" must drop to the title at once.
- Steam join is missing from the menus (LAN app runs without Steam by design; MP app has Steam). Owner wants a way to invite without sharing his IP.
- Story events: host setting "Event choices": Owner of the unit / Host only / First to answer / Majority (host breaks ties). A player whose screen is busy (e.g. recruitment open) must still get the event popup/vote; never a dead vote.
- Portraits: our owner colour bar hides the stock status colours (near death etc.); a dead unit still showed HP; unexplained light/dark green colours. Keep stock status colours; show owner differently. Free units get a "Free" tag.
- Connection bars: hover tooltip with ping details (panel and Teams).
- Player panel and Teams button: movable (drag), default both at the top of the screen side by side (Players, then Teams).
- Recruitment: line "Units you recruit are yours"; recruitment timer does not count down live.
- Task Planner and Consume Policy: only your own units editable/shown.
- Teams: lending/borrowing must work from the Teams window too (also in the dungeon); remove the leftover "Region ..." line.
- Expedition plan: step 1 must allow selecting allies' units (their owners approve in the proposal). Opening the plan must be local (today it opens on both, closing closes one). Rename "Enter dungeon" to "Send to friends" and the unresolved-warning prompt to "Send the proposal to your friends anyway?". Proposal "Stays at the settlement" shows portraits.
- Move Players panel / Teams button: right-click + drag, tooltip "Right-click and drag to move" (owner).
- Owner: same app offers Steam AND direct IP (choice). Panel settings: "Show tooltips on hover" toggle + note "Right-click and drag to move the multiplayer windows". Teams = allocate units only; expeditions start only via the regular expedition interface. Rejoin mid-dungeon must land at the exact current dungeon state (prove).
- Multiplayer page: "Recent worlds" history: world name, day, connection (Steam host / IP), players, your units; click = reconnect; x removes (owner 2026-10-13).
- POPULATION rule (owner 2026-10-13, inside lan_v8): host setting, default Shared. Modes, each with a clear description:
  Shared (stock): one clan cap from reputation, everyone fills it, first come.
  Per player: every player has the full cap for his own units (I see "my units / 4").
  Split equally: the cap is divided between connected players (remainder +1 to the first players).
  Multiplied: cap x connected players, one shared pool (two players both see "x / 8").
  Overflow allowed (after a rule change / disconnect): nobody is removed, only no new hires above the cap. The check runs inside the replayed recruit order (same on all peers). With more than 8 units in the base, the unit bar shows only your own units (others in the Teams window).
- Dedicated server idea (later, owner thought): a passive headless host with no player, time paused while nobody is connected.
- CHAT in lan_v8 (owner 2026-10-13): Enter open/send, Escape close, fade 12 s, history, movable, outside the sim; mockup board Chat.dc.html.
## lan_v8 owner test feedback (2026-10-13 23:30) -> lan_v9
- Host page Rules summary text overflows its box (3 lines spill below the border).
- Rules panel: tooltip on hover for each rule. Escape must close every interface we created (Rules panel, Teams, chat, panel settings, proposal steps where allowed).
- Expedition plan step 2 (supplies): show and modify ONLY your own units, even as the host (allies pack their own).
- Teams in the dungeon: highlight the units that are in the dungeon team (on the run) vs at the settlement.
- CHAT BROKEN for the owner: Enter opens the box but the field is not focused; he can never focus or type (game shortcuts disabled incl. Enter?). Must auto-focus and accept typing at once.
- Teams showed 1 red connection bar for a local player (same state-mapping bug as the lobby had?).
- Esc menu shifted down by our 2 added buttons: re-centre vertically.

## lan_v8 owner test feedback, part 2 (2026-10-13 23:40) -> lan_v9
- Players panel + Teams button vanish behind any opened game window (Consume Policy, Task Planner...). They must stay visible on top of game windows.
- Players panel and Teams button must move SEPARATELY (each its own right-click drag, own saved position), not as one block.
- Make them smaller: compact size option, and the panel can collapse to its header only.
- Next owner test = Mac host + Windows (CrossOver) guest on this Mac, locally. lan_v9 must ship with the Windows kit built from the same source, plus a one-page "test Mac + CrossOver" guide.

## lan_v8 owner test feedback, part 3 (2026-10-13 23:50) -> lan_v9
- BUG: a unit lent TO me: I can use its skills but I cannot MOVE it. Borrower must have full control (move + skills), same as owner.
- Units that are not mine (and not lent to me): clicking one must give NO control. No move orders, no skills. I may view its info, but the skill bar and all action buttons are greyed out with a clear "Not your unit (owned by X)" state.
- Owner decision 2026-10-14 (gear "own storage" + a lent unit): CONTROL follows the borrower (orders, skills, pick-up
  areas), OWNERSHIP stays with the owner: items the lent unit picks up go to the OWNER's storage, never the borrower's
  (ord_mp.c it_unit_owner = owner for item tags; it_may_order = dsnet_may_order for orders). Done in uxgame (lan_v9).

## Owner correction on tooltips (2026-10-14 00:45) -> lan_v9
- I misread the request. Wanted: OUR OWN explanatory tooltips on EVERY multiplayer feature, button and setting we added (Rules, panel cog settings, Teams, lend, proposal, chat, connection bars, recent worlds, host page, join page...), in the SAME FORMAT as the game's own tooltips (the game's tooltip window: title + body, same font/frame), explaining what it does and how it works.
- Today we have only a plain custom box on the 7 Rules rows. Replace it with the game-format tooltip and cover everything.
- "Show tooltips on hover" setting stays (turns ours off).
- Side issue in v9_rules_tip_lobby.png: stray "3 / 4" text at the top centre of the screen in the lobby.

## Owner requests (2026-10-14 00:55) -> lan_v9
- Player colours: ONE colour per player, the same everywhere (panel dot, names above units, portrait chip, Teams, chat names, lobby, proposal, votes, recent worlds). Today they do not match (v8 frame: panel dots vs unit labels differ).
- Chat: while a player types, others do NOT get the chat opened; they see a small line "<Name> is typing..." (in that player's colour), gone when sent/cancelled or after a few seconds of no typing.
- BUG (reported before): a unit down on the floor (dying/downed) gets a purple life bar in single player; in multiplayer it does not. Find the root cause and restore the stock look.
- Owner decision 2026-10-14: own-storage gear mode + lent unit: picked-up items go to the unit's OWNER's storage (control = borrower, ownership = owner).

## lan_v9 scope CLOSED (owner OK, 2026-10-14 01:15)
- Only open items finish: game-format tooltips, move-after-rejoin root cause, own-storage lent pickup, purple downed life bar, colour pair test, Teams button snap. Any new request -> lan_v10.
- After v9 ships: stop all long-running agents; restart fresh lanes from a short handoff file (cost: 12 h contexts).

## lan_v10 (owner, 2026-10-14 01:25)
- Co-op awareness: show where each friend's camera is (in their colour), and a ping (mark a spot on the map, seen by all, in your colour). Design in game style first.
- Colour-blind check of the player palette (blue / periwinkle / ice / cyan look close).
- Task list for the owner's first test with a friend (screen recording -> review).
- Code-based UX flow analysis: extract every screen/state and transition from the code, draw the flowchart, check against usability rules (dead ends, missing feedback, inconsistent Back/Escape, unexplained refusals, unreachable states). Started early (read-only, no test pairs).
- Comparison with other co-op games: skipped for now (owner).

## Owner answers (2026-10-14 01:40)
- Units per player (1/2/3/4/6) = how many units you may OWN in base (current build is right). Dungeon team: always max 4 units in total. (Supersedes "bench + 3 per run" above.)
- lan_v9 also fixes 2 bugs from research/ux_flow_v10.md: #1 Portal X soft-lock (others stuck paused), #2 wrong "The owner left the game" text when the guest's OWN connection dropped.
- lan_v10 colours: your OWN units keep the game's default look (no tint). Other players get a colour: auto from a colour-blind-safe palette that avoids every game status colour; each player can CHANGE other players' colours for their own screen only (local setting).
- lan_v10: see-through of other players' units can never be 0 (minimum ~30%).
- lan_v10: optional outline/contour highlight: off / own units / friends' units / both.
- lan_v10: camera awareness + ping, "do it good": research what good co-op games do; a ping disappears when a unit reaches the spot (plus a timeout). Owner trusts the design; implement well.
- Ping (owner 2026-10-14 01:55): NO automatic timeout. A ping stays until a unit reaches it or a player deletes it. B = place a ping; B with the mouse over an existing ping = delete it (toggle). Middle-click also places. Owner loves the "is typing" chat UX.

## Session crash 2026-10-14 03:03 (evidence: ~/Desktop/crash_log.txt from the owner)
- WindowServer (pid 93417) exited at 03:03:08 with no message; loginwindow: "Logout triggered by windowserver exit". Mac did not reboot (boot Oct 9). No WindowServer crash report found.
- In the 5 minutes before: WindowServer handled ~290 new short client connections/min and ~860 TCC permission requests/min, steady. Source: our rig guards (rig.py start loop: winlist = CGWindowListCopyWindowInfo(All) + 2 lsappinfo per check, every 1 s per game copy, also for orphan copies).
- Not proven as THE cause, but the only abnormal load on WindowServer. Mitigation: guard check every 3 s after the first minute (was 1 s), max 2 pairs, kill orphan copies after any crash.

## Next features (owner, 2026-10-14 04:10) - plan + build tonight, NOT integrated into the multiplayer MVP (lan_v9)
Source threads: research/discord/1559702991592751194.txt (announcement), 1559240976932016158.txt (feedback).
- Foreign Expedition: take units from your own save into a fully kitted hub world, dive together with friends, units return with XP / levels / loot. Owner: units are COPIED (home keeps them and keeps working); results come back only per settings. Settings: death permanent or not, XP counts or not, items come back or not. Custom challenges / enemies later.
- Spectator mode: "Watch" join option, anyone invited; sees everything, owns no units, cannot pause or vote, can chat and ping; host can turn spectators off.
- PvP: fight, Capture the Flag, Protect Base, Tower Defense. Pause = host setting (no pause / planning pause every 5-10 s). Maps: dungeon floors as arenas FIRST (fight, CTF), base-map modes later.
- Dedicated server: skipped (low priority). Mod sync: not planned (vanilla only).
- PvP units (owner 04:20): own units, copied from your save; OR import a unit build from a DS wiki builder URL (dungeonsettlers wiki, ~/dungeonsettlersdb). Plan both.
- v10 story event timeout (owner): after 60 s with a visible countdown, the choice passes to the host.
- v10 expedition proposal (owner): a small side panel, everyone keeps playing, game keeps running (no big blocking window).
- Owner answers 2026-10-14 04:35 for Foreign Expedition / Spectator / PvP: see MORNING_QUESTIONS.md "ANSWERED".

## Launcher (owner 2026-10-14 ~12:15)
- Install = a launcher app (Mac + Windows). Plan it as a GENERIC mod launcher (multiplayer is one mod among others), if possible.
- Game updates: pin the game version (the multiplayer copy stays on the last supported build) and auto-update as soon as a matching mod version is ready.
- Download: GitHub Releases for the files (no Vercel data cost); a download page on dungeonsettlers.wiki links to them. Final choice still open.
- Also lan_v10: partial resync (only the differing parts) + automatic desync report (both peers' logs + state at the first bad step in one file). Catch-up mode dropped (owner: DS is light).
- Launcher answers (owner 2026-10-14): Apple membership NOT now ("Open Anyway" steps); Windows unsigned to start; optional old-build download through Steam with the player's own login: YES (optional). UI technology: owner asked for alternatives to Tauri 2 (pending).
- Launcher UI (owner 2026-10-14): Tauri 2. Full mod loader for everything (multiplayer is one mod), expandable over time.

## Mod loader compatibility tests (2026-10-14, research/loadertest.noindex, research/bepinex_compat.md)
- Windows: our baselib.dll + BepInEx be.788 + Extended Hotbar: both load, Mac host + Windows guest v8_xplay 8/8. Needs winhttp=n,b override.
- Mac Apple chips: MelonLoader test branch crashes on stock DS (16-byte hook on il2cpp_resolve_icall overwrites neighbours: export is a 4-byte jump). With 2 local fixes it loads mods but Il2CppInterop lacks arm64 support (its PR #288) -> mods do nothing. Not usable yet.
- Our dsrig unsets DYLD_INSERT_LIBRARIES: must keep other loaders' entries (launcher step 2 loader merge).

## Wiki builder vs game differences found by PvP wiki import (2026-10-14, ext/pvp/out/wiki*/stat_diff.txt) - candidate dungeonsettlers.wiki fixes, owner to approve
1. Generation roll: game = trunc(10 x (1 + race+bg roll)) (RecruitHelper.ApplyModifiers 0xD60C08); builder rolls (10 + innate trait flats) and keeps the fraction (Elf Beggar Slow Learner Str: game 10.0, builder 9.2).
2. A typed major (Advanced > input my stats) does not feed derived stats (P.Atk, attack speed, move speed, crafting).
3. NOT A BUG (owner 2026-10-14): conditional passives are folded at full value on purpose in the builder.
4. Builder offers developer trait AFFECTER_TestSuperman (+100 all majors).
5. Dwarf, Tal, White Demon have no look in the game (builder offers them).
- Owner 2026-10-14 ~16:00: fix all 5 wiki builder differences on dungeonsettlers.wiki (gates green = push live). Downloads = Cloudflare R2 on his domain (he creates the free Cloudflare account). Ping sound: keep "tutorial alert", owner judges in his test.
- lan_v10 FROZEN 2026-10-14 ~16:55 (release9 runs the release).
- Downloads (owner 2026-10-14 ~17:45): GitHub, his account renamed to AmfiLogos (free when checked); one repo with release files only, no source; profile name Amfi, private email on. Cloudflare dropped (too much work).

## Owner 2026-10-14 ~18:00
- Builder rule: the wiki builder is a TESTING tool; it folds conditional / acquired passives at max on purpose (in game a unit keeps kill/milestone bonuses after removing the skill). Wiki import into the game must reproduce the BUILDER's numbers (max by default is fine), not a "faithful" game unit. -> PvP M3b: apply the builder's folded values on import (later task).
- New feature: a unit BUILDER inside the launcher (custom units, benchmarks): the wiki builder's logic, UI matching the game's expedition-start / recruit screen, everything choosable (no random). Design it on the design canvas first.
- All his GitHub projects must be PRIVATE (none public for now).
- Owner 2026-10-14 ~18:10: ONE release folder only: rig/lan_latest (each release replaces it; previous build moved to rig/old_versions/lan_latest-YYYY-MM-DD-HHMM). No vN folders for the owner.
- Owner 2026-10-14 ~19:15 (after the Fable review, research/review_fable_2026-10-14.md): real 2-person internet/Steam test LATER, with a bigger build (keep building features in parallel). Downloads: a public repo eventually, not now (all private until then).
- lan_v10 FROZEN again 2026-10-14 ~19:35 for the full release_gate (38 checks).

## Mods per world + scope + difficulty (owner 2026-10-14 ~19:45)
- Mods are set PER WORLD / SAVE (profiles), not globally for every game on the computer.
- Each mod declares its scope: GAMEPLAY (changes how the game works -> every player must have it; auto-download signed) or CLIENT (comfort / only changes things for yourself -> never required from others). Example given by owner: weapon-skill-slot should be client-side (verify: the other player's game must accept the orders it produces without the mod).
- Mod GROUPS / folders: apply a group to a specific world or save.
- Custom difficulty in the launcher: vanilla difficulties (Devastation locked until Very Hard is completed, as in the game) OR custom difficulties (toggle on/off); first custom one = Hell (existing builtin mod ~/DungeonSettlersMods/manager/builtin/hell-difficulty). Custom difficulties can later be downloaded like mods.
- Correction: "Cheaper recruits" is a real mod of the owner (~/DungeonSettlersMods/mods/cheaper-recruits), not an invention.
- Owner 2026-10-14 ~19:55: Spectator HUD A (free camera + friends' view marks) is the preferred design. When following one player, keep that overview as a MINIATURE view on screen (shows where each player looks, to see what someone is planning); movable like our other windows, can be hidden.
- Owner 2026-10-14 ~20:00: PvP (and other modes) get a host rule 'Vanilla check': ON = no gameplay mods allowed for anyone (client mods still fine), enforced at join; OFF = 'Allow mods': the host's gameplay mods are required and shared (server-mode style).
- Owner 2026-10-14 ~20:10: fog of war does not need to be synced. Add a host option 'Shared fog of war: On / Off' (Off = each player sees only what their own units revealed). Must stay display-only: the sim's map state stays identical on all peers; exclude pure view/fog fields from the full-state check with a written reason.

## Crash root cause and fix (2026-10-14 ~20:45)
- 4 screen-session crashes (WindowServer exits -> logout). Common factor: our window guards hammering WindowServer: rig.py guard per game copy (winlist = full CGWindowListCopyWindowInfo + 2 lsappinfo, every 1 s, later 3 s) AND win/tools/dsmp_guard.py every 0.1 s (600 full window lists/min) for 2 hours, sometimes 2 instances at once (both alive at the 20:12 crash).
- Fix: rig guards share ONE window list + front-app value per 2.5 s system-wide (locked cache file); dsmp_guard polls every 2 s, single instance, exits after 60 s idle; release_gate runs one pair at a time.

## Full-state check exclusions (gate fix lane, 2026-10-15 ~01:30)
- Not compared: RawMapContainerSaveData/Maps/<map>/BuildingVariations and BuildingVariationSavedFlags. Reason: the sprite variant of each building tile is picked by the VIEW (EntityObjectsContainer.ResolveSavedSpriteVariation) when the tile's object is built on screen, so it follows each player's camera. Display only (owner 2026-10-14: view / fog state need not be synced). Evidence: soak 15 min, checks every 600 steps, DSRIG_FULL_KEEP saves: 7 of 87 checks differed, only in this field; fingerprints and deep hash equal. Fog (VisitedPositions) did not differ in that soak and stays compared.
- Lines are hashed without the trailing CR: Windows writes the save with CRLF, so every section differed on Mac host + Windows guest (gate round 1: reload loop in win_partial / win_mp2 / win_floorgen10).
- A full-state difference found while a desync is already flagged is left to that resync (partial first); before, it left force_full set and the NEXT desync skipped the partial resync (desync_partial pos#5).
- 00:33 rig.py DSRIG_MAX_PAIRS default 4 -> 2 for the night run (screen-crash safety). Restore to 4 when the owner is back.
- 06:35 rig.py: rig/reserve.lock (lane prefix inside, max 1 h) lets a lane that needs both slots (spectator 3-copy test) reserve them; other lanes get no new leases meanwhile.

## core next merge (2026-10-15 ~05:01-07:35, lane ext/core_next, NOTES.md there)
- Server command log trim (dsnet_server.c log_trim): after the start, entries older than the newest join-point save (or the
  pending join point) are dropped in chunks of 4096. rig.log rotates to rig.log.1 past 32 MB at start; fp.log / trace.bin
  rotate at 64 / 256 MB (rig_det.c).
- HELLO proto 2: gameplay mod-set hash + match rule + launcher modset_lines from DSML_SESSION (mp_config.c mp_session_mods).
  Host refuses a different set (rule mods) or any gameplay mod (rule vanilla): DSNET_REJ_MODSET + words + host lines.
  No launcher = no gameplay mods. Spec: ext/core_next/NOTES.md "HELLO modset fields".
- Shared fog of war rule (DSNET_RULES 7th byte, WELCOME, session state; Rules row 8 "Shared fog", default Off) + mp_fog.c:
  Off = each player's fog / enemy display follows only their own units (plus buildings, unowned units, temporary
  reveals); display only, the sim's VisionArea / visited / stacks are untouched, so VisitedPositions stays in the
  full-state check (no exclusion needed). Research: ext/core_next/fog_research.md.
- Random scan fixes (ext/core_next/random_scan.md, NOTES.md): camera shake + loading tips view (rig/tools/gen_ranges.py,
  build/det_ranges.tsv: +149 view methods), InitState/set_state-scoped seeded blocks keep their seed, HashHelpers seed pinned.
  Windows TODO (windows lane): port the gen_ranges changes into win/port/gen_port.py, re-run win/win_map.py, add
  "FogOfWarView.SetHalfVisible" to gen_port HARMLESS (folded with LoadVisible on Windows).
- Late join (mp_game.c): a multiplayer load skips the game's WaitUntilGameStabilizes wait (1.5-3.5 s); DSRIG_JOIN_FAST=0 = stock.
- Gate: det_out/gate_corenext_20261015-0706 (smoke 3/3, mp2 19 PASS, left_prompt 16 PASS). Soak 15 min (fog Off): 3 PASS, 0 desync, 30 full checks equal (build/core_test/soak_20261015-071443). Shared build/dsrig.dylib = this merge since 07:31.
- 08:05 fog fix (loader lane's SIGBUS during the host's join-point reload): mp_fog.c drops its cached VisionDataHandler
  while busy() or an mp load is pending (view vanilla, full redraw after). Gate det_out/gate_corenext_20261015-0809 green,
  5 late joins clean, soak 15 min fog Off 30/30 full checks equal (build/core_test/soak_20261015-082001). Shared dylib 08:36.
- 09:20 rig.py _free_stale: a dead lease pid no longer frees a pair while a copy of it runs or within 10 min of the lease (pairs were given twice at 06:51 and 09:06).
- 09:25 rig.py release <pair> [lane]: with a lane, only that lane's lease is removed (likely cause of the double leases: a late release removed the new holder's lease).

## spectator merge (2026-10-15 09:03-09:53, lane ext/spectator, NOTES.md there)
- Spectator mode S1-S4 (watch from the Join page / Recent worlds, host rule "Spectators", max 8, outside the sim:
  never in present_mask, saves, hashes or votes; join from the cached join-point save, no pause) + S5 HUD A and the
  mini overview (ui_game.c 8k: a second camera renders the map into a RawImage while a spectator follows a player;
  players' view rectangles, unit dots, pings; drag, - / +, x, O; cog rows "Mini overview (O)", "Overview size").
- Rebased on src after core next: DSNET_PROTO 3 = HELLO build, match rule, modset, lines, u8 role, name; WELCOME
  ... has_world, world[48], shared_fog, role, allow_spectators, max_spectators. Rules rows 9: row 7 "Shared fog",
  row 8 "Spectators" (narrow panel 640 high, lobby 376); mp_worlds.txt 10th field = spectators; MPB_WATCH = 70.
- Backups src/old_versions/*-2026-10-15-0905.*. Gate on build/merge_spectator.dylib: det_out/gate_spectator_20261015-0912
  (smoke 3/3, mp2 19 PASS, left_prompt 16 PASS). Pair: ext/spectator/out/sp1_merged1 16/16. 3 copies (host + player +
  spectator, 10 min of play): out/mini_merged1 15/15, 37350 common fingerprints equal, 0 desyncs. Unit: net, mp_test,
  ui_test (514), mpmenu_test all pass. Shared build/dsrig.dylib = this merge since 09:53.

## launcher merge (2026-10-15 ~09:50-10:32, lane ~/ds_launcher/modcheck, NOTES.md there)
- Wrong-modset notice: after a DSNET_REJ_MODSET refusal the guest's stock notice box names the gameplay mods that differ
  (Missing / Extra / Other version (host X, you Y) / Same version, different files); Vanilla check: "Turn off: ...".
  New header src/net/ds_modcheck.h (header only: no build.sh / Windows gen_port change). mp_game.c: short reject_text line,
  exported mp_game_modset_text (once per refusal, logs "mp: modset refusal: ..."), an mp.json join refused for mods now ends
  the session so the notice shows. mpmenu.c: join by address + Steam/ended path open the notice; M.notice 320 -> 640.
- Gate (merge_launcher.dylib, det_out/gate_launcher_20261015-0955_*): smoke 3/3, mp2 19 PASS 0 FAIL, left_prompt 16 PASS,
  prefs identical. Pair cases (~/ds_launcher/tests/modset_pair.py): mods differ, vanilla check, same mods = 3 PASS.
  Shared build/dsrig.dylib = this merge since 10:32 (previous one in build/old_versions/).
- Weapon-skill slot stays GAMEPLAY: sim in sync 10 min host-only, but save QuickSlots (5 vs 4) differ in the full-state
  check (MORNING_QUESTIONS, ~/ds_launcher/NOTES_weaponslot.md).

## Foreign Expedition merge (2026-10-15 ~11:00-11:27, lane ext/foreign_expedition, NOTES.md there)
- Merged into src: fe/ (fe_json, fe_save, fe_game, fe_menu.inc, fe_ui_api.h, test/fe_cli.c), build.sh (+ fe sources), mp_core (DSNET_FE_START),
  mp_game (join-point hub build, poll, delete guard), mpmenu.c/.h/_model.c (World row, Expedition Rules, Bring / Results / Expedition
  results windows, FE ids after the spectator ids, MPA_FE_* = 1<<21..23), net (mail BLOB_MAIL = 4 next to PATCH/REPORT; guest mail routed
  before the report-only check, host mail before PATCH; DSNET_FE_START = 21), ord_mp (recruit refusal in a hub with Recruiting Off),
  ord_game (key + amount of a picked item read before the pick-up: ord_last_pickup), ui_game (popup 8 guard, Guild Contact button hidden
  in a hub with Recruiting Off, ui_items key/amount, ui_find), rig_cmd (fe_* commands), tests (mpmenu_test FE block, test_dsnet fe_mail /
  fe_start, test_mp stub fe_on_start).
- Backups: src/old_versions/*-2026-10-15-1126.*; previous shared dylib build/old_versions/dsrig-2026-10-15-1126-before-fe.dylib.
- Gate on build/merge_fe.dylib: rig/det_out/gate_fe_20261015-1110 smoke 3/3, mp2 19 PASS 0 FAIL, prefs identical; left_prompt 16 PASS
  (det_out/gate_fe_leftprompt_rerun.log; first try: connection refused right after start, then the rerun passed). Unit: net ALL PASS,
  mp_test ALL PASS, ui_test 514, teams_test 437097, mpmenu_test, trn_test pass. Pair: FE dive on the merged dylib 23/23
  (ext/foreign_expedition/out/dive_20261015-112055). Shared build/dsrig.dylib = this merge since 11:26.

## PvP merge (2026-10-15 ~11:26-12:10, lane ext/pvp, NOTES.md there)
- Merged into src on top of the FE merge: pvp_core/pvp_game/pvp_units/pvp_wiki/pvp_build (+ tables), pvp_modcheck.c/.h +
  pvp_mods.c (host rule "Vanilla check": On = no gameplay mod for anyone, Off = the host's gameplay mods for everyone; checked at
  Start Match, refusal names the player and the mod), pvp_ui.inc (PvP window, HUD, result, Bring Units; included by mpmenu.c),
  pvp_test/. Hooks: mp_core (DSNET_FE_START also calls pvp_on_units_start; each side acts on its own run id), mp_game (PvP tick,
  fingerprint mix, PVPB block in the join-point save, round/match snapshot override, own-units world build after FE's hub build),
  mpmenu.c (Esc "PvP Match", windows, mpm_pvp test command), rig_main (pvp_install), rig_cmd (pvp_* commands, unit_stats, downed),
  trn_game (arena rooms, trn_apply_map_request non-static), ui_game (CTF flag tag), ui_teams (no portal in an arena),
  ord_smoke (attack payload, order through the lockstep), build.sh (+ pvp sources, -lz), test_mp stub.
- One mailbox reader: fe/fe_game.c fe_poll hands kinds it does not know to pvp_mail_take (PvP kinds 11-16; FE 1-5). Before this,
  FE's poll ate PvP's mails (seen in the merge test: the Vanilla report never reached the host).
- PvP is now in the shared dylib; whether it ships in the next lan_latest is the owner's call (MORNING_QUESTIONS).
- Backups: src/old_versions/*-2026-10-15-1209.*; previous shared dylib build/old_versions/dsrig-2026-10-15-1210-before-pvp.dylib.
- Gate on build/merge_pvp.dylib: rig/det_out/gate_pvp_20261015-1200 smoke 3/3, mp2 19 PASS 0 FAIL, left_prompt 16 PASS, prefs
  identical (the 11:30 run on the earlier merge: left_prompt failed "both running", and the then-shared FE build failed it the
  same way: flaky). Unit: net ALL PASS, mp_test ALL PASS, pvp_test 585, mpmenu_test, trn_test, ui_test 514. Pairs on the merged
  dylib: ext/pvp/out/m_van2 7/7, m_bring 8/8, m_ui 9/9. Shared build/dsrig.dylib = this merge since 12:10.

## FE fix: left_prompt regression (2026-10-15 12:11-12:30, lane ext/foreign_expedition)
- Cause: the menu remembered World = Foreign Expedition in mp_menu.json; after FE dives a copy's normal Host + Start took the expedition
  path ("Nobody brought units"), both copies stayed paused (left_prompt "timeout: both running"). Reproduced: "fe":1 in A's prefs ->
  shared 11:26 build fails the same way; without it 3/3 PASS (bisect det_out/lp_bisect_*).
- Fix: Host page opens at World "Your save" every time (mpmenu_model MPB_HOST sets fe 0); mp_menu.json no longer restores "fe" (the
  expedition rules still are). mpmenu_test check added. Backups src/old_versions/*-2026-10-15-1230.c.
- Also (test side, lane only): the FE dive test moved mp.json away and never restored it -> the next left_prompt on that pair got
  "connection refused" (the 11:10 and 12:11 gate failures); it now restores mp.json; A-D restored.
- Gate on build/merge_fe_fixw.dylib: det_out/gate_fe_fixw_20261015-1211 smoke 3/3, mp2 19 PASS 0 FAIL; left_prompt with "fe":1 in the
  host's prefs 16 PASS 0 FAIL (det_out/lp_fixw_fe1_b.log); FE dive on it 23/23 (ext/foreign_expedition/out/dive_20261015-121129).
  Shared build/dsrig.dylib = this build since 12:30.

## GitHub visibility (2026-10-15, replaces the 2026-10-14 "all private" rule for these 2 repos)
- Owner, word for word: "Make them public". github.com/AmfiLogos/ds_multiplayer and github.com/AmfiLogos/ds_launcher are PUBLIC. All his other repos stay private.
- So: never commit game files, dumps, saves, binaries or keys (LANE_RULES.md rule 5).

## Distance (owner 2026-10-15)
- "it's impossible to have 2 players on different floors atm in regular playthrough": no different-floor test. Retest only if a feature ever splits players across floors.

## v11 candidate (gatefix, 2026-10-15 12:31-17:36) - CHECK only, nothing shipped (lan_latest = v10)
- Frozen src rig/release/v11c_src (all lanes merged: corenext, spectator, launcher, FE + left_prompt fix, PvP), Mac dylib e58c6dec81d172da,
  summary rig/release/gate_e58c6dec81d172da.summary. Result: GREEN = Mac 31/31 (round 2) + Windows 8/8 (rerun after win/ fixes), src unchanged.
- Round 1 (stopped): uxgame / teams_v10 Escape checks red because FE results left in the copies' data/fe opened the FE results
  window at load and took the first Escape. release_gate.sh now moves data/fe of every copy aside at start (data/fe_gatestash_<time>).
- Windows port fixes (win/ only): PvP split-Guid calls (gen_port ABI rewrites); FactionManagementView.OnWorldRefresh inlined on
  Windows (new WIN_WRAP on FactionManagementPresenter.WorldRefreshed, caught by the inline-audit stop); zlib inflate for pvp_wiki.c
  (win/third_party/zlib from the local Unity il2cpp zlib 1.3.1); fe/*.c added to the Windows module list (st_mtimespec / localtime_r shims);
  Windows reads DSRIG_RANGES_WIN (the Mac ranges path leaked into the kit run); W stopped before the base soak.
- Found: gen_port always wrote det_ranges_win.tsv to ~/ds_multiplayer/build, but W / the gate / the kits / the bottles read
  win/build/det_ranges_win.tsv, stale since 2026-10-12 (same rules until the core_next merge, so v7-v10 were consistent). Now written to win/build.
  The released v10 kit and "DS Windows Test" keep the 2026-10-12 file (their tested combination).
- For a v11 release: rerun release_gate.sh v11 on the final src (the gate now includes the Windows fixes), then release_all.

## lan_v11 released (shipv11, 2026-10-15 17:56-18:05) - owner: "Ship now (2026-10-15)" (AskUserQuestion, on the two-part green result)
- Mac dylib sha256 e58c6dec81d172da1f13a0e7f1890c3f32be6d8a33509342ee1913a33b5c879b (rig/release/dsrig-e58c6dec81d172da.dylib), Mac ranges
  bb8bb987e6762fc3... (rig/release/det_ranges-e58c6dec81d172da.tsv), frozen src rig/release/v11c_src (= v11_src, diff empty).
- Windows baselib.dll sha256 2938e2161e3c852afae06c28e66e1dbaf6e39535b67ef47a9f233ca74b8299da (copy rig/release/baselib-2938e2161e3c852a.dll),
  Windows ranges a505e49379c0ca22... (rig/release/det_ranges_win-a505e49379c0ca22.tsv; 70595 ranges = the count the tested kit run loaded).
- Evidence: Mac 31/31 = rig/old_versions/gate_e58c6dec81d172da-2026-10-15-1749.summary (round 2, 14:00; the stopped 17:42 gate moved it there and
  left a partial rig/release/gate_e58c6dec81d172da.summary: not the evidence). Windows 8/8 = rig/det_out/win_lane_e58c6dec81d172da_20261015-1654/.
  Release note rig/release/release_note_e58c6dec81d172da.txt.
- release_all.sh --owner-approved <sha> <Mac summary> <Windows rerun dir> "<owner words>": new path for a two-part green gate the owner approved
  (Mac lines no FAIL except windows_*, dylib/src unchanged PASS, every .res PASS, DLL hash = the rerun's). Never edits a summary.
  Also: release_all now uses the gate-frozen release/det_ranges-<sha16>.tsv (before: build/det_ranges.tsv, which other lanes rewrite).
- Shipped: rig/lan_latest (v10 -> rig/old_versions/lan_latest-2026-10-15-1757), ~/Applications/Dungeon Settlers MP.app (old one in
  ~/Applications/old_versions/Dungeon Settlers MP-2026-10-15-1757.app), friend kit rig/DungeonSettlersMP_friend_kit.zip, all dylib e58c6dec.
  "DS Windows Test": baselib.dll 2938e216 + det_ranges_win a505e493 (old in win/old_versions/*DSWindowsTest-2026-10-15-1758*).
  Windows test kit win/kit/DSMP_WindowsTestKit_25846389.zip (DLL 2938e216, ranges a505e493, payload from the 17:33 v11 kit dry run; old one in win/old_versions).
- GitHub: tag lan_v11, release kit-lan_v11 https://github.com/AmfiLogos/ds_multiplayer/releases/tag/kit-lan_v11
