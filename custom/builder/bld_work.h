/* bld_work - the Unit Builder's own background thread: it opens the model (bld_js.h: the wiki's files from the Test Fights
 * cache, downloaded at first use) and runs every click on it, so the game never waits on a download or on QuickJS.
 * Jobs run in order; done() runs later on the main thread from the My Units frame tick (mu_tick_add). Stack 8 MB
 * (QuickJS needs more than 1 MB). */
#pragma once

typedef struct bld_job bld_job;
struct bld_job {
    void (*run)(bld_job *j);         /* worker thread */
    void (*done)(bld_job *j);        /* main thread; frees the job */
    bld_job *next;
};
int  bld_work_start(void);           /* 0 ok (once; later calls are no-ops) */
void bld_work_post(bld_job *j);      /* takes the job */
int  bld_work_busy(void);            /* jobs queued, running or not delivered yet */
