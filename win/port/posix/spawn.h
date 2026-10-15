/* shim: no posix_spawn on Windows; the multi-process dsnet test gets ENOSYS */
#pragma once
#include <errno.h>
#include <sys/types.h>
typedef int posix_spawn_file_actions_t;
typedef int posix_spawnattr_t;
static inline int posix_spawn_file_actions_init(posix_spawn_file_actions_t *a) { *a = 0; return 0; }
static inline int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *a, int f, int t) { (void)a; (void)f; (void)t; return 0; }
static inline int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *a) { (void)a; return 0; }
static inline int posix_spawn(pid_t *pid, const char *p, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
                              char *const argv[], char *const envp[]) {
    (void)p; (void)fa; (void)at; (void)argv; (void)envp; *pid = -1; return ENOSYS;
}
