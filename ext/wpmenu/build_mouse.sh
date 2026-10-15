#!/bin/zsh
# Test build of src/ with the fake mouse (-DTEAMS_MOUSE_TEST: ui_teams {action:"mouse", x, y, button}) -> build/wpmenu_mouse.dylib.
# Same file list as src/build.sh (read from it, so it follows that list).
set -e; cd "${0:A:h}/../../src"; mkdir -p ../build
cmd=$(sed -n '/^nice -n 19 clang/,/-lz$/p' build.sh | sed 's/\\$//' | tr '\n' ' ')
cmd=${cmd/-o ..\/build\/dsrig.dylib.tmp/-o ../build/wpmenu_mouse.dylib.tmp -DTEAMS_MOUSE_TEST}
eval "$cmd"
codesign -f -s - ../build/wpmenu_mouse.dylib.tmp 2>/dev/null
mv -f ../build/wpmenu_mouse.dylib.tmp ../build/wpmenu_mouse.dylib
echo built ../build/wpmenu_mouse.dylib
