// objc/runtime.h stand-in for the Windows build: mpmenu.c's AppKit test input (Mac only) compiles to no-ops.
// objc_getClass returns NULL, so every message goes to nil and the test helpers return 0 / do nothing.
#pragma once
#include <stddef.h>
typedef void *id; typedef void *SEL; typedef void *Class; typedef void (*IMP)(void);   /* BOOL: the Windows int BOOL (windows.h) */
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#ifndef nil
#define nil ((id)0)
#endif
static inline Class objc_getClass(const char *n) { (void)n; return NULL; }
static inline SEL sel_registerName(const char *n) { (void)n; return NULL; }
static inline Class object_getClass(id o) { (void)o; return NULL; }
static inline IMP class_replaceMethod(Class c, SEL s, IMP i, const char *t) { (void)c; (void)s; (void)i; (void)t; return NULL; }
static inline IMP class_getMethodImplementation(Class c, SEL s) { (void)c; (void)s; return NULL; }
