# Lane rules (from 2026-10-15; replace NIGHT_RULES_2026-10-15.md)

Repo: ~/ds_multiplayer is git, pushed PUBLIC to github.com/AmfiLogos/ds_multiplayer. `main` = merged src.

1. One lane = one git branch + one worktree. Start: `git -C ~/ds_multiplayer worktree add ../ds_mp_<lane> -b <lane>`. Edit only in your worktree. Never copy src into ext/ again; ext/<lane>/ holds only NOTES.md, lane tests and tools.
2. Never edit main directly. Never edit src while rig/release.lock exists.
3. Merge into main: take the lock `mkdir rig/src_merge.lock` (fails = someone else merges; wait). In your worktree run `git merge main` first, fix conflicts, build to build/merge_<lane>.dylib, run gate.sh unit/smoke + your pair test on THAT dylib. Green: `git -C ~/ds_multiplayer merge --no-ff <lane>`, push, copy the dylib over the shared build/dsrig.dylib, then `rmdir rig/src_merge.lock`. Write one PLAN.md section.
4. A bad merge is undone with `git revert -m 1 <merge commit>`, never by hand.
5. Public repo: never commit game files, dumps (dump.cs, il2cpp.h, script.json, methods.tsv), saves, DLLs/dylibs, logs or screenshots. Keep .gitignore; never `git add -f`. Grep your diff for keys before a push.
6. Game test pairs: only `python3 rig/rig.py lease <lane>` and `release <pair> <lane>`. Leases are refused while rig/release.lock exists. Max 4 pairs machine-wide (DSRIG_MAX_PAIRS); the release gate uses 1.
7. Max one Windows (CrossOver) copy machine-wide: use `rig/wine_lease.py` (`take <owner>` before, `release <owner>` after, `status`; never mkdir or remove `rig/wine.lock` by hand). Never install into "DS Windows Test" except a release. Heavy builds: nice -n 19, max 4 threads.
8. Never `rm`. Move throwaways to an old/ folder.
9. Run tests in the foreground (timeout up to 600000 ms); split longer runs.
10. Owner away: never block; safe default + one line in MORNING_QUESTIONS.md.
11. Final report under 300 words; long output in ext/<lane>/NOTES.md.
12. Release: tag the merged commit `lan_vNN` and push the tag; publish the friend kit as a GitHub release (no saves, no game files inside).
13. Owner rules (2026-10-16): never write the owner's name, Mac user name or home path into anything pushed or published (pre-push hook blocks it; never --no-verify); no Claude credit lines in commits, tags, PRs or notes; tests only where a failure means a real break; test game copies always small, windowed, hidden, notch fix off, never on the owner's screen; release lanes publish the moment the gate is green.
