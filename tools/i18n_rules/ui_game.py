# i18n rules for src/ui_game.c (status lines, HUD, notices, tooltips, player list). Engine: tools/i18n_wrap.py
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import i18n_wrap as W  # noqa: E402

COLOR = True

SINKS = {
    "notice": (0, "fmt"),
    "snprintf": (2, "fmt"),
    "tip_at": (1, "text"),
    "tmp_set": (1, "text"),
    "line_text": (1, "text"),
    "plist_set": (1, "text"),
}

DENY = [
    r'\.json"',                 # settings file paths
    r'^"%s/%s a=',              # see-through probe (rig)
    r'^"%s<color=#f16071>X</color>"$',   # the red X mark alone, not a word
    r'^"Player"$',              # default player NAME sent to the others (content, not UI text)
    r'^"(true|false|ok|missing)"$',
]

HELPERS = r'''/* i18n: "<color=hex>text</color>" with the colour outside the translated text (8 rotating buffers: one call
   may use a few at once) */
static const char *ui_col(const char *hex, const char *text) {
    static char b[8][512]; static int k;
    char *o = b[k++ & 7];
    snprintf(o, sizeof b[0], "<color=%s>%s</color>", hex, text);
    dsi_utf8_fix_end(o);
    return o;
}
/* i18n: a word the game itself shows, from its own text table in the current language
   (Refactor.TextKeyExtensions.GetText, Mac 0x80EBAC; win_map gives the Windows address). Our English (TR) when the
   key is missing ("TextKey not found : ..."). Kept until the language changes (dsi_generation). Main thread, in a
   campaign (the game's table is loaded by then). */
static const char *game_text(const char *key, const char *en) {
    static struct { char key[64]; unsigned gen; char s[128]; } c[8];
    unsigned g = dsi_generation() + 1u;
    int i = 0;
    while (i < 7 && c[i].key[0] && strcmp(c[i].key, key)) i++;   /* more keys than slots: the last slot is shared */
    if (strcmp(c[i].key, key) || c[i].gen != g) {
        snprintf(c[i].key, sizeof c[i].key, "%s", key); c[i].gen = g;
        str_utf8(F(0x80EBAC, Obj (*)(Obj, void *))(u_str(key), NULL), c[i].s, sizeof c[i].s);
        if (!strncmp(c[i].s, "TextKey not found", 17)) c[i].s[0] = 0;
    }
    return c[i].s[0] ? c[i].s : TR(en);
}

/* ---------------------------------------------------------------- session view */'''

EDITS = [
    # header: string tables + safe C -> il2cpp strings
    ('#include "ord_api.h"\n#include <stdarg.h>',
     '#include "ord_api.h"\n#include "ds_i18n.h"\n#include "ui_utf8.h"\n#include <stdarg.h>'),
    ('static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }',
     'static inline Obj u_str(const char *s) { return (Obj)ui_str_new(F(0x6F9738, void *(*)(const char *)), s); }   /* broken UTF-8: a repaired copy (ui_utf8.h) */'),
    ('/* ---------------------------------------------------------------- session view */', HELPERS),
    ('char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);',
     'char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); dsi_utf8_fix_end(b);'),
    # names in fixed buffers: never end inside a character
    ('    snprintf(out, cap, "%s", nm[0] ? nm : "an absent player");\n}',
     '    snprintf(out, cap, "%s", nm[0] ? nm : TR("an absent player"));\n    dsi_utf8_fix_end(out);\n}'),
    ('snprintf(tag, cap, " <color=#8a8088>[%s, away]</color>", nm); }',
     'char aw[64]; snprintf(aw, sizeof aw, TR("%s, away"), nm); snprintf(tag, cap, " <color=#8a8088>[%s]</color>", aw); dsi_utf8_fix_end(tag); }'),
    ('snprintf(tag, sizeof tag, " <color=#8a8088>%s%s%s, away</color>", dot ? dot : "", dot && dot[0] ? " " : "", nm); }',
     'char aw[64]; snprintf(aw, sizeof aw, TR("%s, away"), nm); snprintf(tag, sizeof tag, " <color=#8a8088>%s%s%s</color>", dot ? dot : "", dot && dot[0] ? " " : "", aw); dsi_utf8_fix_end(tag); }'),
    # whole sentences instead of glued pieces
    ('else notice("%s belongs to %s%s", un, ui_player_label(&p, a.target, pn), me() == 0 ? " (Admin mode lets the host take it)" : "");',
     'else if (me() == 0) notice(TR("%s belongs to %s (Admin mode lets the host take it)"), un, ui_player_label(&p, a.target, pn));\n'
     '        else notice(TR("%s belongs to %s"), un, ui_player_label(&p, a.target, pn));'),
    (' : "yes";', ' : TR("yes");'),
    ('tmp_set(btx, "<color=#ccbeb8>Cancel</color>");',
     'tmp_set(btx, ui_col("#ccbeb8", game_text("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel"))));'),
    ('"<color=#a69085>%s</color>", onoff[k].txt);', '"<color=#a69085>%s</color>", TR(onoff[k].txt));'),
    ('nn += (size_t)snprintf(note + nn, sizeof note - nn, "%sYou control %s\'s units until they return", nn ? "\\n" : "", nm[0] ? nm : "an absent player");',
     'nn += (size_t)snprintf(note + nn, sizeof note - nn, "%s", nn ? "\\n" : "");\n'
     '            if (nn < sizeof note) nn += (size_t)snprintf(note + nn, sizeof note - nn, TR("You control %s\'s units until they return"), nm[0] ? nm : TR("an absent player"));'),
    ('? "resyncing" : ns == DSNET_NET_LOADING ? "loading" : behind >= UI_BEHIND_CATCHUP ? "catching up" : "in sync";',
     '? TR("resyncing") : ns == DSNET_NET_LOADING ? TR("loading") : behind >= UI_BEHIND_CATCHUP ? TR("catching up") : TR("in sync");'),
    # by-value check of a label (i18n_sites 2d): by state now
    ('static char save_label[128];', 'static char save_label[128]; static int save_label_ours;   /* 1 = this build wrote its own text into the stock Save label */'),
    ('if (strstr(cur, "Only the host") && save_label[0]) tmp_set(t, save_label);   /* a label from an older build */',
     'if (save_label_ours && save_label[0]) { tmp_set(t, save_label); save_label_ours = 0; }   /* by state, not by text (i18n): nothing writes it now */'),
    ('u_str(spect() ? "You are watching: only the host saves this world" : "Only the host saves this world")',
     'u_str(spect() ? TR("You are watching: only the host saves this world") : TR("Only the host saves this world"))'),
    # "Watching %s's %s" with world / expedition: one sentence each
    ('    const char *what = sv ? "world" : "expedition", *whose = sv ? hn : nm;\n'
     '    if (full) snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>Watching %s\'s %s</color></smallcaps>", whose, what);\n'
     '    else snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>Watching %s\'s %s</color></smallcaps> <size=80%%><color=#a69085>(click for options)</color></size>", whose, what);',
     '    char wt[160]; snprintf(wt, sizeof wt, sv ? TR("Watching %s\'s world") : TR("Watching %s\'s expedition"), sv ? hn : nm);\n'
     '    if (full) snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>%s</color></smallcaps>", wt);\n'
     '    else snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>%s</color></smallcaps> <size=80%%><color=#a69085>%s</color></size>", wt, TR("(click for options)"));'),
    # lists of names and the verb after them
    ('const char *sep = k == 0 ? "" : k == cnt - 1 ? " and " : ", ";',
     'const char *sep = k == 0 ? "" : k == cnt - 1 ? TR(" and ") : TR(", ");'),
    ('"<color=#a69085> %s this too</color>", cnt > 1 ? "see" : "sees");',
     '"<color=#a69085> %s</color>", TRN("sees this too", "see this too", cnt));'),
    ('"%s%s", k == 0 ? "" : k == cnt - 1 ? " and " : ", ", ui_player_label(&p, i, nm)); k++; }',
     '"%s%s", k == 0 ? "" : k == cnt - 1 ? TR(" and ") : TR(", "), ui_player_label(&p, i, nm)); k++; }'),
    ('" %s the same part of the map as you.", cnt > 1 ? "look at" : "looks at");',
     '" %s", TRN("looks at the same part of the map as you.", "look at the same part of the map as you.", cnt));'),
    # game words that stand alone: the game's own text
    ('else snprintf(ft, sizeof ft, "<color=#a69085>Settlement</color>");',
     'else snprintf(ft, sizeof ft, "%s", ui_col("#a69085", game_text("TEXTKEY_MAPTYPE_Base", TR_NOOP("Settlement"))));'),
    ('tip_at(u_gc_get(h_result_pill), "Portal", b);',
     'tip_at(u_gc_get(h_result_pill), game_text("TEXTKEY_DungeonResultUI_Portal", TR_NOOP("Portal")), b);'),
    ('tip_at(sb, "Save Campaign", ', 'tip_at(sb, game_text("TEXTKEY_SystemMenu_SaveCampaign", TR_NOOP("Save Campaign")), '),
    # population line: two sentences
    ('    if (!left) { snprintf(out, cap_, "<color=#d9be8f>Units you recruit are yours. Population full%s (%d/%d): host rule %s</color>",\n'
     '                          pool ? "" : " for you", have, cap, pop_mode_text(mode)); return; }',
     '    if (!left) { snprintf(out, cap_, pool ? TR("<color=#d9be8f>Units you recruit are yours. Population full (%d/%d): host rule %s</color>")\n'
     '                                          : TR("<color=#d9be8f>Units you recruit are yours. Population full for you (%d/%d): host rule %s</color>"),\n'
     '                          have, cap, pop_mode_text(mode)); return; }'),
    ('tip_at(u_gc_get(h_pl[rows[k].id]), rows[k].t, rows[k].b);', 'tip_at(u_gc_get(h_pl[rows[k].id]), TR(rows[k].t), TR(rows[k].b));'),
    ('char who[40] = "a player"; if (row_player[r] >= 0)', 'char who[40]; snprintf(who, sizeof who, "%s", TR("a player")); if (row_player[r] >= 0)'),
    ('char ttl[40] = "Away"; if (who >= 0)', 'char ttl[40]; snprintf(ttl, sizeof ttl, "%s", TR("Away")); if (who >= 0)'),
    # "press >> to agree" found by text (i18n_sites 2d): by state now (ui_model ui_status_line_agree)
    ('static char status_text[256], pause_text[256];',
     'static int status_agree;                                     /* the status line asks the others to press >> (by state, not by text) */\nstatic char status_text[256], pause_text[256];'),
    ('char line[256] = "";\n    pause_text[0] = 0;', 'char line[256] = ""; int agree = 0;\n    pause_text[0] = 0;'),
    ('ui_status_line(ns, &v, &p, me(), line, sizeof line);', 'ui_status_line(ns, &v, &p, me(), line, sizeof line); agree = ui_status_line_agree();'),
    ('            snprintf(pause_text, sizeof pause_text, "%s", line);   /* the banner says the same */',
     '            snprintf(pause_text, sizeof pause_text, "%s", line); agree = 0;   /* the banner says the same */'),
    ('    snprintf(status_text, sizeof status_text, "%s", line);\n',
     '    snprintf(status_text, sizeof status_text, "%s", line); status_agree = agree;\n'),
    ('if (strstr(status_text, "press >>"))', 'if (status_agree)'),
]


def hook(t, log):
    # label tables: English stays the key in the table, TR() where it is drawn
    t = W.wrap_region(t, 'onoff[] = {', '};', "TR_NOOP", log)
    t = W.wrap_region(t, 'static const struct { int id; const char *t, *b; } rows[] = {', '};', "TR_NOOP", log)
    t = W.wrap_region(t, 'const char *lab[3] = {', '};', "TR", log)
    t = W.wrap_region(t, 'static const char *pop_mode_text(int m) {', '}', "TR", log)
    t = W.wrap_region(t, 'static const char *conn_words(int lvl) {', '}', "TR", log)
    return t
