# i18n rules for src/ui_tip.c (our texts in the game's tooltip window). No text of its own: the callers pass
# translated titles and bodies; here only the safe C -> il2cpp string and whole characters in the fixed buffers.
EDITS = [
    ('#include "rig.h"\n#include <stdio.h>', '#include "rig.h"\n#include "ds_i18n.h"\n#include "ui_utf8.h"\n#include <stdio.h>'),
    ('static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }',
     'static inline Obj u_str(const char *s) { return (Obj)ui_str_new(F(0x6F9738, void *(*)(const char *)), s); }   /* broken UTF-8: a repaired copy (ui_utf8.h) */'),
    ('    snprintf(cur_title, sizeof cur_title, "%s", title);\n    snprintf(cur_body, sizeof cur_body, "%s", body);',
     '    snprintf(cur_title, sizeof cur_title, "%s", title); dsi_utf8_fix_end(cur_title);   /* a long translation: cut at a character */\n'
     '    snprintf(cur_body, sizeof cur_body, "%s", body); dsi_utf8_fix_end(cur_body);'),
]
