/* mu_clip - the system clipboard's text, for the one-click "Add from wiki link" (owner 2026-10-15: "I should just
 * click and it should open the thing with the clipboard"). Read only: the clipboard is never written.
 * Mac: NSPasteboard through the Objective-C runtime (no Objective-C file; works in the game process, which has AppKit).
 *      env DSMP_CLIP_PB = a named pasteboard instead of the general one (tests: never touches the player's clipboard).
 * Windows: OpenClipboard + CF_UNICODETEXT, converted to UTF-8.
 * Also the OS timer behind mu_ui.h mu_tick (the one other OS call of the windows): Mac a CFRunLoopTimer on the main run
 * loop, Windows SetTimer on the calling (main) thread's message queue; both fire between the game's frames. */
#include "mu_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void (*os_tick_fn)(void);
static VOID CALLBACK os_tick(HWND w, UINT m, UINT_PTR id, DWORD t) { (void)w; (void)m; (void)id; (void)t; if (os_tick_fn) os_tick_fn(); }
int mu_os_timer(void (*fn)(void), int ms) { os_tick_fn = fn; return SetTimer(NULL, 0, (UINT)ms, os_tick) ? 0 : -1; }
int mu_clip_test_put(const char *text) { (void)text; return -1; }     /* tests run on the Mac */
int mu_clip_test_release(void) { return -1; }
int mu_clip_text(char *out, size_t n) {
    if (!n) return -1;
    out[0] = 0;
    int rc = -1;
    for (int tries = 0; tries < 5 && !OpenClipboard(NULL); tries++) Sleep(10);   /* another program may hold it a moment */
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *w = (const wchar_t *)GlobalLock(h);
        if (w) {
            int k = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
            if (k <= 0) {                                                  /* longer than out: keep what fits */
                int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
                char *tmp = need > 0 ? malloc((size_t)need) : NULL;
                if (tmp && WideCharToMultiByte(CP_UTF8, 0, w, -1, tmp, need, NULL, NULL) > 0) {
                    size_t L = n - 1; while (L > 0 && ((unsigned char)tmp[L] & 0xC0) == 0x80) L--;
                    memcpy(out, tmp, L); out[L] = 0; rc = 0;
                }
                free(tmp);
            } else rc = 0;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return rc;
}
#else
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreFoundation/CoreFoundation.h>
static void os_tick(CFRunLoopTimerRef t, void *info) { (void)t; ((void (*)(void))info)(); }
int mu_os_timer(void (*fn)(void), int ms) {
    CFRunLoopTimerContext ctx = { 0, (void *)fn, NULL, NULL, NULL };
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + ms / 1000.0, ms / 1000.0, 0, 0, os_tick, &ctx);
    if (!tm) return -1;
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    return 0;
}

typedef void *oid;
static oid msg0(oid o, const char *sel) { return ((oid (*)(oid, SEL))objc_msgSend)(o, sel_registerName(sel)); }
static oid msg1(oid o, const char *sel, oid a) { return ((oid (*)(oid, SEL, oid))objc_msgSend)(o, sel_registerName(sel), a); }
static oid nsstr(const char *s) {
    return ((oid (*)(oid, SEL, const char *))objc_msgSend)((oid)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s);
}
/* the pasteboard: general, or the named one of DSMP_CLIP_PB */
static oid board(void) {
    oid cls = (oid)objc_getClass("NSPasteboard");
    if (!cls) return NULL;                                                /* AppKit not loaded (a command-line tool) */
    const char *nm = getenv("DSMP_CLIP_PB");
    return nm && *nm ? msg1(cls, "pasteboardWithName:", nsstr(nm)) : msg0(cls, "generalPasteboard");
}
int mu_clip_text(char *out, size_t n) {
    if (!n) return -1;
    out[0] = 0;
    oid pool = msg0(msg0((oid)objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    int rc = -1;
    oid pb = board();
    if (pb) {
        oid s = msg1(pb, "stringForType:", nsstr("public.utf8-plain-text"));   /* NSPasteboardTypeString */
        const char *u = s ? (const char *)msg0(s, "UTF8String") : NULL;
        if (u) {
            size_t L = strlen(u); if (L >= n) { L = n - 1; while (L > 0 && ((unsigned char)u[L] & 0xC0) == 0x80) L--; }
            memcpy(out, u, L); out[L] = 0; rc = 0;
        }
    }
    msg0(pool, "drain");
    return rc;
}
/* tests only: put text on the DSMP_CLIP_PB pasteboard (refused without it: the player's clipboard is never written) */
int mu_clip_test_put(const char *text) {
    const char *nm = getenv("DSMP_CLIP_PB");
    if (!nm || !*nm) return -1;
    oid pool = msg0(msg0((oid)objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    oid pb = board(); int rc = -1;
    if (pb) {
        msg0(pb, "clearContents");
        rc = ((signed char (*)(oid, SEL, oid, oid))objc_msgSend)(pb, sel_registerName("setString:forType:"), nsstr(text), nsstr("public.utf8-plain-text")) ? 0 : -1;
    }
    msg0(pool, "drain");
    return rc;
}
int mu_clip_test_release(void) {
    const char *nm = getenv("DSMP_CLIP_PB");
    if (!nm || !*nm) return -1;
    oid pb = board(); if (pb) msg0(pb, "releaseGlobally");
    return 0;
}
#endif
