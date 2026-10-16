// cm_il.c - il2cpp by name for Custom Modes (interface in cm_il.h). Names checked in the 25846389 dump.
#include "cm_il.h"
#include <stdio.h>
#include <string.h>

static const DSLHost *H; static DSLPlugin *ME;
static struct {
    void *(*class_get_type)(void *); Obj (*type_get_object)(void *);
    uintptr_t (*gc_new)(Obj, bool); Obj (*gc_get)(uintptr_t); void (*gc_free)(uintptr_t);   /* Unity 6: pointer-sized */
    const void *(*class_get_methods)(void *, void **); const char *(*method_get_name)(const void *);
    uint32_t (*method_param_count)(const void *); void *(*method_param)(const void *, uint32_t);
    char *(*type_get_name)(void *); bool (*method_is_generic)(const void *); void (*il_free)(void *);
    Obj (*string_new)(const char *);
} I;
static int ok = -1;

void *cm_il_class(const char *ns, const char *cls) { return H && H->find_class ? H->find_class(ns, cls) : NULL; }
int32_t cm_il_field(void *k, const char *name) { return k && H->field_offset ? H->field_offset(k, name) : -1; }
static cm_m msig(void *k, const char *name, int argc, const char *sig) {
    cm_m r = { 0 }; if (!k) return r;
    for (void *kk = k; kk && !r.fn; kk = NULL) {
        void *it = NULL; const void *mi;
        while ((mi = I.class_get_methods(kk, &it))) {
            if (strcmp(I.method_get_name(mi), name) || (int)I.method_param_count(mi) != argc) continue;
            if (I.method_is_generic && I.method_is_generic(mi)) continue;
            if (sig) {
                char buf[256] = ""; size_t o = 0;
                for (int i = 0; i < argc; i++) {
                    char *tn = I.type_get_name(I.method_param(mi, (uint32_t)i));
                    o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
                    if (tn && I.il_free) I.il_free(tn);
                }
                if (strcmp(buf, sig)) continue;
            }
            r.mi = mi; r.fn = H->method_address((void *)mi); break;
        }
    }
    return r;
}
cm_m cm_il_method(const char *ns, const char *cls, const char *name, int argc, const char *sig) {
    cm_m r = { 0 };
    if (ok != 1) return r;
    r = msig(cm_il_class(ns, cls), name, argc, sig);
    if (!r.fn) H->log(ME, "il2cpp: %s.%s.%s(%d) not found", ns, cls, name, argc);
    return r;
}
Obj cm_il_type_obj(void *k) { return k ? I.type_get_object(I.class_get_type(k)) : NULL; }
Obj cm_il_str(const char *s) { return ok == 1 && s ? I.string_new(s) : NULL; }
void cm_il_utf8(Obj s, char *out, size_t cap) {                        /* System.String: length +0x10, UTF-16 +0x14 */
    if (!cap) return; out[0] = 0; if (!s) return;
    int32_t len = CM_AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14); size_t o = 0;
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        char b[4]; int n;
        if (u < 0x80) { b[0] = (char)u; n = 1; }
        else if (u < 0x800) { b[0] = (char)(0xC0 | u >> 6); b[1] = (char)(0x80 | (u & 63)); n = 2; }
        else if (u < 0x10000) { b[0] = (char)(0xE0 | u >> 12); b[1] = (char)(0x80 | (u >> 6 & 63)); b[2] = (char)(0x80 | (u & 63)); n = 3; }
        else { b[0] = (char)(0xF0 | u >> 18); b[1] = (char)(0x80 | (u >> 12 & 63)); b[2] = (char)(0x80 | (u >> 6 & 63)); b[3] = (char)(0x80 | (u & 63)); n = 4; }
        if (o + (size_t)n >= cap) break;
        memcpy(out + o, b, (size_t)n); o += (size_t)n;
    }
    out[o] = 0;
}
uintptr_t cm_il_hnew(Obj o) { return o ? I.gc_new(o, false) : 0; }
Obj cm_il_hget(uintptr_t h) { return h ? I.gc_get(h) : NULL; }
void cm_il_hfree(uintptr_t h) { if (h) I.gc_free(h); }
void cm_il_hset(uintptr_t *h, Obj o) { if (*h) I.gc_free(*h); *h = o ? I.gc_new(o, false) : 0; }

/* ------------------------------------------------------------------ Unity */
static cm_m m_getcomp_child, m_alive, m_find, m_go, m_tr, m_getcomp, m_setactive, m_parent, m_count, m_child, m_sib_get, m_sib_set, m_destroy,
            m_inst, m_rect, m_raycast, m_sprite, m_color, m_aspect, m_overflow, m_wrap, m_prefw, m_trunc, m_gettext,
            m_rl_item, m_rl_skill, m_rl_tree, m_rl_aff, m_rl_ready, m_portrait, m_spr_name;
static void *k_graphic, *k_image, *k_tmp, *k_rl, *k_uvm; static int32_t f_uvm_db = -1;

int cm_il_init(const DSLHost *host, DSLPlugin *self) {
    if (ok >= 0) return ok;
    H = host; ME = self;
    if (!H || !H->il2cpp_ready || !H->il2cpp_ready() || !H->il2cpp_fn || !H->find_class || !H->method_address || !H->field_offset) return 0;
#define X(var, name) *(void **)&I.var = H->il2cpp_fn(name)
    X(class_get_type, "il2cpp_class_get_type"); X(type_get_object, "il2cpp_type_get_object");
    X(gc_new, "il2cpp_gchandle_new"); X(gc_get, "il2cpp_gchandle_get_target"); X(gc_free, "il2cpp_gchandle_free");
    X(class_get_methods, "il2cpp_class_get_methods"); X(method_get_name, "il2cpp_method_get_name");
    X(method_param_count, "il2cpp_method_get_param_count"); X(method_param, "il2cpp_method_get_param");
    X(type_get_name, "il2cpp_type_get_name"); X(method_is_generic, "il2cpp_method_is_generic"); X(il_free, "il2cpp_free");
    X(string_new, "il2cpp_string_new");
#undef X
    ok = 0;
    if (!I.class_get_type || !I.type_get_object || !I.gc_new || !I.gc_get || !I.gc_free || !I.class_get_methods || !I.method_get_name
        || !I.method_param_count || !I.method_param || !I.type_get_name || !I.string_new) { H->log(ME, "il2cpp: exports missing"); return 0; }
    ok = 1;
    const char *UE = "UnityEngine", *UI = "UnityEngine.UI", *TM = "TMPro";
    m_alive = cm_il_method(UE, "Object", "op_Implicit", 1, NULL);
    m_find = cm_il_method(UE, "Object", "FindObjectOfType", 2, "System.Type,System.Boolean");
    m_destroy = cm_il_method(UE, "Object", "Destroy", 1, NULL);
    m_inst = cm_il_method(UE, "Object", "Instantiate", 2, "UnityEngine.Object,UnityEngine.Transform");
    m_go = cm_il_method(UE, "Component", "get_gameObject", 0, NULL); m_tr = cm_il_method(UE, "Component", "get_transform", 0, NULL);
    m_getcomp = cm_il_method(UE, "Component", "GetComponent", 1, "System.Type");
    m_getcomp_child = cm_il_method(UE, "Component", "GetComponentInChildren", 2, "System.Type,System.Boolean");
    m_setactive = cm_il_method(UE, "GameObject", "SetActive", 1, NULL);
    m_parent = cm_il_method(UE, "Transform", "get_parent", 0, NULL); m_count = cm_il_method(UE, "Transform", "get_childCount", 0, NULL);
    m_child = cm_il_method(UE, "Transform", "GetChild", 1, NULL); m_sib_get = cm_il_method(UE, "Transform", "GetSiblingIndex", 0, NULL);
    m_sib_set = cm_il_method(UE, "Transform", "SetSiblingIndex", 1, NULL);
    m_rect = cm_il_method(UE, "RectTransform", "get_rect", 0, NULL);
    m_spr_name = cm_il_method(UE, "Object", "get_name", 0, NULL);
    k_graphic = cm_il_class(UI, "Graphic"); k_image = cm_il_class(UI, "Image"); k_tmp = cm_il_class(TM, "TMP_Text");
    m_raycast = cm_il_method(UI, "Graphic", "set_raycastTarget", 1, NULL);
    m_color = cm_il_method(UI, "Graphic", "set_color", 1, NULL);
    m_sprite = cm_il_method(UI, "Image", "set_sprite", 1, NULL); m_aspect = cm_il_method(UI, "Image", "set_preserveAspect", 1, NULL);
    m_overflow = cm_il_method(TM, "TMP_Text", "set_overflowMode", 1, NULL);
    m_wrap = cm_il_method(TM, "TMP_Text", "set_textWrappingMode", 1, NULL);
    m_prefw = cm_il_method(TM, "TMP_Text", "get_preferredWidth", 0, NULL);
    m_trunc = cm_il_method(TM, "TMP_Text", "get_isTextTruncated", 0, NULL);
    m_gettext = cm_il_method("Refactor", "TextKeyExtensions", "GetText", 1, NULL);
    k_rl = cm_il_class("Refactor", "ResourceLoader");
    m_rl_item = cm_il_method("Refactor", "ResourceLoader", "LoadItemIcon", 1, NULL);
    m_rl_skill = cm_il_method("Refactor", "ResourceLoader", "LoadSkillIcon", 1, NULL);
    m_rl_tree = cm_il_method("Refactor", "ResourceLoader", "LoadSkillTreeIcon", 1, NULL);
    m_rl_aff = cm_il_method("Refactor", "ResourceLoader", "LoadAffecterIcon", 1, NULL);
    m_rl_ready = cm_il_method("Refactor", "ResourceLoader", "get_IsGameResourceLoaderInitialized", 0, NULL);
    k_uvm = cm_il_class("Refactor", "UnitVisualDataManager"); f_uvm_db = cm_il_field(k_uvm, "_database");
    m_portrait = cm_il_method("Util", "UnitVisualDatabase", "GetPortraitSprite", 2, NULL);
    cm_m *need[] = { &m_alive, &m_find, &m_destroy, &m_inst, &m_go, &m_tr, &m_getcomp, &m_setactive, &m_parent, &m_count, &m_child,
                     &m_sib_get, &m_sib_set, &m_rect };
    for (size_t i = 0; i < sizeof need / sizeof *need; i++) if (!need[i]->fn) { ok = 0; H->log(ME, "il2cpp: a Unity method is missing (%zu)", i); }
    return ok;
}

bool cm_alive(Obj o) { return o && m_alive.fn && CM_CALL(m_alive, bool, Obj, const void *)(o, m_alive.mi); }
Obj cm_find(void *k, bool inactive) {
    if (!k || !m_find.fn) return NULL;
    Obj o = CM_CALL(m_find, Obj, Obj, bool, const void *)(cm_il_type_obj(k), inactive, m_find.mi);
    return cm_alive(o) ? o : NULL;
}
Obj cm_go(Obj c) { return cm_alive(c) ? CM_CALL(m_go, Obj, Obj, const void *)(c, m_go.mi) : NULL; }
Obj cm_tr(Obj c) { return cm_alive(c) ? CM_CALL(m_tr, Obj, Obj, const void *)(c, m_tr.mi) : NULL; }
Obj cm_comp(Obj c, void *k) {
    if (!cm_alive(c) || !k) return NULL;
    Obj r = CM_CALL(m_getcomp, Obj, Obj, Obj, const void *)(c, cm_il_type_obj(k), m_getcomp.mi);
    return cm_alive(r) ? r : NULL;
}
void cm_set_active(Obj c, bool on) {
    if (!cm_alive(c)) return;
    void *kgo = cm_il_class("UnityEngine", "GameObject");
    Obj g = c;
    { static void *(*obj_class)(Obj); if (!obj_class) *(void **)&obj_class = H->il2cpp_fn("il2cpp_object_get_class");
      if (obj_class && obj_class(c) != kgo) g = cm_go(c); }
    if (g) CM_CALL(m_setactive, void, Obj, bool, const void *)(g, on, m_setactive.mi);
}
Obj cm_parent(Obj t) { return cm_alive(t) ? CM_CALL(m_parent, Obj, Obj, const void *)(t, m_parent.mi) : NULL; }
int cm_child_count(Obj t) { return cm_alive(t) ? CM_CALL(m_count, int, Obj, const void *)(t, m_count.mi) : 0; }
Obj cm_child(Obj t, int i) { return cm_alive(t) ? CM_CALL(m_child, Obj, Obj, int, const void *)(t, i, m_child.mi) : NULL; }
int cm_sibling(Obj t) { return cm_alive(t) ? CM_CALL(m_sib_get, int, Obj, const void *)(t, m_sib_get.mi) : -1; }
void cm_set_sibling(Obj t, int i) { if (cm_alive(t)) CM_CALL(m_sib_set, void, Obj, int, const void *)(t, i, m_sib_set.mi); }
void cm_destroy(Obj o) { if (cm_alive(o)) CM_CALL(m_destroy, void, Obj, const void *)(o, m_destroy.mi); }
Obj cm_instantiate(Obj orig, Obj par) { return cm_alive(orig) ? CM_CALL(m_inst, Obj, Obj, Obj, const void *)(orig, par, m_inst.mi) : NULL; }
typedef struct { float x, y, w, h; } RectF;
int cm_rect_size(Obj rt, float *w, float *h) {
    if (!cm_alive(rt)) return -1;
    RectF r = CM_CALL(m_rect, RectF, Obj, const void *)(rt, m_rect.mi);
    if (w) *w = r.w; if (h) *h = r.h;
    return 0;
}

/* ------------------------------------------------------------------ UGUI / TMP */
typedef struct { float r, g, b, a; } Col;
void cm_raycast(Obj rt, bool on) {
    Obj g = cm_comp(rt, k_graphic);
    if (g && m_raycast.fn) CM_CALL(m_raycast, void, Obj, bool, const void *)(g, on, m_raycast.mi);
}
int cm_set_sprite(Obj rt, Obj sprite, uint32_t rgba) {
    Obj im = cm_comp(rt, k_image); if (!im || !m_sprite.fn) return -1;
    CM_CALL(m_sprite, void, Obj, Obj, const void *)(im, sprite, m_sprite.mi);
    if (m_color.fn) { Col c = { (rgba >> 24) / 255.f, (rgba >> 16 & 255) / 255.f, (rgba >> 8 & 255) / 255.f, (rgba & 255) / 255.f };
                      CM_CALL(m_color, void, Obj, Col, const void *)(im, c, m_color.mi); }
    if (m_aspect.fn) CM_CALL(m_aspect, void, Obj, bool, const void *)(im, true, m_aspect.mi);
    return 0;
}
/* the TMP text of a widget: its own (kit text) or the first one under it (kit button label) */
static Obj tmp_of(Obj rt) {
    Obj t = cm_comp(rt, k_tmp); if (t || !cm_alive(rt) || !m_getcomp_child.fn) return t;
    t = CM_CALL(m_getcomp_child, Obj, Obj, Obj, bool, const void *)(rt, cm_il_type_obj(k_tmp), true, m_getcomp_child.mi);
    return cm_alive(t) ? t : NULL;
}
Obj cm_text_of(Obj rt) { return tmp_of(rt); }
void cm_text_ellipsis(Obj rt) {
    Obj t = tmp_of(rt); if (!t) return;
    if (m_wrap.fn) CM_CALL(m_wrap, void, Obj, int, const void *)(t, 0, m_wrap.mi);          /* TextWrappingModes.NoWrap */
    if (m_overflow.fn) CM_CALL(m_overflow, void, Obj, int, const void *)(t, 1, m_overflow.mi);  /* TextOverflowModes.Ellipsis */
}
float cm_text_preferred_w(Obj rt) { Obj t = tmp_of(rt); return t && m_prefw.fn ? CM_CALL(m_prefw, float, Obj, const void *)(t, m_prefw.mi) : -1; }
int cm_text_truncated(Obj rt) { Obj t = tmp_of(rt); return t && m_trunc.fn ? CM_CALL(m_trunc, bool, Obj, const void *)(t, m_trunc.mi) : 0; }

/* ------------------------------------------------------------------ the game's texts and icons */
int cm_gettext(const char *key, char *out, size_t cap) {
    if (!cap) return -1; out[0] = 0;
    if (!key || !*key || !m_gettext.fn) return -1;
    Obj s = CM_CALL(m_gettext, Obj, Obj, const void *)(cm_il_str(key), m_gettext.mi);
    cm_il_utf8(s, out, cap);
    if (!out[0] || !strcmp(out, key)) { snprintf(out, cap, "%s", key); return -1; }
    return 0;
}
#define ICACHE 512
static struct { int kind; char key[96]; uintptr_t h; } ic[ICACHE]; static int nic;
static uintptr_t h_rl;
Obj cm_icon(int kind, const char *key) {
    if (ok != 1 || !key || !*key) return NULL;
    for (int i = 0; i < nic; i++) if (ic[i].kind == kind && !strcmp(ic[i].key, key)) { Obj o = cm_il_hget(ic[i].h); if (cm_alive(o)) return o; ic[i].h = 0; }
    Obj sp = NULL;
    if (kind == CM_ICON_PORTRAIT) {
        /* the portrait table (UnitVisualDatabase, a ScriptableObject): through the world's UnitVisualDataManager, else
           the loaded asset itself (title screen); then a loaded sprite of that name (as mpmenu.c portrait_sprite) */
        Obj uvm = cm_find(k_uvm, true), db = uvm && f_uvm_db >= 0 ? CM_AT(uvm, f_uvm_db, Obj) : NULL;
        static cm_m all; if (!all.fn) all = cm_il_method("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1, "System.Type");
        if (!cm_alive(db) && all.fn) {
            Obj arr = CM_CALL(all, Obj, Obj, const void *)(cm_il_type_obj(cm_il_class("Util", "UnitVisualDatabase")), all.mi);
            if (arr && CM_AT(arr, 0x18, uintptr_t) > 0) db = CM_AT(arr, 0x20, Obj);
        }
        if (cm_alive(db) && m_portrait.fn) sp = CM_CALL(m_portrait, Obj, Obj, Obj, bool, const void *)(db, cm_il_str(key), false, m_portrait.mi);
        static int64_t last_scan = -100000; static cm_m tnow;
        if (!tnow.fn) tnow = cm_il_method("UnityEngine", "Time", "get_realtimeSinceStartup", 0, NULL);
        float now = tnow.fn ? CM_CALL(tnow, float, const void *)(tnow.mi) : 0;
        if (!cm_alive(sp) && all.fn && now - last_scan > 20) {           /* loaded sprites by name, at most every 20 s */
            last_scan = (int64_t)now;
            Obj arr = CM_CALL(all, Obj, Obj, const void *)(cm_il_type_obj(cm_il_class("UnityEngine", "Sprite")), all.mi);
            size_t n = arr ? CM_AT(arr, 0x18, uintptr_t) : 0;
            for (size_t i = 0; i < n && !sp; i++) {
                Obj o = CM_AT((char *)arr + 0x20, i * 8, Obj); char nm[96];
                cm_il_utf8(CM_CALL(m_spr_name, Obj, Obj, const void *)(o, m_spr_name.mi), nm, sizeof nm);
                if (!strcmp(nm, key)) sp = o;
            }
        }
    } else {
        Obj rl = cm_il_hget(h_rl);
        if (!cm_alive(rl)) { rl = cm_find(k_rl, true); cm_il_hset(&h_rl, rl); }
        if (!rl) return NULL;
        if (m_rl_ready.fn && !CM_CALL(m_rl_ready, bool, Obj, const void *)(rl, m_rl_ready.mi)) return NULL;
        cm_m m = kind == CM_ICON_ITEM ? m_rl_item : kind == CM_ICON_SKILL ? m_rl_skill : kind == CM_ICON_TREE ? m_rl_tree : m_rl_aff;
        if (m.fn) sp = CM_CALL(m, Obj, Obj, Obj, const void *)(rl, cm_il_str(key), m.mi);
    }
    if (!cm_alive(sp)) return NULL;
    char nm[96] = ""; if (m_spr_name.fn) cm_il_utf8(CM_CALL(m_spr_name, Obj, Obj, const void *)(sp, m_spr_name.mi), nm, sizeof nm);
    if (nic < 40) H->log(ME, "icon %d %s -> sprite \"%s\"", kind, key, nm);
    if (!strncmp(nm, "DefaultIcon", 11) || !strncmp(nm, "Default", 7)) return NULL;   /* a missing key: the game's default */
    if (nic < ICACHE) { ic[nic].kind = kind; snprintf(ic[nic].key, sizeof ic[nic].key, "%s", key); ic[nic].h = cm_il_hnew(sp); nic++; }
    return sp;
}
