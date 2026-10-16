/* bld_work - the Unit Builder's background thread (bld_work.h). */
#include "bld_work.h"
#include "mu_ui.h"            /* mu_tick_add: the main-thread tick of the Custom Modes windows */
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION mu; static CONDITION_VARIABLE cv; static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_sync(PINIT_ONCE o, PVOID p, PVOID *c) { (void)o; (void)p; (void)c; InitializeCriticalSection(&mu); InitializeConditionVariable(&cv); return TRUE; }
#define LOCK() (InitOnceExecuteOnce(&once, init_sync, NULL, NULL), EnterCriticalSection(&mu))
#define UNLOCK() LeaveCriticalSection(&mu)
#define WAIT() SleepConditionVariableCS(&cv, &mu, INFINITE)
#define WAKE() WakeConditionVariable(&cv)
#else
#include <pthread.h>
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mu)
#define UNLOCK() pthread_mutex_unlock(&mu)
#define WAIT() pthread_cond_wait(&cv, &mu)
#define WAKE() pthread_cond_signal(&cv)
#endif

static bld_job *q_head, *q_tail, *d_head, *d_tail;   /* to run / done, both FIFO */
static int running, started;

static void push(bld_job **h, bld_job **t, bld_job *j) { j->next = NULL; if (*t) (*t)->next = j; else *h = j; *t = j; }
static bld_job *pop(bld_job **h, bld_job **t) { bld_job *j = *h; if (j) { *h = j->next; if (!*h) *t = NULL; } return j; }

#ifdef _WIN32
static DWORD WINAPI loop(LPVOID u)
#else
static void *loop(void *u)
#endif
{
    (void)u;
    for (;;) {
        LOCK();
        while (!q_head) WAIT();
        bld_job *j = pop(&q_head, &q_tail); running = 1;
        UNLOCK();
        j->run(j);
        LOCK(); push(&d_head, &d_tail, j); running = 0; UNLOCK();
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void deliver(void) {                          /* main thread */
    for (;;) {
        LOCK(); bld_job *j = pop(&d_head, &d_tail); UNLOCK();
        if (!j) return;
        j->done(j);
    }
}

int bld_work_start(void) {
    if (started) return 0;
#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 8u << 20, loop, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!th) return -1;
    CloseHandle(th);
#else
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20);
    pthread_t th; int r = pthread_create(&th, &at, loop, NULL); pthread_attr_destroy(&at);
    if (r) return -1;
    pthread_detach(th);
#endif
    if (mu_tick_add(deliver)) return -1;
    started = 1;
    return 0;
}

void bld_work_post(bld_job *j) { LOCK(); push(&q_head, &q_tail, j); WAKE(); UNLOCK(); }

int bld_work_busy(void) { LOCK(); int b = q_head || running || d_head; UNLOCK(); return b; }
