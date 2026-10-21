# Map files (not in git)

The Foreign Expedition hub and the PvP v1 world start from map files that ship inside the Multiplayer mod, one per
Steam build, next to the module (`src/fe_hub.c`):

- `hub_<build>.json`: the hub. A full player base with every building, no units, everything unlocked.
  Made with `python3 tools/make_hub_template.py <base save> maps/hub_<build>.json --stock-template maps/base_<build>.json`.
- `base_<build>.json`: the PvP v1 world and the hub's fallback. The day-0 starting base (the wiki's base template,
  dungeonsettlers.wiki `/assets/saves/base_<build>.json`).

They are game saves, so they stay out of git (`.gitignore`). The ds_modloader mod build (`loader/modules/build_mods.sh`)
copies them from this folder (`MP_MAPS`) into both platform copies of the mod; `scan_saves.py` lets only these two
names through.
