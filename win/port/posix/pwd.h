/* shim: no passwd database; callers fall back to getenv("USER") (Wine passes the Unix env through) */
#pragma once
struct passwd { char *pw_name; char *pw_gecos; };
static inline struct passwd *getpwuid(int uid) { (void)uid; return 0; }
static inline int getuid(void) { return 0; }
