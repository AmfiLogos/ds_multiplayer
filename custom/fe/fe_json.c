/* fe_json: see fe_json.h */
#include "fe_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- build */
static jv *mk(int t) { jv *v = calloc(1, sizeof *v); v->t = (uint8_t)t; return v; }
jv *jnull(void) { return mk(JN_NULL); }
jv *jbool(int b) { return mk(b ? JN_TRUE : JN_FALSE); }
static jv *mktext(int t, const char *s, size_t n) { jv *v = mk(t); v->s = malloc(n + 1); memcpy(v->s, s, n); v->s[n] = 0; v->n = n; return v; }
jv *jstrv(const char *raw) { return mktext(JN_STR, raw, strlen(raw)); }
jv *jnum_i(long long i) { char b[32]; int n = snprintf(b, sizeof b, "%lld", i); return mktext(JN_NUM, b, (size_t)n); }
jv *jnum_d(double d) {
    char b[40]; int n;
    if (d == (double)(long long)d && d > -1e15 && d < 1e15) n = snprintf(b, sizeof b, "%.1f", d);
    else n = snprintf(b, sizeof b, "%.9g", d);
    return mktext(JN_NUM, b, (size_t)n);
}
jv *jarr(void) { return mk(JN_ARR); }
jv *jobj(void) { return mk(JN_OBJ); }

static void grow(jv *v) {
    if (v->cnt < v->cap) return;
    v->cap = v->cap ? v->cap * 2 : 4;
    v->v = realloc(v->v, v->cap * sizeof *v->v);
    if (v->t == JN_OBJ) { v->k = realloc(v->k, v->cap * sizeof *v->k); v->kn = realloc(v->kn, v->cap * sizeof *v->kn); }
}
void jpush(jv *a, jv *val) { grow(a); a->v[a->cnt++] = val; }
void jarr_del(jv *a, size_t i) {
    if (i >= a->cnt) return;
    jfree(a->v[i]);
    memmove(a->v + i, a->v + i + 1, (a->cnt - i - 1) * sizeof *a->v);
    a->cnt--;
}

void jfree(jv *v) {
    if (!v) return;
    free(v->s);
    for (size_t i = 0; i < v->cnt; i++) { jfree(v->v[i]); if (v->k) free(v->k[i]); }
    free(v->v); free(v->k); free(v->kn); free(v->ix);
    free(v);
}

jv *jcopy(const jv *v) {
    if (!v) return NULL;
    jv *c = mk(v->t);
    if (v->s) { c->s = malloc(v->n + 1); memcpy(c->s, v->s, v->n + 1); c->n = v->n; }
    if (v->cnt) {
        c->cap = c->cnt = v->cnt;
        c->v = malloc(c->cap * sizeof *c->v);
        if (v->t == JN_OBJ) { c->k = malloc(c->cap * sizeof *c->k); c->kn = malloc(c->cap * sizeof *c->kn); }
        for (size_t i = 0; i < v->cnt; i++) {
            c->v[i] = jcopy(v->v[i]);
            if (v->t == JN_OBJ) { c->kn[i] = v->kn[i]; c->k[i] = malloc(v->kn[i] + 1); memcpy(c->k[i], v->k[i], v->kn[i] + 1); }
        }
    }
    return c;
}

/* ---------------------------------------------------------------- object index */
static uint64_t fnv(const char *s, size_t n) { uint64_t h = 0xcbf29ce484222325ULL; for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001B3ULL; } return h; }
static void ix_build(jv *o) {
    free(o->ix); o->ix = NULL; o->ixcap = 0;
    if (o->cnt < 16) return;
    size_t cap = 32; while (cap < o->cnt * 2) cap *= 2;
    o->ix = calloc(cap, sizeof *o->ix); o->ixcap = cap;
    for (size_t i = 0; i < o->cnt; i++) {
        size_t h = fnv(o->k[i], o->kn[i]) & (cap - 1);
        while (o->ix[h]) h = (h + 1) & (cap - 1);
        o->ix[h] = (uint32_t)i + 1;
    }
}
static long find(const jv *o, const char *key) {
    if (!o || o->t != JN_OBJ) return -1;
    size_t kl = strlen(key);
    if (o->cnt >= 16) {
        jv *m = (jv *)o;
        if (!m->ix) ix_build(m);
        size_t h = fnv(key, kl) & (m->ixcap - 1);
        for (; m->ix[h]; h = (h + 1) & (m->ixcap - 1)) {
            size_t i = m->ix[h] - 1;
            if (m->kn[i] == kl && !memcmp(m->k[i], key, kl)) return (long)i;
        }
        return -1;
    }
    for (size_t i = 0; i < o->cnt; i++) if (o->kn[i] == kl && !memcmp(o->k[i], key, kl)) return (long)i;
    return -1;
}
jv *jget(const jv *o, const char *key) { long i = find(o, key); return i < 0 ? NULL : o->v[i]; }
jv *jpath(const jv *o, const char *dotted) {
    char buf[256]; snprintf(buf, sizeof buf, "%s", dotted);
    const jv *cur = o;
    char *sp = NULL;
    for (char *t = strtok_r(buf, ".", &sp); t && cur; t = strtok_r(NULL, ".", &sp)) cur = jget(cur, t);
    return (jv *)cur;
}
int jset(jv *o, const char *key, jv *val) {
    if (!o || o->t != JN_OBJ) { jfree(val); return -1; }
    long i = find(o, key);
    if (i >= 0) { jfree(o->v[i]); o->v[i] = val; return 0; }
    grow(o);
    size_t kl = strlen(key);
    o->k[o->cnt] = malloc(kl + 1); memcpy(o->k[o->cnt], key, kl + 1); o->kn[o->cnt] = kl;
    o->v[o->cnt++] = val;
    if (o->ix) {
        if (o->cnt * 2 > o->ixcap) ix_build(o);
        else { size_t h = fnv(key, kl) & (o->ixcap - 1); while (o->ix[h]) h = (h + 1) & (o->ixcap - 1); o->ix[h] = (uint32_t)o->cnt; }
    }
    return 0;
}
jv *jtake(jv *o, const char *key) {
    long i = find(o, key); if (i < 0) return NULL;
    jv *v = o->v[i];
    free(o->k[i]);
    memmove(o->v + i, o->v + i + 1, (o->cnt - i - 1) * sizeof *o->v);
    memmove(o->k + i, o->k + i + 1, (o->cnt - i - 1) * sizeof *o->k);
    memmove(o->kn + i, o->kn + i + 1, (o->cnt - i - 1) * sizeof *o->kn);
    o->cnt--;
    free(o->ix); o->ix = NULL; o->ixcap = 0;
    return v;
}
int jdel(jv *o, const char *key) { jv *v = jtake(o, key); if (!v) return 0; jfree(v); return 1; }

int jis(const jv *v, int t) { return v && v->t == t; }
int jstreq(const jv *v, const char *s) { return v && v->t == JN_STR && v->n == strlen(s) && !memcmp(v->s, s, v->n); }
double jnumv(const jv *v) { return v && v->t == JN_NUM ? strtod(v->s, NULL) : 0; }
int jtruthy(const jv *v) {
    if (!v) return 0;
    switch (v->t) { case JN_TRUE: return 1; case JN_NUM: return jnumv(v) != 0; case JN_STR: return v->n > 0; case JN_ARR: case JN_OBJ: return v->cnt > 0; }
    return 0;
}
char *jstrdup_raw(const jv *v) { return strdup(v && v->t == JN_STR ? v->s : ""); }

/* ---------------------------------------------------------------- parse */
typedef struct { const char *p, *e; char *err; size_t errn; int depth; } P;
static void ws(P *p) { while (p->p < p->e && (*p->p == ' ' || *p->p == '\n' || *p->p == '\r' || *p->p == '\t')) p->p++; }
static int fail(P *p, const char *m) { if (p->err && !p->err[0]) snprintf(p->err, p->errn, "%s at byte %ld", m, (long)(p->p - (p->e - (p->e - p->p)))); return 0; }
static int rawstr(P *p, const char **s, size_t *n) {
    if (p->p >= p->e || *p->p != '"') return fail(p, "string expected");
    const char *b = ++p->p;
    while (p->p < p->e && *p->p != '"') { if (*p->p == '\\') p->p++; p->p++; }
    if (p->p >= p->e) return fail(p, "unterminated string");
    *s = b; *n = (size_t)(p->p - b); p->p++;
    return 1;
}
static jv *val(P *p) {
    ws(p);
    if (p->p >= p->e) { fail(p, "value expected"); return NULL; }
    if (++p->depth > 200) { fail(p, "too deep"); return NULL; }
    jv *r = NULL;
    char c = *p->p;
    if (c == '{') {
        r = mk(JN_OBJ); p->p++; ws(p);
        if (p->p < p->e && *p->p == '}') { p->p++; goto done; }
        for (;;) {
            const char *ks; size_t kn; ws(p);
            if (!rawstr(p, &ks, &kn)) goto bad;
            ws(p); if (p->p >= p->e || *p->p != ':') { fail(p, "':' expected"); goto bad; }
            p->p++;
            jv *x = val(p); if (!x) goto bad;
            grow(r); r->k[r->cnt] = malloc(kn + 1); memcpy(r->k[r->cnt], ks, kn); r->k[r->cnt][kn] = 0; r->kn[r->cnt] = kn; r->v[r->cnt++] = x;
            ws(p);
            if (p->p < p->e && *p->p == ',') { p->p++; continue; }
            if (p->p < p->e && *p->p == '}') { p->p++; break; }
            fail(p, "',' or '}' expected"); goto bad;
        }
    } else if (c == '[') {
        r = mk(JN_ARR); p->p++; ws(p);
        if (p->p < p->e && *p->p == ']') { p->p++; goto done; }
        for (;;) {
            jv *x = val(p); if (!x) goto bad;
            jpush(r, x); ws(p);
            if (p->p < p->e && *p->p == ',') { p->p++; continue; }
            if (p->p < p->e && *p->p == ']') { p->p++; break; }
            fail(p, "',' or ']' expected"); goto bad;
        }
    } else if (c == '"') {
        const char *s; size_t n; if (!rawstr(p, &s, &n)) return NULL;
        r = mktext(JN_STR, s, n);
    } else if (c == 't' && p->e - p->p >= 4 && !memcmp(p->p, "true", 4)) { p->p += 4; r = mk(JN_TRUE); }
    else if (c == 'f' && p->e - p->p >= 5 && !memcmp(p->p, "false", 5)) { p->p += 5; r = mk(JN_FALSE); }
    else if (c == 'n' && p->e - p->p >= 4 && !memcmp(p->p, "null", 4)) { p->p += 4; r = mk(JN_NULL); }
    else if (c == '-' || (c >= '0' && c <= '9')) {
        const char *b = p->p;
        while (p->p < p->e && strchr("+-0123456789.eE", *p->p)) p->p++;
        r = mktext(JN_NUM, b, (size_t)(p->p - b));
    } else { fail(p, "bad value"); return NULL; }
done:
    p->depth--;
    return r;
bad:
    jfree(r);
    return NULL;
}
jv *jparse(const char *text, size_t len, char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    P p = { text, text + len, err, errn, 0 };
    if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) p.p += 3;
    jv *v = val(&p);
    if (v) { ws(&p); if (p.p != p.e) { fail(&p, "trailing data"); jfree(v); return NULL; } }
    return v;
}
jv *jload(const char *path, char *err, size_t errn) {
    FILE *f = fopen(path, "rb");
    if (!f) { if (err) snprintf(err, errn, "cannot open %s", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f); fclose(f);
    jv *v = got == (size_t)n ? jparse(b, (size_t)n, err, errn) : NULL;
    free(b);
    return v;
}

/* ---------------------------------------------------------------- write */
typedef struct { char *p; size_t n, cap; } W;
static void wput(W *w, const char *s, size_t n) {
    if (w->n + n + 1 > w->cap) { while (w->n + n + 1 > w->cap) w->cap = w->cap ? w->cap * 2 : 1 << 16; w->p = realloc(w->p, w->cap); }
    memcpy(w->p + w->n, s, n); w->n += n; w->p[w->n] = 0;
}
static void wnl(W *w, int indent, int depth) { if (!indent) return; wput(w, "\n", 1); for (int i = 0; i < indent * depth; i++) wput(w, " ", 1); }
static int kcmp_ctx_n;
static const jv *kcmp_o;
static int kcmp(const void *a, const void *b) {
    size_t i = *(const size_t *)a, j = *(const size_t *)b;
    int r = strcmp(kcmp_o->k[i], kcmp_o->k[j]);
    return r;
}
static void wval(W *w, const jv *v, int indent, int depth, int sorted) {
    switch (v->t) {
    case JN_NULL: wput(w, "null", 4); break;
    case JN_TRUE: wput(w, "true", 4); break;
    case JN_FALSE: wput(w, "false", 5); break;
    case JN_NUM: wput(w, v->s, v->n); break;
    case JN_STR: wput(w, "\"", 1); wput(w, v->s, v->n); wput(w, "\"", 1); break;
    case JN_ARR:
        wput(w, "[", 1);
        for (size_t i = 0; i < v->cnt; i++) { if (i) wput(w, ",", 1); wnl(w, indent, depth + 1); wval(w, v->v[i], indent, depth + 1, sorted); }
        if (v->cnt) wnl(w, indent, depth);
        wput(w, "]", 1); break;
    case JN_OBJ: {
        size_t *ord = malloc((v->cnt + 1) * sizeof *ord);
        for (size_t i = 0; i < v->cnt; i++) ord[i] = i;
        if (sorted) { kcmp_o = v; qsort(ord, v->cnt, sizeof *ord, kcmp); }
        wput(w, "{", 1);
        for (size_t x = 0; x < v->cnt; x++) {
            size_t i = ord[x];
            if (x) wput(w, ",", 1);
            wnl(w, indent, depth + 1);
            wput(w, "\"", 1); wput(w, v->k[i], v->kn[i]); wput(w, indent ? "\": " : "\":", indent ? 3 : 2);
            wval(w, v->v[i], indent, depth + 1, sorted);
        }
        if (v->cnt) wnl(w, indent, depth);
        wput(w, "}", 1);
        free(ord); break;
    }
    }
    (void)kcmp_ctx_n;
}
char *jdump(const jv *v, int indent, size_t *len) { W w = { 0 }; wval(&w, v, indent, 0, 0); if (len) *len = w.n; return w.p; }
char *jcanon(const jv *v, size_t *len) { W w = { 0 }; wval(&w, v, 0, 0, 1); if (len) *len = w.n; return w.p; }
uint64_t jhash(const jv *v) { size_t n; char *s = jcanon(v, &n); uint64_t h = fnv(s, n); free(s); return h; }
int jequal(const jv *a, const jv *b) {
    if (!a || !b) return a == b;
    if (a->t != b->t) return 0;
    switch (a->t) {
    case JN_NUM: return jnumv(a) == jnumv(b);
    case JN_STR: return a->n == b->n && !memcmp(a->s, b->s, a->n);
    case JN_ARR:
        if (a->cnt != b->cnt) return 0;
        for (size_t i = 0; i < a->cnt; i++) if (!jequal(a->v[i], b->v[i])) return 0;
        return 1;
    case JN_OBJ:
        if (a->cnt != b->cnt) return 0;
        for (size_t i = 0; i < a->cnt; i++) { jv *o = jget(b, a->k[i]); if (!o || !jequal(a->v[i], o)) return 0; }
        return 1;
    }
    return 1;
}
int jsave_new(const jv *v, const char *path) {
    struct stat st; if (stat(path, &st) == 0) return -1;
    size_t n; char *s = jdump(v, 2, &n);
    char tmp[2048]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) { free(s); return -1; }
    size_t w = fwrite(s, 1, n, f); int e = fclose(f); free(s);
    if (w != n || e) { unlink(tmp); return -1; }
    return rename(tmp, path) ? -1 : 0;
}
