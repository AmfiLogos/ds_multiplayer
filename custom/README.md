# Custom Modes moved

Since the Units Laboratory split (2026-10-20), Foreign Expedition and PvP are back in this repo, in `src/` (`mp_modes.c`,
`fe_*.c`, `pvp_*.c`; moved from ds_custommodes ac78178). ds_custommodes keeps Units Laboratory only (My Units, Unit
Builder, Wiki Build Gallery, Test Fights), a launcher mod that no multiplayer session needs.

Custom Modes (My Units, Unit Builder, Gallery, Test Fights, Foreign Expedition hub, PvP) now lives in its own repo:
[github.com/AmfiLogos/ds_custommodes](https://github.com/AmfiLogos/ds_custommodes).

Its history up to ds_multiplayer main 50c4d98 (v13 = 29f0fa6) stays here, under custom/. The ds_custommodes tag `v13` has
the same files as custom/ at 29f0fa6.

Builds in this repo (src/build.sh, win/build_win.sh, win/win_map.py, rig/gate13) read Custom Modes from a checkout of
ds_custommodes: `CM_REPO=<folder>`, default `~/ds_custommodes`. The mod loader builds it from its own pin (loader/cm_pin.txt).
