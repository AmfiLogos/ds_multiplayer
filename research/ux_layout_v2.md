# Multiplayer mod: layout spec v2 (2026-10-13, lane uxdesign)

Owner feedback: the organization of the mod's screens is still poor. This spec gives each screen fixed sections, a fixed order, sizes, and the stock parts to clone. Style values (fonts, colours, prefabs) come from `research/ux_design_2026-10-13.md` section 2. No source edited, no game run for this spec (the stock sprites were read from the game's own asset bundles, read-only).

Units: canvas units. The HUD / menu canvas has a 1663x1080 reference, so 1 unit = 0.909 px at 1512x982 and 0.5 px at 960x540. "Lora 16" = Lora-Regular SDF size 16. "Cinzel 24" = Cinzel-Bold SDF size 24.

## 0. Stock parts used in this spec

| Part | Stock source (clone it) | Size |
|---|---|---|
| **Gear icon** | Sprite **`ITEM_CogWheel`** (16x16 pixel-art cog, grey; bundle `icon_gameplay_assets_all`). It is the game's Cog Wheel item icon. Copy: `research/ux_shots/design/sprites/ITEM_CogWheel.png`. | shown at 20x20 |
| Gear button frame | Sprite `topLineButton_Frame1` (Recruit `Img_BackGroundBox/Img_ExitButton`, already used by the Teams HUD button) | 32x32 |
| Collapse toggle | Sprites `ListToggle_On` / `ListToggle_Off` (framed up / down chevrons, uiprefab bundle) | 24x24 |
| Value selector arrows | Sprites `ArrrowIcon_Previous` / `ArrrowIcon_Next` (stock spelling) | 20x20 |
| On / off toggle | Sprites `ToggleButton_Normal` / `ToggleButton_Selected` | 28x28 |
| Window (large) | Task Planner `Img_BackPanel` (ContentsBoxNew) | 1320x888 |
| Window (small) | Notice box `CommonNoticeView/Panel_Notice/Img_BackGroundBox` (sprite Box: border `#915847`, fill `#222025`) | stretch |
| Section title band | Expedition `Img_CurrentRegionTitle` (RegionTitleDecorate) + Cinzel 24 | 556x44 |
| HUD header band | `QuestUIView/SubUI_QuestSlot/Panel_Title` (gradientTransparent) + Lora 18 | 340x24 |
| List rows | `Slot1` (#36323a) / `Slot2` (#2b282f), alternating (Task Planner rows) | wide x 32-44 |
| Primary button | Recruit `Img_RecruitButton` (RecruitButton, red) + Lora 18 | 232x40 |
| Secondary button | Expedition `BackButton` (BottomButton) + Lora 16 | 206x30 |
| Divider | `Devider_Detail2` | width x 2 |
| Menu button (title column) | `SubUI_TitleMenuButton_*` (Button_Normal) + Lora 22 | 262x52 |

**About the gear (the owner asked for "the game's own gear icon"):** I listed every Sprite in every bundle of the installed game (`StreamingAssets/aa/StandaloneOSX/*.bundle`) and searched the names for gear, cog, setting, option, config and wrench. The game has **no gear or settings icon in its UI art.** Its HUD reaches options through the menu button (sprite `Button2_Icon_Menu`, three bars) and then the text button "Option". The only cog in the game's art is the item icon `ITEM_CogWheel` (and `ITEM_AncientCogWheel`, a gold version). It is a clear gear shape in the game's pixel style, so this spec uses it inside the stock 32x32 top-line frame. The owner should confirm that an item icon used as a settings button is acceptable.
- **How to get it at run time:** in a loaded world the resource panel already shows Cog Wheels (`PlayerOwnedPanelView`, `Img_ItemIcon` with sprite `ITEM_CogWheel`), so the sprite is in memory. On the title screen the gameplay icon bundle may not be loaded yet. Look it up by name over all loaded Sprites (`Resources.FindObjectsOfTypeAll<Sprite>()`). If it is not found, load it through the game's item-icon lookup. I have not verified either path in the game.

## 1. Connection indicator (same widget everywhere)

Three vertical bars built from plain Images (the game has no signal-bar sprite; the Teams code already makes plain rects with `new_rect`).

```
 ▂▄▆      bar 1: 3x5   bar 2: 3x8   bar 3: 3x11   gap 2   total 13x11, bottom-aligned
```
| Level | When (from `dsnet_player_net`: ping_ms, ticks_behind, state) | Lit bars | Colour (stock HUD colours) |
|---|---|---|---|
| 3 | state in sync AND ping < 120 ms | 3 | `#71ab40` (stock HP green) |
| 2 | ping 120-250 ms, OR catching up (ticks_behind ≥ 40, the dsnet `ok_behind` default) | 2 | `#ea9f3d` (stock hunger orange) |
| 1 | ping > 250 ms, OR resyncing (state 2), OR loading (state 1) | 1 | `#c82e48` (stock stress red) |
| X | disconnected / left | none; a "✕" in Lora 14 `#f16071` in the same 13x11 box | |

- Unlit bars: `#4a4550`.
- No milliseconds anywhere, not even in a tooltip. Tooltip text (stock `TooltipUI`, if it can be fed): "Good connection" / "Slow connection" / "Poor connection: loading or resyncing" / "Disconnected".
- Your own row shows no bars (blank 13x11).
- Replace: `ui_signal_bars` (today 4 levels, thresholds 60 / 120 / 200), `ui_player_row3` (`|` glyphs, `#58D68D`), the "%d ms" text in `ui_player_row` / `ui_player_row2` (src/ui_model.c:357-428).
- Used in: the lobby player list (§2b), the in-game panel (§3), the Teams other-player rows (§4), the disconnect box title row (§5a).

## 2. Title: host page and lobby

### 2a. Host page (title column; it stays a column like the stock title menu)

The stock column: `Img_BackGroundBox` (BackPanel, 324 wide). Buttons are 262x52, Lora 22, stacked every 70 units. The divider is `PlaceHolder/Image` (`Icon_Devider` 260x6).

```
 ┌────────────────────────┐
 │    DUNGEON SETTLERS    │  stock logo (unchanged)
 │                        │
 │       Host a Game      │  Cinzel 24 #ccbeb8, centred, 262x34   (section header)
 │ Your name              │  Lora 14 #a69085, left, 262x18
 │ [ The owner               ] │  input 262x44, text Lora 18 #ccbeb8, vertically centred, no magnifier
 │                        │
 │ [⚙] Settings        [v]│  row 262x36: gear button 32x32 (frame + ITEM_CogWheel 20x20),
 │  Pause anyone · Speed  │   "Settings" Lora 18, ListToggle 24x24 at the right;
 │  on · Gear shared      │   summary under it: Lora 12 #a69085, 2 lines max
 │ ~~~~~~~ divider ~~~~~~ │  Icon_Devider 260x6
 │ [  Host from a save  ] │  menu button 262x52, Lora 22   <- first = main path
 │ [ Host a new campaign] │  menu button 262x52, Lora 22
 │ [        Back        ] │  menu button 262x52, Lora 22
 └────────────────────────┘
```
Settings open (click the gear, the word "Settings" or the toggle): the summary is replaced by three selector rows. The divider and the buttons move down.
```
 │ [⚙] Settings        [^]│
 │ Pause       ‹ anyone ›  │  row 262x32: label Lora 16 left; value Lora 16 #ccbeb8 centred
 │ Speed-up    ‹   on   ›  │   between ArrrowIcon_Previous / _Next (20x20) at x 150 and 250
 │ Gear  ‹ shared, own units › │ value text may shrink to Lora 14; never wraps
```
- The settings are no longer button-shaped rows that look like actions (today "Pause: anyone" looks like "Host from a save").
- The join page uses the same column grammar: header "Join a Game" (Cinzel 24); "Host address" label + field; "Your name" label + field; error line Lora 14 `#f16071` under the fields; divider; [Connect] and [Back].
- Owning files: src/mpmenu.c (clones, layout), src/mpmenu_model.c (labels, visible set; new state "settings open").

### 2b. Lobby (host and guest): a framed window, not the Esc menu with hidden buttons

Window: small-window frame (notice `Box`), centre anchor, 760x620. The Esc menu's dim layer stays behind it. The same window opens from Esc > "Lobby" in game.

```
 ┌──────────────────────────────────────────────────────────────┐  760 wide
 │                     MULTIPLAYER LOBBY                         │  Cinzel 32, y +270 (title band 0-70 from top)
 │──────────────────────────────────────────────────────────────│  Devider_Detail2
 │ PLAYERS  2 / 4                                               │  Cinzel 20 #ccbeb8 + Lora 14 #a69085, y +200
 │ ┌──────────────────────────────────────────────────────────┐ │
 │ │ ● the owner          host                                     │ │  row 712x34 Slot1: dot (owner colour) + name Lora 18,
 │ │ ● Ben           in the lobby                       ▂▄▆   │ │  status Lora 14 #a69085 at x -100, bars at the right edge -24
 │ │   (empty)                                                │ │  rows 3-4: Slot1/Slot2, Lora 14 #a69085 "(empty)"
 │ │   (empty)                                                │ │
 │ └──────────────────────────────────────────────────────────┘ │  block 712x136, top y +180
 │ INVITE                                                       │  Cinzel 20, y +20
 │  Steam friends      [ Steam invite ]                         │  Lora 16 label at x -340; BottomButton 206x30 at x -40
 │  Same network       192.168.1.4:47777   [ Copy ]             │  Lora 16 address; BottomButton 100x30 "Copy" -> "Copied"
 │  Over the internet  open port 47777 on your router           │  Lora 14 #a69085
 │ [⚙] SETTINGS  Pause anyone · Speed-up on · Gear shared  [v]  │  Cinzel 20 + gear 32x32 left of it; summary Lora 14
 │                                                              │  #a69085; ListToggle 24 at the right (host only)
 │──────────────────────────────────────────────────────────────│  Devider_Detail2, y -250
 │ [ Leave multiplayer ]                          [   START   ] │  BottomButton 206x30 at (-252,-285);
 └──────────────────────────────────────────────────────────────┘  RecruitButton 232x40 at (+240,-285)
```
- Settings open (host only): the Invite block collapses to one line ("Invite: Steam invite · Copy address"). Three selector rows (as in §2a) plus "Admin mode  [toggle]" (ToggleButton 28x28) take its place. The bottom bar does not move.
- **Guest view:** title "THE OWNER'S LOBBY". No gear and no toggle: the settings line is read-only. No Invite block. Instead, Lora 16 `#a69085` centred: "The game starts when the owner presses Start." Bottom bar: [Leave multiplayer] only, at the left.
- **Start**: enabled when at least the host is in. With a player still loading, Start stays enabled, but the line above the bottom bar reads "Ben is still loading" (Lora 14 `#ea9f3d`).
- Remove: the left-aligned text block on the black plate, "Local: ..." / "Friends outside your home: ..." in one paragraph, and Start in 3rd place.
- Owning files: src/mpmenu.c (`lobby_layout` replaced by a window build), src/mpmenu_model.c (`mpm_lobby_text` split into fields).

## 3. In-game player panel

Same structure as the lobby players block, in the HUD style. Position unchanged (owner): top, under the portrait bar (`PL_TOP` 196, `PL_X` -300). Hidden while the lobby, the character window, a popup, the Portal window or the disconnect box is open (design report #3, #20).

```
 ┌──────────────────────────────┐  frame: notice Box sprite, alpha 0.92, width 300 (fixed)
 │ Players (2)          [⚙] [^] │  header 300x32: gradientTransparent band; Lora 18 #ccbeb8;
 │──────────────────────────────│   gear 28x28 (frame + cog 18x18), ListToggle 24x24
 │ Paused by Ben                │  status row (only when there is one): Lora 16 #d9be8f, 24 high
 │ ● the owner        host           │  rows 300x26: dot + name Lora 16, status Lora 14 #a69085
 │ ● Ben                   ▂▄▆  │   at x 130, bars right-aligned at -12
 └──────────────────────────────┘
```
- **Collapsed** ([v]): header band only (300x32), and the status row stays under it if one exists (pause and votes must stay visible).
- **Gear** opens a small framed pop-over under the header, right-aligned to the panel (notice Box, 260 wide). It closes on a click outside it or on the gear:
```
              ┌──────────────────────────┐
              │ See-through     ‹  0% ›  │  Lora 14 label #a69085, value Lora 14 #ccbeb8 between arrows
              │ Names above     ‹Others› │
              │ Admin mode (host)  [  ]  │  ToggleButton 28x28; host only; "On" turns the header gear tint #f16071
              └──────────────────────────┘
```
- The settings are no longer always-visible text rows.
- Status texts unchanged in meaning: pause, speed request, vote waiting, "You control Ben's units until they return" (Lora 14 `#d9be8f`, under Ben's row).
- Owning files: src/ui_game.c (`plist_frame`, plate, click areas), src/ui_model.c (row texts, bars).

## 4. Teams window: does the earlier spec fit? Yes, with two additions

The hierarchy spec in ux_design_2026-10-13.md section 3 fits this structure:
- zone 1 = your units in the light top strip (hero place);
- zone 2 = free units under the stock region-title band;
- zone 3 = other players' / reserved rows, muted;
- bottom bar = region selector + hint + primary Ready at the bottom right.

It already follows "header, sections, primary bottom right". Additions:
1. Zone-3 row headers get the connection indicator (§1) right after the player's name: "Ben's units ▂▄▆ · Ready".
2. The title band's top-line chip shows the ready count ("Ready 1 / 2"). No gear in this window: Teams has no settings. Host admin mode lives in the panel gear (§3), and the lobby gear for the host.
```
 ┌───────────────────────────────── TEAMS ─────────────────── [Ready 1/2] [X] ┐
 │ (i)                                                                         │
 │ YOUR UNITS        [card][card*][ empty ][ empty ] ...                       │  light strip, cards 92x108
 │ 2 units · 1/3 run                                                           │
 │ ✓ Ready                                                                     │
 │ ╞═ FREE UNITS ═╡  Left-click to take                                        │  RegionTitleDecorate band
 │ [card][card][card] ...                                                      │  Slot1 row, cards 1.0
 │ ▌Ben's units ▂▄▆ · Not ready   [c][c*]                                      │  Slot2 row, cards 0.75, alpha .45
 │ ▌Reserved for Ana (away)       [c]                                          │  Slot2 row, alpha .35
 │ Region [ Veil of Silence ]  your vote · 1 of 2 voted   Next: press Ready  [ READY ] │ bottom bar
 └─────────────────────────────────────────────────────────────────────────────┘
```

## 5. Other screens with unclear grouping

### 5a. Disconnect box (host, a player dropped)
Today: title "Ben disconnected", one question line, [Wait] [Keep playing]. Nothing says what each choice does, and the same fact also shows in the panel and the banner.
```
 ┌──────────────────────────────────────────────┐  stock CommonNotice box (unchanged frame)
 │            Ben disconnected   ✕               │  Lora 24 #ccbeb8 + the indicator's X
 │  The game is paused for everyone.            │  Lora 16 #ccbeb8
 │  Waiting 0:42                                │  Lora 14 #a69085, counts up
 │  Keep playing: you control Ben's units       │  Lora 14 #a69085
 │  until Ben comes back.                       │
 │      [ Keep playing ]      [ Wait for Ben ]  │  stock No / Yes positions: secondary left, primary right
 └──────────────────────────────────────────────┘
```
- The default focus (hatched) stays on "Wait for Ben". Hide the panel status row while the box is open. If Ben comes back, the box closes itself with the toast "Ben is back".
- Host-left box (guest) uses the same grammar: title "The owner left the game" (Lora 24), body Lora 16, [OK]. Owning files: src/ui_game.c, src/mpmenu.c.

### 5b. Map-change vote (floor / settlement)
Today the voters get a stock Yes / No box. The proposer only gets a panel status line and then a toast.
```
 Voter (stock CommonNotice):
 ┌──────────────────────────────────────────────┐
 │        Go down to floor 3?                   │  Lora 24
 │  the owner wants to go down.                      │  Lora 16
 │  the owner ✓   Ben …            52 s left         │  Lora 14 #a69085; ✓ in #71ab40; "…" = not voted yet
 │            [ No ]            [ Yes ]         │  stock buttons, unchanged
 └──────────────────────────────────────────────┘
 Proposer (no box): a fixed strip in the toast style, top centre, under the pause banner,
 HelpNoticeView look (dotted rules), Lora 16 #ccbeb8:
   "Go down to floor 3? Waiting for Ben (1 of 2 agree) · 52 s      [ Cancel ]"   <- BottomButton 120x30
```
- Result: toast on all peers ("Ben said no: staying on floor 2" / "Everyone agreed: going down"). Owning files: src/ui_game.c, src/ui_model.c.

### 5c. Speed request
It stays in the panel status row (§3). Add the voters inline ("Ben wants Fast · press >> to agree"). No box: speed is not worth a modal.

### 5d. Story vote (popup with choices)
Inside the stock story popup, under its title, one line in Lora 14 `#a69085`: "Ben chooses" (non-choosers; the choices greyed through `interactable = false`), or "Vote · 1 of 2 · host breaks ties" (vote mode). Your vote is marked by a `CheckBox_Selected` icon next to the choice, not "(your vote)" text.

### 5e. Portal (return screen), read-only side
Inside the Portal window under its title: a `TextBox` pill (420x34, Lora 16) "Ben is choosing who returns". The buttons are greyed with the stock disabled state (design report #12b, #14).

## 6. Order of work (my suggestion)
1. §1 indicator: one widget in ui_model + ui_game, used by the lobby, panel and Teams.
2. §3 panel. It is the screen seen most, and it fixes the overlap and the clipped rows.
3. §2b lobby, then §2a host / join pages.
4. Teams zones (design report §3, plus §4 here).
5. §5a-e boxes and lines.
