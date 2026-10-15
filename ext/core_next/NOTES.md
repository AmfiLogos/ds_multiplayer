# core next lane: notes (night 2026-10-15)

Layout: base/ = snapshot of src at 01:45 (merge base), work/ = my changes (+ pending_1942 mp.h / mp_config.c),
lane_rand/ and lane_join/ = sub-lane copies (only their named files differ), build/ = lane dylibs and det_ranges.

## Task 2: System.Random / UnityEngine.Random scan (full table: random_scan.md, 58 rows)
UNSAFE
1. PostProcessingManager.<Co_Shake>d__4.MoveNext 0xA5F3E0 (camera shake from skill FX): Random.Range(0,3) on the SIM
   stream, but only on peers that show that map (FXPresenter.PlayFX drops FX of other maps) -> desync. Fix (lane_rand):
   gen_ranges tests every segment of nested class names + view word PostProcessingManager.
2. CampaignRandomService.ExecuteAtSequence (0x17D3BC4/0x17D3DF0/0x17D401C), EstablishUnitGenerator (0x81EFE4/0x81EC98):
   InitState(seed)-scoped draws were swapped to the sim stream during det load (peers equal, but seed ignored).
   Fix (lane_rand): hooks InitState 0x50E7710 + set_state 0x50E7800; inside a game-seeded block, no swap.
3. TextSheet.GetRandomTip 0xA6A8A8 / DataSheetManager.GetRandomTip 0xA5E960 (loading tip): took sim counter values in
   det load (host 2 vs joiner 3 uses). Fix (lane_rand): view.
UNPROVEN
- HashHelpers.RandomSeed statics (0x664E390/398/3A0), random per process, used by ValueTuple hashes; only dictionary
  lookups seen. Fix anyway (lane_rand): pinned to 0x1B873593 after each cctor.
- UnitSpeechAction.Execute 0xCE5AF4 (view Guid counter; looks like an FX key only), SpawnDungeonNPC 0xDA8B30
  (Environment.TickCount seed only when its seed reader is null), combat hit rolls 0x94C008-0x94C0F0 (never in a trace;
  sim routing by code reading only).
SAFE: everything else (unhooked insideUnitSphere/onUnitSphere: no gameplay caller; insideUnitCircle / rotation / ColorHSV
not in this build).
Windows: win/port/gen_port.py has its own copy of the view rules (det_ranges_win.tsv) and win_map.tsv lacks the new
addresses: after the merge run win/win_map.py and port the 3 gen_ranges changes into gen_port.py (Windows lane's files).

## HELLO modset fields (task 4; code in work/net, work/mp_config.c, work/mp_core.c; not merged into src yet)
- Source: env DSML_SESSION file (launcher Session). mp_session_mods() (mp_config.c) reads "modset" (64 hex),
  "rule" ("mods" | "vanilla") and "modset_lines" (copied whole, joined by '\n'). No DSML_SESSION = rule mods, modset ""
  = no gameplay mods (counts as DSNET_MODSET_NONE = sha256("")). mp_start (mp_core.c) passes them to dsnet_set_modset().
- HELLO, proto 2 (DSNET_PROTO 2): u16 proto, u8 want_host, u32 mod_version, u32 round_mode, uid[16], u8 build_len, build[],
  u8 match_rule (0 mods, 1 vanilla), u8 modset_len (0 or 64), modset[] (hex), u16 lines_len, lines[] (modset_lines,
  '\n'-joined, < 4096 bytes), name[] (rest).
- Host check (dsnet_server.c modset_refused): the host's own HELLO is the reference. Rule mods: joiner hash must equal
  host hash. Rule vanilla: host and joiner must both be DSNET_MODSET_NONE. Mismatch = P_REJECT reason DSNET_REJ_MODSET (8).
- P_REJECT layout: u8 type, u8 reason, u16 words_len, words (English sentence naming both "id@version" lists),
  then for MODSET only: u8 host_rule, u16 host_lines_len, host lines ('\n'-joined modset_lines).
- Guest side C API (net/dsnet.h): dsnet_reject_reason(c) == DSNET_REJ_MODSET; dsnet_reject_text(DSNET_REJ_MODSET) = the words;
  dsnet_reject_host_mods(&host_rule, buf, DSNET_MODLINES_MAX) = host rule + host lines (0 ok, -1 none);
  dsnet_my_mod_lines() = this copy's own lines (what it sent). Process-wide, newest refusal.
- Unit test: net/test/test_dsnet.c test_modset (7 cases). Session parse checked with a sample file.

## Results (2026-10-15 morning)
- Merged into src 05:01-07:31 (PLAN.md "core next merge"). Bug found by the first gate: mp_fog.c stored the il2cpp gchandle
  as 32 bits -> SIGSEGV at campaign load (crashed every lane's copies 05:01-06:25 through the shared dylib). Fixed (uintptr_t).
- Gate det_out/gate_corenext_20261015-0706: smoke 3/3, mp2 19 PASS 0 FAIL, left_prompt 16 PASS.
- Soak 15 min, fog Off (per player): 0 desync, 30 full-state checks equal (build/core_test/soak_20261015-071443).
  fog: "layout ok" on host and guest; once on the guest "vision handler / map -1 does not match map 0: left vanilla" (fallback, safe).
- Late join (join_freeze_test, 3 joins each, same merged dylib): DSRIG_JOIN_FAST=0 host reload 6.08-6.19 s, until running
  7.14-7.23 s; fast (default) host reload 3.07-3.10 s, until running 3.93-3.97 s. Runs: lane_join/runs/.
- Unit: dsnet all pass (+ logtrim, modset, rules with shared_fog), mp_test, mpmenu_test.
- NOT run: rig/det_test.py with the random fixes (slots reserved for other lanes until 08:00; tools/det_test_rand.py is ready).
  Not checked by eye: per-player fog display (what each player sees), the 8-row Rules panel layout. Windows build not run.
- 08:05 second fog crash fixed (stale VisionDataHandler across a reload). Gate 0809 green, 5 late joins, soak 0820 clean. Shared dylib 08:36.
- 09:53 det_test (merged dylib, 6000 ticks): IDENTICAL on every tick, sim trace records identical (det_merge.log).
- 09:55 soak_far (tools/soak_far.py, fog Off, Y's units sent to the farthest player unit, ~15 tiles from X's group;
  the base save has no farther player unit): 0 desync, 30/30 full checks equal (build/core_test/soak_20261015-095515).
