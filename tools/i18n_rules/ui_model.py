# i18n rules for src/ui_model.c (player labels, colour names, owner tags, status / vote lines). Pure C: the tests
# link ui_test/dsi_impl_test.c; with no language file every text stays English (the tests assert English).
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import i18n_wrap as W  # noqa: E402

COLOR = False   # no ui_col here (pure model); the few tagged words keep their tag

SINKS = {"snprintf": (2, "fmt")}

DENY = [
    r'\\"',                       # mp_ui.json writer
    r'<b>X</b>',                  # the red X mark
]

EDITS = [
    ('#include "ui_model.h"\n#include <ctype.h>', '#include "ui_model.h"\n#include "ds_i18n.h"\n#include <ctype.h>'),
    ('const char *ui_palette_name(int i) { return color_names[(unsigned)i % UI_PALETTE_N]; }',
     'const char *ui_palette_name(int i) { return TR(color_names[(unsigned)i % UI_PALETTE_N]); }'),
    # name lists: the separator is translated too
    ('if (len < cap) len += (size_t)snprintf(out + len, cap - len, "%s%s", n ? ", " : "", nm);',
     'if (len < cap) len += (size_t)snprintf(out + len, cap - len, "%s%s", n ? TR(", ") : "", nm);'),
    ('        len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));\n    }\n    return n;',
     '        len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? TR(", ") : "", ui_player_label(p, i, nm));\n    }\n    return n;'),
    # "Waiting for " + names + " (loading)": one sentence
    ('''        size_t len = (size_t)snprintf(out, cap, "Waiting for ");
        int n = 0;
        for (int i = 0; i < UI_MAX_PLAYERS && len < cap; i++) {
            if (i == me || !(v->busy & v->present & 1u << i)) continue;
            char nm[40];
            len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));
        }
        if (len < cap) snprintf(out + len, cap - len, " (loading)");
        return;''',
     '''        char names[400] = ""; size_t len = 0;
        int n = 0;
        for (int i = 0; i < UI_MAX_PLAYERS && len < sizeof names; i++) {
            if (i == me || !(v->busy & v->present & 1u << i)) continue;
            char nm[40];
            len += (size_t)snprintf(names + len, sizeof names - len, "%s%s", n++ ? TR(", ") : "", ui_player_label(p, i, nm));
        }
        snprintf(out, cap, TR("Waiting for %s (loading)"), names);
        return;'''),
    # "Ben and Ana want Fast": the verb follows the count; the agree line is known by state (ui_game.c tooltip)
    ('snprintf(out, cap, "%s %s %s \\u00B7 press >> to agree", names, n > 1 ? "want" : "wants", ui_speed_text(want));',
     'snprintf(out, cap, TRN("%s wants %s \\u00B7 press >> to agree", "%s want %s \\u00B7 press >> to agree", n), names, ui_speed_text(want));\n'
     '        status_agree = 1;'),
    ('void ui_status_line(ui_net_state st, const ui_votes *v, const ui_players *p, int me, char *out, size_t cap) {\n    if (!cap) return;',
     'static int status_agree;\nint ui_status_line_agree(void) { return status_agree; }\n'
     'void ui_status_line(ui_net_state st, const ui_votes *v, const ui_players *p, int me, char *out, size_t cap) {\n    status_agree = 0;\n    if (!cap) return;'),
    # story line: "then you decide" / "then Ben decides"
    ('else snprintf(tail, sizeof tail, " \\u00B7 %d s, then %s decide%s", left_s, me == 0 ? "you" : hn, me == 0 ? "" : "s");',
     'else if (me == 0) snprintf(tail, sizeof tail, TR(" \\u00B7 %d s, then you decide"), left_s);\n'
     '        else snprintf(tail, sizeof tail, TR(" \\u00B7 %d s, then %s decides"), left_s, hn);'),
    # map vote: "Waiting for " + names + " (n of m agree)"
    ('''    size_t len = (size_t)snprintf(out, cap, "Waiting for ");
    int n = 0, yes = 0, all = 0;''',
     '''    char names[400] = ""; size_t len = 0;
    int n = 0, yes = 0, all = 0;'''),
    ('''        if (m->vote[i] >= 0 || len >= cap) continue;
        char nm[40];
        len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));
    }
    if (len < cap) snprintf(out + len, cap - len, " (%d of %d agree)", yes, all);''',
     '''        if (m->vote[i] >= 0 || len >= sizeof names) continue;
        char nm[40];
        len += (size_t)snprintf(names + len, sizeof names - len, "%s%s", n++ ? TR(", ") : "", ui_player_label(p, i, nm));
    }
    snprintf(out, cap, TR("Waiting for %s (%d of %d agree)"), names, yes, all);'''),
    ('m->vote[i] == 0 ? "<color=#f16071>no</color>" : "\\u2026";', 'm->vote[i] == 0 ? TR("<color=#f16071>no</color>") : "\\u2026";'),
    # map vote result: "Ana, Bo said no: staying here"
    ('''    size_t n = 0; int k = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && n < cap; i++)
        if ((present >> i & 1u) && m->vote[i] == 0) n += (size_t)snprintf(out + n, cap - n, "%s%s", k++ ? ", " : "", ui_player_label(p, i, nm));
    if (!k) n = (size_t)snprintf(out, cap, "The vote");
    if (n < cap) {
        if (m->kind == UI_MAP_NEXT_FLOOR && cur_floor_no >= 1) snprintf(out + n, cap - n, " said no: staying on floor %d", cur_floor_no);
        else snprintf(out + n, cap - n, " said no: staying here");
    }''',
     '''    char names[400] = ""; size_t n = 0; int k = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && n < sizeof names; i++)
        if ((present >> i & 1u) && m->vote[i] == 0) n += (size_t)snprintf(names + n, sizeof names - n, "%s%s", k++ ? TR(", ") : "", ui_player_label(p, i, nm));
    if (m->kind == UI_MAP_NEXT_FLOOR && cur_floor_no >= 1) {
        if (k) snprintf(out, cap, TR("%s said no: staying on floor %d"), names, cur_floor_no);
        else snprintf(out, cap, TR("The vote said no: staying on floor %d"), cur_floor_no);
    } else if (k) snprintf(out, cap, TR("%s said no: staying here"), names);
    else snprintf(out, cap, "%s", TR("The vote said no: staying here"));'''),
    ('const char *st = state == 0 ? "in sync" : state == 1 ? "loading" : state == 2 ? "<color=#f16071>out of sync</color>" : "";',
     'const char *st = state == 0 ? TR("in sync") : state == 1 ? TR("loading") : state == 2 ? TR("<color=#f16071>out of sync</color>") : "";'),
    ('player == me ? " (you)" : "", player == me ? "" : ping, st);', 'player == me ? TR(" (you)") : "", player == me ? "" : ping, st);'),
]


def hook(t, log):
    t = W.wrap_region(t, 'static const char *const color_names[UI_PALETTE_N] = {', '};', "TR_NOOP", log)
    t = W.wrap_region(t, 'const char *ui_speed_text(int s) {', '}', "TR", log)
    t = W.wrap_region(t, 'const char *ui_map_kind_text(int kind) {', '\n}', "TR", log)
    t = W.wrap_region(t, 'const char *ui_map_short(int kind) {', '}', "TR", log)
    t = W.wrap_region(t, 'const char *what = m->kind == UI_MAP_NEXT_FLOOR ?', ';', "TR", log)
    t = W.wrap_region(t, 'const char *ui_mini_size_text(int size) {', '}', "TR", log)
    t = W.wrap_region(t, 'const char *ui_outline_text(int m) {', '}', "TR", log)
    t = W.wrap_region(t, 'const char *ui_names_text(int m) {', '}', "TR", log)
    return t
