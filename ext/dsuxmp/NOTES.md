# dsuxmp sub-lanes (UX review 2026-10-25, ds_multiplayer)

## uxmp-pvp

Files: `src/pvp_ui.c`, `src/pvp_game.c`, `src/pvp_rules.c`, `src/pvp_rules.h`, `src/pvp_rules_core.c`, `src/pvp_units.c`,
`src/pvp_modcheck.c`, `lang/*.json`. No `src/net/` change: P1 uses dsnet mail (two new app mail kinds, 17 and 18).

### What changed (player view)
- M2: Start Match with a team that has no unit (or no player) keeps the window open and shows the reason in red
  (#F16071, the lobby's refusal colour). Nothing is sent. The teams are counted as the match start counts them
  (`pvp_core.c` start: the units each player of the team owns; brought units: the host's roster).
- P1: a guest's PvP window shows the host's rules and teams. The host mails what its window shows (mail kind 17,
  `pvpr_view_format` text) when it changes, when a player joins, and when a guest opens its window (kind 18 = "ask").
  Display only: `pvp_rule_get`, the fingerprint and the join-point block never read it; the match rules still go in
  the stamped order at Start. Rate: at most 4 sends a second, checked every 10 frames.
- P2: every PvP match message (planning pause, countdown, "Fight!", flags, preparation) shows on a dark plate under the
  score panel, under whatever shows below the bars (Ready / Propose Fight, Bring Units, the vote box, the round screen).
  The planning one stays while the pause lasts, the others 4 s. Without the match HUD (world back): the game's notice.
- M5: the PvP window, its Bring Units list (the text list; the card window already followed the scale) and the result
  window: root scale = the game's UI scale (`mu_ui_scale`), then smaller only to fit the screen with 16 px free.
- M11: Title Case on my buttons and row labels: Save as Preset..., Delete Preset, Propose Fight, Not Yet, To Win,
  Round Time, Vanilla Check, Enemy Vision / Windows / Portraits / Health Bars / Status / View Range. The two refusal
  sentences that name the rule say "Vanilla Check" too. Keys renamed in all 10 tables (translations kept).

### Decisions (question, pick, why)
- Which messages move under the panel? All PvP match messages, not only the planning one: they all came from the same
  call and sat in the same place over our HUD.
- M2 with no player on a team: a new line ("Each team needs at least one player. Click a player's name to move them to
  the other team.") in 9 languages, because "Right-click a unit's portrait" cannot fix a team with nobody in it.
- P1 transport: mail (outside the sim) instead of the stamped rules order: a display change must never touch the
  match state, the desync fingerprint or the join-point save.
- A guest with no window data yet shows the default rules (it showed its own saved rules before).

### Tests
- `ext/dsuxmp/pvp_pair.py` (pair, two hidden loader copies UPA / UPB, OS port; instances from
  `ext/dsuxmp/make_instance_pvp.sh`). The dylib = `ext/dsuxmp/build_mouse.sh <out>`: the LOADER'S multiplayer module
  (mp_module.c + src, as ds_modloader loader/build.sh) with the fake mouse. Trap: the rig's own dylib (src/build.sh) also
  loads as a mod but has no loader host: no kit, so no UI scale (M5 looked broken) and no Bring card window.
  Before-build = commit 2b33cf6 (origin/main + the probe only): `SRC=<git archive 2b33cf6>/src build_mouse.sh <out>`.
- Traps met: shot paths must be under `~/ds_*` (DSRIG_WRITE_DIRS); the guest that arrives with no unit gets the Teams
  window (close it first); the base save's units can be out in 4 s (the round ended before the first planning pause):
  the test adds +60 Constitution; `UIScaleManager.SetScale(v, true)` while running rescales our windows (they read the
  scale) but the game's HUD keeps its layout, so P2 is measured at the start scale only (runs at 1.0 and 1.4).
- Not updated (other lane's old test, not in gate 0126): `ext/pvp/tests/pvp_ui.py` hovers the tip title 'Round time'.
