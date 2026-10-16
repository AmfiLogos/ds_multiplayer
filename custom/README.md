# Custom Modes moved

Custom Modes (My Units, Unit Builder, Gallery, Test Fights, Foreign Expedition hub, PvP) now lives in its own repo:
[github.com/AmfiLogos/ds_custommodes](https://github.com/AmfiLogos/ds_custommodes).

Its history up to ds_multiplayer main 98fd330 (v13 = d6a6156) stays here, under custom/. The ds_custommodes tag `v13` has
the same files as custom/ at d6a6156.

Builds in this repo (src/build.sh, win/build_win.sh, win/win_map.py, rig/gate13) read Custom Modes from a checkout of
ds_custommodes: `CM_REPO=<folder>`, default `~/ds_custommodes`. The mod loader builds it from its own pin (loader/cm_pin.txt).
