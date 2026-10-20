# source me: env for win/winbot/mac/mac.sh driving the Steam copy M of lane steamwin
I=$HOME/ds_mp_steamwin_ldr/loader/test_env/instances/M
export MAC_LOADER=$HOME/ds_mp_steamwin_ldr/loader MAC_BUILD=$HOME/ds_mp_steamwin_ldr/loader/build MAC_COPY=M MAC_LANE=steamwin
export WINBOT_STATE=$HOME/.ds_winbot_steamwin DSRIG_CTL=1 MAC_NAME=MacHost
export DSL_PROFILE=$I/data/Mods/active.json DSL_SETTINGS_FILE=$I/data/Mods/settings.json DSML_SESSION=$I/data/Mods/worlds.json
export DSL_CLOUD=0 DSL_UPDATE=0 DSL_CRASHREP_TEST=$I/crashrep
