#!/usr/bin/env python3
"""left_prompt regression (2026-10-15 ~11:40): World = Foreign Expedition was remembered in mp_menu.json, so a later normal
Host + Start took the expedition path. Fix: the Host page opens at "Your save" every time (expedition rules stay remembered)."""
import sys
S = sys.argv[1]   # a src tree
p = S + '/mpmenu_model.c'; s = open(p).read()
a = "        if (c == MPB_HOST) return go(m, MPM_TITLE_HOST);\n"
b = ("        if (c == MPB_HOST) { m->set.fe = 0; return go(m, MPM_TITLE_HOST); }   /* World opens at \"Your save\" every time: an expedition is\n"
     "                                                                             chosen on purpose (left_prompt regression 2026-10-15) */\n")
assert s.count(a) == 1; open(p, 'w').write(s.replace(a, b))
p = S + '/mpmenu.c'; s = open(p).read()
a = '    if (jnum(b, "fe", &v)) M.set.fe = v != 0;\n'
assert s.count(a) == 1; open(p, 'w').write(s.replace(a, '    /* "fe" (World) is not restored: the Host page opens at "Your save" (the expedition rules below are) */\n'))
p = S + '/mpmenu_test/mpmenu_test.c'; s = open(p).read()
a = "        mpm_step(&f, &t, MPB_WORLD); CHECK(f.set.fe == 1);\n"
b = a + ("        { mpm_model g2 = f; g2.state = MPM_TITLE_MENU; mpm_step(&g2, &t, MPB_HOST); CHECK(g2.set.fe == 0); }   /* reopened Host page: Your save */\n")
assert s.count(a) == 1; open(p, 'w').write(s.replace(a, b))
print('patched', S)
