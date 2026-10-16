/* http_host - a stand-in for the game process for the HTTP probe: hardened runtime + the game's own entitlements
 * (run.sh signs it so), AppKit and CFNetwork linked as UnityPlayer links them, NSApplication on the main thread and the
 * main run loop running. It does nothing itself: the injected probe_dylib does the work and exits. 20 s cap. */
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <stdio.h>
#include <stdlib.h>

static void cap(CFRunLoopTimerRef t, void *u) { (void)t; (void)u; printf("host: 20 s cap reached\n"); exit(2); }
int main(void) {
    void *app = ((void *(*)(void *, SEL))objc_msgSend)((void *)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    printf("host: NSApplication %s\n", app ? "up" : "missing"); fflush(stdout);
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 20, 0, 0, 0, cap, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), t, kCFRunLoopCommonModes);
    CFRunLoopRun();
    return 0;
}
