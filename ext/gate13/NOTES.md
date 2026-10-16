# Lane gate13 (v13 release gate), 2026-10-16

Runner: rig/gate13/run_all.sh (G1-G12 in order; lib.sh chk = one verdict line per check, one rerun on red), rerun.sh and
rerun2.sh (red items after runner fixes, same frozen inputs). Output (git-ignored): ext/gate13/out/ (summary.txt, inputs.txt,
one log per check). Release note: ext/gate13/release_note_v13.md. Result and decisions: PLAN.md "v13 released".

Runner traps found (fixed): zsh $0 inside a function is the function name; test output folders must start empty (a My
Units library left there); loader test soft/v13_test.py waits on rig/release.lock itself (v13t_run.py hides only the gate's
own lock); a fresh worktree lacks git-ignored win/25846389 (dump, symlink) and win/instances; Windows probe mods are kept in
ext/gate13/probes_win (crossmods' kit no longer holds them); xplay_run's release() stub raises after pvp_v1_pair's results;
the login shell's grep is a wrapper (scans use python / command grep); glob qualifiers fail in the agent shell (explicit MODS).
