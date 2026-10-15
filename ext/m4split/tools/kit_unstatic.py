#!/usr/bin/env python3
"""M4 split: turn mpmenu.c's helpers used by the Custom Modes windows into exported ones (mpmenu_kit.h).
Usage: kit_unstatic.py <mpmenu.c>  (edits in place; idempotent checks print what changed)."""
import re, sys
p = sys.argv[1]; s = open(p).read(); o = s
def sub(pat, rep, n=1, flags=re.M):
    global s
    s2, k = re.subn(pat, rep, s, flags=flags)
    if k != n: print(f'WARN {pat!r}: {k} (want {n})')
    s = s2
sub(r'^#include "fe/fe_ui_api.h"\n', '#include "mpmenu_kit.h"\n')
sub(r'^enum \{ K_RT, K_TR, .*K_N \};\n', '')
sub(r'^static int mm_is\(', 'int mm_is(')
for fn in ['u_str', 'u_gc_new', 'u_gc_get', 'u_gc_free', 'u_alive', 'u_go', 'u_tr', 'u_go_active', 'u_go_active_self', 'u_active', 'u_on',
           'u_instantiate', 'u_tr_parent', 'u_sibling', 'u_set_sibling']:
    sub(r'^static inline \w+ ' + fn + r'\(.*\n', '')
sub(r'^typedef struct \{ float x, y; \} V2;\n', '')
sub(r'^typedef struct \{ float x, y, w, h; \} RectF2;\n', '')
sub(r'^typedef struct \{ float x, y, z; \} V3;\n', '')
sub(r'^typedef struct \{ float r, g, b, a; \} Col;\n', '')
sub(r'^enum \{ S_FRAME, .*S_N \};\n', '')
sub(r'^static const Col C_MAIN = .*\n.*C_ORANGE = .*\n', '')
sub(r'^/\* approved mockups .*\n', '')
sub(r'^static const Col C_BAND = .*\n.*C_LINE = .*\n.*C_START = .*\n', '')
FN = {'class_by_name': 3, 'find_view_x': 1, 'title_view': 1, 'esc_view': 1, 'popup_activate': 1, 'image_of': 1, 'str_utf8': 1,
      'utf16_to_utf8': 2, 'tmp_set': 1, 'txt_style': 1, 'txt_color': 1, 'img_set': 1, 'place': 1, 'rect_of': 2, 'mk_img': 3,
      'mk_txt': 1, 'mk_button': 1, 'btn_label': 1, 'first_image': 2, 'eb': 1, 'portrait_sprite': 1, 'tip_try': 1, 'assets_scan_now': 2}
for fn, n in FN.items():
    sub(r'^static ((?:const )?(?:void|int|Obj|bool) ?\*?\s*)' + fn + r'\(', r'\1' + fn + '(', n)
sub(r'^static int64_t frames;', 'int64_t frames;')
sub(r'^static int scan_done, scans;', 'int scan_done; static int scans;')
sub(r'^static char my_name\[64\];', 'char my_name[64];')
sub(r'^static char tip_now\[64\];', 'char tip_now[64];')
sub(r'^static uintptr_t h_tb\[MPB_COUNT\];', 'uintptr_t h_tb[MPB_COUNT];')
sub(r'^static uintptr_t h_lwr\[MPB_COUNT\];', 'uintptr_t h_lwr[MPB_COUNT];')
sub(r'^static uintptr_t h_lw_root, h_lw_view,', 'uintptr_t h_lw_root; static uintptr_t h_lw_view,')
sub(r'^static mpm_model M;', 'mpm_model M;')
open(p, 'w').write(s)
print('changed' if s != o else 'unchanged')
