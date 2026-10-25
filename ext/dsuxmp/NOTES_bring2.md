# dsuxmp2-bring: Bring Units card window, CARD RULE + CLOSE RULE (owner 2026-10-25)

Branch `dsuxmp2-bring` (base dsuxmp2 c4c4b26). Files: `src/mu_bring.c`, `src/mu_ui.c` (one comment), test
`ext/dsuxmp/bring_test.py` + `bring_make_instance.sh` (hidden copy UBR) + `bring_probes_on_main.patch` (before-build probes).

## What changed
- CARD RULE: each unit card is drawn INSIDE its row button (as Units Laboratory 0.1.37, ds_custommodes 0d5216a): a click on
  the card, its texts or icons goes up to that button, which flips the tick. Measured: origin/main already ticked a card on
  a click anywhere on it (the row button under the card since 0e1cf7e, 2026-10-20; the card, texts and icons let clicks
  through, and the game's hover cards are polled, not raycast). So this part changes nothing a player sees today; it keeps
  the rule if a part of a card ever takes the hover (Units Laboratory's snapshot line). The other picker rows of the window
  (saves on the left, Saves / My Units tabs) are whole buttons already.
- CLOSE RULE: the game's red X (`mpesc_close_x(&esc_bring, ...)`), 38 units, 16 from the right edge, centred on the title
  line (10..50). A click = Escape = Cancel (`finish(0)`: nothing sent, the ticks not kept). With the X the Cancel button is
  gone; Bring stays; the page arrows are centred in the room Cancel left. Older kit (NULL): Cancel stays.
  `tools/close_x_guard.py` no longer lists mu_bring.c.
- Test read-back (`mpm_bring_state`): `tgt_px` also gives `card0`, `icon0` (card 0's first icon after its face) and `x`;
  `tip` = the game's hover card state (mu_tip_test_state).

## Test
`ext/dsuxmp/bring_test.py <lang> <W>x<H> <ui scale> <dylib> [--tag NAME]` on one hidden loader copy UBR (make it once:
`bring_make_instance.sh UBR`), shared loader build `~/ds_launcher_wt_dsuxmp2/loader/build`. Take a slot first
(`slot.sh take dsuxmp2-bring 1`). dylib: `LOADER=~/ds_launcher_wt_dsuxmp2/loader zsh ext/dsuxmp/build_mouse.sh <out>`.
Before-build: `git archive origin/main` + `bring_probes_on_main.patch` (read-back only), then
`SRC=<that>/src LOADER=... build_mouse.sh <out>` (copy `win/math/build/libdsmath_arm64.a` into that tree first).
Hub host lobby > Bring Units: CARD_middle, CARD_icon (fake mouse through the game's Input), X_shown (`mpm_esc`), X_closes
(window closes, fe_status `received` unchanged, opened again with nothing ticked); guards HOVER_card, BRING_sends.

Results (2026-10-25): en 960x540 UI 1.4 and de 1728x1000 UI 1.0: after 6/6 PASS; before (origin/main e7ba260 + probes)
4 PASS, 2 FAIL (X_shown, X_closes). CARD_middle / CARD_icon / HOVER_card / BRING_sends pass on both.

## Decisions (question, pick, why)
- Where the red X goes: top right, centred on the title line, 38 units, 16 from the edge; the game's Load Campaign X and
  Units Laboratory's windows sit there.
- The page arrows after Cancel went: centred between "Picked n / max" and Bring (the free room), like before with Cancel.

## Traps
- On origin/main `mpm_esc` is "open the Esc menu" (the CLOSE RULE dump came with c4c4b26): the test sends
  `mpm_return_game` after it when the reply has no `windows`.
- `i18n_tool.py extract` rewrites `lang/_where.tsv` (line numbers only): not committed here (merge noise for the parent).
