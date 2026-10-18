#!/bin/zsh
# mac.sh - Mac side of the int013 cross-machine multiplayer test (hidden Mac test copy W <-> Windows VM).
#   start                 take one rig pair lease (lane vmwin-mac), put the int013 dsload + mods_all/mac into copy W, start it
#                         hidden (batchmode, muted, guard kills it on any visible window / focus). Title after ~40-90 s.
#   stop                  quit the copy, release the lease
#   host [hub|dungeon|<save file>]  title > Multiplayer > Host > Load: hosts a save over direct TCP (menu port 47777)
#                         hub (default) = MoonlightBrigade base map (Teams / expedition), dungeon = ElvenExpedition map 1
#   join <ip> <port>      title > Multiplayer > Join by address > type ip:port > Connect
#   status                scene/map/tick, lockstep, MP (peers, slot, paused, desyncs), menu state + port, own / other
#                         players' / free units, last 4 chat lines, listening sockets
#   chat <text>           open chat, type, Enter; prints the last chat lines
#   teams [k=v ...]       ui_teams pass-through (no args = state; action=open|close|click guid=.. button=1|go|...)
#   expedition            host: Teams > Go to dungeon with its units > Accept; guest: answer the receiver window (approve)
#   leave                 guest leave (stock Quit to title), waits for the title
#   rejoin                leave if needed, then join the last address again
#   logs <dir>            copy player/dsload/rig/guard logs, ErrorLog, mp_* files + build proof json into <dir>
#   press <BUTTON> | cmd '<json>' | proof    extras: one menu button, raw control command, int013 build proof
# Env: MAC_WINDOW=1 = hidden-window mode instead of batchmode; MAC_NAME = player name (default Mac).
# Host flow: host -> (guest joins) -> press START (closes the lobby, game runs) -> chat / teams / expedition.
# ---
exec /usr/bin/python3 "${0:A:h}/mac.py" "$@"
