/* sg_js - the wiki's save generator in QuickJS (see sg_js.h). */
#include "sg_js.h"
#include "quickjs/quickjs.h"
#include "tinf/tinf.h"
#include "sg_embed.h"          /* sg_embed.c: sg_prelude_js[], sg_glue_js[] (+ _len) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct sg_engine {
    JSRuntime *rt;
    JSContext *ctx;
    sg_template_fn tpl; void *tpl_user;
    int deadline_ms;
    double t_end;               /* monotonic seconds; 0 = no deadline running */
    int timed_out;
};

static double now_s(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static int on_interrupt(JSRuntime *rt, void *u) {
    (void)rt; sg_engine *e = u;
    if (e->t_end > 0 && now_s() > e->t_end) { e->timed_out = 1; return 1; }
    return 0;
}

/* the pending exception as text ("Error: ... \n stack") */
static void take_exc(sg_engine *e, const char *what, char *err, size_t errn) {
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

static void arm(sg_engine *e) { e->timed_out = 0; e->t_end = e->deadline_ms > 0 ? now_s() + e->deadline_ms / 1000.0 : 0; }
static void disarm(sg_engine *e) { e->t_end = 0; }

static int run_script(sg_engine *e, const char *src, size_t n, const char *name, char *err, size_t errn) {
    /* JS_Eval wants a NUL at src[n] */
    char *buf = malloc(n + 1);
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

/* ---- natives for sg_prelude.js */
static JSValue js_template(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    sg_engine *e = JS_GetContextOpaque(c);
    const char *file = JS_ToCString(c, argv[0]);
    if (!file) return JS_EXCEPTION;
    char *text = NULL; size_t len = 0; char er[300] = "";
    int rc = e->tpl ? e->tpl(e->tpl_user, file, &text, &len, er, sizeof er) : (snprintf(er, sizeof er, "no template source"), -1);
    JS_FreeCString(c, file);
    if (rc || !text) { free(text); return JS_ThrowInternalError(c, "template: %s", er); }
    JSValue s = JS_NewStringLen(c, text, len);
    free(text);
    return s;
}

unsigned char *sg_inflate_raw(const unsigned char *in, size_t n, size_t *out_n) {
    size_t cap = n * 4 + 1024;
    for (int k = 0; k < 8; k++, cap *= 4) {
        unsigned char *o = malloc(cap);
        if (!o) return NULL;
        unsigned int dl = (unsigned int)cap;
        int r = tinf_uncompress(o, &dl, in, (unsigned int)n);
        if (r == TINF_OK) { *out_n = dl; return o; }
        free(o);
        if (r != TINF_BUF_ERROR) return NULL;
        if (cap > (64u << 20)) return NULL;
    }
    return NULL;
}

static JSValue js_inflate(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    /* latin1 in -> latin1 out (one char per byte); links are small */
    uint32_t len;
    JSValue lv = JS_GetPropertyStr(c, argv[0], "length");
    if (JS_ToUint32(c, &len, lv)) { JS_FreeValue(c, lv); return JS_EXCEPTION; }
    JS_FreeValue(c, lv);
    unsigned char *in = malloc(len ? len : 1);
    if (!in) return JS_ThrowOutOfMemory(c);
    JSValue code_at = JS_GetPropertyStr(c, argv[0], "charCodeAt");
    for (uint32_t i = 0; i < len; i++) {
        JSValue a = JS_NewUint32(c, i), ch = JS_Call(c, code_at, argv[0], 1, &a);
        int32_t v = 0; JS_ToInt32(c, &v, ch); JS_FreeValue(c, ch);
        in[i] = (unsigned char)v;
    }
    JS_FreeValue(c, code_at);
    size_t on = 0;
    unsigned char *out = sg_inflate_raw(in, len, &on);
    free(in);
    if (!out) return JS_ThrowTypeError(c, "Decompression failed");
    /* bytes -> JS string of code units 0..255: build through a UTF-8 encoding of each byte */
    char *u8 = malloc(on * 2 + 1); size_t k = 0;
    if (!u8) { free(out); return JS_ThrowOutOfMemory(c); }
    for (size_t i = 0; i < on; i++) {
        unsigned b = out[i];
        if (b < 0x80) u8[k++] = (char)b;
        else { u8[k++] = (char)(0xC0 | (b >> 6)); u8[k++] = (char)(0x80 | (b & 0x3F)); }
    }
    free(out);
    JSValue s = JS_NewStringLen(c, u8, k);
    free(u8);
    return s;
}

/* run a JS expression that yields a Promise (or a value); settle it; return the value (caller frees) */
static int settle(sg_engine *e, JSValue p, JSValue *out, const char *what, char *err, size_t errn) {
    JSContext *c = e->ctx;
    if (JS_IsException(p)) { take_exc(e, what, err, errn); return -1; }
    if (!JS_IsObject(p) || (int)JS_PromiseState(c, p) < 0) { *out = p; return 0; }   /* not a promise */
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
        const char *m = JS_ToCString(c, v);
        JSValue mv = JS_IsObject(v) ? JS_GetPropertyStr(c, v, "message") : JS_UNDEFINED;
        const char *mm = JS_IsString(mv) ? JS_ToCString(c, mv) : NULL;
        snprintf(err, errn, "%s", mm ? mm : (m ? m : "failed"));
        if (mm) JS_FreeCString(c, mm);
        if (m) JS_FreeCString(c, m);
        JS_FreeValue(c, mv); JS_FreeValue(c, v);
        return -1;
    }
    *out = v;
    return 0;
}

/* call CMF.<fn>(arg) where arg is a JS string (parsed with JSON.parse when json != 0) */
static int call_cmf(sg_engine *e, const char *fn, const char *arg, int json, JSValue *out, char *err, size_t errn) {
    JSContext *c = e->ctx;
    JSValue g = JS_GetGlobalObject(c), cmf = JS_GetPropertyStr(c, g, "CMF"), f = JS_GetPropertyStr(c, cmf, fn);
    JSValue a = arg ? JS_NewString(c, arg) : JS_UNDEFINED;
    if (json && arg) {
        JSValue pa = JS_ParseJSON(c, arg, strlen(arg), "<opts>");
        JS_FreeValue(c, a);
        a = pa;
    }
    int rc;
    arm(e);
    if (JS_IsException(a)) { take_exc(e, fn, err, errn); rc = -1; }
    else if (!JS_IsFunction(c, f)) { snprintf(err, errn, "CMF.%s missing", fn); rc = -1; }
    else rc = settle(e, JS_Call(c, f, cmf, arg ? 1 : 0, &a), out, fn, err, errn);
    disarm(e);
    JS_FreeValue(c, a); JS_FreeValue(c, f); JS_FreeValue(c, cmf); JS_FreeValue(c, g);
    return rc;
}

static char *dup_str(JSContext *c, JSValueConst v, size_t *n) {
    size_t len = 0;
    const char *s = JS_ToCStringLen(c, &len, v);
    if (!s) return NULL;
    char *o = malloc(len + 1);
    if (o) { memcpy(o, s, len); o[len] = 0; if (n) *n = len; }
    JS_FreeCString(c, s);
    return o;
}
static char *json_of(sg_engine *e, JSValueConst v) {
    JSValue s = JS_JSONStringify(e->ctx, v, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(s)) { JS_FreeValue(e->ctx, JS_GetException(e->ctx)); return NULL; }
    char *o = dup_str(e->ctx, s, NULL);
    JS_FreeValue(e->ctx, s);
    return o;
}

/* sg_glue.js lifts the panel's helpers out of the builder source (see there) */
static int set_builder_src(sg_engine *e, const sg_sources *src) {
    JSValue g = JS_GetGlobalObject(e->ctx);
    JS_SetPropertyStr(e->ctx, g, "__CM_BUILDER_SRC", JS_NewStringLen(e->ctx, src->builder_js, src->builder_n));
    JS_FreeValue(e->ctx, g);
    return 0;
}

sg_engine *sg_open(const sg_sources *src, sg_template_fn tpl, void *tpl_user, const sg_det *det, char *err, size_t errn) {
    if (!src || !src->data_js || !src->builder_js || !src->savegen_js) { snprintf(err, errn, "the wiki scripts are missing"); return NULL; }
    sg_engine *e = calloc(1, sizeof *e);
    if (!e) { snprintf(err, errn, "out of memory"); return NULL; }
    e->tpl = tpl; e->tpl_user = tpl_user; e->deadline_ms = 60000;
    e->rt = JS_NewRuntime();
    if (!e->rt) { free(e); snprintf(err, errn, "QuickJS runtime failed"); return NULL; }
    JS_SetMemoryLimit(e->rt, (size_t)1 << 30);
    JS_SetInterruptHandler(e->rt, on_interrupt, e);
    e->ctx = JS_NewContext(e->rt);
    if (!e->ctx) { JS_FreeRuntime(e->rt); free(e); snprintf(err, errn, "QuickJS context failed"); return NULL; }
    JS_SetContextOpaque(e->ctx, e);
    JSValue g = JS_GetGlobalObject(e->ctx);
    JS_SetPropertyStr(e->ctx, g, "__cm_template", JS_NewCFunction(e->ctx, js_template, "__cm_template", 1));
    JS_SetPropertyStr(e->ctx, g, "__cm_inflate", JS_NewCFunction(e->ctx, js_inflate, "__cm_inflate", 1));
    if (det) {
        JS_SetPropertyStr(e->ctx, g, "__DET_SEED", JS_NewUint32(e->ctx, det->seed));
        JS_SetPropertyStr(e->ctx, g, "__DET_NOW", JS_NewFloat64(e->ctx, det->now_ms));
    }
    JS_FreeValue(e->ctx, g);
    static const char win_on[] = "var window = global;", win_off[] = "window = undefined;";
    if ((det && det->js && run_script(e, det->js, strlen(det->js), "det_prelude.js", err, errn)) ||
        run_script(e, (const char *)sg_prelude_js, sg_prelude_js_len, "sg_prelude.js", err, errn) ||
        /* the order and the window trick of the wiki's own node harness (pipeline/savegen/holders_harness.js) */
        run_script(e, win_on, strlen(win_on), "<window>", err, errn) ||
        run_script(e, src->data_js, src->data_n, "data.js", err, errn) ||
        run_script(e, win_off, strlen(win_off), "<window>", err, errn) ||
        run_script(e, src->builder_js, src->builder_n, "builder.js", err, errn) ||
        run_script(e, src->savegen_js, src->savegen_n, "savegen.js", err, errn) ||
        set_builder_src(e, src) ||
        run_script(e, (const char *)sg_glue_js, sg_glue_js_len, "sg_glue.js", err, errn)) {
        sg_close(e);
        return NULL;
    }
    return e;
}

void sg_close(sg_engine *e) {
    if (!e) return;
    if (e->ctx) JS_FreeContext(e->ctx);
    if (e->rt) JS_FreeRuntime(e->rt);
    free(e);
}

void sg_set_deadline_ms(sg_engine *e, int ms) { if (e) e->deadline_ms = ms; }

char *sg_panel(sg_engine *e, char *err, size_t errn) {
    JSValue v;
    if (call_cmf(e, "panel", NULL, 0, &v, err, errn)) return NULL;
    char *o = json_of(e, v);
    JS_FreeValue(e->ctx, v);
    if (!o) snprintf(err, errn, "panel: not JSON");
    return o;
}

int sg_generate(sg_engine *e, const char *opts_json, sg_result *r, char *err, size_t errn) {
    memset(r, 0, sizeof *r);
    JSValue v;
    if (call_cmf(e, "generate", opts_json, 1, &v, err, errn)) return -1;
    JSContext *c = e->ctx;
    JSValue f = JS_GetPropertyStr(c, v, "file"), n = JS_GetPropertyStr(c, v, "filename"), p = JS_GetPropertyStr(c, v, "problems");
    r->file = dup_str(c, f, &r->file_n);
    r->filename = dup_str(c, n, NULL);
    uint32_t np = 0;
    JSValue pl = JS_GetPropertyStr(c, p, "length"); JS_ToUint32(c, &np, pl); JS_FreeValue(c, pl);
    r->problems = (int)np;
    JSAtom at = JS_NewAtom(c, "file");                  /* meta = the rest of the result */
    JS_DeleteProperty(c, v, at, 0);
    JS_FreeAtom(c, at);
    r->meta = json_of(e, v);
    JS_FreeValue(c, f); JS_FreeValue(c, n); JS_FreeValue(c, p); JS_FreeValue(c, v);
    if (!r->file || !r->filename || !r->meta) { sg_result_free(r); snprintf(err, errn, "generate: bad result"); return -1; }
    return 0;
}

void sg_result_free(sg_result *r) {
    if (!r) return;
    free(r->file); free(r->filename); free(r->meta);
    memset(r, 0, sizeof *r);
}

char *sg_decode_blob(sg_engine *e, const char *blob, char *err, size_t errn) {
    JSValue v;
    if (call_cmf(e, "decode", blob, 0, &v, err, errn)) return NULL;
    char *o = json_of(e, v);
    JS_FreeValue(e->ctx, v);
    if (!o) snprintf(err, errn, "decode: not JSON");
    return o;
}

int sg_template_file(sg_engine *e, const char *mode, const char *boss, char *out, size_t n) {
    JSContext *c = e->ctx;
    JSValue g = JS_GetGlobalObject(c), cmf = JS_GetPropertyStr(c, g, "CMF");
    JSValue f = JS_GetPropertyStr(c, cmf, "templateFor"), u = JS_GetPropertyStr(c, cmf, "templateUrl");
    JSValue a[2] = { JS_NewString(c, mode ? mode : ""), boss ? JS_NewString(c, boss) : JS_UNDEFINED };
    JSValue name = JS_Call(c, f, cmf, 2, a), url = JS_IsException(name) ? JS_EXCEPTION : JS_Call(c, u, cmf, 1, &name);
    int rc = -1;
    if (!JS_IsException(url)) {
        const char *s = JS_ToCString(c, url);
        if (s) { const char *b = strrchr(s, '/'); snprintf(out, n, "%s", b ? b + 1 : s); JS_FreeCString(c, s); rc = 0; }
    } else JS_FreeValue(c, JS_GetException(c));
    JS_FreeValue(c, url); JS_FreeValue(c, name); JS_FreeValue(c, a[0]); JS_FreeValue(c, a[1]);
    JS_FreeValue(c, u); JS_FreeValue(c, f); JS_FreeValue(c, cmf); JS_FreeValue(c, g);
    return rc;
}
