// mu_timer.c - the OS main-thread timer behind mu_ui.h mu_tick (the timer half of ds_custommodes units/mu_clip.c; the
// clipboard half is not needed here). Mac: a CFRunLoopTimer on the main run loop. Windows: SetTimer on the calling (main)
// thread's message queue. Both fire between the game's frames.
#include "mu_ui.h"
#ifdef _WIN32
#include <windows.h>
static void (*os_tick_fn)(void);
static VOID CALLBACK os_tick(HWND w, UINT m, UINT_PTR id, DWORD t) { (void)w; (void)m; (void)id; (void)t; if (os_tick_fn) os_tick_fn(); }
int mu_os_timer(void (*fn)(void), int ms) { os_tick_fn = fn; return SetTimer(NULL, 0, (UINT)ms, os_tick) ? 0 : -1; }
#else
#include <CoreFoundation/CoreFoundation.h>
static void os_tick(CFRunLoopTimerRef t, void *info) { (void)t; ((void (*)(void))info)(); }
int mu_os_timer(void (*fn)(void), int ms) {
    CFRunLoopTimerContext ctx = { 0, (void *)fn, NULL, NULL, NULL };
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + ms / 1000.0, ms / 1000.0, 0, 0, os_tick, &ctx);
    if (!tm) return -1;
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    return 0;
}
#endif
