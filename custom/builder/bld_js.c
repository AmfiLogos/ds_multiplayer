/* bld_js - the Unit Builder's model in QuickJS (see bld_js.h). Same engine pieces and load order as Test Fights
 * (custom/fights/sg_js.c): sg_prelude.js (the browser bits the wiki's scripts use), data.js under `window`, builder.js,
 * then our bld_glue.js. */
#include "bld_js.h"
#include "quickjs/quickjs.h"
#include "sg_embed.h"          /* custom/fights/sg_embed.c: sg_prelude_js[] (+ _len) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct bld_engine {
    JSRuntime *rt;
    JSContext *ctx;
    int deadline_ms;
    double t_end;
    int timed_out;
};

static double now_s(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}
static int on_interrupt(JSRuntime *rt, void *u) {
    (void)rt; bld_engine *e = u;
    if (e->t_end > 0 && now_s() > e->t_end) { e->timed_out = 1; return 1; }
    return 0;
}
static void arm(bld_engine *e) { e->timed_out = 0; e->t_end = e->deadline_ms > 0 ? now_s() + e->deadline_ms / 1000.0 : 0; }
static void disarm(bld_engine *e) { e->t_end = 0; }

static void take_exc(bld_engine *e, const char *what, char *err, size_t errn) {
    JSContext *c = e->ctx;
    JSValue x = JS_GetException(c);
    const char *m = JS_ToCString(c, x);
    const char *st = NULL; JSValue sv = JS_UNDEFINED;
    if (JS_IsObject(x)) { sv = JS_GetPropertyStr(c, x, "stack"); if (JS_IsString(sv)) st = JS_ToCString(c, sv); }
    if (e->timed_out) snprintf(err, errn, "%s: stopped after %d ms", what, e->deadline_ms);
    else snprintf(err, errn, "%s: %s%s%.300s", what, m ? m : "?", st ? " | " : "", st ? st : "");
    if (st) JS_FreeCString(c, st);
    if (m) JS_FreeCString(c, m);
    JS_FreeValue(c, sv); JS_FreeValue(c, x);
}

static int run_script(bld_engine *e, const char *src, size_t n, const char *name, char *err, size_t errn) {
    char *buf = malloc(n + 1);                                 /* JS_Eval wants a NUL at src[n] */
    if (!buf) { snprintf(err, errn, "%s: out of memory", name); return -1; }
    memcpy(buf, src, n); buf[n] = 0;
    arm(e);
    JSValue v = JS_Eval(e->ctx, buf, n, name, JS_EVAL_TYPE_GLOBAL);
    disarm(e);
    free(buf);
    if (JS_IsException(v)) { take_exc(e, name, err, errn); return -1; }
    JS_FreeValue(e->ctx, v);
    return 0;
}

/* a value or a Promise -> its settled value (caller frees) */
static int settle(bld_engine *e, JSValue p, JSValue *out, const char *what, char *err, size_t errn) {
    JSContext *c = e->ctx;
    if (JS_IsException(p)) { take_exc(e, what, err, errn); return -1; }
    if (!JS_IsObject(p) || (int)JS_PromiseState(c, p) < 0) { *out = p; return 0; }
    for (;;) {
        JSContext *c1;
        int r = JS_ExecutePendingJob(e->rt, &c1);
        if (r < 0) { take_exc(e, what, err, errn); JS_FreeValue(c, p); return -1; }
        if (r == 0) break;
    }
    JSPromiseStateEnum st = JS_PromiseState(c, p);
    if (st == JS_PROMISE_PENDING) { snprintf(err, errn, "%s: never finished", what); JS_FreeValue(c, p); return -1; }
    JSValue v = JS_PromiseResult(c, p);
    JS_FreeValue(c, p);
    if (st == JS_PROMISE_REJECTED) {
        JSValue mv = JS_IsObject(v) ? JS_GetPropertyStr(c, v, "message") : JS_UNDEFINED;
        const char *mm = JS_IsString(mv) ? JS_ToCString(c, mv) : NULL, *m = mm ? NULL : JS_ToCString(c, v);
        snprintf(err, errn, "%s: %s", what, mm ? mm : (m ? m : "failed"));
        if (mm) JS_FreeCString(c, mm);
        if (m) JS_FreeCString(c, m);
        JS_FreeValue(c, mv); JS_FreeValue(c, v);
        return -1;
    }
    *out = v;
    return 0;
}

bld_engine *bld_open(const char *data_js, size_t data_n, const char *builder_js, size_t builder_n, const char *pre_js,
                     char *err, size_t errn) {
    if (!data_js || !builder_js) { snprintf(err, errn, "the wiki builder files are missing"); return NULL; }
    bld_engine *e = calloc(1, sizeof *e);
    if (!e) { snprintf(err, errn, "out of memory"); return NULL; }
    e->deadline_ms = 20000;
    e->rt = JS_NewRuntime();
    if (!e->rt) { free(e); snprintf(err, errn, "QuickJS runtime failed"); return NULL; }
    JS_SetMemoryLimit(e->rt, (size_t)512 << 20);
    JS_SetInterruptHandler(e->rt, on_interrupt, e);
    e->ctx = JS_NewContext(e->rt);
    if (!e->ctx) { JS_FreeRuntime(e->rt); free(e); snprintf(err, errn, "QuickJS context failed"); return NULL; }
    static const char win_on[] = "var window = global;", win_off[] = "window = undefined;";
    if ((pre_js && run_script(e, pre_js, strlen(pre_js), "pre.js", err, errn)) ||
        run_script(e, (const char *)sg_prelude_js, sg_prelude_js_len, "sg_prelude.js", err, errn) ||
        run_script(e, win_on, strlen(win_on), "<window>", err, errn) ||
        run_script(e, data_js, data_n, "data.js", err, errn) ||
        run_script(e, win_off, strlen(win_off), "<window>", err, errn) ||
        run_script(e, builder_js, builder_n, "builder.js", err, errn)) { bld_close(e); return NULL; }
    JSValue g = JS_GetGlobalObject(e->ctx);                    /* bld_glue.js lifts the build rules out of this text */
    JS_SetPropertyStr(e->ctx, g, "__BLD_BUILDER_SRC", JS_NewStringLen(e->ctx, builder_js, builder_n));
    JS_FreeValue(e->ctx, g);
    if (run_script(e, (const char *)bld_glue_js, bld_glue_js_len, "bld_glue.js", err, errn)) { bld_close(e); return NULL; }
    return e;
}

void bld_close(bld_engine *e) {
    if (!e) return;
    if (e->ctx) JS_FreeContext(e->ctx);
    if (e->rt) JS_FreeRuntime(e->rt);
    free(e);
}
void bld_set_deadline_ms(bld_engine *e, int ms) { if (e) e->deadline_ms = ms; }

char *bld_call(bld_engine *e, const char *fn, const char *arg_json, char *err, size_t errn) {
    if (!e) { snprintf(err, errn, "the Unit Builder is not running"); return NULL; }
    JSContext *c = e->ctx;
    JSValue g = JS_GetGlobalObject(c), bld = JS_GetPropertyStr(c, g, "BLD"), f = JS_GetPropertyStr(c, bld, fn);
    JSValue a = JS_UNDEFINED, v = JS_UNDEFINED;
    char *out = NULL;
    arm(e);
    if (arg_json) a = JS_ParseJSON(c, arg_json, strlen(arg_json), "<arg>");
    if (JS_IsException(a)) take_exc(e, fn, err, errn);
    else if (!JS_IsFunction(c, f)) snprintf(err, errn, "BLD.%s missing", fn);
    else if (!settle(e, JS_Call(c, f, bld, arg_json ? 1 : 0, &a), &v, fn, err, errn)) {
        JSValue s = JS_JSONStringify(c, v, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(s)) take_exc(e, fn, err, errn);
        else {
            size_t n = 0; const char *t = JS_IsString(s) ? JS_ToCStringLen(c, &n, s) : NULL;
            if (t) { out = malloc(n + 1); if (out) { memcpy(out, t, n); out[n] = 0; } JS_FreeCString(c, t); }
            else snprintf(err, errn, "%s: no result", fn);
        }
        JS_FreeValue(c, s);
    }
    disarm(e);
    JS_FreeValue(c, v); JS_FreeValue(c, a); JS_FreeValue(c, f); JS_FreeValue(c, bld); JS_FreeValue(c, g);
    return out;
}
