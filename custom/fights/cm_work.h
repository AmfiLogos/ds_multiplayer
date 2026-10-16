/* cm_work - one background thread for the slow parts of Test Fights and Gallery (wiki downloads, the save generator),
 * so the game never waits on them. Jobs run in order on that thread (it owns the Test Fights engine: QuickJS is used
 * from one thread only); done() runs later on the main thread from cmw's frame tick. Stack 8 MB (QuickJS needs > 1 MB). */
#pragma once

typedef struct cm_job cm_job;
struct cm_job {
    void (*run)(cm_job *j);          /* worker thread */
    void (*done)(cm_job *j);         /* main thread; frees the job (and what it holds) */
    void *ud;
    cm_job *next;
};
int  cm_work_start(void);            /* starts the thread and adds the main-thread delivery to cmw's tick. 0 ok */
void cm_work_post(cm_job *j);        /* takes the job */
int  cm_work_busy(void);             /* jobs queued or running */
