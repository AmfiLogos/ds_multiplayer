# i18n rules for src/ui_teams.c (Teams screen, proposal panel, Going block). Engine: tools/i18n_wrap.py.
# EDITS run first (exact text), then the generic SINKS pass, then COLOR.
# The colour macros (C_MAIN, C_SEC, C_ACC, C_WARN) break the literal scanner, so every "<color=" C_x ">..." site
# is an EDIT: ui_col(C_x, TR("text")) / ui_colf(C_x, TR("fmt"), ...) keep the tags out of the translated text.

SINKS = {
    "notice": (0, "fmt"),
    "snprintf": (2, "fmt"),
    "tip_on": (1, "text"),
    "center_msg": (0, "text"),
    "tmp_set": (1, "text"),
    "set_text": (8, "text"),
    "p_text": (7, "text"),
    "section_header": (8, "text"),
}

DENY = [
    r'<color="$',              # macro pieces: "<color=" C_SEC ">..."
    r'^">',
    r'\\"cmd\\"',              # rig command JSON
    r'\.json"',                # file names
    r'Panel_',                 # transform paths
    r'^"Region: %s%s%s%s"$',   # region_line: state / tests only, never drawn
    r'^" \(proposal by "$',
    r'^"\(proposed\)"$',       # t at the bottom bar: built, then (void)t (owner lan_v7: no region line in Teams)
    r'pick another one in Go to dungeon',
    r'^" no units"$',          # side_s: log / test state only
    r'Going block hidden',     # test probe
    r'>RUN<',                  # 22x22 fallback tag when the check sprite is missing (fixed width)
]

COLOR = True

EDITS = [
    # ---- includes + helpers
    ('#include <dlfcn.h>\n\n/* ----',
     '#include <dlfcn.h>\n#include "ds_i18n.h"\n#include "ui_utf8.h"\n\n/* ----'),
    ('static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }',
     'static inline Obj u_str(const char *s) { return (Obj)ui_str_new(F(0x6F9738, void *(*)(const char *)), s); }   /* i18n: cut UTF-8 never blanks a label */'),
    (r'''static void str_utf8(Obj s, char *out, size_t cap) {         /* il2cpp String -> UTF-8 (BMP is enough here) */
    size_t n = 0;
    if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t);
    const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len && n + 4 < cap; i++) {
        uint32_t u = c[i];
        if (u < 0x80) out[n++] = (char)u;
        else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 63)); }
        else { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | (u >> 6 & 63)); out[n++] = (char)(0x80 | (u & 63)); }
    }
    out[n] = 0;
}''',
     r'''static void str_utf8(Obj s, char *out, size_t cap) {         /* il2cpp String -> UTF-8: whole code points, surrogate pairs too */
    size_t n = 0;
    if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t);
    const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00u); i++; }
        else if (u >= 0xD800 && u < 0xE000) u = 0xFFFD;      /* a lone surrogate: never written as bytes */
        char b[4]; size_t m;
        if (u < 0x80) { b[0] = (char)u; m = 1; }
        else if (u < 0x800) { b[0] = (char)(0xC0 | u >> 6); b[1] = (char)(0x80 | (u & 63)); m = 2; }
        else if (u < 0x10000) { b[0] = (char)(0xE0 | u >> 12); b[1] = (char)(0x80 | (u >> 6 & 63)); b[2] = (char)(0x80 | (u & 63)); m = 3; }
        else { b[0] = (char)(0xF0 | u >> 18); b[1] = (char)(0x80 | (u >> 12 & 63)); b[2] = (char)(0x80 | (u >> 6 & 63)); b[3] = (char)(0x80 | (u & 63)); m = 4; }
        if (n + m >= cap) break;
        memcpy(out + n, b, m); n += m;
    }
    out[n] = 0;
}''' ),
    ('''    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rig_log("teams: %s", b);''',
     '''    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); dsi_utf8_fix_end(b);
    rig_log("teams: %s", b);'''),
    ('HelpNoticeView.Show */\n}\nstatic void player_name(int p, char out[40]) {\n',
     '''HelpNoticeView.Show */\n}\n/* i18n: colour tags stay out of the translated text (8 rotating buffers: enough for one statement) */
static const char *ui_col(const char *hex, const char *text) {
    static char b[8][512]; static unsigned k; char *o = b[k++ & 7];
    snprintf(o, 512, "<color=%s>%s</color>", hex, text ? text : ""); dsi_utf8_fix_end(o);
    return o;
}
static const char *ui_colf(const char *hex, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *ui_colf(const char *hex, const char *fmt, ...) {
    char in[480]; va_list ap; va_start(ap, fmt); vsnprintf(in, sizeof in, fmt, ap); va_end(ap); dsi_utf8_fix_end(in);
    return ui_col(hex, in);
}
static void str_utf8(Obj s, char *out, size_t cap);
/* i18n: a game text by TEXTKEY (Refactor.TextKeyExtensions.GetText, Mac 0x80EBAC; Windows by name through win_map),
   kept until the language changes (dsi_generation); our English (TR) when the key is missing. Main thread, campaign. */
static const char *game_text(const char *key, const char *en, char *buf, size_t cap, unsigned *gen) {
    if (*gen == dsi_generation() + 1u && buf[0]) return buf;
    Obj s = F(0x80EBAC, Obj (*)(Obj, void *))(u_str(key), NULL);
    str_utf8(s, buf, cap);
    if (!buf[0] || !strncmp(buf, "TextKey not found", 17)) { buf[0] = 0; return TR(en); }
    *gen = dsi_generation() + 1u;
    return buf;
}
static const char *next_text(void) {                         /* the stock expedition window's own "Next" (SubUI_ExpeditionPlanLowerPanel) */
    static char b[96]; static unsigned g;
    return game_text("TEXTKEY_Shared_Next", TR_NOOP("Next"), b, sizeof b, &g);
}
static void player_name(int p, char out[40]) {
'''),
    # ---- regions: the game's own names
    ('''static const char *region_name(int r) {
    static const char *n[] = { "Veil of Silence", "Festering Burrow", "Abandoned Mine", "Forgotten Abyss", "Dead Swamp",
                               "Tower of Apocalypse", "Abyss" };
    return r >= 0 && r < (int)(sizeof n / sizeof n[0]) ? n[r] : "(none)";
}''',
     '''/* i18n: the game's names, TEXTKEY_ExpeditionPlanUI_<DungeonRegion>_NAME (DungeonRegion 0..6, dump.cs); n[] is the
   fallback when a key is missing */
static const char *region_name(int r) {
    static const char *n[] = { TR_NOOP("Veil of Silence"), TR_NOOP("Festering Burrow"), TR_NOOP("Abandoned Mine"), TR_NOOP("Forgotten Abyss"),
                               TR_NOOP("Dead Swamp"), TR_NOOP("Tower of Apocalypse"), TR_NOOP("Abyss") };
    static const char *const key[] = { "TEXTKEY_ExpeditionPlanUI_VeilSilence_NAME", "TEXTKEY_ExpeditionPlanUI_FesteringBurrow_NAME",
        "TEXTKEY_ExpeditionPlanUI_AbandonedMine_NAME", "TEXTKEY_ExpeditionPlanUI_ForgottenAbyss_NAME", "TEXTKEY_ExpeditionPlanUI_DeadSwamp_NAME",
        "TEXTKEY_ExpeditionPlanUI_TowerApocalypse_NAME", "TEXTKEY_ExpeditionPlanUI_Abyss_NAME" };
    static char got[7][128]; static unsigned gen[7];
    if (r < 0 || r >= (int)(sizeof n / sizeof n[0])) return TR("(none)");
    return game_text(key[r], n[r], got[r], sizeof got[r], &gen[r]);
}'''),
    # ---- proposal ended (one sentence per case, names as one list)
    (r'''    else if (e == TM_PE_TIMEOUT) {
        size_t l = (size_t)snprintf(prop_event, sizeof prop_event, "No answer from");
        for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof prop_event - 44; p++)
            if (mask >> p & 1u) { char n2[40]; pname(p, n2); l += (size_t)snprintf(prop_event + l, sizeof prop_event - l, "%s %s", l > 14 ? "," : "", n2); }
        snprintf(prop_event + l, sizeof prop_event - l, ": expedition cancelled");
    }''',
     r'''    else if (e == TM_PE_TIMEOUT) {
        char who[160] = ""; size_t l = 0;
        for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof who - 44; p++)
            if (mask >> p & 1u) { char n2[40]; pname(p, n2); l += (size_t)snprintf(who + l, sizeof who - l, "%s%s", l ? TR(", ") : "", n2); }
        snprintf(prop_event, sizeof prop_event, TR("No answer from %s: expedition cancelled"), who);
    }
    dsi_utf8_fix_end(prop_event);'''),
    ('char m[160]; snprintf(m, sizeof m, "%s proposes an expedition to %s", pn_, region_name(r)); center_msg(m);',
     'char m[300]; snprintf(m, sizeof m, TR("%s proposes an expedition to %s"), pn_, region_name(r)); center_msg(m);'),
    ('l += (size_t)snprintf(out + l, cap > l ? cap - l : 0, "%s%s", l ? ", " : "", nm);',
     'l += (size_t)snprintf(out + l, cap > l ? cap - l : 0, "%s%s", l ? TR(", ") : "", nm);'),
    # ---- fonts: the game's font for the current language (LanguageSetting.GetFont), else the template's
    ('''0x580));\n}\nstatic void tmp_font(Obj t, Obj f) {                         /* TMP_Text.font, with that font's own material */''',
     '''0x580));\n}\n/* i18n: LanguageSetting.GetFont(FontStyleType: EasyRead 0, VeryDecorative 2), Mac 0x898A28 (Windows by name through
   win_map): the font the game's own texts use in the current language (CJK: its own faces). The templates come from
   prefab assets, whose serialized Lora / Cinzel never change with the language. Template font when it gives nothing. */
static Obj lang_font(int style) { Obj f = F(0x898A28, Obj (*)(int32_t, void *))(style, NULL); return u_alive(f) && tm_is(f, K_FA) ? f : NULL; }
static Obj font_title(void) { Obj f = lang_font(2); return f ? f : u_gc_get(h_font_title); }
static Obj font_body(void) {
    Obj f = lang_font(0);
    return f ? f : (tm_is(tmp_of(u_gc_get(h_tpl_text)), K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tmp_of(u_gc_get(h_tpl_text)), NULL) : NULL);
}
static void tmp_font(Obj t, Obj f) {                         /* TMP_Text.font, with that font's own material */'''),
    ('    tmp_font(o, cinzel ? u_gc_get(h_font_title) : (tm_is(tmp_of(u_gc_get(h_tpl_text)), K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tmp_of(u_gc_get(h_tpl_text)), NULL) : NULL));',
     '    tmp_font(o, cinzel ? font_title() : font_body());'),
    ('tmp_font(t, u_gc_get(h_font_title)); tmp_set(t, "Teams"); }',
     'tmp_font(t, font_title()); tmp_set(t, TR("Teams")); }'),
    ('if (t) { tmp_font(t, u_gc_get(h_font_title)); rt_pos(u_tr(t), 0, top - 26); tmp_set(t, "<color=" C_MAIN ">Going</color>"); }',
     'if (t) { tmp_font(t, font_title()); rt_pos(u_tr(t), 0, top - 26); tmp_set(t, ui_col(C_MAIN, TR("Going"))); }'),
    # ---- folded bar
    ('AL_LEFT, 1, "<color=" C_MAIN ">Teams</color>");', 'AL_LEFT, 1, ui_col(C_MAIN, TR("Teams")));'),
    ('AL_RIGHT, 0, "<color=" C_ACC ">Show</color>");', 'AL_RIGHT, 0, ui_col(C_ACC, TR("Show")));'),
    ('AL_RIGHT, 0, "<color=" C_ACC ">Close</color>");', 'AL_RIGHT, 0, ui_col(C_ACC, TR("Close")));'),
    # ---- lending chip
    ('snprintf(t, sizeof t, "<color=" C_ACC ">Lend</color>");', 'snprintf(t, sizeof t, "%s", ui_col(C_ACC, TR("Lend")));'),
    ('snprintf(t, sizeof t, "<color=" C_ACC ">Lent: </color><color=%s>%s</color>", ui_player_color(lt), nm);',
     'snprintf(t, sizeof t, "<color=" C_ACC ">%s </color><color=%s>%s</color>", TR("Lent:"), ui_player_color(lt), nm);'),
    ('snprintf(t, sizeof t, "<color=" C_ACC ">From </color><color=%s>%s</color>", ui_player_color(owner), nm);',
     'snprintf(t, sizeof t, "<color=" C_ACC ">%s </color><color=%s>%s</color>", TR("From"), ui_player_color(owner), nm);'),
    # ---- Teams window
    ('run_ ? "<color=" C_ACC ">On the run</color>" : "<color=" C_SEC ">At the settlement</color>"',
     'run_ ? ui_col(C_ACC, TR("On the run")) : ui_col(C_SEC, TR("At the settlement"))'),
    ('"<color=" C_MAIN ">Your units</color>"', 'ui_col(C_MAIN, TR("Your units"))'),
    (r'''snprintf(t, sizeof t, "<color=" C_SEC ">%d unit%s \xC2\xB7 max %d</color>", mine, mine == 1 ? "" : "s", st.max_mark > 0 ? st.max_mark : 3);''',
     r'''snprintf(t, sizeof t, "%s", ui_colf(C_SEC, TRN("%d unit \xC2\xB7 max %d", "%d units \xC2\xB7 max %d", mine), mine, st.max_mark > 0 ? st.max_mark : 3));'''),
    ('"<color=" C_SEC ">Take a free unit below (left-click).</color>"', 'ui_col(C_SEC, TR("Take a free unit below (left-click)."))'),
    ('"<color=" C_MAIN ">Free units</color>"', 'ui_col(C_MAIN, TR("Free units"))'),
    ('"<color=" C_SEC ">Left-click to take</color>"', 'ui_col(C_SEC, TR("Left-click to take"))'),
    ('"<color=" C_SEC ">No free units</color>"', 'ui_col(C_SEC, TR("No free units"))'),
    ('snprintf(hd, sizeof hd, "<color=" C_SEC ">Reserved for %s (away)</color>", rn);',
     'snprintf(hd, sizeof hd, "%s", ui_colf(C_SEC, TR("Reserved for %s (away)"), rn));'),
    ('''snprintf(hd, sizeof hd, "<color=" C_SEC ">%s's units%s</color>", ui_player_label(&pls, pl, lb), !here ? "  <size=88%>left the game</size>" : "");''',
     '''char un_[160]; snprintf(un_, sizeof un_, TR("%s's units"), ui_player_label(&pls, pl, lb)); dsi_utf8_fix_end(un_);
            snprintf(hd, sizeof hd, "<color=" C_SEC ">%s%s%s%s</color>", un_, !here ? "  <size=88%>" : "", !here ? TR("left the game") : "", !here ? "</size>" : "");'''),
    ('snprintf(stl, sizeof stl, "<color=" C_WARN ">Multiplayer is not running yet: changes here are not applied.</color>");',
     'snprintf(stl, sizeof stl, "%s", ui_col(C_WARN, TR("Multiplayer is not running yet: changes here are not applied.")));'),
    ('AL_RIGHT, 0, "<color=" C_ACC ">Hide</color>");', 'AL_RIGHT, 0, ui_col(C_ACC, TR("Hide")));'),
    # ---- proposal panel
    ('static char prop_ok_s[40], prop_no_s[40], prop_wait_s[200], prop_time_s[120];',
     'static char prop_ok_s[120], prop_no_s[120], prop_wait_s[400], prop_time_s[120];'),
    (r'''    size_t l = (size_t)snprintf(prop_wait_s, sizeof prop_wait_s, wm ? "Waiting for" : "Everyone approved");
    for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof prop_wait_s - 44; p++)
        if (wm >> p & 1u) { char nm[40]; pname(p, nm); l += (size_t)snprintf(prop_wait_s + l, sizeof prop_wait_s - l, "%s %s", l > 11 ? "," : ":", p == st.me ? "you" : nm); }''',
     r'''    if (!wm) snprintf(prop_wait_s, sizeof prop_wait_s, "%s", TR("Everyone approved"));
    else {
        char who[300] = ""; size_t l = 0;
        for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof who - 44; p++)
            if (wm >> p & 1u) { char nm[40]; pname(p, nm); l += (size_t)snprintf(who + l, sizeof who - l, "%s%s", l ? TR(", ") : "", p == st.me ? TR("you") : nm); }
        snprintf(prop_wait_s, sizeof prop_wait_s, TR("Waiting for: %s"), who); dsi_utf8_fix_end(prop_wait_s);
    }'''),
    ('"<color=" C_MAIN ">Expedition proposal</color>"', 'ui_col(C_MAIN, TR("Expedition proposal"))'),
    ('prop_folded ? "<color=" C_ACC ">Show</color>" : "<color=" C_ACC ">Hide</color>"',
     'prop_folded ? ui_col(C_ACC, TR("Show")) : ui_col(C_ACC, TR("Hide"))'),
    (r'''"<color=" C_SEC ">Proposed by you \xC2\xB7 </color><color=%s>%s</color>", tc, prop_time_s);''',
     r'''"<color=" C_SEC ">%s \xC2\xB7 </color><color=%s>%s</color>", TR("Proposed by you"), tc, prop_time_s);'''),
    (r'''else snprintf(t, sizeof t, "<color=" C_SEC ">Proposed by </color><color=%s>%s</color><color=" C_SEC "> \xC2\xB7 </color><color=%s>%s</color>",''',
     r'''else snprintf(t, sizeof t, "<color=" C_SEC ">%s </color><color=%s>%s</color><color=" C_SEC "> \xC2\xB7 </color><color=%s>%s</color>", TR("Proposed by"),'''),
    ('snprintf(t, sizeof t, "<color=" C_SEC ">Region</color>  <color=" C_MAIN ">%s</color>", region_name(prop.region));',
     'snprintf(t, sizeof t, "<color=" C_SEC ">%s</color>  <color=" C_MAIN ">%s</color>", TR("Region"), region_name(prop.region));'),
    ('snprintf(t, sizeof t, "<color=" C_SEC ">%d at home</color>", stays);',
     'snprintf(t, sizeof t, "%s", ui_colf(C_SEC, TR("%d at home"), stays));'),
    ('"<color=" C_SEC ">No units: watches the expedition</color>"', 'ui_col(C_SEC, TR("No units: watches the expedition"))'),
    ('snprintf(t, sizeof t, "<color=%s>%d / %d members</color>", prop_np > TM_PARTY_CAP ? C_WARN : C_MAIN, prop_np, TM_PARTY_CAP);',
     'snprintf(t, sizeof t, "%s", ui_colf(prop_np > TM_PARTY_CAP ? C_WARN : C_MAIN, TR("%d / %d members"), prop_np, TM_PARTY_CAP));'),
    # ---- the stock expedition window (receiver) + Going block
    (r'''snprintf(tx, sizeof tx, "<color=" C_SEC ">%s \xC2\xB7 proposed by </color><color=%s>%s</color>", region_name(prop.region), ui_player_color(prop.proposer), pn_);''',
     r'''snprintf(tx, sizeof tx, "<color=" C_SEC ">%s \xC2\xB7 %s </color><color=%s>%s</color>", region_name(prop.region), TR("proposed by"), ui_player_color(prop.proposer), pn_);'''),
    ('tmp_set(tv, "<color=" C_SEC ">No units: watches</color>");', 'tmp_set(tv, ui_col(C_SEC, TR("No units: watches")));'),
    ('Obj nl = next_label(view); if (nl) tmp_set(nl, "Next");', 'Obj nl = next_label(view); if (nl) tmp_set(nl, next_text());'),
    ('''    snprintf(side_time_s, sizeof side_time_s, "<color=%s>%d s left</color><color=" C_SEC "> to pack and answer</color>", l <= 10 ? C_WARN : C_ACC, l);''',
     '''    char n_[64]; snprintf(n_, sizeof n_, TR("%d s left"), l); dsi_utf8_fix_end(n_);
    snprintf(side_time_s, sizeof side_time_s, "<color=%s>%s</color><color=" C_SEC "> %s</color>", l <= 10 ? C_WARN : C_ACC, n_, TR("to pack and answer"));
    dsi_utf8_fix_end(side_time_s);'''),
    ('static char side_time_s[160];', 'static char side_time_s[320];'),
    # stock labels: compare with the same translated text we write (every language)
    ('''            char cur[120] = ""; str_utf8(tmp_get(tx), cur, sizeof cur);
            if (strcmp(cur, "Send the proposal to your friends anyway?")) tmp_set(tx, "Send the proposal to your friends anyway?");''',
     '''            char cur[400] = ""; str_utf8(tmp_get(tx), cur, sizeof cur);
            const char *ask = TR("Send the proposal to your friends anyway?");
            if (strcmp(cur, ask)) tmp_set(tx, ask);'''),
    ('''Obj nl = next_label(v); const char *want = stock_mode == 2 ? "Next" : "Send to friends";
        char cur[40] = "";''',
     '''Obj nl = next_label(v); const char *want = stock_mode == 2 ? next_text() : TR("Send to friends");
        char cur[160] = "";'''),
    # ---- tooltips
    ('''    return lv >= 3 ? "Good connection. In sync." : lv == 2 ? "Slow connection, or catching up with the game."
         : lv == 1 ? "Slow connection, or the game is loading or catching up." : "Disconnected. The game waits, or the host keeps playing their units.";''',
     '''    return lv >= 3 ? TR("Good connection. In sync.") : lv == 2 ? TR("Slow connection, or catching up with the game.")
         : lv == 1 ? TR("Slow connection, or the game is loading or catching up.") : TR("Disconnected. The game waits, or the host keeps playing their units.");'''),
    ('static char tip_last[80], tip_body_last[400];', 'static char tip_last[200], tip_body_last[900];'),
    ('static int tip_header(Obj h, int p) {\n    char t[80], b[200], nm[40];',
     'static int tip_header(Obj h, int p) {\n    char t[160], b[600], nm[40];'),
    ('static int tip_chip(Obj c, int u) {\n    char t[80], b[200], nm[40];',
     'static int tip_chip(Obj c, int u) {\n    char t[160], b[600], nm[40];'),
    ('static int tip_lrow(Obj r, int i) {\n    char t[80], b[200], nm[40];',
     'static int tip_lrow(Obj r, int i) {\n    char t[160], b[600], nm[40];'),
    ('char t[80], b[300], nm[40]; int p = prow_pl[i]', 'char t[160], b[900], nm[40]; int p = prow_pl[i]'),
    ('{ char m_[120]; snprintf(m_, sizeof m_,', '{ char m_[300]; snprintf(m_, sizeof m_,'),
]
