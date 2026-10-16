// cm_il.h - il2cpp by NAME for Custom Modes' solo windows (home, My Units, Bring Units): classes, methods by parameter
// types, fields, strings, gc handles, a few Unity / UGUI / TMP calls the dsl_ui kit does not offer. Everything goes
// through the loader host (find_class, method_address, il2cpp_fn), so the same code runs on Mac and Windows.
// Main thread only. cm_il_init() after il2cpp_init (dsl_plugin_init or later).
#pragma once
#include "dsl_sdk.h"
#include <stdbool.h>
#include <stdint.h>

typedef void *Obj;
typedef struct cm_m { const void *mi; void *fn; } cm_m;
#define CM_AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CM_CALL(m, R, ...) ((R (*)(__VA_ARGS__))(m).fn)

int  cm_il_init(const DSLHost *host, DSLPlugin *self);   /* 1 ok (cached), 0 not yet / missing exports */
cm_m cm_il_method(const char *ns, const char *cls, const char *name, int argc, const char *sig);   /* sig NULL = any */
void *cm_il_class(const char *ns, const char *cls);
int32_t cm_il_field(void *klass, const char *name);     /* -1 = none */
Obj  cm_il_type_obj(void *klass);                        /* System.Type of a class */
Obj  cm_il_str(const char *utf8);
void cm_il_utf8(Obj s, char *out, size_t cap);           /* System.String -> UTF-8 ("" for NULL) */
uintptr_t cm_il_hnew(Obj o);
Obj  cm_il_hget(uintptr_t h);
void cm_il_hfree(uintptr_t h);
void cm_il_hset(uintptr_t *h, Obj o);

/* Unity */
bool cm_alive(Obj o);                                    /* UnityEngine.Object alive (op_Implicit) */
Obj  cm_find(void *klass, bool inactive);                /* Object.FindObjectOfType(Type, bool) */
Obj  cm_go(Obj comp);
Obj  cm_tr(Obj comp);
Obj  cm_comp(Obj comp_or_go, void *klass);               /* GetComponent(Type) on the object's GameObject */
void cm_set_active(Obj comp_or_go, bool on);
Obj  cm_parent(Obj tr);
int  cm_child_count(Obj tr);
Obj  cm_child(Obj tr, int i);
int  cm_sibling(Obj tr);
void cm_set_sibling(Obj tr, int i);
void cm_destroy(Obj obj);                                /* Object.Destroy */
Obj  cm_instantiate(Obj original, Obj parent_tr);        /* Object.Instantiate(Object, Transform) */
int  cm_rect_size(Obj rt, float *w, float *h);           /* RectTransform.rect size; 0 ok */
/* UGUI / TMP on kit widgets (DSLObj = RectTransform) */
void cm_raycast(Obj rt, bool on);                        /* Graphic.raycastTarget of the widget's Graphic */
int  cm_set_sprite(Obj rt, Obj sprite, uint32_t rgba);   /* Image.sprite + color + preserveAspect; 0 ok */
Obj  cm_text_of(Obj rt);                                 /* the widget's TMP_Text (own, else the first child's) */
void cm_text_ellipsis(Obj rt);                           /* TMP: overflowMode = Ellipsis, no wrapping */
float cm_text_preferred_w(Obj rt);                       /* TMP preferredWidth (-1 = no text) */
int  cm_text_truncated(Obj rt);                          /* TMP isTextTruncated (1 = cut with an ellipsis) */
/* the game's own texts and icons */
int  cm_gettext(const char *textkey, char *out, size_t cap);   /* TextKeyExtensions.GetText; 0 = found (out != key) */
enum { CM_ICON_ITEM = 1, CM_ICON_SKILL, CM_ICON_TREE, CM_ICON_AFFECTER, CM_ICON_PORTRAIT };
Obj  cm_icon(int kind, const char *key);                 /* Sprite, or NULL (the game's default icon counts as NULL) */
