/* cm_work.c - see cm_work.h */
#include "cm_work.h"
#include "cm_win.h"
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION mu; static CONDITION_VARIABLE cv;
#define LOCK() EnterCriticalSection(&mu)
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

static cm_job *todo, *todo_tail, *fin, *fin_tail; static int running, started;

static void push(cm_job **h, cm_job **t, cm_job *j) { j->next = NULL; if (*t) (*t)->next = j; else *h = j; *t = j; }
static cm_job *pop(cm_job **h, cm_job **t) { cm_job *j = *h; if (j) { *h = j->next; if (!*h) *t = NULL; } return j; }

#ifdef _WIN32
static DWORD WINAPI loop(LPVOID a)
#else
static void *loop(void *a)
#endif
{
    (void)a;
    for (;;) {
        LOCK();
        while (!todo) WAIT();
        cm_job *j = pop(&todo, &todo_tail); running = 1;
        UNLOCK();
        j->run(j);
        LOCK(); push(&fin, &fin_tail, j); running = 0; UNLOCK();
    }
    return 0;
}
static void deliver(void *ud) {
    (void)ud;
    for (;;) {
        LOCK(); cm_job *j = pop(&fin, &fin_tail); UNLOCK();
        if (!j) break;
        j->done(j);
    }
}
int cm_work_start(void) {
    if (started) return 0;
#ifdef _WIN32
    InitializeCriticalSection(&mu); InitializeConditionVariable(&cv);
    HANDLE th = CreateThread(NULL, 8u << 20, loop, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!th) return -1;
    CloseHandle(th);
#else
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 8u << 20);
    pthread_t th; int r = pthread_create(&th, &at, loop, NULL); pthread_attr_destroy(&at);
    if (r) return -1;
    pthread_detach(th);
#endif
    if (cmw_tick_add(deliver, NULL)) return -1;
    started = 1;
    return 0;
}
void cm_work_post(cm_job *j) { LOCK(); push(&todo, &todo_tail, j); WAKE(); UNLOCK(); }
int cm_work_busy(void) { LOCK(); int b = todo != NULL || running; UNLOCK(); return b; }
