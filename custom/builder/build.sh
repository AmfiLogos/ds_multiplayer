#!/bin/zsh
# Build the Unit Builder's test tools from the module's flags (custom/build_module.sh CF): the QuickJS engine files of
# custom/fights, our engine wrapper, and the My Units store / link reader of custom/units for the Save checks.
# Output in <repo>/build_bld/ (git-ignored). usage: custom/builder/build.sh
set -e
H=${0:A:h}; C=${H:h}; R=${C:h}; B=$R/build_bld; mkdir -p $B/obj
CF=(-arch arm64 -mmacosx-version-min=12.0 -D_DARWIN_C_SOURCE -O2 -Wall -Wno-unused-function -fwrapv -I$R/sdk -I$R/src
    -I$C -I$C/fe -I$C/fights -I$C/units -I$C/builder)
SRCS=(fights/qjs_quickjs.c fights/qjs_cutils.c fights/qjs_dtoa.c fights/qjs_regexp.c fights/qjs_unicode.c fights/tinf/tinflate.c
      fights/sg_embed.c builder/bld_js.c builder/bld_embed.c
      fe/fe_json.c fe/fe_save.c units/pvp_wiki.c units/pvp_build.c units/myunits.c units/wk_link.c units/wk_http.c)
OBJS=()
for s in $SRCS; do
  o=$B/obj/${${s:r}//\//_}.o; OBJS+=($o)
  [[ $o -nt $C/$s && $o -nt $H/bld_glue.js ]] || nice -n 19 clang $CF -c $C/$s -o $o &
  while (( $(jobs | wc -l) >= 4 )); do sleep 0.2; done
done
wait
for o in $OBJS; do [[ -f $o ]] || { echo "missing $o" >&2; exit 1; }; done
nice -n 19 clang $CF -o $B/bld_cli $H/test/bld_cli.c $OBJS -framework CoreFoundation -framework CFNetwork -lz -lm
echo "built: $B/bld_cli"
