# Known issue (v13): PvP v1 with a Mac host and a Windows guest resyncs during the match

Found by the v13 release gate (lane gate13, 2026-10-16). Shipped as a known issue (team-lead decision). Owner after the
release: mpfix.

## What happens
- PvP v1 on the base map, Mac host + Windows guest (CrossOver bottle DSMP, rig copy W, gated baselib.dll).
- Plan checks 1, 4 and 7 pass in every run (PvP world on both, combat, back to the title). Checks 0-5 pass too.
- Check 6 (fingerprints equal for 500+ common ticks from the match start, no desync) fails in all 4 runs:
  the Windows guest flags 1-3 desyncs per match and resyncs by itself; the host flags 0. The match still ends with the
  same winner and score on both (check 5).
- The guest's own report: "fingerprint differs ... sections units DIFF ... rng DIFF" (positions and owners ok), first at
  steps 990-1320 of the match.

| Run (rig/gate13 log) | Common ticks | First diff | Desyncs host/guest |
|---|---|---|---|
| try1 10:52 | 106 (23351..23456) | none in the common range | 0/3 |
| 10:54 | 240 (23233..23472) | none | 0/3 |
| try1 11:57 | 200 (22926..23125) | none | 0/1 |
| 11:59 | 251 (23086..23336) | none | 0/2 |

Mac host + Windows guest mp2 (normal play, 3000 ticks) has 19 PASS 0 FAIL on the same build, so the difference is in
the PvP match path (units + rng sections), not in normal cross-play.

## Evidence (local, git-ignored)
~/ds_mp_gate13/ext/gate13/out/known_pvp_xplay/: the four test logs, fepvp_out_last/ (pvp_v1_pair output of the last
run) and W_rig.log (the Windows guest's rig log with the DESYNC lines). Command:
`cd ~/ds_multiplayer/win && python3 xplay_run.py <pair> ~/ds_mp_gate13/ext/fepvp13/tests/pvp_v1_pair.py --math --dylib <dsrig.dylib> --tag x`
(with the gated baselib.dll in C:\DSW and the wine lock; rig/gate13/g11.sh function pvpx does all of it).
Build: MP main 29f0fa6 (dsrig.dylib sha256 ddf76a8b...), LD 942ab7f.
