#!/bin/zsh
# Build the mod/rig dylib (arm64 only). Output: ../build/dsrig.dylib (ad-hoc signed).
# rig + determinism (rig_*), multiplayer glue (mp_*), dsnet (net/, net/steam/), orders (ord_*), UI (ui_*).
# ord_test/ord_smoke.c only adds the ord_inject / ord_select / ord_hover / ord_stats test commands.
# Multiplayer menu (mpmenu*).
# i18n (ds_i18n.c = the one DSI_IMPLEMENTATION; language files lang/<code>.json next to the dylib, see mpmenu.c lang_dir_init).
# Transitions (trn_*): floor seeds, flow barrier, host-only autosave.
# Multiplayer API (mp_api.c / mp_api.h, MASTER_PLAN 2.2 B) + the menu kit (mpmenu_kit.h) for Custom Modes.
# Bring Units card window (mu_bring.c + mu_ui/tip/look/card/save/timer, pvp_wiki, cm_il: copies of the Units Laboratory card
# code, owner 2026-10-20; needs the loader's widget kit, so only the loader module shows it; ../sdk = dsl_sdk.h).
# Foreign Expedition + PvP (mp_modes.c, fe_*.c, pvp_*.c): part of multiplayer since the Units Laboratory split (moved from
# ds_custommodes ac78178). Units Laboratory (mod id custom-modes, github.com/AmfiLogos/ds_custommodes; checkout CM_REPO,
# default ~/ds_custommodes) is a launcher mod a session never needs: built after this one by $CM_REPO/build_module.sh into
# ../build/custom-modes.dylib for the rig's My Units tests (the rig loads it from next to dsrig.dylib like the loader does;
# DSRIG_CUSTOM=0 = without it). OUT=<dir> builds into another folder.
# Mac math swap (windows lane, win/NOTES.md sections 7 + 17): ON by default since v9 (-DDSMATH_DEFAULT_ON=1, same as the
# Windows build); off only with DSMP_MATH=0 or <DSRIG_DIR>/dsmp_math.off (measurements, tests).
# Needs win/math/build/libdsmath_arm64.a (run win/math/build_math.sh once).
set -e; cd "${0:A:h}"; OUT=${OUT:-../build}; mkdir -p $OUT
python3 ../tools/rv_gate.py > /dev/null || { python3 ../tools/rv_gate.py >&2; exit 1; }   # every game address through rv() (src/rig_rv.c)
nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0 -O2 ${=EXTRA_CFLAGS} -Wall -Wno-unused-function -D_DARWIN_C_SOURCE -I../sdk -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -o $OUT/dsrig.dylib.tmp \
  rig_main.c rig_hook.c rig_sock.c rig_cmd.c rig_win.c rig_det.c rig_rv.c \
  mp_core.c mp_config.c mp_game.c mp_fog.c mp_api.c mp_bridge.c mp_moddata.c mp_rand.c mp_modchat.c mp_solo.c \
  net/dsnet_link.c net/dsnet_server.c net/dsnet_client.c net/steam/dsnet_steam.c \
  ord_core.c ord_game.c ord_mp.c ord_test/ord_smoke.c \
  ui_game.c ui_model.c ui_tip.c ui_teams.c ui_teams_model.c ui_friends.c mpmenu.c mpmenu_model.c extaddr.c upnpport.c \
  trn_logic.c trn_game.c trn_mp.c ds_i18n.c \
  mp_modes.c fe_game.c fe_menu.c fe_hub.c fe_json.c fe_save.c fe_http.c pvp_core.c pvp_game.c pvp_modcheck.c pvp_mods.c pvp_ui.c pvp_units.c pvp_rules.c pvp_rules_core.c pvp_maps.c \
  mu_bring.c mu_ui.c mu_tip.c mu_look.c mu_card.c mu_save.c mu_timer.c pvp_wiki.c cm_il.c \
  ../win/math/math_mac.c -I../win/math -DDSMATH_DEFAULT_ON=1 ../win/math/build/libdsmath_arm64.a -lz
codesign -f -s - $OUT/dsrig.dylib.tmp 2>/dev/null
mv -f $OUT/dsrig.dylib.tmp $OUT/dsrig.dylib     # new inode: never overwrite a mapped dylib
echo built $OUT/dsrig.dylib
CM=${CM_REPO:-$HOME/ds_custommodes}
[[ -f $CM/build_module.sh ]] || { echo "Custom Modes checkout not found: $CM (git clone https://github.com/AmfiLogos/ds_custommodes, or set CM_REPO)" >&2; exit 1; }
MP_DYLIB=${OUT:A}/dsrig.dylib OUT=${OUT:A} SDK=${PWD:h}/sdk MP_SRC=$PWD zsh $CM/build_module.sh
# build/lang next to the rig dylib (the rig reads lang/<code>.json beside it): the multiplayer table, then the Units
# Laboratory table over it (as rig/gate024/i18n/rebuild_testtrees.sh). Without it every rig test runs in English
# (v0.1.20 + v0.1.21 gates: g0NN_extaddr failed its translated-line checks; was fixed only in a release-folder script).
mkdir -p $OUT/lang
python3 - ../lang $CM/lang $OUT/lang <<'E'
import json, os, sys
mp, cm, out = sys.argv[1:4]
n = 0
for f in sorted(os.listdir(mp)):
    if not f.endswith('.json'): continue
    m = json.load(open(os.path.join(mp, f))); p = os.path.join(cm, f); c = json.load(open(p)) if os.path.exists(p) else {}
    t = os.path.join(out, f + '.tmp'); open(t, 'w').write(json.dumps({**m, **c}, ensure_ascii=False, indent=1)); os.replace(t, os.path.join(out, f)); n += 1
print(f'built {out}: {n} language tables')
E
# the map files (maps/README.md, not in git) next to the rig dylib, as the mod build puts them next to the module
for m in ../maps/hub_*.json(N) ../maps/base_*.json(N); do cp -c $m $OUT/ 2>/dev/null || cp $m $OUT/; done
