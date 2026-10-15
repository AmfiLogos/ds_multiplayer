/* shim: see spawn.h */
#pragma once
static inline int waitpid(int pid, int *st, int o) { (void)pid; (void)o; if (st) *st = -1; return -1; }
#define WIFEXITED(s) 0
#define WEXITSTATUS(s) (-1)
