# Custom Modes moved

Custom Modes (My Units, Unit Builder, Gallery, Test Fights, Foreign Expedition hub, PvP) now lives in its own repo:
[github.com/AmfiLogos/ds_custommodes](https://github.com/AmfiLogos/ds_custommodes).

Its history up to ds_multiplayer main d57e7c7 (v13 = 84be482) stays here, under custom/. The ds_custommodes tag `v13` has
the same files as custom/ at 84be482.

Builds in this repo (src/build.sh, win/build_win.sh, win/win_map.py, rig/gate13) read Custom Modes from a checkout of
ds_custommodes: `CM_REPO=<folder>`, default `~/ds_custommodes`. The mod loader builds it from its own pin (loader/cm_pin.txt).
