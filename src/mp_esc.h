/* mp_esc.h - Escape for the multiplayer windows through the loader kit (owner 2026-10-25: "make sure Escape works to close
 * our custom windows"). The kit owns the key (DSLUi esc_push / esc_drop, kit 0.1.28): one press closes the top-most pushed
 * window that shows, as its own Close / Cancel / OK, and the game never sees that press. Our windows push themselves here
 * when they show and drop when they hide (mpesc_sync, every frame). Without that kit (older loader, the rig's own dylib)
 * mpesc_sync returns 0 and the window keeps its old Escape path.
 *  - Our boxes on the stock CommonNoticeView (Leave, invite, tag, mods boxes) and the ui lane's disconnect prompt lie over
 *    every window of ours: while one is up our windows step off the kit's stack, so that press goes to the box.
 *  - mpesc_took(): our own Escape checks (title Back, lobby Rules, Leave box, PvP, Bring) skip a press the kit takes for
 *    one of ours, so one press never closes two things. */
#pragma once

typedef struct mpesc_win {
    void *id;                      /* the object pushed (identity only, never read: the kit keeps a weak handle); NULL = none */
    void (*close)(void *ud);       /* exactly the window's own Close / Cancel / OK; runs on the main thread between frames */
    void *ud;
    const char *name;              /* for the log */
    void *held;                    /* shows but off the kit's stack while a box of ours is over it (identity only) */
    int order;                     /* when it opened: held windows go back on the stack in this order */
} mpesc_win;

enum { MPESC_BOX = 1 };            /* flags: the window is one of our stock boxes itself (it never steps off for a box) */

/* every frame: shown = the window shows now. Pushes obj when it shows (or shows as another object), drops it when it
   hides. 1 = the kit has the key for this window (pushed, or held under a box of ours: the box gets the press); its own
   Escape check skips. 0 = no kit Escape (older loader, the rig's own dylib, refused): the window's old Escape path. */
int  mpesc_sync(mpesc_win *w, void *obj, int shown, int flags);
void mpesc_drop(mpesc_win *w);     /* closed or hidden another way: off the kit's stack now */
int  mpesc_took(void);             /* 1 = the kit takes (or just took) this frame's Escape for one of our windows */
int  mpesc_box_over(void);         /* 1 = one of our stock boxes (or the ui lane's prompt) is up over our windows */
