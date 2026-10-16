/* mu_link - "Add from wiki link" in one click (owner 2026-10-15: "for the wiki link when I click it tells me to paste
 * which is odd, I should just click and it should open the thing with the clipboard"; "it doesn't work with the short
 * links, it should"). MASTER_PLAN flows F and G.
 * The click reads the clipboard. A wiki builder link there (any form src/wk_link.h reads: ?team= / #team=, #b=~CODE,
 * #b=<JSON>, a bare code, the short /t/<id> and /build/<id> links, any language prefix) is read on a worker thread
 * (a short link = one HTTPS GET to dungeonsettlers.wiki, 5 s at most), then the units go into My Units as one new
 * team and the library is saved. Only when the clipboard holds no link does the UI show a box (mul_submit).
 * The game's frame never waits: mul_poll each frame. One job at a time. Pure C (+ the platform clipboard, threads). */
#pragma once
#include <stddef.h>
#include "pvp_wiki.h"

#define MUL_TIMEOUT_MS 5000

enum { MUL_IDLE = 0, MUL_BUSY, MUL_DONE, MUL_NEED_BOX, MUL_FAILED };

typedef struct mul_result {
    int state;                         /* MUL_* */
    int code;                          /* MUL_FAILED: the wk_link.h WKL_* reason */
    int n;                             /* MUL_DONE: units added */
    char msg[400];                     /* a player-facing line for every state but IDLE / BUSY */
    char team_id[17];                  /* MUL_DONE: the new team */
    char ids[WK_MAX_UNITS][17];        /* MUL_DONE: the new units */
} mul_result;

/* the clipboard as UTF-8 (mu_clip.c). 0 = text read (may be ""), -1 = no text on it / no clipboard */
int  mu_clip_text(char *out, size_t n);
int  mu_clip_test_put(const char *text);   /* Mac tests only: the DSMP_CLIP_PB pasteboard, never the player's */
int  mu_clip_test_release(void);

/* the button. Returns the new state: MUL_BUSY (a link: working), MUL_NEED_BOX (no link on the clipboard), or the
   current one when a job still runs */
int  mul_click(void);
/* the box's OK with its text; same returns as mul_click (MUL_NEED_BOX again when the text is no link either) */
int  mul_submit(const char *text);
/* every frame while not IDLE: fills r (may be NULL), returns the state. A finished read is added to My Units and saved
   here, on the caller's thread, so the library file is only ever written from the game's own thread. */
int  mul_poll(mul_result *r);
/* back to IDLE (the UI closed the message); refused (returns MUL_BUSY) while the worker runs */
int  mul_reset(void);
