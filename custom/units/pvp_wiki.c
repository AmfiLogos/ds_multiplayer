/* pvp_wiki - DS wiki builder team link decoder (see pvp_wiki.h). */
#include "pvp_wiki.h"
#include "pvp_wiki_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <zlib.h>

const char *wk_race_key(int i) { return i >= 0 && i < WK_RACE_N ? WK_RACE[i] : ""; }
const char *wk_item_key(int i) { return i >= 0 && i < WK_ITEM_N ? WK_ITEM[i] : ""; }
const char *wk_slot_name(int i) { return i >= 0 && i < WK_SLOT_N ? WK_SLOT[i] : ""; }

/* ---- base64url */
static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}
static uint8_t *b64url_dec(const char *s, size_t n, size_t *out_n) {
    uint8_t *o = malloc(n * 3 / 4 + 3); size_t k = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '=') break;
        int v = b64v((unsigned char)s[i]); if (v < 0) { free(o); return NULL; }
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; o[k++] = (uint8_t)(acc >> bits); }
    }
    *out_n = k; return o;
}
static uint8_t *inflate_raw(const uint8_t *in, size_t n, size_t *out_n) {
    size_t cap = n * 8 + 1024; uint8_t *o = malloc(cap);
    z_stream z; memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) { free(o); return NULL; }
    z.next_in = (Bytef *)in; z.avail_in = (uInt)n;
    int rc;
    do {
        if (z.total_out >= cap) { cap *= 2; if (cap > (1u << 20)) { inflateEnd(&z); free(o); return NULL; } o = realloc(o, cap); }
        z.next_out = o + z.total_out; z.avail_out = (uInt)(cap - z.total_out);
        rc = inflate(&z, Z_NO_FLUSH);
    } while (rc == Z_OK);
    size_t got = z.total_out; inflateEnd(&z);
    if (rc != Z_STREAM_END) { free(o); return NULL; }
    *out_n = got; return o;
}

/* ---- LEB128 reader (as builder.js DSBR, but reads past the end are reported) */
typedef struct { const uint8_t *b; size_t n, i; int over; } rd;
static uint32_t ru(rd *r) {
    uint32_t v = 0; int sh = 0; uint8_t x;
    do { x = r->i < r->n ? r->b[r->i] : 0; if (r->i >= r->n) r->over = 1; r->i++; if (sh < 32) v |= (uint32_t)(x & 0x7f) << sh; sh += 7; } while ((x & 0x80) && sh < 64);
    return v;
}
static int rbyte(rd *r) { if (r->i >= r->n) { r->over = 1; r->i++; return 0; } return r->b[r->i++]; }
static int rzz(rd *r) { uint32_t u = ru(r); return (u & 1) ? -(int)((u + 1) >> 1) : (int)(u >> 1); }

typedef struct { char *e; size_t n; int bad; } errs;
static void fail(errs *E, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(errs *E, const char *fmt, ...) {
    if (E->bad) return;
    E->bad = 1; va_list ap; va_start(ap, fmt); vsnprintf(E->e, E->n, fmt, ap); va_end(ap);
}
static void read_list(rd *r, errs *E, int *cnt, int *out, int table_n, const char *what) {
    uint32_t n = ru(r); *cnt = 0;
    if (n > WK_MAX_LIST) { fail(E, "%s: %u entries (max %d)", what, n, WK_MAX_LIST); return; }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t v = ru(r);
        if ((int)v >= table_n) { fail(E, "%s: index %u outside the table (%d)", what, v, table_n); return; }
        out[(*cnt)++] = (int)v;
    }
}
static int dec_build(rd *r, wk_build *b, errs *E) {
    memset(b, 0, sizeof *b); b->bg = -1; b->portrait = -1;
    for (int s = 0; s < WK_SLOTS; s++) b->eq[s] = -1;
    uint32_t race = ru(r); if ((int)race >= WK_RACE_N) { fail(E, "race index %u outside the table", race); return -1; }
    b->race = (int)race;
    uint32_t bg = ru(r); if (bg) { if ((int)bg - 1 >= WK_BG_N) { fail(E, "background index %u outside the table", bg - 1); return -1; } b->bg = (int)bg - 1; }
    b->lvl = (int)ru(r);
    if (b->lvl < 1 || b->lvl > 255) { fail(E, "level %d", b->lvl); return -1; }
    uint32_t tp = ru(r); int t6[6]; for (int i = 5; i >= 0; i--) { t6[i] = (int)(tp % 5); tp /= 5; }
    for (int i = 0; i < 6; i++) b->tal[i] = t6[i] < WK_TIER_N ? t6[i] : 1;
    uint32_t fl = ru(r);
    b->offer = fl & 3; b->roll = (fl >> 2) & 3; b->advanced = (fl >> 4) & 1; b->add_shield = (fl >> 5) & 1; b->hide_head = (fl >> 15) & 1;
    if (b->offer > 2) b->offer = 0;
    if (b->roll > 2) b->roll = 1;
    int hasName = (fl >> 6) & 1, hasPort = (fl >> 7) & 1, hasMan = (fl >> 8) & 1, hasMB = (fl >> 9) & 1, hasPStk = (fl >> 10) & 1, hasSouls = (fl >> 11) & 1,
        hasSI = (fl >> 12) & 1, hasSC = (fl >> 13) & 1, hasAcq = (fl >> 14) & 1, hasElix = (fl >> 16) & 1, hasCond = (fl >> 17) & 1;
    read_list(r, E, &b->ninds, b->inds, WK_IND_N, "traits");
    read_list(r, E, &b->ntrees, b->trees, WK_TREE_N, "skill trees");
    read_list(r, E, &b->nlearned, b->learned, WK_SKILL_N, "skills");
    read_list(r, E, &b->ninsc, b->insc, WK_INSC_N, "inscriptions");
    if (E->bad) return -1;
    uint32_t eqmask = ru(r);
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) if (eqmask >> s & 1) {
        uint32_t it = ru(r); if ((int)it >= WK_ITEM_N) { fail(E, "item index %u outside the table", it); return -1; }
        b->eq[s] = (int)it;
    }
    uint32_t tagmask = ru(r);
    for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) if (tagmask >> s & 1) {
        uint32_t n = ru(r); if (n > 8) { fail(E, "slot %s: %u tags", wk_slot_name(s), n); return -1; }
        for (uint32_t k = 0; k < n; k++) {
            int idx = (int)ru(r), frac = rbyte(r), it = b->eq[s];
            if (it < 0 || idx >= WK_ITEM_NTAGS[it]) continue;                      /* as the browser: a tag the item lacks is skipped */
            double lo = WK_TAG_RANGE[2 * (WK_ITEM_TAG_AT[it] + idx)], hi = WK_TAG_RANGE[2 * (WK_ITEM_TAG_AT[it] + idx) + 1];
            b->tag[s][b->ntag[s]].idx = idx; b->tag[s][b->ntag[s]].v = (float)(lo + (frac / 255.0) * (hi - lo)); b->ntag[s]++;
        }
    }
    if (hasName) {
        uint32_t len = ru(r); if (len > 48) { fail(E, "name of %u bytes", len); return -1; }
        for (uint32_t k = 0; k < len; k++) { int c = rbyte(r); if (k < sizeof b->name - 1) b->name[k] = (char)c; }
        b->name[len < sizeof b->name ? len : sizeof b->name - 1] = 0;
        for (char *p = b->name; *p; p++) if ((unsigned char)*p < 32 || *p == '"' || *p == '\\') *p = ' ';   /* no control / JSON-breaking bytes */
    }
    if (hasPort) { int pi = (int)ru(r); if (pi < WK_PORTRAITS_N[b->race]) b->portrait = pi; }
    if (hasMan) {
        uint32_t n = ru(r); if (n > WK_MAX_LIST) { fail(E, "%u stat overrides", n); return -1; }
        for (uint32_t k = 0; k < n; k++) { int si = (int)ru(r); float v = rzz(r) / 100.0f; if (si < WK_STAT_N) { b->man[b->nman].stat = si; b->man[b->nman].v = v; b->nman++; } }
    }
    if (hasMB) { b->has_mb = 1; for (int k = 0; k < 6; k++) b->mb[k] = rzz(r) / 100.0f; }
    if (hasPStk) {
        uint32_t n = ru(r); if (n > WK_MAX_LIST) { fail(E, "%u passive stacks", n); return -1; }
        for (uint32_t k = 0; k < n; k++) { int sk = (int)ru(r), st = (int)ru(r); if (sk < WK_SKILL_N) { b->pstk[b->npstk].skill = sk; b->pstk[b->npstk].st = st; b->npstk++; } }
    }
    if (hasSouls) read_list(r, E, &b->nsouls, b->souls, WK_SOUL_N, "souls");
    if (hasSI) read_list(r, E, &b->nsaved_inds, b->saved_inds, WK_IND_N, "saved traits");
    if (hasSC) read_list(r, E, &b->nsaved_insc, b->saved_insc, WK_INSC_N, "saved inscriptions");
    if (hasAcq) read_list(r, E, &b->nacq, b->acq, WK_ACQ_N, "acquired traits");
    if (hasElix) { b->has_elix = 1; uint32_t n = ru(r); for (uint32_t k = 0; k < n; k++) { int u = (int)ru(r); if (k < 6) b->elix[k] = u; } }
    if (hasCond) { uint32_t n = ru(r); for (uint32_t k = 0; k < n; k++) { int ci = (int)ru(r); if (k < 8 && (int)k < WK_COND_N && ci > 0 && ci <= WK_COND_OPTS[k]) b->cond[k] = ci; } b->ncond = WK_COND_N < 8 ? WK_COND_N : 8; }
    return E->bad ? -1 : 0;
}
static int parse_blob(const uint8_t *by, size_t n, wk_build *out, int max, int *fpm, errs *E) {
    if (n < 4) { fail(E, "link too short"); return -1; }
    rd r = { by, n, 0, 0 };
    int ver = rbyte(&r); if (ver != 1 && ver != 2) { fail(E, "unknown link version %d", ver); return -1; }
    int fp = rbyte(&r) << 8; fp |= rbyte(&r);
    uint32_t cnt = ru(&r);
    if (cnt < 1 || cnt > WK_MAX_UNITS || (int)cnt > max) { fail(E, "%u units in the link (1 to %d)", cnt, max < WK_MAX_UNITS ? max : WK_MAX_UNITS); return -1; }
    for (uint32_t i = 0; i < cnt; i++) if (dec_build(&r, &out[i], E)) return -1;
    if (r.over || r.i > n) { fail(E, "link cut short (corrupted or truncated)"); return -1; }
    if (fpm) *fpm = fp != WK_FP;
    return (int)cnt;
}
int wk_decode(const char *text, wk_build *out, int max, int *fp_mismatch, char *err, size_t errn) {
    errs E = { err, errn, 0 }; if (err && errn) err[0] = 0;
    if (!text) { fail(&E, "no link"); return -1; }
    const char *p = strstr(text, "team="), *s = p ? p + 5 : text;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    size_t n = 0; while (s[n] && s[n] != '&' && s[n] != '#' && s[n] != ' ' && s[n] != '\n' && s[n] != '\r') n++;
    if (!n) { fail(&E, "no team code in the link"); return -1; }
    size_t bn; uint8_t *by = b64url_dec(s, n, &bn);
    if (!by) { fail(&E, "the team code is not base64url"); return -1; }
    /* as the browser: raw (v1) first, then inflate (v2) */
    char e1[200]; errs E1 = { e1, sizeof e1, 0 };
    int k = parse_blob(by, bn, out, max, fp_mismatch, &E1);
    if (k < 0) {
        size_t in; uint8_t *raw = inflate_raw(by, bn, &in);
        if (raw) { k = parse_blob(raw, in, out, max, fp_mismatch, &E); free(raw); }
        else fail(&E, "%s", e1[0] ? e1 : "not a team link");
    }
    free(by);
    return k;
}

/* ---- JSON (key names) */
typedef struct { char *p; size_t n, cap; } sb;
static void sp(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sp(sb *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int need = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (b->n + (size_t)need + 1 > b->cap) { b->cap = (b->n + (size_t)need + 1) * 2; b->p = realloc(b->p, b->cap); }
    va_start(ap, fmt); vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap); va_end(ap); b->n += (size_t)need;
}
static void slist(sb *b, const char *key, const int *v, int n, const char *const *tab) {
    sp(b, ",\"%s\":[", key); for (int i = 0; i < n; i++) sp(b, "%s\"%s\"", i ? "," : "", tab[v[i]]); sp(b, "]");
}
char *wk_json(const wk_build *bs, int n) {
    sb b = { 0 }; sp(&b, "[");
    static const char *offer[] = { "Least", "Ordinary", "Plentiful" }, *roll[] = { "min", "avg", "max" };
    for (int u = 0; u < n; u++) {
        const wk_build *x = &bs[u];
        sp(&b, "%s{\"race\":\"%s\",\"bg\":", u ? "," : "", WK_RACE[x->race]);
        if (x->bg >= 0) sp(&b, "\"%s\"", WK_BG[x->bg]); else sp(&b, "null");
        sp(&b, ",\"lvl\":%d,\"tal\":{", x->lvl);
        for (int i = 0; i < 6; i++) sp(&b, "%s\"%s\":\"%s\"", i ? "," : "", WK_MAJ[i], WK_TIER[x->tal[i]]);
        sp(&b, "},\"offer\":\"%s\",\"roll\":\"%s\",\"advanced\":%s,\"addShield\":%s,\"hideHead\":%s", offer[x->offer], roll[x->roll],
           x->advanced ? "true" : "false", x->add_shield ? "true" : "false", x->hide_head ? "true" : "false");
        slist(&b, "inds", x->inds, x->ninds, WK_IND); slist(&b, "insctrees", x->trees, x->ntrees, WK_TREE);
        slist(&b, "learned", x->learned, x->nlearned, WK_SKILL); slist(&b, "insc", x->insc, x->ninsc, WK_INSC);
        sp(&b, ",\"eq\":{"); int f = 0;
        for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) if (x->eq[s] >= 0) sp(&b, "%s\"%s\":\"%s\"", f++ ? "," : "", WK_SLOT[s], WK_ITEM[x->eq[s]]);
        sp(&b, "},\"tags\":{"); f = 0;
        for (int s = 0; s < WK_SLOT_N && s < WK_SLOTS; s++) if (x->ntag[s]) {
            sp(&b, "%s\"%s\":[", f++ ? "," : "", WK_SLOT[s]);
            for (int k = 0; k < x->ntag[s]; k++) sp(&b, "%s{\"i\":%d,\"v\":%.4f}", k ? "," : "", x->tag[s][k].idx, x->tag[s][k].v);
            sp(&b, "]");
        }
        sp(&b, "},\"manual\":{");
        for (int k = 0; k < x->nman; k++) sp(&b, "%s\"%s\":%.2f", k ? "," : "", WK_STAT[x->man[k].stat], x->man[k].v);
        sp(&b, "}");
        if (x->name[0]) sp(&b, ",\"name\":\"%s\"", x->name);
        if (x->portrait >= 0 && WK_PORTRAITS[x->race]) sp(&b, ",\"portrait\":\"%s\"", WK_PORTRAITS[x->race][x->portrait]);
        if (x->has_mb) { sp(&b, ",\"majorBase\":{"); for (int k = 0; k < 6; k++) sp(&b, "%s\"%s\":%.2f", k ? "," : "", WK_MAJ[k], x->mb[k]); sp(&b, "}"); }
        if (x->npstk) { sp(&b, ",\"passiveStacks\":{"); for (int k = 0; k < x->npstk; k++) sp(&b, "%s\"%s\":%d", k ? "," : "", WK_SKILL[x->pstk[k].skill], x->pstk[k].st); sp(&b, "}"); }
        if (x->nsouls) slist(&b, "souls", x->souls, x->nsouls, WK_SOUL);
        if (x->nsaved_inds) slist(&b, "savedInds", x->saved_inds, x->nsaved_inds, WK_IND);
        if (x->nsaved_insc) slist(&b, "savedInsc", x->saved_insc, x->nsaved_insc, WK_INSC);
        if (x->nacq) slist(&b, "acqInds", x->acq, x->nacq, WK_ACQ);
        if (x->has_elix) { sp(&b, ",\"elix\":{"); int g2 = 0; for (int k = 0; k < 6; k++) if (x->elix[k] > 0) sp(&b, "%s\"%s\":%d", g2++ ? "," : "", WK_MAJ[k], x->elix[k]); sp(&b, "}"); }
        if (x->ncond) { sp(&b, ",\"cond\":["); int g2 = 0; for (int k = 0; k < x->ncond; k++) if (x->cond[k] > 0) sp(&b, "%s\"%s\"", g2++ ? "," : "", WK_COND[k][x->cond[k] - 1]); sp(&b, "]"); }
        sp(&b, "}");
    }
    sp(&b, "]");
    return b.p;
}
uint64_t wk_hash(const wk_build *b, int n) {
    char *j = wk_json(b, n); uint64_t h = 1469598103934665603ULL;
    for (char *p = j; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ULL; }
    free(j); return h;
}
