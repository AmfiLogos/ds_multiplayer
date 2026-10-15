#!/usr/bin/env python3
"""FE hunks of ui_game.c applied onto the CURRENT src/ui_game.c (the lane copy is older than any src snapshot, so a
3-way merge would revert newer src lines). Writes merge/tree/ui_game.c."""
import os, re
R = os.path.expanduser('~/ds_multiplayer'); L = open(R + '/ext/foreign_expedition/src/ui_game.c').read(); s = open(R + '/src/ui_game.c').read()
def one(hay, needle):
    assert hay.count(needle) == 1, needle[:80]; return needle
def between(t, a, b):
    i = t.index(a); j = t.index(b, i); return t[i:j]
# 1. decl + Guild Contact block, before hk_activate_view
blk = between(L, 'int fe_recruit_allowed(void);', 'static void hk_activate_view(Obj cont, int type, void *mi) {')
a = one(s, 'static void result_opened(void);\nstatic void hk_activate_view(Obj cont, int type, void *mi) {\n    if (u_gc_get(h_popups) != cont) { u_gc_free(h_popups); h_popups = u_gc_new(cont); }\n')
guard = [l for l in L.splitlines() if 'type == 8 && !fe_recruit_allowed()' in l][0]
s = s.replace(a, 'static void result_opened(void);\n' + blk + 'static void hk_activate_view(Obj cont, int type, void *mi) {\n    if (u_gc_get(h_popups) != cont) { u_gc_free(h_popups); h_popups = u_gc_new(cont); }\n' + guard + '\n')
# 3. ui_items key + amount
old = re.search(r'        bput\(&b, "%s\{\\"guid\\":\\"%s\\",\\"cell\\":\[%d,%d\],\\"owner\\":%d\}", k\+\+ \? "," : "", gs, \(int\)x, \(int\)y,\n             c \? dsnet_item_owner\(c, \(const uint8_t \*\)&g\) : -9\);\n', s).group(0)
new = re.search(r'        char key\[80\] = ""; str_utf8.*?\n.*?\n.*?-9, key, \*\(int32_t \*\)\(e \+ 0x30\)\);\n', L, re.S).group(0)
s = s.replace(one(s, old), new)
# 4. ui_find, before ui_items dispatch (only if src has none)
if '"ui_find"' not in s:
    uf = between(L, '    if (!strcmp(cmd, "ui_find")) {', '\n    }\n') + '\n    }\n'
    a = one(s, '    if (!strcmp(cmd, "ui_items")) return ui_items_cmd(line);\n')
    s = s.replace(a, uf + a)
# 5. ui_frame
a = one(s, 'void ui_frame(void) {\n    static unsigned n;\n')
s = s.replace(a, a + '    if (n % 60 == 30) guild_button_apply();                  /* also after loads that skip the view\'s refresh */\n')
# 6. hooks
a = one(s, '        { 0xF62554, hk_activate_view, (void **)&o_activate_view, "ui PopUpViewContainer.ActivateView" },\n')
hk = ''.join(l + '\n' for l in L.splitlines() if 'hk_faction_' in l and '{ 0xEF' in l)
s = s.replace(a, a + hk)
os.makedirs(R + '/ext/foreign_expedition/merge/tree', exist_ok=True)
open(R + '/ext/foreign_expedition/merge/tree/ui_game.c', 'w').write(s); print('ok', len(s))
