// Copy of ds_custommodes units/mu_look.c at 095d27a, for the multiplayer mod's Bring Units card window (no link to Units Laboratory).
// mu_look.c - a unit's avatar and in-game sprite, drawn by the game (mu_look.h).
// The sprite is the game's own: a hidden, inactive copy of the race's UnitVisual (from the unit prefab the game spawns
// settlers with), given the portrait's profile row and the worn items, then UnitVisual.PaintAll composes the parts into
// one texture exactly as for a unit on the map (draw order, offsets, hair hidden under a helmet or hood). Each look
// (profile + gear) keeps its own copy, so every card shows its own sprite; the oldest copies are dropped past LCACHE.
#include "mu_look.h"
#include "mu_ui.h"
#include "cm_il.h"
#include "pvp_wiki.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const DSLHost *H;
static void *(*object_new)(void *klass);
static void *(*class_parent)(void *klass);
static void *(*class_get_methods)(void *klass, void **iter);
static const char *(*method_name)(const void *mi);
static uint32_t (*method_pcount)(const void *mi);
static Obj (*invoke)(const void *mi, void *obj, void **params, Obj *exc);
static void (*format_exc)(Obj ex, char *msg, int size);

static int ready = -1;
static void *k_uv, *k_prof, *k_ptab, *k_prow, *k_itab, *k_irow, *k_dict;
static int32_t f_ptab_rows = -1, f_itab_rows = -1, f_prow_portrait = -1, f_prow_weight = -1, f_irow_key = -1, f_irow_sub = -1, f_uv_main = -1;
static cm_m m_all, m_name, m_rl_unit, m_children, m_prof_ctor, m_prof_vtype, m_uv_setprof, m_uv_refresh, m_uv_paint, m_dict_ctor,
            m_go_ctor, m_go_active, m_go_tr, m_dict_set;
static uintptr_t h_stage, h_rl;
static void logonce(const char *what) {
    static char seen[8][80]; static int n;
    for (int i = 0; i < n; i++) if (!strcmp(seen[i], what)) return;
    if (n < 8) snprintf(seen[n++], sizeof seen[0], "%s", what);
    mu_log("look: %s", what);
}
/* a method on the class or one of its parents (generic instances included: Dictionary<BodyPartsType, string>.set_Item) */
static cm_m up_method(void *k, const char *name, int argc) {
    cm_m r = { 0 };
    for (; k && !r.fn; k = class_parent(k)) {
        void *it = NULL; const void *mi;
        while ((mi = class_get_methods(k, &it))) if (!strcmp(method_name(mi), name) && (int)method_pcount(mi) == argc) { r.mi = mi; r.fn = *(void **)mi; break; }
    }
    return r;
}
static int init(void) {
    if (ready >= 0) return ready;
    H = mu_host(); ready = 0;
    if (!H || !H->il2cpp_fn) return 0;
    *(void **)&object_new = H->il2cpp_fn("il2cpp_object_new"); *(void **)&class_parent = H->il2cpp_fn("il2cpp_class_get_parent");
    *(void **)&class_get_methods = H->il2cpp_fn("il2cpp_class_get_methods"); *(void **)&method_name = H->il2cpp_fn("il2cpp_method_get_name");
    *(void **)&method_pcount = H->il2cpp_fn("il2cpp_method_get_param_count");
    *(void **)&invoke = H->il2cpp_fn("il2cpp_runtime_invoke"); *(void **)&format_exc = H->il2cpp_fn("il2cpp_format_exception");
    k_uv = cm_il_class("Refactor", "UnitVisual"); k_prof = cm_il_class("Refactor", "UnitProfileData");
    k_ptab = cm_il_class("Util.Sheet", "UnitProfileTable"); k_prow = cm_il_class("Util.Sheet", "UnitProfileTableData");
    k_itab = cm_il_class("Util.Sheet", "ItemTable"); k_irow = cm_il_class("Util.Sheet", "ItemTableData");
    k_dict = cm_il_class("Refactor", "BodyPartsStringDictionary");
    if (!object_new || !class_parent || !class_get_methods || !invoke || !k_uv || !k_prof || !k_ptab || !k_prow || !k_itab || !k_irow || !k_dict) { logonce("classes missing"); return 0; }
    f_ptab_rows = cm_il_field(k_ptab, "tableDatas"); f_itab_rows = cm_il_field(k_itab, "tableDatas");
    if (f_ptab_rows < 0) for (void *k = class_parent(k_ptab); k && f_ptab_rows < 0; k = class_parent(k)) f_ptab_rows = cm_il_field(k, "tableDatas");
    if (f_itab_rows < 0) for (void *k = class_parent(k_itab); k && f_itab_rows < 0; k = class_parent(k)) f_itab_rows = cm_il_field(k, "tableDatas");
    f_prow_portrait = cm_il_field(k_prow, "Portrait"); f_prow_weight = cm_il_field(k_prow, "AppearWeight");
    f_irow_key = cm_il_field(k_irow, "Key"); f_irow_sub = cm_il_field(k_irow, "ItemSubType");
    f_uv_main = cm_il_field(k_uv, "_generatedMainSprite");
    m_all = cm_il_method("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1, "System.Type");
    m_name = cm_il_method("UnityEngine", "Object", "get_name", 0, NULL);
    m_rl_unit = cm_il_method("Refactor", "ResourceLoader", "LoadUnitObject", 1, NULL);
    m_children = cm_il_method("UnityEngine", "Component", "GetComponentInChildren", 2, NULL);
    m_prof_ctor = cm_il_method("Refactor", "UnitProfileData", ".ctor", 3, NULL);
    m_prof_vtype = cm_il_method("Refactor", "UnitProfileData", "get_UnitVisualType", 0, NULL);
    m_uv_setprof = cm_il_method("Refactor", "UnitVisual", "SetProfileData", 1, NULL);
    m_uv_refresh = cm_il_method("Refactor", "UnitVisual", "RefreshAllEquipment", 1, NULL);
    m_uv_paint = cm_il_method("Refactor", "UnitVisual", "PaintAll", 0, NULL);
    m_dict_ctor = cm_il_method("Refactor", "BodyPartsStringDictionary", ".ctor", 0, NULL);
    m_dict_set = up_method(k_dict, "set_Item", 2);
    m_go_ctor = cm_il_method("UnityEngine", "GameObject", ".ctor", 1, "System.String");
    m_go_active = cm_il_method("UnityEngine", "GameObject", "SetActive", 1, NULL);
    m_go_tr = cm_il_method("UnityEngine", "GameObject", "get_transform", 0, NULL);
    if (f_ptab_rows < 0 || f_itab_rows < 0 || f_prow_portrait < 0 || f_irow_key < 0 || f_irow_sub < 0 || f_uv_main < 0 || !m_all.fn || !m_name.fn
        || !m_rl_unit.fn || !m_children.fn || !m_prof_ctor.fn || !m_prof_vtype.fn || !m_uv_setprof.fn || !m_uv_refresh.fn || !m_uv_paint.fn
        || !m_dict_ctor.fn || !m_dict_set.fn || !m_go_ctor.fn || !m_go_active.fn || !m_go_tr.fn) { logonce("methods or fields missing"); return 0; }
    return ready = 1;
}
/* a game method through il2cpp_runtime_invoke: a managed exception is caught and logged (never thrown through us);
   *ok = 0 then. Value-type arguments go by pointer; a value-type result comes boxed. */
static Obj inv(cm_m m, Obj self, void **args, const char *what, int *ok) {
    Obj ex = NULL; Obj r = invoke(m.mi, self, args, &ex);
    if (ex) {
        char msg[400] = "exception"; if (format_exc) format_exc(ex, msg, sizeof msg);
        char l[520]; snprintf(l, sizeof l, "%s: %s", what, msg); logonce(l);
        if (ok) *ok = 0; return NULL;
    }
    return r;
}
static Obj first_of(void *k) {
    Obj arr = CM_CALL(m_all, Obj, Obj, const void *)(cm_il_type_obj(k), m_all.mi);
    return arr && CM_AT(arr, 0x18, uintptr_t) ? CM_AT(arr, 0x20, Obj) : NULL;
}
/* List<T>: _items 0x10 (array: data at 0x20), _size 0x18 */
static size_t list_n(Obj l) { return l ? (size_t)CM_AT(l, 0x18, int32_t) : 0; }
static Obj list_at(Obj l, size_t i) { Obj a = CM_AT(l, 0x10, Obj); return a ? CM_AT((char *)a + 0x20, i * 8, Obj) : NULL; }
static int str_is(Obj s, const char *t) { char b[128]; cm_il_utf8(s, b, sizeof b); return !strcmp(b, t); }

/* the profile row of a portrait (a row the recruit pool rolls first), else the race's first face */
static Obj profile_row(const mu_look *u) {
    static uintptr_t h_tab; Obj tab = cm_il_hget(h_tab);
    if (!cm_alive(tab)) { tab = first_of(k_ptab); if (!tab) return NULL; cm_il_hset(&h_tab, tab); }
    Obj rows = CM_AT(tab, f_ptab_rows, Obj); size_t n = list_n(rows); Obj best = NULL;
    if (u->portrait[0]) for (size_t i = 0; i < n; i++) {
        Obj r = list_at(rows, i); if (!r || !str_is(CM_AT(r, f_prow_portrait, Obj), u->portrait)) continue;
        if (!best || (f_prow_weight >= 0 && CM_AT(r, f_prow_weight, int32_t) > 0)) best = r;
    }
    if (best || !u->race[0]) return best;
    const char *rk = strncmp(u->race, "AFFECTER_", 9) ? u->race : u->race + 9;      /* AFFECTER_Elf -> Elf..., LizardMan -> Lizard... */
    char pre[40]; snprintf(pre, sizeof pre, "%s", rk); if (!strcmp(pre, "LizardMan")) snprintf(pre, sizeof pre, "Lizard");
    for (size_t i = 0; i < n; i++) {
        Obj r = list_at(rows, i); char b[96]; if (!r) continue;
        cm_il_utf8(CM_AT(r, f_prow_portrait, Obj), b, sizeof b);
        if (!strncmp(b, pre, strlen(pre)) && (f_prow_weight < 0 || CM_AT(r, f_prow_weight, int32_t) > 0)) return r;
    }
    return NULL;
}
/* the item's body part (UnitVisualView.ConvertPartsType): weapons -> Equipment_Hand_Primary, shield / torch ->
   Equipment_Hand_Secondary, helmet -> Equipment_Head, hood -> Equipment_Hood, armour -> Equipment_Body; -1 = not drawn */
static int item_part(const char *key) {
    static uintptr_t h_tab; Obj tab = cm_il_hget(h_tab);
    if (!cm_alive(tab)) { tab = first_of(k_itab); if (!tab) return -1; cm_il_hset(&h_tab, tab); }
    Obj rows = CM_AT(tab, f_itab_rows, Obj); size_t n = list_n(rows);
    for (size_t i = 0; i < n; i++) {
        Obj r = list_at(rows, i); if (!r || !str_is(CM_AT(r, f_irow_key, Obj), key)) continue;
        int st = CM_AT(r, f_irow_sub, int32_t);
        if (st > 100 && st < 199) return 11;
        if (st == 304 || st == 401) return 0;
        if (st == 301) return 10;
        if (st == 302) return 15;
        if (st == 303) return 6;
        return -1;
    }
    return -1;
}

/* ------------------------------------------------------------------ the copies */
#define LCACHE 48
static struct { char sig[700]; uintptr_t h_uv, h_crop; unsigned used; } C[LCACHE];
static unsigned tick;
typedef struct { float x, y, z; } V3;
static Obj stage(void) {                       /* the holder, far outside any camera; each copy is switched off once painted */
    Obj g = cm_il_hget(h_stage);
    if (cm_alive(g)) return CM_CALL(m_go_tr, Obj, Obj, const void *)(g, m_go_tr.mi);
    g = object_new(cm_il_class("UnityEngine", "GameObject")); if (!g) return NULL;
    CM_CALL(m_go_ctor, void, Obj, Obj, const void *)(g, cm_il_str("mu_look_stage"), m_go_ctor.mi);
    cm_il_hset(&h_stage, g);
    Obj tr = CM_CALL(m_go_tr, Obj, Obj, const void *)(g, m_go_tr.mi);
    static cm_m pos; if (!pos.fn) pos = cm_il_method("UnityEngine", "Transform", "set_position", 1, NULL);
    if (pos.fn) CM_CALL(pos, void, Obj, V3, const void *)(tr, (V3){ -50000, -50000, 0 }, pos.mi);
    return tr;
}
static const char *const VTYPE[] = { "HumanStandardMale", "HumanStandardFemale", "HumanFatUnisex", "ElfStandardMale", "ElfStandardFemale",
                                     "ElfSlimUnisex", "LizardMan", "LycanMale", "LycanFemale" };
static Obj make_sprite(const mu_look *u) {
    if (init() != 1) return NULL;
    Obj row = profile_row(u);
    if (!row) { logonce("no profile row for the portrait"); return NULL; }
    char sig[700]; size_t o = (size_t)snprintf(sig, sizeof sig, "%p|%d", row, u->hide_head);
    for (int i = 0; i < u->ngear && i < MU_LOOK_GEAR; i++) o += (size_t)snprintf(sig + o, o < sizeof sig ? sizeof sig - o : 0, "|%s", u->gear[i]);
    int slot = -1, old = 0;
    for (int i = 0; i < LCACHE; i++) {
        if (C[i].h_uv && !strcmp(C[i].sig, sig)) {
            Obj uv = cm_il_hget(C[i].h_uv);
            Obj cr = cm_il_hget(C[i].h_crop);
            if (cm_alive(uv) && cm_alive(cr)) { C[i].used = ++tick; return cr; }
            slot = i; break;
        }
        if (!C[i].h_uv) { if (slot < 0) slot = i; }
        else if (C[i].used < C[old].used) old = i;
    }
    if (slot < 0) {                             /* the least recently drawn look goes */
        slot = old; Obj uv = cm_il_hget(C[slot].h_uv);
        if (cm_alive(uv)) cm_destroy(cm_go(uv));
        cm_il_hfree(C[slot].h_uv); cm_il_hfree(C[slot].h_crop); C[slot].h_uv = C[slot].h_crop = 0;
    }
    /* the profile (the game's own constructor from the table row: visual type, head, body, skin, hair, eyes) */
    int ok = 1;
    Obj prof = object_new(k_prof); if (!prof) return NULL;
    { void *a[3] = { row, cm_il_str(""), cm_il_str("") }; inv(m_prof_ctor, prof, a, "UnitProfileData", &ok); }
    Obj bv = ok ? inv(m_prof_vtype, prof, NULL, "UnitVisualType", &ok) : NULL;
    int vt = bv ? CM_AT(bv, 0x10, int32_t) : -1;
    if (vt < 0 || vt >= (int)(sizeof VTYPE / sizeof *VTYPE)) { logonce("unknown visual type"); return NULL; }
    /* the race's unit prefab, as the game loads it to spawn a settler */
    Obj rl = cm_il_hget(h_rl);
    if (!cm_alive(rl)) { rl = first_of(cm_il_class("Refactor", "ResourceLoader")); if (!rl) { logonce("no ResourceLoader"); return NULL; } cm_il_hset(&h_rl, rl); }
    char pk[80]; snprintf(pk, sizeof pk, "UNIT_PlayerUnit_%s", VTYPE[vt]);
    void *a1[1] = { cm_il_str(pk) };
    Obj prefab = inv(m_rl_unit, rl, a1, "LoadUnitObject", &ok);
    if (!cm_alive(prefab)) { logonce("unit prefab not loaded"); return NULL; }
    bool inact = true; void *a2[2] = { cm_il_type_obj(k_uv), &inact };
    Obj puv = inv(m_children, prefab, a2, "GetComponentInChildren", &ok);
    if (!cm_alive(puv)) { logonce("no UnitVisual in the prefab"); return NULL; }
    Obj st = stage(); if (!st) return NULL;
    Obj uv = cm_instantiate(puv, st);           /* the visual's object only (its Awake fills the part tables) */
    if (!cm_alive(uv)) { logonce("copy failed"); return NULL; }
    /* worn items -> parts, then the game paints */
    /* the game's own "nothing worn" (UnitVisualView.GetEmptyEquipInfo), then the items over it */
    static void *k_view; static cm_m m_empty; static uintptr_t h_view;
    if (!k_view) { k_view = cm_il_class("Refactor.View", "UnitVisualView"); if (!k_view) k_view = cm_il_class("Refactor", "UnitVisualView");
                   if (k_view) m_empty = cm_il_method(cm_il_class("Refactor.View", "UnitVisualView") ? "Refactor.View" : "Refactor", "UnitVisualView", "GetEmptyEquipInfo", 0, NULL); }
    Obj view = cm_il_hget(h_view);
    if (!view && k_view) { view = object_new(k_view); h_view = cm_il_hnew(view); }
    Obj eq = view && m_empty.fn ? inv(m_empty, view, NULL, "GetEmptyEquipInfo", &ok) : NULL;
    if (!eq) { logonce("no empty equipment info"); cm_destroy(cm_go(uv)); return NULL; }
    { int32_t pt = 13; void *a0[2] = { &pt, cm_il_str("") }; inv(m_dict_set, eq, a0, "set_Item", &ok); }   /* the carried item slot must exist */
    for (int i = 0; i < u->ngear && i < MU_LOOK_GEAR; i++) {
        int part = u->gear[i][0] ? item_part(u->gear[i]) : -1;
        if (part < 0 || (u->hide_head && (part == 10 || part == 15))) continue;
        int32_t pt = part; void *a3[2] = { &pt, cm_il_str(u->gear[i]) };
        inv(m_dict_set, eq, a3, "set_Item", &ok);
    }
    { void *a4[1] = { prof }; if (ok) inv(m_uv_setprof, uv, a4, "SetProfileData", &ok); }
    { void *a5[1] = { eq }; if (ok) inv(m_uv_refresh, uv, a5, "RefreshAllEquipment", &ok); }
    { static cm_m unequip; if (!unequip.fn) unequip = cm_il_method("Refactor", "UnitVisual", "Unequip", 1, NULL);   /* carrying nothing: */
      int32_t pt = 13; void *a6[1] = { &pt }; if (ok && unequip.fn) inv(unequip, uv, a6, "Unequip", &ok); }       /* else DefaultIcon is drawn */
    if (ok) inv(m_uv_paint, uv, NULL, "PaintAll", &ok);
    CM_CALL(m_go_active, void, Obj, bool, const void *)(cm_go(uv), false, m_go_active.mi);   /* painted: never drawn or run again */
    Obj sp = CM_AT(uv, f_uv_main, Obj);
    if (!cm_alive(sp)) { logonce("PaintAll made no sprite"); cm_destroy(cm_go(uv)); return NULL; }
    /* the game's canvas is 64 x 40 px, the character stands bottom centre: show the 30 x 30 square around it */
    static cm_m tex, create; if (!tex.fn) { tex = cm_il_method("UnityEngine", "Sprite", "get_texture", 0, NULL);
                                            create = cm_il_method("UnityEngine", "Sprite", "Create", 4, "UnityEngine.Texture2D,UnityEngine.Rect,UnityEngine.Vector2,System.Single"); }
    Obj t = tex.fn ? inv(tex, sp, NULL, "Sprite.texture", &ok) : NULL, cr = NULL;
    if (t && create.fn) {
        struct { float x, y, w, h; } r = { 17, 0, 30, 30 }; struct { float x, y; } pv = { 0.5f, 0.5f }; float ppu = 16;
        void *a7[4] = { t, &r, &pv, &ppu }; cr = inv(create, NULL, a7, "Sprite.Create", &ok);
    }
    if (!cm_alive(cr)) cr = sp;
    snprintf(C[slot].sig, sizeof C[slot].sig, "%s", sig); C[slot].h_uv = cm_il_hnew(uv); C[slot].h_crop = cm_il_hnew(cr); C[slot].used = ++tick;
    return cr;
}

/* ------------------------------------------------------------------ a random recruit */
/* a face the recruit pool rolls (UnitProfileTable row with AppearWeight > 0, by its weight) and a gear set a settler of
   that kind would wear: one weapon the player can own (the wiki's item list; each kind as likely), body armour and often a helmet or hood of
   the weapon's material when the game has one, a shield or torch with some one-handed weapons */
static unsigned rnd_state;
static unsigned rnd(void) {
    if (!rnd_state) rnd_state = (unsigned)time(NULL) * 2654435761u ^ (unsigned)clock() ^ 0x9E3779B9u;
    rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5;
    return rnd_state;
}
static int item_sub(const char *key) {                    /* ItemSubType of an item key, -1 = not in the game's table */
    static uintptr_t h_tab; Obj tab = cm_il_hget(h_tab);
    if (!cm_alive(tab)) { tab = first_of(k_itab); if (!tab) return -1; cm_il_hset(&h_tab, tab); }
    Obj rows = CM_AT(tab, f_itab_rows, Obj); size_t n = list_n(rows);
    for (size_t i = 0; i < n; i++) { Obj r = list_at(rows, i); if (r && str_is(CM_AT(r, f_irow_key, Obj), key)) return CM_AT(r, f_irow_sub, int32_t); }
    return -1;
}
static int ends(const char *s, const char *t) { size_t a = strlen(s), b = strlen(t); return a >= b && !strcmp(s + a - b, t); }
/* the weapon's kind: its last word (GreatSword is one; Mace and Club are both blunt) */
static const char *wkind(const char *key) {
    if (ends(key, "GreatSword")) return "GreatSword";
    if (ends(key, "Club")) return "Mace";
    const char *k = key + strlen(key);
    while (k > key && !(k[-1] >= 'A' && k[-1] <= 'Z')) k--;
    return k > key ? k - 1 : key;
}
/* the weapon's material: the word right before its kind (ITEM_CopperSword -> Copper, ITEM_FireBoneStaff -> Bone) */
static void material(const char *key, char *out, size_t n) {
    const char *k = strncmp(key, "ITEM_", 5) ? key : key + 5; size_t e = strlen(k), b;
    if (ends(k, "GreatSword")) e -= 10;                       /* the kind word (GreatSword is two) */
    else { while (e > 0 && !(k[e - 1] >= 'A' && k[e - 1] <= 'Z')) e--; if (e) e--; }
    b = e; while (b > 0 && !(k[b - 1] >= 'A' && k[b - 1] <= 'Z')) b--;
    if (b) b--;
    if (b >= 5 && !strncmp(k + b - 5, "Blood", 5) && !strncmp(k + b, "Wood", 4)) b -= 5;   /* BloodWood */
    snprintf(out, n, "%.*s", (int)(e - b), k + b);
}
int mu_look_random(mu_look *u) {
    if (!u) return -1;
    memset(u, 0, sizeof *u);
    if (init() != 1 || f_prow_weight < 0) return -1;
    static uintptr_t h_tab; Obj tab = cm_il_hget(h_tab);
    if (!cm_alive(tab)) { tab = first_of(k_ptab); if (!tab) { logonce("random: no UnitProfileTable"); return -1; } cm_il_hset(&h_tab, tab); }
    Obj rows = CM_AT(tab, f_ptab_rows, Obj); size_t n = list_n(rows); long long sum = 0;
    for (size_t i = 0; i < n; i++) { Obj r = list_at(rows, i); int w = r ? CM_AT(r, f_prow_weight, int32_t) : 0; if (w > 0) sum += w; }
    if (sum <= 0) { logonce("random: no recruit face"); return -1; }
    long long pick = (long long)(rnd() % (unsigned)sum);
    for (size_t i = 0; i < n; i++) {
        Obj r = list_at(rows, i); int w = r ? CM_AT(r, f_prow_weight, int32_t) : 0; if (w <= 0) continue;
        if (pick < w) { cm_il_utf8(CM_AT(r, f_prow_portrait, Obj), u->portrait, sizeof u->portrait); break; }
        pick -= w;
    }
    /* the player's items by kind (the wiki's list, kinds from the game's ItemTable) */
    enum { WEAPON, ARMOUR, HEAD, OFF, NK };
    static char keys[NK][48][48]; static int nk[NK] = { -1 };
    if (nk[0] < 0) {
        memset(nk, 0, sizeof nk);
        for (int i = 0; ; i++) {
            const char *k = wk_item_key(i); if (!k || !*k) break;
            int st = item_sub(k), kind = st > 100 && st < 199 && !ends(k, "Arrow") ? WEAPON : st == 303 ? ARMOUR
                                    : st == 301 || st == 302 ? HEAD : st == 304 || st == 401 ? OFF : -1;
            if (kind >= 0 && nk[kind] < 48) snprintf(keys[kind][nk[kind]++], sizeof keys[0][0], "%s", k);
        }
        mu_log("look: random gear pool: %d weapons, %d armours, %d helmets / hoods, %d shields / torches", nk[WEAPON], nk[ARMOUR], nk[HEAD], nk[OFF]);
    }
    if (!nk[WEAPON]) return 0;                                /* the face alone */
    /* a kind first (sword, mace, spear, bow, greatsword, staff ...: the game has many more staffs), then one of it */
    const char *kinds[16]; int nkd = 0;
    for (int i = 0; i < nk[WEAPON]; i++) {
        const char *w = wkind(keys[WEAPON][i]); int seen = 0;
        for (int j = 0; j < nkd; j++) seen |= !strcmp(kinds[j], w);
        if (!seen && nkd < 16) kinds[nkd++] = w;
    }
    const char *kd = kinds[rnd() % (unsigned)nkd]; int of[48], no = 0;
    for (int i = 0; i < nk[WEAPON]; i++) if (!strcmp(wkind(keys[WEAPON][i]), kd)) of[no++] = i;
    const char *wp = keys[WEAPON][of[rnd() % (unsigned)no]];
    char mat[32]; material(wp, mat, sizeof mat);
    snprintf(u->gear[u->ngear++], sizeof u->gear[0], "%s", wp);
    /* armour, head (2 in 3), off-hand (1 in 2 with a sword, mace, club or spear): the weapon's material when there is one */
    for (int kind = ARMOUR; kind < NK; kind++) {
        if (!nk[kind]) continue;
        if (kind == HEAD && rnd() % 3 == 0) continue;
        int one_hand = (ends(wp, "Sword") && !ends(wp, "GreatSword")) || ends(wp, "Mace") || ends(wp, "Club") || ends(wp, "Spear");
        if (kind == OFF && (!one_hand || rnd() % 2)) continue;
        int same[48], ns = 0;
        for (int i = 0; i < nk[kind]; i++) { char m[32]; material(keys[kind][i], m, sizeof m); if (mat[0] && !strcmp(m, mat)) same[ns++] = i; }
        int i = ns ? same[rnd() % (unsigned)ns] : (int)(rnd() % (unsigned)nk[kind]);
        snprintf(u->gear[u->ngear++], sizeof u->gear[0], "%s", keys[kind][i]);
    }
    return 0;
}

/* ------------------------------------------------------------------ public */
DSLObj mu_look_sprite(DSLObj parent, float x, float y, float w, float h, const mu_look *u) {
    const DSLUi *U = mu_kit(); if (!U || !u) return NULL;
    Obj sp = make_sprite(u); if (!sp) return NULL;
    DSLObj p = U->panel(parent, x, y, w, h, 0); if (!p) return NULL;
    cm_set_sprite(p, sp, 0xFFFFFFFFu);          /* preserveAspect: the 64 x 40 canvas keeps its shape */
    static cm_m type; if (!type.fn) type = cm_il_method("UnityEngine.UI", "Image", "set_type", 1, NULL);
    static void *kim; if (!kim) kim = cm_il_class("UnityEngine.UI", "Image");
    Obj im = kim ? cm_comp(p, kim) : NULL;
    if (im && type.fn) CM_CALL(type, void, Obj, int32_t, const void *)(im, 0, type.mi);   /* Simple (the kit's panels are Sliced) */
    cm_raycast(p, false);
    return p;
}
DSLObj mu_look_avatar(DSLObj parent, float x, float y, float s, const mu_look *u) {
    if (!u || !u->portrait[0] || !cm_icon(CM_ICON_PORTRAIT, u->portrait)) return NULL;
    return mu_icon(parent, x, y, s, CM_ICON_PORTRAIT, u->portrait, 0, NULL);
}
static void jstr(const jv *v, char *out, size_t n) {
    if (!n) return; out[0] = 0;
    if (v && v->t == JN_STR) { size_t k = v->n < n - 1 ? v->n : n - 1; memcpy(out, v->s, k); out[k] = 0; }
}
void mu_look_from_build(const jv *b, mu_look *u) {
    memset(u, 0, sizeof *u);
    if (!b) return;
    jstr(jget(b, "race"), u->race, sizeof u->race); jstr(jget(b, "portrait"), u->portrait, sizeof u->portrait);
    u->hide_head = jtruthy(jget(b, "hideHead"));
    static const char *const SLOTS[] = { "Primary", "Secondary", "Head", "Body" };
    jv *eq = jget(b, "eq");
    for (int i = 0; i < 4 && eq; i++) { jstr(jget(eq, SLOTS[i]), u->gear[u->ngear], sizeof u->gear[0]); if (u->gear[u->ngear][0]) u->ngear++; }
}

/* test: a look made now ("race|portrait|item,item"): "ok <ms>" or "none <ms>" */
__attribute__((visibility("default"))) int mu_look_test(const char *s, char *out, size_t cap) {
    mu_look u; memset(&u, 0, sizeof u);
    char b[600]; snprintf(b, sizeof b, "%s", s ? s : "");
    char *p2 = strchr(b, '|'), *p3 = p2 ? strchr(p2 + 1, '|') : NULL;
    if (p2) *p2++ = 0; if (p3) *p3++ = 0;
    snprintf(u.race, sizeof u.race, "%s", b); if (p2) snprintf(u.portrait, sizeof u.portrait, "%s", p2);
    for (char *t = p3 ? strtok(p3, ",") : NULL; t && u.ngear < MU_LOOK_GEAR; t = strtok(NULL, ",")) snprintf(u.gear[u.ngear++], sizeof u.gear[0], "%s", t);
    clock_t t0 = clock();                       /* CPU time of the main thread's work */
    Obj sp = make_sprite(&u);
    snprintf(out, cap, "%s %.2f ms", sp ? "ok" : "none", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    return sp ? 0 : 1;
}
