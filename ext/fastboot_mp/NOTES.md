# fastboot_mp lane notes (2026-10-17)

Branch `fastboot_mp`. Mac only (Windows: not built, not run; the new hook is inside `#ifdef __APPLE__`).
Input: `research/fastboot_profile.md`, section "Multiplayer: late join and resync".

## What changed

| change | file | effect | lockstep |
|---|---|---|---|
| Absent view lookups retried every 15 frames per call site (handle + names); cached handles unchanged | `src/mpmenu.c` (`find_view_x`) | `FindObjectOfType` no longer runs every frame for views the scene does not have | UI only |
| Immediate lookups: during a rig command (`mpmenu_fv_kick` from `rig_exec`), 10 frames after a key / mouse press (`Input.anyKeyDown`), and `find_view_now` (field source, Leave box, notice boxes) | `src/mpmenu.c`, `src/mpmenu.h`, `src/rig_cmd.c` | a view a click or a command opens is found at once | UI only |
| `title_sync`: no `TitleMenuView` lookup outside the title scene (it returned there anyway) | `src/mpmenu.c` | the most frequent absent lookup during play and loads is gone | UI only |
| Paused steps run as a burst (down to the normal 3-step buffer); the burst stops at a step with commands or ticks | `src/mp_core.c` (`mp_ticks_to_run_now`, `mp_frame`) | after a reload the slower peer reaches the resume step at once instead of pacing 10-20 paused steps at 60 steps/s | same commands at the same step on every peer; only the count per frame changes (as in the catch-up path) |
| The game's fade-out wait in an mp load (`CallFadeOut` d__15 MoveNext 0xDB02B8: `WaitUntil(ScreenFadeEffectView.IsFadeOutCompleted)`) ends after one frame | `src/mp_game.c` (`h_fadeout`) | -0.8 s per load, host and guest, Fast join on or off | no tick runs in a load (det hold); the old world is replaced by the save; det streams come from the host's det state at the lock |
| Auto off-switch: the fade hook goes in only when the method's first 8 bytes are build 25846389's (`hook_checked`); else a log line and stock behaviour | `src/mp_game.c` | another build / another hook at that address = stock fade | - |

Switches: `DSRIG_MP_FADE=1` keeps the stock fade wait. Status: `mp.load_fade_skips`.

## The "2.3 s both ready -> running" gap

It does not exist. The 2.3 s came from reading `mp: ready again: local speed set to 1 (now 1)` as the resume. That line is
printed on EVERY rising edge of `rig_det_mp_ready()`, and readiness drops for one frame at every busy edge of the game
(about every 300 steps = 5 s in these saves, `busy() 1 / trn 1` for one frame). The line 2.3 s later was the next such edge.
`rig.log` also holds several days without dates; grep by time alone mixes runs (cut the log at the last `rig start pid`).

A trace build (env `DSRIG_RESUME_TRACE`, not committed) logged the ready parts, dsnet pause votes, busy mask, timer and
tick_until at every change, and frame times. One full resync (host C, guest D):

- guest ready, paused (its own resume vote sent at once) -> host ready, paused, 10-20 steps behind `tick_until`
  (the server timer moved on between the join point and the host's busy flag) -> host paced those paused steps
  (0.15-0.3 s) -> resume step (both votes in) -> first world tick after the load: 480-580 ms (`rig_det_sim_tick`, game
  work: every unit's first update after the load), on both peers.
- Our own waiting after both are ready was only the paced paused steps + one server step / TIMECTL (<= 33 ms).
  The burst removes the pacing. The first tick is game work inside the lockstep tick: not cut.

## MoveSceneToTitle: not shipped (not provably safe)

`<MoveSceneToTitle>d__59.MoveNext` (0xD74890) calls `CampaignViewLoadHelper.SetupViewsForScene`, `SelectProcessor.Clear`,
`Resources.UnloadUnusedAssets`, `GC.Collect` x2, `RefreshTitleScenePreview`, `AddLogTimeAndSave`, `ClearOnSceneChange`.
`ClearOnSceneChange` (0xD6F4F4) calls `CampaignEntityLoadHelper.ClearOnMapChange`, `EntityObjectFactory.ClearExpandedSpriteCache`,
`ResourceLoader.ReleaseSceneAssets` (plus interface calls not resolved here). The scene change unloads every world object
(units, their behaviour trees and coroutines). `LoadCampaign` (d__73 0xD7209C) then reads the save and builds the world
in a fresh scene (`StartCampaignTransition`, `InitializeCampaignData`, `LoadCampaignFromSaveData`).
Skipping the trip leaves the old world's objects and the containers it would clear alive while the new world is built.
In a full resync the guest's old world is by definition different from the host's, so equal state after the load cannot
be proven from the code; a green test run would not be a proof. Saving 0.65-0.84 s per side is not worth that risk.
Possible later cut (not done, not analysed): `RefreshTitleScenePreview` (title base preview build) inside the trip.

## Numbers (same runs, base = this branch before the changes, both dylibs side by side on two pairs)

fastjoin_pair.py default args (3 joins, 40 s play), seconds per join:

| | until_running_s | host_reload_s | guest_loaded_s |
|---|---|---|---|
| Fast join off, before | 7.39 / 8.43 / 7.63 | 6.37 / 7.24 / 6.48 | 3.59 / 3.80 / 3.23 |
| Fast join off, after | 6.81 / 6.88 / 6.80 | 5.61 / 5.93 / 5.60 | 2.79 / 2.69 / 2.88 |
| Fast join on, before | 4.38 / 4.40 / 4.56 | 3.39 / 3.48 / 3.38 | 3.56 / 3.22 / 3.18 |
| Fast join on, after | 4.10 / 3.60 / 3.80 | 2.67 / 2.66 / 2.62 | 2.86 / 2.47 / 2.50 |

Full resync (desync_partial_test.py rng,hp --runs 2 --fallback 2, the two "task" fallbacks; trace builds of base and
after, same code + logging; "running" = first frame with the session unpaused, it includes the ~0.5 s first world tick):

| | DESYNC -> both loaded | both ready -> both running | DESYNC -> both running |
|---|---|---|---|
| before | 7.30 / 7.16 | 0.74 / 0.89 | 8.06 / 8.08 |
| after | 6.42 / 6.56 | 0.59 / 0.75 | 7.02 / 7.31 |

Tools here: `resync_times.py HOST GUEST` (the last run of each copy's rig.log), `resume_trace.patch` (the trace build:
`patch -p1` on a copy of src, build with `OUT=...`, run with `DSRIG_RESUME_TRACE=1`), `gate_pinned.sh <dylib>`.

## Tests on the after dylib (build/fastboot_mp/dsrig.dylib, never copied over build/dsrig.dylib)

- `src/mp_test` make test: ALL PASS (30 tests).
- desync_partial_test.py rng,hp --runs 2 --fallback 2: 24 PASS 0 FAIL (partial 4, reload 2), 0 extra desyncs.
- fastjoin_pair.py --mode off: 4/4 PASS; --mode on: 4/4 PASS (0 desync, full-state check equal, skip lines as expected).
- `ext/fastboot_mp/gate_pinned.sh` (gate.sh steps pinned to a dylib): smoke 3/3, mp2 19 PASS 0 FAIL. left_prompt and the
  PvP pair test: see the commit / final report (the v0.1.7 release gate held the rig lock at the time).

## Not measured

- The main-thread share of `find_view_x` after the change (no sample run). Expected: the per-frame absent lookups are 1/15.
- Other per-frame hitch seen in the logs, not ours to cut here: `ui: sprite scan: 17436 sprites ... (ITEM_CogWheel MISSING)`
  retries about every 2.4 s on copies without that sprite (ui lane).
