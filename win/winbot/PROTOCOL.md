# winbot: Mac-side driver for the Windows test VM

Goal (owner, 2026-10-18): Windows VM checks run bot-style, like the Mac rig. Commands go to the game's control port,
waits are on events (port answers, log lines), and logs are collected at the end. Screenshots stay only for checks
that need the screen (translated text, layout, a box that fits its text).

## Parts
- `agent/winagent.c` -> `winagent.exe` (x64, GUI subsystem, no window). Runs in the guest's interactive session
  (started once with `vmrun runProgramInGuest -noWait -interactive`). It opens N outbound TCP connections to the hub
  on the Mac and serves one request at a time on each. Outbound only, so the guest firewall needs no rule.
- `winrig.py` (Mac): the hub (`winrig.py hub`, a background process), the client library and the CLI. Same command
  set as the Mac rig (`rig.py` / the winlane `mac.py`).
- `win_checklist.py` (Mac): the release checklist for the VM, driven by winrig.

Why outbound: the game's control port listens on 127.0.0.1 in the guest (never on the network), and a non-admin
program cannot add a firewall rule. The guest can always connect out to the Mac (vmnet8 host address, for example
172.16.175.1). The agent forwards each game command to 127.0.0.1:<ctl.port> inside the guest.

## Wire format
One JSON object per line (UTF-8, `\n` ends a line) in each direction. Strings are JSON strings (escapes `\" \\ \n
\r \t \uXXXX`). Numbers are integers unless noted.

### Hello (agent -> hub, first line on each connection)
`{"hello":"winagent","ver":1,"token":"<token>","pid":1234,"conn":0,"computer":"...","user":"..."}`
The hub closes a connection whose token is wrong.

### Request (hub -> agent) and response (agent -> hub)
Every request has `"id"` (integer) and `"op"`. Every response has the same `"id"`, `"ok"` (true/false), and on
failure `"err"` (short text, with the Windows error code when there is one).

| op | request fields | response fields |
|---|---|---|
| `ping` | | `pid`, `ver`, `time` (Unix seconds, float), `computer`, `user` |
| `game` | `line` (string: one control command, JSON text), `port` (int) or `port_file` (path; read on every call), `timeout_ms` (default 90000) | `port`, `resp` (string: the game's answer line, without `\n`) |
| `run` | `exe` (path), `args` (string: the rest of the command line, already quoted), `cwd`, `env` (array of `"K=V"`; added to / replacing the agent's own environment; `"K="` sets an empty value), `wait_ms` (0 = do not wait), `capture` (bool: stdout+stderr to `out`, needs `wait_ms`>0), `show` (bool: normal window; default false = CREATE_NO_WINDOW for console programs) | `pid`, and when waited: `exit` (or `"timeout":true`), `out` (string, at most 1 MB, UTF-8; console bytes are taken as the OEM code page and converted) |
| `ps` | `name` (optional: exe name, case-insensitive, for example `DungeonSettlers.exe`) | `procs`: `[{"pid","ppid","name","windows":[{"title","x","y","w","h","hung"}]}]` (visible top-level windows of that process; `hung` = IsHungAppWindow) |
| `kill` | `pid` or `name` | `killed`: `[pids]` |
| `read` | `path`, `offset` (bytes; negative = from the end), `max` (bytes, default 4 MB) | `size`, `mtime` (Unix seconds), `offset` (where the data starts), `b64` |
| `write` | `path`, `b64`, `append` (bool), `mkdirs` (bool) | `size`. Not append: written to `<path>.winbot.tmp`, then MoveFileEx REPLACE_EXISTING (a reader never sees half a file). |
| `stat` | `path` | `exists`, `dir`, `size`, `mtime` |
| `ls` | `path` (folder) | `entries`: `[{"name","dir","size","mtime"}]` |
| `move` | `src`, `dst`, `mkdirs` | (MoveFileEx REPLACE_EXISTING + COPY_ALLOWED). There is no delete op: owner rule, nothing is ever deleted. |
| `wait_file` | `path`, `any` (array of substrings), `offset` (bytes; negative = from the end; default 0), `timeout_ms` | `found` (bool), `line` (the first whole line after `offset` that holds one of `any`), `offset` (end of that line, or the size read so far when not found), `size`. A file that is missing waits; a file that got shorter than `offset` is read from 0. Polls every 100 ms. |
| `click` | `x`, `y` (physical desktop pixels), `button` ("left" default, "right"), `double` (bool) | (SetProcessDPIAware, SetCursorPos, SendInput) |
| `key` | `vk` (virtual-key code), `text` (alternative: type a Unicode string) | |
| `reg_get` | `key` (under HKCU, for example `Software\\CanOpener\\Dungeon Settlers`), `name` | `type` ("dword"/"sz"/"other"), `value` (number or string), `exists` |
| `reg_set` | `key`, `name`, `type` ("dword"/"sz"), `value` | |
| `env_user` | `name`, `value` (HKCU\\Environment, REG_SZ; empty string = empty value, never a delete) | |
| `exit` | | then the agent closes every connection and exits |

Paths are UTF-8 and may hold `%VAR%` (ExpandEnvironmentStringsW). The agent converts them to wide strings.

## Agent behaviour
- Arguments: `winagent.exe <host:port> <token> [connections=4] [idle_exit_minutes=720]`.
- One copy per user session: a named mutex `Local\dsm_winagent`. A second copy exits at once (exit code 3).
- Each connection thread: connect (retry every 500 ms while the hub is away), send hello, serve requests until EOF,
  reconnect. Exits after `idle_exit_minutes` with no hub connected.
- Log: `%TEMP%\winagent.log` (start, connect, each request op + duration, errors; one line each, appended).
- Never deletes a file. Never changes the focus on purpose (clicks do move the guest's mouse: that is inside the VM).
