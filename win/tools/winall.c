// winall: one line per on-screen window with alpha > 0: "<pid> <layer> <w>x<h> <owner>". All spaces.
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
static double num(CFDictionaryRef d, CFStringRef k) { double v = -1; CFNumberRef n = CFDictionaryGetValue(d, k); if (n) CFNumberGetValue(n, kCFNumberDoubleType, &v); return v; }
int main(void) {
    CFArrayRef a = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    for (CFIndex i = 0; i < CFArrayGetCount(a); i++) {
        CFDictionaryRef d = CFArrayGetValueAtIndex(a, i);
        CFBooleanRef on = CFDictionaryGetValue(d, kCGWindowIsOnscreen);
        if (!(on && CFBooleanGetValue(on)) || num(d, kCGWindowAlpha) <= 0) continue;
        CGRect r = {0}; CFDictionaryRef b = CFDictionaryGetValue(d, kCGWindowBounds); if (b) CGRectMakeWithDictionaryRepresentation(b, &r);
        char nm[256] = "?"; CFStringRef s = CFDictionaryGetValue(d, kCGWindowOwnerName); if (s) CFStringGetCString(s, nm, sizeof nm, kCFStringEncodingUTF8);
        printf("%d %d %.0fx%.0f %s\n", (int)num(d, kCGWindowOwnerPID), (int)num(d, kCGWindowLayer), r.size.width, r.size.height, nm);
    }
    return 0;
}
