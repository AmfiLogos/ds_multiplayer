# How the game fits text (CJK, ko, long ru/de/fr) - research 2026-10-17

Build 25846389. Sources: uiprefab bundle (`uiprefab_assets_all_836f...`, read with UnityPy plus the monoscripts
bundle), fonts_shared / config bundles, `~/ds_skill_truth/dump_25846389/dump.cs`, Ghidra decompile
`~/ds_skill_truth/decomp_25846389/`. Scripts and raw JSON: session scratchpad `.../scratchpad/cjk/`
(`census.py` -> `ui_gos.json`, `an.py`, `cat.py`, `tree.py`, `extra.py`, `face.py` -> `faces.json`, `dc.py`).

## What the game does

- The game never changes font size, line spacing or character spacing per language. DASText only swaps the font
  asset and the material (`LanguageSetting.GetFont` / `GetMaterial`). CJK text is drawn at the same point size as English.
- The only per-language size rule is tooltip WIDTH (`Tooltip_Base.ApplyLanguageWidth` 0xF38208): 276 default,
  296 for fr/de/es, 326 for ru, 306 for pt-BR (hard-coded). zh/ja/ko use the default 276.
- Body text (tooltips, scroll content) uses a FIXED size (12 in tooltips, 14-16 in popups), wrap on, overflow
  "Overflow", no autosize. The box grows: the text has a ContentSizeFitter (vertical = PreferredSize) at a fixed
  width, inside a VerticalLayoutGroup with childControlHeight off, and the tooltip root has its own fitter.
  Before placing a tooltip the game calls `LayoutRebuilder.ForceRebuildLayoutImmediate`.
- Short labels and buttons in fixed boxes use TMP autosize: 1073 of 2529 texts (42%). Median min/max = 0.75;
  the most common pairs are 10-12, 8-12, 12-14, 12-15 and 20-24. Minimum size is almost always 8, 10 or 12.
- Autosize and a growing box are never mixed (5 of 1073 autosize texts have a fitter).
- Ellipsis is rare: 128 texts (5%), mostly list cells; 48 of them through DASText `_overrideEllipsis`
  (`_maxEllipsisLines` 1 or 2). No Truncate, no Masking anywhere.
- The CommonNotice popup (confirm/cancel box) does NOT grow: box 546x208, `Txt_Notice` 520x109 at size 16, no
  autosize, overflow on. Long text runs out of the box. Its buttons are 130x32 with autosize 8-18.
- CJK fonts are taller: NotoSerifSC / SourceHanSerifTC / NotoSerifJP line height = 1.437 em, Lora = 1.280 em
  (+12%). Korean draws Hangul from the MaruBuri fallback: 1.345 em (+5%). A CJK glyph is 1.0 em wide.

## Evidence

### 1. TMP settings census (uiprefab bundle, 2529 text components: 2523 DASText, 5 TMP UGUI, 1 TMP 3D)

| Setting | Values (count) |
|---|---|
| m_enableAutoSizing | off 1456, on 1073 |
| m_TextWrappingMode | Normal(1) 2419, NoWrap(0) 68, PreserveWhitespaceNoWrap(3) 42 (input fields) |
| m_overflowMode | Overflow(0) 2401, Ellipsis(1) 128 |
| m_lineSpacing | 0: 2373; 80: 124 (credits only); -50: 16 (EntityInventoryUI); -4: 10; others 6 |
| m_characterSpacing | 0: 2411; -1: 112 (EntitySkillUI, CreateCampaignPopUp); -7: 5; -3.5: 1 |
| m_wordSpacing, m_charWidthMaxAdj | 0 everywhere |
| m_margin | 0 on 2378; small left/top pads (8, 4, 10) on the rest |
| Fixed m_fontSize (top) | 14: 955, 12: 796, 16: 297, 10: 75, 18: 64 |
| Autosize min-max (top) | 10-12: 383, 8-12: 170, 8-72: 130, 10-14: 88, 12-14: 76, 8-14: 62, 12-15: 49, 8-16: 41 |
| Autosize min/max ratio | median 0.75 (quartiles 0.67 / 0.75 / 0.83); without the 72-max ones, median 0.83 |

By role (button = text under a Button / DASButton / Click*SoundButton / ToggleButton; title = VeryDecorative font
or name has Title/Header; body = name has Desc/Content/Notice/Info/...):

| Role | n | autosize | median min/max | top autosize pairs | top fixed sizes | fitter on text |
|---|---|---|---|---|---|---|
| button | 412 | 33% | 0.71 | 12-15, 8-12, 8-14, 10-14 | 14, 12, 10, 16 | 0 |
| title | 149 | 18% | 0.71 | 10-14, 10-12, 18-40 | 14, 18, 12, 36, 40 | 26 (vertical Preferred) |
| body | 53 | 25% | 0.67 | 8-12, 8-14 | 12, 16, 14, 18 | 20 (vertical Preferred) |
| label/other | 1915 | 47% | 0.83 | 10-12, 8-12, 8-72, 12-14 | 14, 16, 12 | 32 |

Stock popups and tooltips (from the prefabs):

| Object | Size | Text settings |
|---|---|---|
| CommonNotice/Panel_Notice/Img_BackGroundBox | 546x208, GridLayoutGroup cell 130x32, spacing 53, top pad 162 | - |
| CommonNotice/.../Txt_Notice | 520x109 | Lora 16, no autosize, wrap, Overflow |
| CommonNotice/.../ConfirmButton, CancelButton Txt_Name | 130x32 button | autosize 8-18, wrap, Overflow |
| SystemMenuUI menu buttons Txt_Name | VLG spacing 12 | autosize 20-24 |
| IntroNoticeUI Txt_Title | 965x54 | Cinzel 40 fixed |
| IntroNoticeUI scroll Content | VLG ctrlW1 ctrlH1 pad 25/20/10/10, CSF v=Preferred | Txt_EventScript Lora 14 fixed |
| UpdateNoticeUI Txt_UpdateNoticeTitle | 665x54 | Cinzel autosize 18-72 |
| UpdateNoticeUI Txt_UpdateNewsContent | 580 wide, CSF v=Preferred | Lora 16 fixed, lineSpacing 32 |
| RenameUnitUI Txt_Title | 280x30 | Cinzel 24 fixed |
| SubUI_Alert AlertTitle | 171x26 | Lora 14, Ellipsis, 1 line |
| HelpNotice Txt_PriceValue | 1036x36, CSF h=Preferred | autosize 12-18, charSpacing -1 |
| Tooltip_* Text_Title / Text_MainTitle | fixed width, CSF v=Preferred | Lora 14 (Item: 18), fixed |
| Tooltip_* Text_Desc / Text_Action / Text_Flavor | fixed width, CSF v=Preferred | Lora 12, fixed, wrap, Overflow |
| Tooltip_Item TooltipValueRow Txt_Name / Txt_Value | 220x16 / 120x16 | autosize 10-12 |
| Tooltip_* Txt_HoldKey | hold box | autosize 8-12 |

### 2. Layout: does the box grow?

| Prefab | Root | Inner layout |
|---|---|---|
| Tooltip_Item, Tooltip_Alert, Tooltip_Choice | CSF h=MinSize v=MinSize, VLG (no child control) | Layout_Tooltip 276 wide: VLG ctrlW1 ctrlH0, spacing 4, padding 10/10/6/6, CSF v=MinSize; each text CSF v=Preferred |
| Tooltip_SimpleText, Tooltip_TextFit, Tooltip_LockedReason | CSF h=Preferred v=MinSize, VLG | Layout_Tooltip: VLG ctrlH0, spacing 4, padding 10/10/6/6, CSF h,v=Preferred; Layout_Title VLG + CSF Preferred |
| AlertPanelUI | ScrollRect | AlertContent VLG spacing 6, CSF v=Preferred |
| IntroNoticeUI / UpdateNoticeUI | fixed panel | scroll Content VLG + CSF v=Preferred (grows inside a scroll view) |
| CommonNotice | fixed 546x208 | no fitter, no layout on the text: does not grow |

Fit modes from dump: Unconstrained 0, MinSize 1, PreferredSize 2.
Code: `Tooltip_Base.ShowTooltip` 0xF38070 calls `ApplyLanguageWidth`, then `Show`, then the `DelayedAlignment`
coroutine, which calls `LayoutRebuilder.ForceRebuildLayoutImmediate` and then sets the position.
`CommonNoticeView` (Show_Confirm, Show_Select, SetTextConfirmCancel, SetTextYesNo) never sets a size.

### 3. Per-language changes

| Place | What changes per language | Evidence |
|---|---|---|
| DASText.Awake 0xF5B29C, RefreshOnRunTime 0xF5B3D0, SetText 0xF513DC, SetTextKey 0xF5B704 | font asset + material only; restores the stored wrap / overflow / maxVisibleLines, or applies ellipsis | decompile |
| LanguageFontSO (dump 240373) | 4 mappings x (EasyRead, VeryDecorative) x (Font, Default, SoftOutline16, SoftOutline24). No size, scale or spacing fields | dump |
| CommonNoticeView.SetTextForSpecificLanguage 0xF65728, SetFontForSpecificLanguage 0xF65D48 | font + material (EasyRead, SoftOutline16) only | decompile |
| Tooltip_Base.ApplyLanguageWidth 0xF38208 | tooltip width and hold-box width from `LanguageWidthDictionary` (SerializableDictionary<LanguageType,float>); pt-BR hard-coded 306 / 102 | decompile + prefab |
| UIScaleRoot.ApplyScale 0x863818 | global UI scale x additionalScale (1.0 on all 60), not per language | decompile + prefab |

Tooltip width values (same on all 16 Tooltip_* prefabs):

| Language | Tooltip width | Hold width (most) | vs default |
|---|---|---|---|
| default (en, ko, zh-Hans, zh-Hant, ja) | 276 | 114 | - |
| French, German, Spanish | 296 | 134 | +7% |
| Portuguese (Brazil) | 306 (code) | 102 (code) | +11% |
| Russian | 326 | 154 | +18% |

(Tooltip_Choice hold box: 72 default, 92 fr/de/es, 112 ru.)

Search for other per-language sizing: in all game classes of the decompile (TMPro classes left out), `set_lineSpacing`,
`set_characterSpacing`, `set_wordSpacing`, `set_textWrappingMode` have no caller. `set_fontSize` / autosize setters are
called only by CreditView (18, autosize 14-18), NarrativeIntroView (autosize min 18), TutorialView, DASText_GameObject
(world text) and two Edgar editor classes; none of them reads the language. Only 5 classes read the
current-language static; the others (GeneralSetting, TextSheet, SubUI_SettlerNameCase) do not size text.
TutorialView.AdjustFontSizeIfNeeded 0xF59A0C: start at 16, then -1 per step while the text's preferred height is more
than the rect height (a hand-made shrink-to-fit, same for all languages).

### 4. Font metrics (TMP faceInfo, all assets sampled at 60 pt)

| Font asset | Used for | lineHeight | ascent | descent | sample glyph advance |
|---|---|---|---|---|---|
| Lora-Regular SDF | body, all Latin/Cyrillic languages | 76.80 (1.280 em) | 60.36 | -16.44 | H 47, a 31 |
| Cinzel-Bold SDF | titles, Latin | 80.88 (1.348 em) | 58.56 | -22.32 | H 52, a 39 |
| MaruBuri-Regular / SemiBold SDF | Hangul fallback (ko) | 80.70 (1.345 em) | 57.90 | -22.80 | 가 58 |
| Gabriela-Regular SDF | Cyrillic fallback for Cinzel titles | 76.86 (1.281 em) | 59.16 | -17.70 | Ж 61 |
| NotoSerifSC / SourceHanSerifTC / NotoSerifJP _Main | zh-Hans / zh-Hant / ja body | 86.22 (1.437 em) | 69.06 | -17.16 | 中 60, あ 60 |
| MaShanZheng (zh-Hans titles) | titles | 60.00 (1.000 em) | 52.80 | -7.20 | 中 60 |
| LXGWWenKaiTC (zh-Hant titles) | titles | 79.02 (1.317 em) | 61.92 | -17.10 | 中 60 |
| KaiseiHarunoUmi (ja titles) | titles | 86.88 (1.448 em) | 69.60 | -17.28 | 中 60 |
| LiberationSans SDF (TMP default, last resort) | missing glyphs | 98.89 at 86 pt (1.150 em) | 77.85 | -18.22 | H 62 |

Per line at common sizes: size 12: Lora 15.4 px, MaruBuri 16.1 px, CJK body 17.2 px. Size 16: Lora 20.5 px,
CJK body 23.0 px. (TMP sets the line pitch from the font that draws the glyphs, so a Korean line in a Lora text
gets MaruBuri height. This is standard TMP behaviour, not decompiled here.)

## What our kits should copy

1. Body text and any text that can be long: fixed size (12 for tooltip body, 14 for popup body, 16 for big notices),
   wrap Normal, overflow Overflow, autosize OFF. Give the text a fixed width and a ContentSizeFitter
   vertical = PreferredSize; put it in a VerticalLayoutGroup with childControlHeight = false, spacing 4,
   padding 10/10/6/6, and a fitter on the box root (vertical MinSize or PreferredSize). Call
   `LayoutRebuilder.ForceRebuildLayoutImmediate` on the root before reading its size or placing it.
   This makes the box grow with CJK line height and long ru/de text. Do not copy the CommonNotice fixed box.
2. One-line labels and buttons in a fixed box: autosize ON, max = design size, min = 0.75 x max, rounded, never
   below 8 (game pairs: 10-12, 12-15, 20-24). For a small fixed button that must hold long words (like the 130x32
   CommonNotice buttons) the game goes down to min = 0.44 x max (8-18). Wrap stays on, overflow Overflow.
3. Do not mix autosize and a fitter on the same text.
4. Width per language for boxes with a fixed width: default (en, ko, zh, ja) = W; fr/de/es = W x 1.07;
   pt-BR = W x 1.11; ru = W x 1.18 (tooltip: 276 / 296 / 306 / 326).
5. Fixed-height single-line boxes: height at least 1.44 x font size plus margins, so CJK fits
   (Lora needs 1.28 x size, Korean 1.35 x size).
6. Do not change font size, line spacing or character spacing per language. The game does not.
7. Ellipsis only for list cells (overflow Ellipsis with 1 or 2 max lines, as DASText `_overrideEllipsis`).
