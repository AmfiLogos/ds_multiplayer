# Dungeon Settlers MP

Lockstep multiplayer mod for **Dungeon Settlers** (Steam). All players run the same simulation; only player orders travel over the network.

- **Mac (Apple Silicon):** native dylib loaded into the game.
- **Windows:** `baselib.dll` proxy. Tested in CrossOver; a real Windows PC test is still to do.
- Mac host and Windows guest can play together.

## Download

Releases: https://github.com/AmfiLogos/ds_multiplayer/releases

Mac: download `DungeonSettlersMP_friend_kit.zip`, unzip it in Downloads, quit the game, then in Terminal:

```
zsh ~/Downloads/DungeonSettlersMP/make_mp_app.sh
```

Start "Dungeon Settlers MP" from the Applications folder in your home folder. Keep Steam open and signed in. Your normal game, saves and settings do not change; the MP app uses a copy of your saves.

Windows kit: coming.

## Status

Tested on one Mac over LAN, and Mac host + Windows guest in CrossOver. Not yet tested between two homes over the internet.

## What is in this repository

- `src/` - the mod (C): lockstep core, network, multiplayer menu, fog, spectator, Foreign Expedition, PvP.
- `win/` - Windows port and build.
- `rig/` - test rig: runs pairs of game copies and compares them every step.
- `ext/` - notes and tests of each work lane.
- `PLAN.md` - every design decision. `LANE_RULES.md` - how work is split and merged.

No game files are in this repository. You need your own copy of Dungeon Settlers.

## License

MIT (see LICENSE). Code from other projects keeps its own license: MinHook (`win/third_party/minhook/LICENSE.txt`), musl (`win/math/musl/COPYRIGHT`).
