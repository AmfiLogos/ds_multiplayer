/* mp_esc.c - Escape for the multiplayer windows through the loader kit (mp_esc.h). */
#include "mp_esc.h"
#include "mu_ui.h"
#include "rig.h"
#include <stddef.h>

int mpmenu_box_over(void);                     /* mpmenu.c: one of our boxes on the stock CommonNoticeView is up */
int ui_modal_open(void);                       /* ui_game.c: the ui lane's prompt (player disconnected: Keep playing / Wait) */
void *mpmenu_loader_sym(const char *n);        /* mpmenu.c: a loader export (Windows: baselib.dll) */

#define W_MAX 16
static mpesc_win *ws[W_MAX]; static int nws;   /* windows that show now: pushed, or held off the stack under a box */
static int64_t took_at = -100;                 /* rig_frames when the kit last closed one of ours */
static int order_seq, refused_logged;

int mpesc_box_over(void) { return mpmenu_box_over() || ui_modal_open(); }
#ifndef DSL_UI_HAS                             /* built against a loader SDK older than kit 0.1.28: no kit Escape, old paths */
#define DSL_UI_HAS(ui, field) ((void)(ui), 0)
static inline int dsl_esc_push(const DSLUi *ui, DSLObj win, void (*close)(void *), void *ud, int flags) { (void)ui; (void)win; (void)close; (void)ud; (void)flags; return -1; }
static inline void dsl_esc_drop(const DSLUi *ui, DSLObj win) { (void)ui; (void)win; }
#endif

static void ws_del(mpesc_win *w) { for (int i = 0; i < nws; i++) if (ws[i] == w) { ws[i] = ws[--nws]; return; } }
static int ws_has(const mpesc_win *w) { for (int i = 0; i < nws; i++) if (ws[i] == w) return 1; return 0; }
/* the kit: Escape closes this window (its tick, between frames) */
static void esc_close(void *ud) {
    mpesc_win *w = (mpesc_win *)ud;
    took_at = rig_frames;
    rig_log("esc: Escape closes %s (the kit's key)", w && w->name ? w->name : "a window");
    if (w && w->close) w->close(w->ud);
}
static void kit_drop(mpesc_win *w) { const DSLUi *U = mu_kit(); if (w->id && U) dsl_esc_drop(U, w->id); w->id = NULL; }
static int kit_push(mpesc_win *w, void *obj) {
    if (dsl_esc_push(mu_kit(), obj, esc_close, w, 0)) {
        if (!refused_logged) { refused_logged = 1; rig_log("esc: the kit refused %s: its old Escape path", w->name ? w->name : "a window"); }
        return -1;
    }
    w->id = obj; return 0;
}
void mpesc_drop(mpesc_win *w) { if (w) { kit_drop(w); w->held = NULL; ws_del(w); } }
int mpesc_sync(mpesc_win *w, void *obj, int shown, int flags) {
    const DSLUi *U = mu_kit();
    if (!w || !DSL_UI_HAS(U, esc_push)) { if (w && (w->id || w->held)) mpesc_drop(w); return 0; }   /* older loader / the rig's own dylib */
    if (!shown || !obj) { if (w->id || w->held || ws_has(w)) mpesc_drop(w); return 1; }
    if ((w->id && w->id != obj) || (w->held && w->held != obj)) mpesc_drop(w);           /* shown as another object: a new window */
    if (!ws_has(w)) { if (nws == W_MAX) return 0; ws[nws++] = w; w->order = ++order_seq; }
    if (!(flags & MPESC_BOX) && mpesc_box_over()) {                                         /* under a box of ours: off the stack */
        if (w->id) { kit_drop(w); rig_log("esc: %s steps off the Escape stack under our box", w->name ? w->name : "a window"); }
        w->held = obj; return 1;
    }
    if (w->id) return 1;
    if (w->held) {                                  /* the box closed: every held window back, in the order they opened */
        for (;;) {
            mpesc_win *n = NULL;
            for (int i = 0; i < nws; i++) if (ws[i]->held && (!n || ws[i]->order < n->order)) n = ws[i];
            if (!n) break;
            void *o = n->held; n->held = NULL;
            if (kit_push(n, o) && n == w) return 0;
        }
        return w->id ? 1 : 0;
    }
    if (kit_push(w, obj)) { ws_del(w); return 0; }
    return 1;                                        /* the kit has the key for this window */
}
int mpesc_took(void) {
    /* the kit takes this press for a window of ANY mod (another mod's window or a kit notice over our title page): kit 0.1.28+ */
    static int (*kit_took)(void); static int looked;
    if (!looked) { looked = 1; kit_took = (int (*)(void))mpmenu_loader_sym("dsl_esc_taken"); }
    if (kit_took && kit_took()) return 1;
    for (int i = 0; i < nws; i++) if (ws[i]->id) return 1;
    return rig_frames - took_at <= 1;
}
