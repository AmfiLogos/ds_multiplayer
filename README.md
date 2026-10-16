# Dungeon Settlers MP

Lockstep multiplayer mod for **Dungeon Settlers** (Steam). All players run the same simulation; only player orders travel over the network.

**Roadmap:** what we work on now and next, and where you can help: [ROADMAP.md](ROADMAP.md).

- **Mac (Apple Silicon):** native dylib loaded into the game.
- **Windows:** a module of the mod loader. Tested in CrossOver; a real Windows PC test is still to do.
- Mac host and Windows guest can play together.
- Made for Steam build 25846389 of Dungeon Settlers. Other game builds are not supported.

## The three repositories

- ds_multiplayer (this repo): the Multiplayer mod. Its releases page has the installers.
- [ds_modloader](https://github.com/AmfiLogos/ds_modloader): the mod loader that starts the game with mods,
  the Mac app and the Windows installer. Read its README for install steps, save folders and bug reports.
- [ds_custommodes](https://github.com/AmfiLogos/ds_custommodes): the Custom Modes mod. Foreign Expedition and
  PvP in it need this Multiplayer mod.

## Download

Releases: https://github.com/AmfiLogos/ds_multiplayer/releases

Take the files of the latest release:

- Mac (Apple chip): `Dungeon-Settlers-Modded-Mac.dmg`
- Windows (64-bit): `Dungeon-Settlers-Modded-Setup.exe`

Both hold the mod loader and all four mods (Multiplayer, Custom Modes, Global cooldown, Hell difficulty).
The install steps are in the release notes and in the [ds_modloader README](https://github.com/AmfiLogos/ds_modloader#download-and-install).
`multiplayer-<version>.zip` is the Multiplayer mod alone, to add or update it by hand.

Your normal game, saves and settings do not change: the modded game plays on its own copy of your saves.
The ds_modloader README says where that copy is.

## Report a bug

Open an issue on this repo's Issues page. Say your system, the release and what you did, and attach the log
files. The ds_modloader README says where they are. Do not attach save files.

## Known limits

Tested on one Mac over a local network, and Mac host + Windows guest in CrossOver. Not yet tested between two homes
over the internet, or on a real Windows PC with Steam.

## What is in this repository

- `src/` - the mod (C): lockstep core, network, multiplayer menu, fog, spectator, Foreign Expedition, PvP.
- `win/` - Windows port and build.
- `rig/` - test rig: runs pairs of game copies and compares them every step.
- `ext/` - notes and tests of each work lane.
- `PLAN.md` - every design decision. `LANE_RULES.md` - how work is split and merged.

How to build: [BUILDING.md](BUILDING.md).

No game files are in this repository. You need your own copy of Dungeon Settlers.

## License

MIT (see LICENSE). Code from other projects keeps its own license: MinHook (`win/third_party/minhook/LICENSE.txt`), musl (`win/math/musl/COPYRIGHT`), zlib (`win/third_party/zlib/README`).

The lockstep clock design and the server timer logic (`src/net/dsnet_server.c`, `src/net/dsnet_client.c`) are adapted
from [RimWorld Multiplayer](https://github.com/rwmt/Multiplayer), MIT License, Copyright (c) 2018 Zetrith
(`src/net/LICENSE.rwmt-Multiplayer.txt`). No source file was copied.

The release files hold `licenses/LICENSE.txt` and `licenses/THIRD_PARTY_NOTICES.txt` (made in ds_modloader).
