// units-driver: TEST-ONLY module (never shipped) that drives Custom Modes' windows in a hidden loader test copy.
// Copy of the driver half of ~/ds_launcher/loader/test/mods_src/uikit-demo/uikit_demo.c (fake mouse in UnityEngine.Input,
// real key NSEvents, the game's own frame capture), with targets from Custom Modes' test registry (cm_test_target):
//   "title:<label>" a title column button, "home:<label>" a home page button, "<prefix>:<name>" any registered part.
// DSL_UNITS_TEST=<dir>: <dir>/req.json {"seq":n,"cmd":...} -> <dir>/resp.json {"seq":n,...}
//   cmd state | click {target} | hover {target} | key {code} | type {target,text} | wheel {target,dy} | capture {name}
//       | call {fn, s}  (a Custom Modes test function "int fn(const char *s, char *out, size_t cap)" by name)
#include "dsl_sdk.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

DSL_PLUGIN();
typedef void *Obj;
static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
static char tdir[1024];
static int (*t_rect)(DSLObj, float *); static size_t (*t_label)(DSLObj, char *, size_t);
static DSLObj (*t_title)(const char *, char *, size_t); static DSLObj (*t_notice_btn)(int);
static int (*t_type)(DSLObj, const char *); static int (*t_tip)(char *, size_t);
static DSLObj (*c_target)(const char *); static size_t (*c_state)(char *, size_t); static void *cm_img;

/* ------------------------------------------------------------------ test driver */
static id game_window(void) {                                      /* the biggest window of the app */
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")); id best = nil; double ba = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
        if (f.size.width * f.size.height > ba) { ba = f.size.width * f.size.height; best = w; }
    }
    return best;
}
static double uptime(void) { id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo")); return ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime")); }
static int via_view = 1;                                          /* NSWindow swallows a first click on a non-key window */
static void send_ev(id w, id ev) {
    if (!w || !ev) return;
    if (!via_view) { ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev); return; }
    id v = ((id (*)(id, SEL))objc_msgSend)(w, sel_registerName("contentView"));
    unsigned long t = ((unsigned long (*)(id, SEL))objc_msgSend)(ev, sel_registerName("type"));
    const char *sel = t == 1 ? "mouseDown:" : t == 2 ? "mouseUp:" : t == 5 ? "mouseMoved:" : t == 22 ? "scrollWheel:" : t == 10 ? "keyDown:" : t == 11 ? "keyUp:" : NULL;
    if (v && sel && ((BOOL (*)(id, SEL, SEL))objc_msgSend)(v, sel_registerName("respondsToSelector:"), sel_registerName(sel)))
        ((void (*)(id, SEL, id))objc_msgSend)(v, sel_registerName(sel), ev);
    else ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev);
}
/* fake mouse (test only, as MP's TEAMS_MOUSE_TEST build): Unity reads the pointer from the global cursor, which a test
   must never move, so UnityEngine.Input.GetMouseButton / Down / Up and mousePosition and BaseInput.mouseScrollDelta return
   scripted values while a fake action runs. The stock StandaloneInputModule -> raycast -> OnPointerClick chain decides. */
typedef struct { float x, y, z; } V3; typedef struct { float x, y; } V2;
static int (*frame_now)(const void *);
static int fk_on, fk_down = -1, fk_until, fk_wheel_at = -1; static float fk_x, fk_y, fk_wy;
static bool (*o_mb)(int, const void *), (*o_mbd)(int, const void *), (*o_mbu)(int, const void *);
static V3 (*o_mpos)(const void *); static V2 (*o_wheel)(Obj, const void *);
static int fnow(void) { return frame_now ? frame_now(NULL) : 0; }
static int fk_live(void) { return fk_on && fnow() <= fk_until; }
static bool hk_mb(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down) : o_mb(b, mi); }
static bool hk_mbd(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down) : o_mbd(b, mi); }
static bool hk_mbu(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down + 1) : o_mbu(b, mi); }
static V3 hk_mpos(const void *mi) { return fk_live() ? (V3){ fk_x, fk_y, 0 } : o_mpos(mi); }
static V2 hk_wheel(Obj self, const void *mi) { return fk_live() && fnow() == fk_wheel_at ? (V2){ 0, fk_wy } : o_wheel(self, mi); }
static void fk_at(const float r[4]) { fk_on = 1; fk_x = (r[0] + r[2]) / 2; fk_y = (r[1] + r[3]) / 2; fk_down = -1; fk_wheel_at = -1; }
static int click_rect(const float r[4]) { fk_at(r); fk_down = fnow() + 5; fk_until = fk_down + 30; return 0; }   /* rest 5 frames, press, release */
static int move_rect(const float r[4]) { fk_at(r); fk_until = 0x7fffffff; return 0; }                           /* hover until "mouse_off" */
static int wheel_rect(const float r[4], int dy) { fk_at(r); fk_wheel_at = fnow() + 3; fk_wy = (float)dy; fk_until = fk_wheel_at + 20; return 0; }
static id ns_str(const char *s) { return ((id (*)(Class, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s); }
static int key_code(int kc) {                                     /* one special key (36 Return, 53 Escape) down + up */
    id w = game_window(); if (!w) return -1;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    char one[2] = { kc == 36 ? '\r' : kc == 53 ? 0x1b : 0, 0 };
    for (int up = 0; up < 2; up++) {
        id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL, unsigned short))objc_msgSend)(
            objc_getClass("NSEvent"), sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
            up ? 11 : 10, CGPointZero, 0, uptime(), wn, nil, ns_str(one), ns_str(one), NO, (unsigned short)kc);
        send_ev(w, ev);
    }
    return 0;
}
/* the game's own frame (ScreenCapture.CaptureScreenshotAsTexture, between frames like dsl_soft.c capture()) */
static int capture(const char *name) {
    static Obj (*cap)(const void *); static Obj (*enc)(Obj, const void *); static void (*des)(Obj, const void *);
    if (!cap) { cap = H->find_method_address("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
                enc = H->find_method_address("UnityEngine", "ImageConversion", "EncodeToPNG", 1); des = H->find_method_address("UnityEngine", "Object", "Destroy", 1); }
    if (!cap || !enc) return -1;
    Obj tex = cap(NULL); if (!tex) return -2;
    Obj png = enc(tex, NULL); if (des) des(tex, NULL);
    if (!png) return -3;
    size_t n = (size_t)*(uintptr_t *)((char *)png + 0x18);
    char p[1400]; snprintf(p, sizeof p, "%s/cap/%s.png", tdir, name);
    FILE *f = fopen(p, "wb"); if (!f) return -4;
    fwrite((char *)png + 0x20, 1, n, f); fclose(f);
    return (int)n;
}
static char *jstr(const char *j, const char *k, char *out, size_t cap) {   /* "k":"v" (no escapes needed by the test) */
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); out[0] = 0; if (!p) return NULL;
    p += strlen(pat); while (*p == ' ') p++; if (*p != '"') return NULL; p++;
    size_t n = 0; while (*p && *p != '"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; } out[n] = 0; return out;
}
static double jnum(const char *j, const char *k, double d) { char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); return p ? atof(p + strlen(pat)) : d; }
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }

static DSLObj target(const char *t) {
    if (!strcmp(t, "notice_ok")) return t_notice_btn(0);
    if (!strcmp(t, "notice_cancel")) return t_notice_btn(1);
    if (!strncmp(t, "title:", 6)) return t_title(t + 6, NULL, 0);
    return c_target ? c_target(t) : NULL;
}
static void state(char *out, size_t cap) {
    size_t o = 0; char col[1200] = "", e[1400];
    t_title(NULL, col, sizeof col); jesc(e, sizeof e, col);
    o += (size_t)snprintf(out + o, cap - o, "\"kit\":%d,\"title_column\":\"%s\"", U != NULL, e);
    if (c_state && o + 2 < cap) { o += (size_t)snprintf(out + o, cap - o, ",\"cm\":{"); o += c_state(out + o, cap - o - 2); o += (size_t)snprintf(out + o, cap - o, "}"); }
    { id w = game_window(); CGRect f = w ? ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame")) : CGRectZero;
      o += (size_t)snprintf(out + o, cap - o, ",\"win\":[%.0f,%.0f,%.0f,%.0f]", f.origin.x, f.origin.y, f.size.width, f.size.height); }
    char tt[200] = "", te[260]; int ts = t_tip(tt, sizeof tt); jesc(te, sizeof te, tt);
    o += (size_t)snprintf(out + o, cap - o, ",\"tip\":{\"shown\":%d,\"title\":\"%s\"}", ts, te);
}
static void handle(const char *j) {
    static char out[65536], st[200000];
    char cmd[32], t[256], s[4096]; int seq = (int)jnum(j, "seq", 0), rc = 0; size_t o = 0;
    jstr(j, "cmd", cmd, sizeof cmd); jstr(j, "target", t, sizeof t);
    out[0] = 0;
    if (!U) rc = -100;
    else if (!strcmp(cmd, "state")) { }
    else if (!strcmp(cmd, "click") || !strcmp(cmd, "hover") || !strcmp(cmd, "wheel") || !strcmp(cmd, "rect")) {
        DSLObj w = target(t); float r[4] = { 0 }; int on = w ? t_rect(w, r) : -1;
        char l[400] = "", le[500]; if (w && on >= 0) t_label(w, l, sizeof l); jesc(le, sizeof le, l);
        if (!strcmp(cmd, "rect")) rc = on;
        else if (on <= 0) rc = -1;
        else rc = !strcmp(cmd, "click") ? click_rect(r) : !strcmp(cmd, "hover") ? move_rect(r) : wheel_rect(r, (int)jnum(j, "dy", -3));
        o += (size_t)snprintf(out + o, sizeof out - o, "\"on\":%d,\"at\":[%.0f,%.0f,%.0f,%.0f],\"text\":\"%s\",", on, r[0], r[1], r[2], r[3], le);
    }
    else if (!strcmp(cmd, "mouse_off")) fk_on = 0;
    else if (!strcmp(cmd, "via")) via_view = (int)jnum(j, "view", 1);
    else if (!strcmp(cmd, "key")) rc = key_code((int)jnum(j, "code", 36));
    else if (!strcmp(cmd, "type")) { jstr(j, "text", s, sizeof s); DSLObj w = target(t); rc = w ? t_type(w, s) : -1; }
    else if (!strcmp(cmd, "capture")) { jstr(j, "name", s, sizeof s); rc = capture(s); }
    else if (!strcmp(cmd, "call")) {
        char fn[96]; jstr(j, "fn", fn, sizeof fn); jstr(j, "s", s, sizeof s);
        int (*f)(const char *, char *, size_t) = cm_img ? (int (*)(const char *, char *, size_t))dlsym(cm_img, fn) : NULL;
        static char r2[60000], re[65000]; r2[0] = 0;
        rc = f ? f(s, r2, sizeof r2) : -404; jesc(re, sizeof re, r2);
        o += (size_t)snprintf(out + o, sizeof out - o, "\"out\":\"%s\",", re);
    } else rc = -3;
    state(st, sizeof st);
    char p[1200], q[1200]; snprintf(p, sizeof p, "%s/resp.json.tmp", tdir); snprintf(q, sizeof q, "%s/resp.json", tdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"seq\":%d,\"cmd\":\"%s\",\"rc\":%d,%s%s}\n", seq, cmd, rc, out, st); fclose(f); rename(p, q);
}
/* UGUI's StandaloneInputModule drops every event while the app has no focus (ShouldIgnoreEventsOnNoFocus) and a hidden
   test copy never gets focus: tell the EventSystem it has focus (test only; the input path itself stays stock) */
static void focus_on(void) {
    static Obj (*cur)(const void *); static void (*foc)(Obj, bool, const void *);
    if (!cur) { cur = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
                foc = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "OnApplicationFocus", 1); }
    Obj es = cur ? cur(NULL) : NULL; if (es && foc) foc(es, true, NULL);
}
static void tick(CFRunLoopTimerRef tm, void *info) {
    (void)tm; (void)info;
    focus_on();
    char p[1200]; snprintf(p, sizeof p, "%s/req.json", tdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char j[4096]; size_t n = fread(j, 1, sizeof j - 1, f); fclose(f); j[n] = 0;
    char d[1300]; snprintf(d, sizeof d, "%s/req.done.json", tdir); rename(p, d);
    handle(j);
}


DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    const char *td = getenv("DSL_UNITS_TEST");
    if (!td || !*td) { h->log(me, "DSL_UNITS_TEST not set: driver off"); return 0; }
    snprintf(tdir, sizeof tdir, "%s", td);
    U = dsl_ui(h);
    if (!U) { h->log(me, "no dsl_ui kit: driver off"); return 0; }
    char p[1200]; snprintf(p, sizeof p, "%s/../custom-modes/custom-modes.dylib", h->module_dir(me));
    cm_img = dlopen(p, RTLD_NOLOAD | RTLD_LOCAL);
    if (cm_img) { *(void **)&c_target = dlsym(cm_img, "cm_test_target"); *(void **)&c_state = dlsym(cm_img, "cm_test_state"); }
    h->log(me, "custom-modes image %p (%s): target %p state %p", cm_img, p, (void *)c_target, (void *)c_state);
    *(void **)&t_rect = dlsym(RTLD_DEFAULT, "dsl_ui_test_rect"); *(void **)&t_label = dlsym(RTLD_DEFAULT, "dsl_ui_test_label");
    *(void **)&t_title = dlsym(RTLD_DEFAULT, "dsl_ui_test_title"); *(void **)&t_notice_btn = dlsym(RTLD_DEFAULT, "dsl_ui_test_notice_button");
    *(void **)&t_type = dlsym(RTLD_DEFAULT, "dsl_ui_test_type"); *(void **)&t_tip = dlsym(RTLD_DEFAULT, "dsl_ui_test_tip");
    if (!t_rect || !t_label || !t_title || !t_notice_btn || !t_type || !t_tip) { h->log(me, "loader test exports missing: driver off"); return 0; }
    frame_now = h->find_method_address("UnityEngine", "Time", "get_frameCount", 0);
    int m1 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButton", 1, (void *)hk_mb, (void **)&o_mb);
    int m2 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonDown", 1, (void *)hk_mbd, (void **)&o_mbd);
    int m3 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonUp", 1, (void *)hk_mbu, (void **)&o_mbu);
    int m4 = h->hook_method(me, "UnityEngine", "Input", "get_mousePosition", 0, (void *)hk_mpos, (void **)&o_mpos);
    int m5 = h->hook_method(me, "UnityEngine.EventSystems", "BaseInput", "get_mouseScrollDelta", 0, (void *)hk_wheel, (void **)&o_wheel);
    h->log(me, "test fake mouse hooks: %d %d %d %d %d", m1, m2, m3, m4, m5);
    char cd[1200]; snprintf(cd, sizeof cd, "%s/cap", tdir); mkdir(cd, 0755);
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.5, 0.05, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    h->log(me, "units test driver on (%s)", tdir);
    return 0;
}
