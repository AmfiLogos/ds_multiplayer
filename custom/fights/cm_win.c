/* cm_win.c - see cm_win.h. Same look as the loader's Mods window (loader/ui/dsl_win.c): dim backdrop, body 0x222025,
 * band 0x2D2A31 with a copper line, title in Cinzel, X top right. No game address: Unity and game calls by name. */
#include "cm_win.h"
#include "../fe/fe_json.h"
#include "cm_home.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *Obj;
typedef struct { float x, y; } V2;
typedef struct { float r, g, b, a; } Col;
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define CALL0(mi, R) ((R (*)(const void *))(*(void **)(mi)))

static const DSLHost *H; static DSLPlugin *S; static const DSLUi *U;
const DSLHost *cmw_host(void) { return H; }
const DSLUi *cmw_ui(void) { return U; }
void cmw_log(const char *fmt, ...) {
    char b[1200]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (H && H->log) H->log(S, "%s", b);
}

/* ---------------------------------------------------------------- Unity / il2cpp by name */
static void *(*x_get_methods)(void *, void **); static const char *(*x_mname)(void *); static uint32_t (*x_pcount)(void *);
static void *(*x_param)(void *, uint32_t); static char *(*x_tname)(void *); static void (*x_free)(void *);
static void *(*x_class_type)(void *); static void *(*x_type_obj)(void *); static Obj (*x_str_new)(const char *);
static Obj (*x_obj_new)(void *); static uint16_t *(*x_str_chars)(Obj); static int32_t (*x_str_len)(Obj);
static void *m_amin, *m_amax, *m_pivot, *m_pos, *m_size, *m_key, *m_cur_es, *m_sel_go, *m_go_get, *m_in_focused,
            *m_img_sprite, *m_gr_color, *m_go_tr, *m_shot, *k_image, *k_input;
static void *k_lcr, *m_lcr_ctor, *m_eq_add, *m_save_refresh, *m_find_any, *k_mainflow, *m_pdp, *m_save_dir, *m_try_refresh;
static int32_t off_disp_eq = -1, off_mf_loader = -1;
static void *k_tmv, *k_tmp, *m_refresh_state; static void *(*x_obj_class)(Obj); static int32_t off_mf_presenters = -1, off_vpc_dict = -1;

static void *mth(const char *ns, const char *k, const char *m, int argc) {
    void *c = H->find_class(ns, k), *mi = c ? H->find_method(c, m, argc) : NULL;
    if (!mi) cmw_log("cm_win: %s.%s.%s/%d not found", ns, k, m, argc);
    return mi;
}
static void *mth_sig(const char *ns, const char *k, const char *m, int argc, const char *sig) {
    void *c = H->find_class(ns, k), *it = NULL, *mi;
    while (c && x_get_methods && (mi = x_get_methods(c, &it))) {
        if (strcmp(x_mname(mi), m) || (int)x_pcount(mi) != argc) continue;
        char got[256] = ""; size_t o = 0;
        for (int i = 0; i < argc && o < sizeof got; i++) {
            char *t = x_tname(x_param(mi, (uint32_t)i));
            o += (size_t)snprintf(got + o, sizeof got - o, "%s%s", i ? "," : "", t ? t : "?");
            if (t && x_free) x_free(t);
        }
        if (!strcmp(got, sig)) return mi;
    }
    cmw_log("cm_win: %s.%s.%s(%s) not found", ns, k, m, sig);
    return NULL;
}
#define IL(var, name) (*(void **)&var = H->il2cpp_fn(name))
static void unity_init(void) {
    IL(x_get_methods, "il2cpp_class_get_methods"); IL(x_mname, "il2cpp_method_get_name"); IL(x_pcount, "il2cpp_method_get_param_count");
    IL(x_param, "il2cpp_method_get_param"); IL(x_tname, "il2cpp_type_get_name"); IL(x_free, "il2cpp_free");
    IL(x_class_type, "il2cpp_class_get_type"); IL(x_type_obj, "il2cpp_type_get_object"); IL(x_str_new, "il2cpp_string_new");
    IL(x_obj_new, "il2cpp_object_new"); IL(x_str_chars, "il2cpp_string_chars"); IL(x_str_len, "il2cpp_string_length");
    m_amin = mth("UnityEngine", "RectTransform", "set_anchorMin", 1); m_amax = mth("UnityEngine", "RectTransform", "set_anchorMax", 1);
    m_pivot = mth("UnityEngine", "RectTransform", "set_pivot", 1); m_pos = mth("UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    m_size = mth("UnityEngine", "RectTransform", "set_sizeDelta", 1);
    m_key = mth("UnityEngine", "Input", "GetKeyDownInt", 1);              /* GetKeyDown(KeyCode) has a string twin: the Int one is unique */
    m_cur_es = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
    m_sel_go = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
    m_go_tr = mth("UnityEngine", "GameObject", "get_transform", 0);
    m_go_get = mth_sig("UnityEngine", "Component", "GetComponent", 1, "System.Type");
    m_in_focused = mth("TMPro", "TMP_InputField", "get_isFocused", 0);
    m_img_sprite = mth("UnityEngine.UI", "Image", "set_sprite", 1); m_gr_color = mth("UnityEngine.UI", "Graphic", "set_color", 1);
    m_shot = mth("UnityEngine", "ScreenCapture", "CaptureScreenshot", 1);
    k_image = H->find_class("UnityEngine.UI", "Image"); k_input = H->find_class("TMPro", "TMP_InputField");
    /* the game's own load: EventQueue.AddEvent(new LoadCampaignRequested(name)), as the Load list does */
    k_lcr = H->find_class("Refactor.Main.Event", "LoadCampaignRequested");
    m_lcr_ctor = k_lcr ? H->find_method(k_lcr, ".ctor", 1) : NULL;
    m_eq_add = mth("Refactor.Main", "EventQueue", "AddEvent", 1);
    m_save_refresh = mth("Refactor", "SaveLoadHelper", "RefreshSaveCache", 0);   /* the load preflight reads its header cache */
    m_save_dir = mth("Refactor", "SaveLoadHelper", "GetSaveDirectory", 0);
    m_pdp = mth("UnityEngine", "Application", "get_persistentDataPath", 0);
    m_find_any = mth_sig("UnityEngine", "Object", "FindAnyObjectByType", 1, "System.Type");
    k_mainflow = H->find_class("Refactor.Main", "MainFlow");
    off_mf_loader = k_mainflow ? H->field_offset(k_mainflow, "_campaignSaveLoader") : -1;
    m_try_refresh = mth("Refactor", "CampaignSaveLoader", "TryRefreshSave", 0);
    k_tmv = H->find_class("Refactor.UI", "TitleMenuView");
    k_tmp = H->find_class("Refactor.UI", "TitleMenuPresenter");
    m_refresh_state = mth("Refactor.UI", "TitleMenuPresenter", "RefreshSaveState", 1);
    void *kv = H->find_class("Refactor.View", "ViewPresenterContainer"); off_vpc_dict = kv ? H->field_offset(kv, "_viewPresenters") : -1;
    off_mf_presenters = k_mainflow ? H->field_offset(k_mainflow, "_viewPresenters") : -1;
    IL(x_obj_class, "il2cpp_object_get_class");
    void *kd = H->find_class("Refactor.Main.InputModule", "InputDispatcher");
    off_disp_eq = kd ? H->field_offset(kd, "_eventQueue") : -1;
    if (!k_lcr || !m_lcr_ctor || off_disp_eq < 0) cmw_log("cm_win: LoadCampaignRequested %p ctor %p InputDispatcher._eventQueue %d", k_lcr, m_lcr_ctor, off_disp_eq);
}
static Obj comp_of(Obj any_comp, void *k) {
    if (!any_comp || !k || !m_go_get || !x_class_type || !x_type_obj) return NULL;
    return CALL(m_go_get, Obj, Obj, Obj)(any_comp, x_type_obj(x_class_type(k)), m_go_get);
}
DSLObj cmw_flat(DSLObj panel, uint32_t c) {
    Obj im = comp_of(panel, k_image);
    if (im && m_img_sprite && m_gr_color) {
        CALL(m_img_sprite, void, Obj, Obj)(im, NULL, m_img_sprite);
        CALL(m_gr_color, void, Obj, Col)(im, (Col){ (c >> 24) / 255.f, (c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f }, m_gr_color);
    }
    return panel;
}
static void centre(DSLObj rt) {
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos) return;
    V2 h = { 0.5f, 0.5f }, z = { 0, 0 };
    CALL(m_amin, void, Obj, V2)(rt, h, m_amin); CALL(m_amax, void, Obj, V2)(rt, h, m_amax);
    CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
static void fill_parent(DSLObj rt) {
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos || !m_size) return;
    V2 z = { 0, 0 }, o = { 1, 1 }, h = { 0.5f, 0.5f };
    CALL(m_amin, void, Obj, V2)(rt, z, m_amin); CALL(m_amax, void, Obj, V2)(rt, o, m_amax); CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot);
    CALL(m_size, void, Obj, V2)(rt, z, m_size); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
static int esc_down(void) { return m_key && CALL(m_key, int8_t, int32_t)(27, m_key) != 0; }
static int typing(void) {
    Obj es = m_cur_es ? CALL0(m_cur_es, Obj)(m_cur_es) : NULL;
    Obj go = es && m_sel_go ? CALL(m_sel_go, Obj, Obj)(es, m_sel_go) : NULL;
    Obj tr = go && m_go_tr ? CALL(m_go_tr, Obj, Obj)(go, m_go_tr) : NULL;
    Obj f = tr ? comp_of(tr, k_input) : NULL;
    return f && m_in_focused && CALL(m_in_focused, int8_t, Obj)(f, m_in_focused);
}

/* ---------------------------------------------------------------- the event queue and the frame tick */
static Obj g_queue;                                                   /* MainFlow's EventQueue, seen by the tick */
int cmw_load_campaign(const char *name) {
    if (!g_queue || !k_lcr || !m_lcr_ctor || !m_eq_add || !x_obj_new || !x_str_new) {
        cmw_log("cm_win: cannot queue the load of %s (queue %p)", name, g_queue); return -1;
    }
    /* a save written by us: the game's save list must know it first (as after its own save): CampaignSaveLoader.
       TryRefreshSave = SaveLoadHelper.RefreshSaveCache (SaveHeaderCache.json) + the loader's in-memory set */
    Obj mfo = m_find_any && k_mainflow && x_class_type && x_type_obj ? CALL(m_find_any, Obj, Obj)(x_type_obj(x_class_type(k_mainflow)), m_find_any) : NULL;
    Obj ldr = mfo && off_mf_loader >= 0 ? *(Obj *)((char *)mfo + off_mf_loader) : NULL;
    if (ldr && m_try_refresh) CALL(m_try_refresh, void, Obj)(ldr, m_try_refresh);
    else if (m_save_refresh) { CALL0(m_save_refresh, void)(m_save_refresh); cmw_log("cm_win: no CampaignSaveLoader (MainFlow %p): header cache refreshed only", mfo); }
    Obj ev = x_obj_new(k_lcr); if (!ev) return -1;
    CALL(m_lcr_ctor, void, Obj, Obj)(ev, x_str_new(name), m_lcr_ctor);
    CALL(m_eq_add, void, Obj, Obj)(g_queue, ev, m_eq_add);
    cmw_log("cm_win: LoadCampaignRequested %s queued", name);
    return 0;
}
static int utf8_of(Obj str, char *out, size_t n) {
    if (!str || !x_str_chars || !x_str_len || !n) return -1;
    const uint16_t *c = x_str_chars(str); int32_t l = x_str_len(str); size_t k = 0;
    for (int32_t i = 0; i < l && k + 4 < n; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < l) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
        else { out[k++] = (char)(0xF0 | u >> 18); out[k++] = (char)(0x80 | (u >> 12 & 63)); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
    }
    out[k] = 0;
    return 0;
}
/* the game's own folders (main thread): Application.persistentDataPath and SaveLoadHelper.GetSaveDirectory() */
static char g_data[1024], g_saves[1024];
static void dirs_read(void) {
    if (!g_data[0] && m_pdp) utf8_of(CALL0(m_pdp, Obj)(m_pdp), g_data, sizeof g_data);
    if (!g_saves[0] && m_save_dir) utf8_of(CALL0(m_save_dir, Obj)(m_save_dir), g_saves, sizeof g_saves);
}
int cmw_title_shown(void) {
    return k_tmv && m_find_any && CALL(m_find_any, Obj, Obj)(x_type_obj(x_class_type(k_tmv)), m_find_any) != NULL;
}
/* after we changed the Saves folder ourselves: the save list (CampaignSaveLoader.TryRefreshSave) and the title's Continue
   (TitleMenuPresenter.RefreshSaveState, what the title runs when it opens; it does not read its argument) */
int cmw_saves_changed(void) {
    Obj mfo = m_find_any && k_mainflow ? CALL(m_find_any, Obj, Obj)(x_type_obj(x_class_type(k_mainflow)), m_find_any) : NULL;
    Obj ldr = mfo && off_mf_loader >= 0 ? *(Obj *)((char *)mfo + off_mf_loader) : NULL;
    if (ldr && m_try_refresh) CALL(m_try_refresh, void, Obj)(ldr, m_try_refresh);
    else if (m_save_refresh) CALL0(m_save_refresh, void)(m_save_refresh);
    /* the title presenter: a value of ViewPresenterContainer._viewPresenters (Dictionary<Type, IViewPresenter>), read
       directly (its GetPresenter throws on a key it does not hold). Entries: +0x18 array (data at +0x20), count +0x20,
       entry {int hash, int next, key, value} = 0x18 bytes; hash < 0 = free */
    Obj vpc = mfo && off_mf_presenters >= 0 ? *(Obj *)((char *)mfo + off_mf_presenters) : NULL;
    Obj dict = vpc && off_vpc_dict >= 0 ? *(Obj *)((char *)vpc + off_vpc_dict) : NULL;
    Obj ent = dict ? *(Obj *)((char *)dict + 0x18) : NULL, pr = NULL;
    int32_t cnt = dict ? *(int32_t *)((char *)dict + 0x20) : 0;
    for (int32_t i = 0; ent && i < cnt && !pr; i++) {
        char *e = (char *)ent + 0x20 + (size_t)i * 0x18;
        Obj v = *(int32_t *)e >= 0 ? *(Obj *)(e + 0x10) : NULL;
        if (v && x_obj_class && x_obj_class(v) == k_tmp) pr = v;
    }
    if (pr && m_refresh_state) CALL(m_refresh_state, void, Obj, Obj)(pr, NULL, m_refresh_state);
    cmw_log("cm_win: save list refreshed%s", pr ? ", title Continue refreshed" : " (no title presenter)");
    return pr ? 0 : -1;
}
int cmw_game_data(char *out, size_t n) { if (!g_data[0]) return -1; snprintf(out, n, "%s", g_data); return 0; }
int cmw_saves_dir(char *out, size_t n) {
    if (g_saves[0]) { snprintf(out, n, "%s", g_saves); return 0; }
    if (!g_data[0]) return -1;
    snprintf(out, n, "%s/Saves", g_data); return 0;
}

#define NTICK 8
static struct { void (*fn)(void *); void *ud; } ticks[NTICK]; static int nticks;
int cmw_tick_add(void (*tick)(void *), void *ud) {
    if (nticks >= NTICK) return -1;
    ticks[nticks].fn = tick; ticks[nticks].ud = ud; nticks++; return 0;
}
static void win_tick(void); static void test_tick(void);
static void (*o_disp_update)(Obj self, const void *mi);
static void h_disp_update(Obj self, const void *mi) {
    o_disp_update(self, mi);
    if (!g_data[0]) dirs_read();
    if (self && off_disp_eq >= 0) g_queue = *(Obj *)((char *)self + off_disp_eq);
    win_tick();
    for (int i = 0; i < nticks; i++) ticks[i].fn(ticks[i].ud);
    test_tick();
}

/* ---------------------------------------------------------------- windows */
typedef struct { char id[24]; DSLObj back, win, body; void (*closed)(void *); void *ud; } Win;
#define NWIN 4
static Win wins[NWIN];
static Win *win_of(const char *id) { for (int i = 0; i < NWIN; i++) if (wins[i].win && !strcmp(wins[i].id, id)) return &wins[i]; return NULL; }
static Win *top_win(void) { Win *t = NULL; for (int i = 0; i < NWIN; i++) if (wins[i].win) t = &wins[i]; return t; }
static void win_destroy(Win *w) {
    if (!w || !w->win) return;
    void (*closed)(void *) = w->closed; void *ud = w->ud;
    U->destroy(w->win); U->destroy(w->back);
    char pre[32]; snprintf(pre, sizeof pre, "%s:", w->id); cmw_forget(pre);
    memset(w, 0, sizeof *w);
    if (closed) closed(ud);
}
static void x_click(void *ud, DSLObj w) { (void)w; Win *v = ud; win_destroy(v); }
DSLObj cmw_open(const char *id, const char *title, float w, float h, void (*closed)(void *), void *ud) {
    if (!U) return NULL;
    cmw_close(id);
    Win *v = NULL; for (int i = 0; i < NWIN; i++) if (!wins[i].win) { v = &wins[i]; break; }
    if (!v) return NULL;
    v->back = cmw_flat(U->panel(NULL, 0, 0, 100, 100, 0), 0x0000009E);   /* dims the title and takes its clicks */
    fill_parent(v->back);
    v->win = U->panel(NULL, 0, 0, w, h, 0);
    if (!v->win) { U->destroy(v->back); memset(v, 0, sizeof *v); return NULL; }
    centre(v->win);
    cmw_flat(U->panel(v->win, 4, 4, w - 8, h - 8, 0), 0x222025FF);
    cmw_flat(U->panel(v->win, 4, 4, w - 8, CMW_BAND - 4, 0), 0x2D2A31FF);
    cmw_flat(U->panel(v->win, 4, CMW_BAND, w - 8, 1, 0), 0xB6835AFF);
    snprintf(v->id, sizeof v->id, "%s", id);
    char nm[48];
    snprintf(nm, sizeof nm, "%s:title", id);
    cmw_named(U->text(v->win, 28, 4, w - 120, CMW_BAND - 4, title, 30, DSL_TXT_LEFT | DSL_TXT_TITLE, 0), CMW_TEXT, nm, NULL, NULL, title);
    snprintf(nm, sizeof nm, "%s:close", id);
    DSLObj x = cmw_named(U->button(v->win, w - 62, 12, 42, 42, "X", x_click, v), CMW_BUTTON, nm, x_click, v, "X");
    U->tooltip(x, NULL, "Close (Esc)");
    v->body = cmw_flat(U->panel(v->win, 0, CMW_BAND + 1, w, h - CMW_BAND - 1, 0), 0x00000000);
    v->closed = closed; v->ud = ud;
    return v->body;
}
void cmw_close(const char *id) { win_destroy(win_of(id)); }
int  cmw_is_open(const char *id) { return win_of(id) != NULL; }
static void win_tick(void) {
    Win *t = top_win();
    if (t && esc_down() && !typing()) win_destroy(t);
}

/* ---------------------------------------------------------------- test registry + command file */
typedef struct { char name[64]; DSLObj w; dsl_ui_cb cb; void *ud; int kind; char text[200]; } Reg;
#define NREG 512
static Reg reg[NREG]; static int nreg;
DSLObj cmw_named(DSLObj w, int kind, const char *name, dsl_ui_cb cb, void *ud, const char *text) {
    if (!w || !name) return w;
    Reg *r = NULL;
    for (int i = 0; i < nreg; i++) if (!strcmp(reg[i].name, name)) { r = &reg[i]; break; }
    if (!r) { if (nreg >= NREG) return w; r = &reg[nreg++]; }
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name); r->w = w; r->cb = cb; r->ud = ud; r->kind = kind;
    snprintf(r->text, sizeof r->text, "%s", text ? text : "");
    return w;
}
void cmw_set_text(DSLObj w, const char *s) {
    if (!w) return;
    U->set_text(w, s);
    for (int i = 0; i < nreg; i++) if (reg[i].w == w) snprintf(reg[i].text, sizeof reg[i].text, "%s", s);
}
void cmw_forget(const char *prefix) {
    size_t l = strlen(prefix); int o = 0;
    for (int i = 0; i < nreg; i++) if (strncmp(reg[i].name, prefix, l)) reg[o++] = reg[i];
    nreg = o;
}
#define NOPEN 4
static struct { char win[24]; void (*open)(void); } openers[NOPEN]; static int nopeners;
void cmw_test_opener(const char *win, void (*open)(void)) {
    if (nopeners < NOPEN) { snprintf(openers[nopeners].win, sizeof openers[0].win, "%s", win); openers[nopeners++].open = open; }
}
static void (*state_fns[4])(char *, size_t); static int nstate;
void cmw_test_state(void (*state)(char *, size_t)) { if (nstate < 4) state_fns[nstate++] = state; }

static char test_dir[1024]; static int test_seq;
static void jesc(char *o, size_t n, const char *s) {
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[k++] = '\\'; o[k++] = (char)c; }
        else if (c < 0x20) k += (size_t)snprintf(o + k, n - k, "\\u%04x", c);
        else o[k++] = (char)c;
    }
    o[k] = 0;
}
static char *jstr_of(const jv *o, const char *key) { jv *v = jget(o, key); return v ? jstrdup_raw(v) : NULL; }
static Reg *reg_find(const char *name) { for (int i = 0; i < nreg; i++) if (!strcmp(reg[i].name, name)) return &reg[i]; return NULL; }
static void test_answer(const char *body) {
    char p[1100], t[1100];
    snprintf(p, sizeof p, "%s/result.json", test_dir); snprintf(t, sizeof t, "%s/result.json.tmp", test_dir);
    FILE *f = fopen(t, "wb"); if (!f) return;
    fprintf(f, "{\"seq\":%d,%s}\n", test_seq, body); fclose(f); rename(t, p);
}
/* one test command (JSON) -> answer members (no braces). Main thread. */
static void test_exec(const char *buf, size_t n, char *out, size_t on) {
    char err[200]; jv *c = jparse(buf, n, err, sizeof err);
    if (!c) { snprintf(out, on, "\"ok\":false,\"error\":\"bad json\""); return; }
    char *cmd = jstr_of(c, "cmd"), *name = jstr_of(c, "name"), *text = jstr_of(c, "text"), *win = jstr_of(c, "win");
    snprintf(out, on, "\"ok\":true");
    if (!cmd) snprintf(out, on, "\"ok\":false,\"error\":\"no cmd\"");
    else if (!strcmp(cmd, "open")) {
        int ok = 0;
        for (int i = 0; i < nopeners; i++) if (win && !strcmp(openers[i].win, win)) { openers[i].open(); ok = 1; }
        if (!ok) snprintf(out, on, "\"ok\":false,\"error\":\"no window %s\"", win ? win : "");
    } else if (!strcmp(cmd, "close")) {
        if (win) cmw_close(win); else { Win *t = top_win(); if (t) win_destroy(t); }
    } else if (!strcmp(cmd, "click") || !strcmp(cmd, "type") || !strcmp(cmd, "set")) {
        Reg *r = name ? reg_find(name) : NULL;
        if (!r) snprintf(out, on, "\"ok\":false,\"error\":\"no control %s\"", name ? name : "");
        else {
            DSLObj w = r->w; dsl_ui_cb cb = r->cb; void *ud = r->ud;
            if (r->kind == CMW_TOGGLE) U->toggle_set(w, !U->toggle_get(w));
            if (r->kind == CMW_FIELD && text) U->field_set(w, text);
            if (r->kind == CMW_VALUE) { jv *ix = jget(c, "index"); if (ix) U->value_set(w, (int)jnumv(ix)); }
            if (cb) cb(ud, w);
        }
    } else if (!strcmp(cmd, "shot")) {
        char *path = jstr_of(c, "path");
        if (path && m_shot && x_str_new) CALL(m_shot, void, Obj)(x_str_new(path), m_shot);
        else snprintf(out, on, "\"ok\":false,\"error\":\"no path or no ScreenCapture\"");
        free(path);
    } else if (!strcmp(cmd, "state")) {
        size_t k = (size_t)snprintf(out, on, "\"ok\":true,\"windows\":[");
        int first = 1;
        for (int i = 0; i < NWIN; i++) if (wins[i].win) { k += (size_t)snprintf(out + k, on - k, "%s\"%s\"", first ? "" : ",", wins[i].id); first = 0; }
        k += (size_t)snprintf(out + k, on - k, "],\"controls\":{");
        for (int i = 0; i < nreg && k + 600 < on; i++) {
            char e1[160], e2[420]; jesc(e1, sizeof e1, reg[i].name); jesc(e2, sizeof e2, reg[i].text);
            int v = reg[i].kind == CMW_TOGGLE ? U->toggle_get(reg[i].w) : reg[i].kind == CMW_VALUE ? U->value_get(reg[i].w) : -1;
            k += (size_t)snprintf(out + k, on - k, "%s\"%s\":{\"k\":%d,\"t\":\"%s\",\"v\":%d}", i ? "," : "", e1, reg[i].kind, e2, v);
        }
        k += (size_t)snprintf(out + k, on - k, "}");
        for (int i = 0; i < nstate && k + 2000 < on; i++) {
            char s[8000] = ""; state_fns[i](s, sizeof s);
            if (s[0]) k += (size_t)snprintf(out + k, on - k, ",%s", s);
        }
    } else snprintf(out, on, "\"ok\":false,\"error\":\"unknown cmd\"");
    free(cmd); free(name); free(text); free(win); jfree(c);
}
static void test_tick(void) {
    if (!test_dir[0]) return;
    char p[1100]; snprintf(p, sizeof p, "%s/cmd.json", test_dir);
    FILE *f = fopen(p, "rb"); if (!f) return;
    char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char done[1100]; snprintf(done, sizeof done, "%s/cmd.done", test_dir); rename(p, done);
    char sqs[200]; jv *c = jparse(buf, n, sqs, sizeof sqs); jv *sq = c ? jget(c, "seq") : NULL;
    test_seq = sq ? (int)jnumv(sq) : test_seq + 1; jfree(c);
    static char out[60000]; test_exec(buf, n, out, sizeof out);
    test_answer(out);
}
/* lane units' test driver (units-driver "call {fn:cmf_test, s:<json>}"; targets "fights:<name>" / "gallery:<name>") */
__attribute__((visibility("default"))) int cmf_test(const char *s, char *out, size_t cap) {
    if (!s || !out || !cap) return -1;
    char *body = malloc(60000); if (!body) return -1;
    test_exec(s, strlen(s), body, 60000);
    snprintf(out, cap, "{%s}", body); free(body);
    return 0;
}
static DSLObj find_in(const char *pre, const char *name) { char full[96]; snprintf(full, sizeof full, "%s:%s", pre, name); Reg *r = reg_find(full); return r ? r->w : NULL; }
static DSLObj find_fights(const char *name) { return find_in("fights", name); }
static DSLObj find_gallery(const char *name) { return find_in("gallery", name); }
/* test: the save the title's Continue button would load (TitleMenuView._continueSaveName) */
static void continue_name(char *out, size_t n) {
    static int32_t off = -2; out[0] = 0;
    if (off == -2) off = k_tmv ? H->field_offset(k_tmv, "_continueSaveName") : -1;
    if (off < 0 || !m_find_any) return;
    Obj v = CALL(m_find_any, Obj, Obj)(x_type_obj(x_class_type(k_tmv)), m_find_any);
    if (v) utf8_of(*(Obj *)((char *)v + off), out, n);
}
static size_t short_state(char *out, size_t cap) {
    size_t k = (size_t)snprintf(out, cap, "{\"windows\":\"");
    for (int i = 0; i < NWIN && k < cap; i++) if (wins[i].win) k += (size_t)snprintf(out + k, cap - k, "%s ", wins[i].id);
    if (k < cap) k += (size_t)snprintf(out + k, cap - k, "\"");
    for (int i = 0; i < nstate && k + 100 < cap; i++) { char s[4000] = ""; state_fns[i](s, sizeof s); if (s[0]) k += (size_t)snprintf(out + k, cap - k, ",%s", s); }
    { char cn[300], ce[400]; continue_name(cn, sizeof cn); jesc(ce, sizeof ce, cn); if (k < cap) k += (size_t)snprintf(out + k, cap - k, ",\"continue\":\"%s\"", ce); }
    if (k < cap) k += (size_t)snprintf(out + k, cap - k, "}");
    return k < cap ? k : cap - 1;
}

/* ---------------------------------------------------------------- init */
int cmw_init(const DSLHost *host, DSLPlugin *self) {
    if (H) return U ? 0 : -1;
    H = host; S = self; U = dsl_ui(host);
    if (!U) { cmw_log("cm_win: the loader has no widget kit (API 3 ui): Test Fights and Gallery are off"); return -1; }
    unity_init();                                                     /* the folders are read in the first frame (dirs_read) */
    int r = host->hook_method(self, "Refactor.Main.InputModule", "InputDispatcher", "Update", 0, (void *)h_disp_update, (void **)&o_disp_update);
    if (r) { cmw_log("cm_win: frame hook InputDispatcher.Update refused (%d)", r); U = NULL; return -1; }
    cm_test_register("fights", find_fights, short_state); cm_test_register("gallery", find_gallery, NULL);
    const char *t = getenv("CMF_TEST");
    if (t && *t) { snprintf(test_dir, sizeof test_dir, "%s", t); cmw_log("cm_win: test commands from %s", test_dir); }
    return 0;
}
