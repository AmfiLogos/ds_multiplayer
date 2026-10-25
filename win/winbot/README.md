# winbot: Windows VM checks, bot-style

Same idea as the Mac rig: commands go to the game's control port, waits are on events (port answers, menu states,
log lines), logs are collected at the end. Screenshots stay only for checks of the screen itself (translated text,
layout, a box that fits its text); they are the game's own frames (`DSL_UI_TEST` shot).
Protocol and parts: [PROTOCOL.md](PROTOCOL.md).

## Use
```sh
cd win/winbot && agent/build_agent.sh                 # once: agent/build/winagent.exe
python3 winrig.py up                                  # hub on the Mac + agent in the VM (VM must already run, visibly)
python3 winrig.py start                               # windowed check, no update notice, game started, waits for the title
python3 winrig.py cmd '{"cmd":"status"}'              # any control command (about 10-70 ms, was 3-5 s through vmrun)
python3 winrig.py ui '{"op":"open"}'                  # loader Mods window (DSL_UI_TEST)
python3 winrig.py gshot shot.png                      # the game's own frame
python3 winrig.py logs out/; python3 winrig.py stop
python3 win_checklist.py --out OUT --setup NEW_Setup.exe --prev-setup OLD_Setup.exe --mac-build MACBUILD
```
In Python: `import winrig; w = winrig.Win(); w.up(); w.start(); w.join('172.16.175.1:<pair_port printed by mac.sh start>', wait='lobby'); w.wait(...)`.
`WINBOT_TARGET=wine` runs the same driver against the DSMP CrossOver bottle (agent under Wine, hub on 127.0.0.1).

## Game-side change (branch winbot)
`port/rig_sock_tcp.inc`: after a soft restart the new process B could not open the control port, because A still
held it while B loaded (Windows error 10048). B now retries every second until A has quit (15 min at most), and
writes its pid to `ctl.pid`. `winrig.Win.wait_restart(old_pid)` follows the port to B. Wine proof (2026-10-18):
B logged "in use (10048), waiting for it", took 127.0.0.1:47871 4 s later; the driver saw B 6.2 s after Apply.

## Where the time went (vmwin2, v0.1.15 VM run, 2026-10-18 17:16-18:37, 81 min, not finished)
Tool time 70 min, model time between calls 10 min. 133 calls: 12 game starts, 50 control batches through vmrun,
27 screen captures (14 read by the model), 18 clicks, 15 Mods-window file commands.

| # | Time sink | min |
|---|---|---|
| 1 | Poll loops (sleep 10-12 s) waiting for results that never came (hostmods rejoin 330 + 252 s, join 201 s, xplay 70 s) | 14 |
| 2 | Fixed `sleep 45-55` after each of 12 game starts (warm title is about 15 s) | 10 |
| 3 | Mac test copy restarts and status loops (mac.sh start/stop) | 8 |
| 4 | ZET-U hang repros: kill, start, sleep 50, load, sleep 50, fetch logs (3 runs) | 7 |
| 5 | Fixed sleeps after load_save / join (20-50 s each) | 5 |
| 6 | vmrun round trips: every control batch = 4-5 vmrun calls (3-6 s), each join about 25 s | 4 |
| 7 | Screen captures + model reading them, clicks with 1.5 s waits | 3 |
| 8 | Finding the test hooks (research agent, code reading) | 3 |
| 9 | Language switch: kill, edit UserSetting.json through a .bat, restart (2x) | 2 |
| 10 | Reinstall, snapshot revert, file copies through vmrun | 2 |

## Step inventory: old (vmwin2) -> new (winrig)
| Old step | How | New |
|---|---|---|
| start the game (nosteam bat + `sleep 50`) | vmrun + fixed sleep | `w.start()`: agent `run`, wait for `ping` pid + title over the port |
| close the stock Update Notice (click 1118,894) | click | `prepare()`: HiddenUpdateNoticeVersion 1 (as the Mac rig's prefs) |
| crash-report question, No | screenshot + `mpm_box cancel` | `mpm_status` box text + `mpm_box cancel`, `consent.json` read |
| Mods window state, ZET-U off | screenshot | `ui state` controls (`mod:zetu-suite` on/off, `savewarn:*`) |
| save warning box after load (click 958,622) | screenshot + click | `load_with_boxes()`: box text recorded, `mpm_box confirm` |
| Multiplayer > Host / Join by address | vmmp.py presses + SLEEPs | `w.host()` / `w.join()`: presses wait for the menu state |
| host's popup (Lock Consecration) | screenshot + click | `Mac.answer_boxes()`: `mpm_box cancel` |
| "Get the host's mods", Get | screenshot + `mpm_box confirm` | `mpm_hostmods` box flag, `mpm_box confirm`, `wait_log('dsload.log', ['soft:'])` |
| soft restart, rejoin | poll loops, port lost (10048) | `wait_restart(old_pid)` (game fix), `in_mp_game()` |
| language switch | kill + .bat + restart | `prepare(lang=3/4)` + `start()` |
| alive checks | vmrun listProcessesInGuest | agent `ps` (pid, WerFault, hung window flag) |
| logs | vmrun copy per file | `w.logs(dir)` |
| DE / RU texts and layout | vmrun captureScreen + crop | kept: `w.gshot()` (game frame), item 3 and item 2 only |
