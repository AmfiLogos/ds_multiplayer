# Independent review: plan and approach of the Dungeon Settlers multiplayer mod (2026-10-14, Fable)

Read-only review. Files read: PLAN.md, HANDOFF_v9/v10.md, src/mp_core.c, src/mp_game.c, src/net/dsnet.h,
dsnet_server.c, dsnet_client.c (parts), src/rig_det.c, src/rig_cmd.c (rig_world_hash), src/ord_game.c, src/ord_mp.c,
src/UI_NOTES.md, src/MPMENU_NOTES.md, win/NOTES.md (sections 13-20 + hook table), research/determinism_audit.md,
transitions_design.md, ux_flow_v10.md, ping_camera_v10.md, plan_pvp.md, plan_foreign_expedition_spectator.md,
plan_launcher.md, bepinex_compat.md, ext/pvp/NOTES.md, ext/foreign_expedition/NOTES.md, rig/gate.sh,
rig/release_all.sh, rig/rig.py (lease, guard), win/instances/v10_lane.sh, v10_suite.sh, every win/instances/v10/*.out,
rig/det_out/gate_20261014-*.log, rig/lan_v9/WHATS_IN_V9.txt. No game copy started, no pair leased, no file edited
outside this one.

Marks: **[V]** = seen in the file named. **[I]** = my inference from what I read; not proven by a run.

## Verdict: SOLID WITH RISKS

The netcode and determinism design are sound and well tested on this Mac. The things that are NOT tested are exactly
the things a real first test with a friend will hit: Steam invites, a real Windows PC, real internet latency and loss,
a session longer than ~15 minutes, and real keyboard/mouse. The release process also lets a build freeze before the
full regression runs, which is why v10 failed late. Scope is growing (PvP, Foreign Expedition, spectator, launcher,
unit builder) while the MVP has never been played by two real people on two real machines over the internet.

## Ranked findings

### 1. CRITICAL (process): a build can be frozen and called "gated" before the full regression runs
Evidence **[V]**:
- `rig/gate.sh` runs only: build, `rig.py smoke`, `mp2_test.py --ticks 3000`, `left_prompt_pair.py`. That is the
  "gate green" of PLAN.md. It is a 10-minute smoke, not a regression.
- v10 timeline: `rig/release/v10_src` frozen 16:35; gate 16:35 and 16:54 green (mp2 19/19); dylib
  `dsrig-e7b6c68b07856080` frozen 16:54; PLAN.md "lan_v10 FROZEN ~16:55". The full suites (`v10_lane.sh` 1-3,
  `v10_suite.sh`) started 17:03, AFTER the freeze, and failed: `uxgame.out` 109 PASS / 12 FAIL / 3 tracebacks
  (map change, rejoin mid-floor timed out at 900 s, return screen), `coop.out` 43/2 (outline on the guest),
  `desync_partial.out` 162/13, `win_suite.out` 39/6 (partial resync of positions did not converge on Mac+Windows:
  "after 1 parts the hashes still differ ... positions DIFF", then "deep check timeout"), `x_partial_v10c.out` 40/4.
- `uxgame_cur.out` (18:09, on `pin/dsrig_cur.dylib`, a post-freeze build) passes almost everything: the fixes came
  after the freeze. `build/dsrig.dylib` changed again at 18:56 (sha 7bf39da1, not the frozen one). Six `src/*.c`
  are newer than the frozen dylib.
- Each lane (core / game / menu) validated its own suite on its own build. Nobody ran the merged build through
  every suite before freezing. That is the structural cause, not a slip by one agent.

Recommendation:
- Define "release gate" = `gate.sh` + the full lane list (`v10_lane.sh` 1, 2, 3 + `v10_suite.sh` cross-play) on
  the SAME frozen dylib, all green, one summary file written by the script (sha, suite, PASS/FAIL counts). Freeze
  only after that file exists. `release_all.sh` should refuse a sha that has no green summary file.
- Make the integrated run cheap to repeat: the three lanes already run in parallel on 3 pairs (~1.5 h). That is
  fine. The owner's rule "no partial versions" needs this gate, not a smoke.
- Any FAIL in a suite = not shipped, even when the lane says the test is "a harness problem". Fix the harness
  first (see finding 9), then rerun.

### 2. HIGH (netcode): what the fingerprint does not see, and what the deep hash saw
Evidence **[V]**:
- `rig_cmd.c:356 rig_world_hash` hashes per unit: guid, HP (stat 9), energy (stat 15), task type, dead flag,
  combat state, current / forced / task target; per entity: position bits; plus tick, sim RNG state, weather timers
  and 4 long-lived System.Random states. Nothing else.
- `rig_det.c:1053` says plainly what even the deep hash does NOT cover: behaviour trees, A* paths, animation /
  view state, equipment, inactive mood causes. The deep hash (stats, cooldowns, affecters, inventory, item /
  building / thing lists) runs only every `MP_DEEP_EVERY x fp_interval` steps and is "NOT a desync by itself"
  (`dsnet.h:337-343`).
- `mp_core.c:16-18` (release9 note): "a corrupted RNG stream changed hidden state that resurfaced 2-5 s after a
  proven patch". `desync_partial.out:245`: "deep hash never differed outside a patch window: FAIL, deep_other 1".
  So hidden state that is in neither hash exists and has already produced a late divergence in a test.
- Not hashed at all **[I]**: building states (construction progress, storage contents), task queues and
  priorities, blueprints, farm growth, research / clan data, the global EventQueue backlog, TaskMatcher order,
  story-event state (`ObserverDriven`), region data. A split in any of these shows up in the fingerprint only when
  it later changes a unit stat or a position. On a quiet base (everyone crafting) that can take minutes.

Recommendation:
- Add a periodic (every ~10 s) serialize-and-hash of the campaign save JSON (the audit's own suggestion,
  determinism_audit.md section 7 "Full hash every N ticks: EntityDataContainer.Serialize"). Treat a mismatch like a
  deep-hash mismatch. It is the only check that covers "everything the game saves".
- Run one 60-minute soak (base work only, no dungeon) on a pair, with the full-save hash on. Today the longest
  evidence is ~10 min (`desync_partial`, `teams_desync` 10 expeditions).
- Keep the deep hash as is; make a deep mismatch outside a patch window a release blocker in the test, which it
  already is in `desync_partial_test.py`. Good.

### 3. HIGH (determinism): known unfixed sources and cross-arch gaps
Evidence:
- `transitions_design.md:9, 69-79` **[V]**: floor generation seed = clock-seeded `new System.Random()` in
  `DSDungeonPreDataGenerator..ctor`; Edgar early stop uses `DateTime.Now` on a worker thread. The fix is designed
  ("seed from saved state + iteration cap"). Hooks exist in the Windows table (`GetRandomNumbersGenerator`,
  `GraphBasedGeneratorGrid2D..ctor`, `GetEarlyStoppingHandler b__0`) **[V]** so it looks done, and
  `teams_desync` passes 10 entries. But the fix depends on the generation running the same number of iterations on
  both peers; a slow Mac or a Windows x64 build of Edgar may differ **[I]**. The 10-entry test is Mac+Mac only.
- `rig_det.c:605-623` **[V]**: 4 long-lived System.Random objects are re-seeded at every lock. The audit
  (section 3) lists ~13 clock-seeded `new System.Random()` sites: NPCSpawnPlanner, NPCSpawnHandler,
  `FixTargetUnits`, BT `SetRandomTarget`, `SpendTick`, `SunHeartBloomAttack`. `h_srand` makes every new
  `System.Random()` counter-seeded on the main thread (`rig_det.c:187`), so these are covered as long as they are
  created on the main thread inside the sim. Objects created BEFORE lock (at load) are only covered if they are in
  the 4-object list. Any 5th long-lived Random (a future game update, or one missed) is a silent desync source.
- Shared math: on by default, 24 CRT entries redirected on Windows, "no measurable cost" **[V]**. Fused
  multiply-add: arm64 clang fuses, MSVC x64 does not by default. The audit flagged FMA as the real cross-arch risk
  (section 5). The xplay tests pass 6000 ticks identical, which is strong evidence, but only on one save and one
  fight. **[I]**: FMA differences appear in long accumulations (stat formulas, movement), not in short fights.
- Guid: counter-based in sim, separate stream out of sim **[V]**. A Guid created out of sim that later enters the
  sim (an item created by a UI action that is not an order) would differ per peer. The orders lane routes UI
  events as orders, so this is covered when every path is captured. The audit lists "direct writes with no event"
  (section 6) as the largest risk; ord_game.c covers the Control modes. Residual: new UI paths added in v8-v10
  (Teams, lend, proposal, chat) are outside the sim by design, good.

Recommendation:
- Windows x64: run `teams_desync` (10 floor entries) cross-play, not only Mac+Mac, before the Windows kit goes to
  a real PC. `win_suite.out` has no floor-generation test **[V]**.
- Add an assertion: at lock, count `System.Random` instances reachable from MainFlow that are not in the 4-list
  (a one-time scan); log them. Cheap insurance against the next game build.
- One 2000-tick cross-play run on a BASE save with crafting and building (long accumulations), not only a fight save.

### 4. HIGH (untested paths before a real friend test)
Evidence **[V]**:
- Steam: `WHATS_IN_V9.txt` "Steam join/invite: not tested (test copies have no Steam). The Windows copy has no
  Steam join yet". `win/NOTES.md:157` "the Steam bridge is not ported". `steam_check.c:131` prints "(offline:
  lobby / P2P not tested)". The owner's stated connect path for friends is "Steam invite" (PLAN.md Live version).
- Real Windows PC: never. CrossOver only (`WHATS_IN_V9.txt`). Real keys / mouse on Windows: "your hand test covers
  them".
- Internet: all tests on localhost TCP. `dsnet_cfg` has `max_behind 90`, `ok_behind 40`, `rx timeout 5000 ms`
  **[V]**. No test injects latency, jitter or loss. TCP hides loss as delay, so the failure mode is "timer held"
  (server stops stepping while a peer is > 90 steps behind, `dsnet_server.c:404`), which every player sees as a
  freeze with no message naming the slow peer (ux_flow F10 is about disconnects, not slow peers).
- Save size over Steam P2P: join-point saves are sent in 1 KB fragments, up to 64 MB (`dsnet.h:375`). A 20 MB
  save at Steam relay speeds is tens of seconds during which everyone is paused (PAUSE_ALL) **[I]**. Also every late
  join makes the HOST reload its own save (`mp_game.c:53, 257-261 host_reload`) **[V]**: a friend who joins late
  pauses and reloads the host too.
- Host migration: none. Host closes = session ends for all (`mpmenu_model.c:181, 188`, ux_flow F7). Acceptable
  for an MVP, but say it in the UI.

Recommendation (before the first friend test, in this order):
1. Steam invite Mac to Mac with the owner's and the friend's real accounts, even once, before anything else. If
   the Steam bridge fails, "Join by address" over the internet needs the host to open a router port, and the
   lobby already says so **[V]**. Test that path too, from outside the LAN (phone hotspot).
2. A latency test on this Mac: `pfctl` dummynet or a tiny TCP proxy with 100 ms / 2% loss between the pair, run
   mp2 + a floor change. Add it to the suite as `net_lag`.
3. One real Windows PC run of the kit (the owner has the `DSMP_WindowsTestKit`; a friend with a PC is the test).

### 5. MEDIUM (Windows port fragility across game updates)
Evidence **[V]**:
- The port is a translation: `win_map.py` maps Mac RVAs to Windows RVAs by name; `gen_port` rewrites arm64 ABI
  idioms (split Guid in two u64, V2 by value, frame-pointer walks) into x64 ones; `inline_audit.py` finds hooks
  whose body MSVC inlined and `WIN_WRAP` wrappers re-implement them (9 fully inlined, all fixed; 11 partial).
  148 hooked methods. `win/NOTES.md` sections 15, 16, 20.
- Each step has already failed once and been patched: the regex that missed hex-address tables (v8), the hook
  table capped at 128 (v8 -> 512), the frame-pointer chain that MSVC does not keep (v9), `.pdata` chained
  unwind ranges (v9). These are the kinds of bugs a new game build will produce again.
- `det_ranges_win.tsv` (caller classification by RVA range, used by `rig_det.c cls()`) is per build and per
  platform. A wrong range = a sim RNG draw routed to the view stream = silent desync on one platform only.

How fragile: every game update needs the Mac dump, the Windows dump, `win_map`, `inline_audit` (both static and
`--log` runtime union), `det_ranges` regeneration, the kit dry run (6000 ticks IDENTICAL) and the xplay suite.
`plan_launcher.md` section 4 estimates "days, not hours" per update. I agree.

Recommendation:
- Make the kit dry run (`kit_record.py`, Mac reference vs Windows fp.log) the ONE hard gate for every Windows
  build. It already exists and it is the right tool. Add a second reference on a base-work save.
- Stop the build when `inline_audit` finds a NEW fully-inlined hook with no wrapper (today the build stops only
  when the Mac hook text that a wrapper mirrors changes **[V]**). The missing case is a new inlining.
- Name-based lookup (`find_method_address`, plan_launcher section 4 layer 1) for the ~100 virtual / delegate
  hooks with 0 direct callers would survive updates; keep RVA tables only for inlined sites.

### 6. MEDIUM (tests test the right code, but not the real input layer)
Evidence **[V]**:
- Copies run `-batchmode` or hidden; input = socket commands (`rig.py send`), Unity `Event.KeyboardEvent`
  for chat, a pinned "mouse" dylib (`pin/dsrig_mouse_v10.dylib`, Input.GetMouseButton hooks) for click tests.
  No OS key events, no real pointer, no Steam, no window focus.
- Consequence already seen: lan_v8 chat "Enter opens the box but the field is not focused; he can never type"
  (PLAN.md) passed every hidden test and failed in the owner's hands. `wrap_xplay` on Windows: "Real OS key presses
  (WM_KEYDOWN) cannot be sent to a hidden copy".
- Good: fingerprints are compared on every pair test, frames are saved for the owner, prefs are checked.

Recommendation: keep the hidden suites (they are the regression). Add ONE short visible-window script on the
demo mode (`rig --window`, announced to the owner, on another Space as the rig already supports) that sends real
CGEvents: Enter, type, Enter, Escape, right-click drag of the panel, B ping. Run it only at release, not per lane.

### 7. MEDIUM (scope and order)
Evidence **[V]**: PLAN.md "Next features" (PvP, Foreign Expedition, spectator, dedicated server idea), "Launcher"
(generic mod loader, Tauri 2, unit builder inside the launcher, GitHub releases), BepInEx research, wiki builder
fixes. `ext/pvp` and `ext/foreign_expedition` are built to M3/M6 and F6 in separate source copies with their own
dylibs. Meanwhile: no friend test yet, Steam untested, v10 release failed its own regression.

Judgement: the extensions are well isolated (own `ext/*/src`, own dylib, own rig lane), so they do not hurt the
MVP code. They do consume the thing that is scarce: pair time on this Mac (max 3 pairs, WindowServer crash
2026-10-14 03:03 under rig load) and the lead's attention. The right order for "a solid version to real players":
1. lan_v10 green on the full gate (finding 1) and shipped to `rig/lan_latest`.
2. Owner + one friend, Mac + Mac, Steam invite, 30 minutes, screen recording, the 12-step task list in
   ux_flow_v10.md section 5 (good list, use it as is).
3. Fix what that test shows. Then the Windows friend.
4. Launcher step 1-4 (manifest, loader merge, MP as a plugin, Steam detect) because it is the install path real
   players will use and the loader merge (finding 8) is risky enough to need its own soak.
5. Spectator (small, reuses viewer + dsnet role) before Foreign Expedition and PvP: it is what the Discord asked
   for and it has the least sim risk.
6. Foreign Expedition, then PvP. Both are save-surgery heavy; both need the full-save hash (finding 2) to be safe.

### 8. MEDIUM (launcher / mod loader plan)
Evidence **[V]**: plan_launcher.md. Strong points: signed index (ed25519) + per-file sha256, immutable versions,
no peer-to-peer file transfer, mod-set hash in HELLO, pin the game build and auto-switch, Steam install never
touched (APFS clone / copy), public GitHub releases.
Risks:
- Hook-engine merge (TLDR 2): `rig_hook.c` and `dsl_hook.c` both use the zero tail of `__TEXT` for stubs;
  "collision is a conclusion, not a test". This is a real crash risk and must be a separate milestone with its own
  soak before MP becomes a plugin.
- The signing key is a single point of failure; the plan says offline + backup + a rotation key. Good. Add: the
  launcher must refuse an index older than the last seen (planned, CVE reference). Good.
- Owner decisions pending in the plan that are being made by default: public repo (the owner later said ALL his
  GitHub projects private; a private repo cannot serve release downloads without a login) **[V: PLAN.md 18:00 vs
  plan_launcher TLDR 5]**. This is a direct conflict. Decide: one public release-only repo (no source, as the
  owner described at 17:45), or a different host.
- BepInEx compatibility (bepinex_compat.md): the conclusion "keep our native loader; Windows may add pinned
  BepInEx; Mac has no IL2CPP loader today" is right and well sourced. The HELLO mod-set hash must list BepInEx
  plugin DLLs with a `sim` flag, unknown = sim. Good. Risk: a BepInEx Harmony patch and our MinHook on the same
  function (e.g. `GameSpeedSetting.SetSpeed`, `InputDispatcher`) will chain in an unknown order. Test the top 3
  Nexus mods against our hook list before saying "compatible" on the download page.
- Tauri 2 vs native: Tauri is fine for the UI; WebView2 on Windows 10 is an install step some players will hit.
  Not a blocker.

### 9. LOW (harness and small code issues seen in passing)
- `desync_partial_test.py` item corruption fails with "no item with a position entry on map 1" on the Moonlight
  save (9 FAILs) **[V]**: the fixture has no items lying on the dungeon map. The test needs a save with floor
  items, or a pre-step that drops one. These FAILs hide real results.
- `rig.py:494` default `DSRIG_MAX_PAIRS` is 4; the comment says 3; HANDOFF_v9 says 2 after the crash **[V]**.
  Set the default to the owner's rule (3) in code.
- The server command log grows without bound (`dsnet_server.c:65-78`, doubling realloc) **[V]**. At ~30 bytes per
  order it is harmless for hours; a 6-hour session with chat-level order rates is still MBs. Fine, but the
  join-point `send_log_from(save_seq_after)` means a late joiner replays everything since the last save: after a
  long time with no join point that is a long replay. Issue a join point (host save) every N minutes when players
  are present, or on join only (today) and accept the replay.
- `MP_PATCH_TIMEOUT_MS 5000` and `MP_PROBATION_STEPS 600` are good conservative choices after the release9 RNG
  incident. The partial-resync positions case on Mac+Windows did not converge (`win_suite.out:57-64`); until it
  does, partial resync should be OFF for cross-play builds (the full reload works there: "fallback to the full
  reload" PASS lines).
- Owner hashes: unit and item owner tables are hashed with the fingerprint (`dsnet_report_fingerprint` doc)
  **[V]**. Good. Lend state is "in the owner hash" **[V]**. Good.

## Answers to the review questions, short

1. Netcode: sound (RimWorld clock design copied correctly: stamped commands, tickUntil only after sentCmds
   arrived `dsnet_client.c:128`, server holds the timer for slow peers, PAUSE_ALL on join/desync, join-point save
   to all, partial resync with fingerprint proof and probation, full reload fallback). Hidden desync: anything not
   in the two hashes (finding 2). Unhandled: packet loss is TCP delay then a 5 s silence cut; no auto reconnect
   (manual "Recent worlds"); no host migration; slow peer = everyone freezes with no name on screen; a save that
   fails to load is retried twice then the guest sits on the title (`mp_game.c:245-256`) with the host paused.
2. Determinism: the right three fixes are in (own loop, sync A*, split RNG/Guid) plus HashCode seed, persistent
   Randoms, floor seed. Gaps: finding 3.
3. Windows port: works today, fragile per update, finding 5. Robust = kit dry run as hard gate + inlining stop +
   name lookup for non-inlined hooks.
4. v10 failed late because the freeze gate is a smoke and the merged build was never regressed before freezing.
   Finding 1. Tests test the real code paths but not real input or Steam (finding 6).
5. Order: finding 7. Missing before a friend test: Steam invite, internet latency, a real Windows PC, one 60-min
   soak (findings 2, 4).
6. Launcher: sound plan; hook-engine merge and the public/private repo conflict are the two things to settle first.
7. Decided implicitly today: repo visibility (public needed for downloads vs "all private"); partial resync on
   cross-play (should be off until it converges); "gate green" meaning (smoke vs full regression); the host also
   reloading on every late join (UX cost the owner has not been shown); no host migration; spectator before or
   after Foreign Expedition.

## What to do next, in order

1. Make the release gate = full regression on one frozen sha, with a summary file; `release_all.sh` refuses
   without it. Rerun lan_v10 on it. Fix the `desync_partial` fixture so its FAILs are real.
2. Turn partial resync off for Mac+Windows until `win_suite` positions converge; keep it on Mac+Mac.
3. Add the periodic full-save hash as a deep check; run one 60-minute base-only soak on a pair.
4. Steam invite test with two real accounts (Mac+Mac). Then an internet "Join by address" test from outside the
   LAN. Then a lag-injection pair test in the suite.
5. Ship lan_v10 to `rig/lan_latest`, run the 12-step friend test from ux_flow_v10.md section 5 with a recording.
6. Cross-play floor-generation test (10 entries) and a base-work cross-play run, then the Windows kit to one real
   PC.
7. Launcher: settle repo visibility with the owner; hook-engine merge as its own milestone with a soak; MP as a
   plugin only after that.
8. Spectator role, then Foreign Expedition, then PvP, each behind the same full gate and the full-save hash.
