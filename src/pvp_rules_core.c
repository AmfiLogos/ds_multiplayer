/* pvp_rules_core - PvP rule table, values, the rules file (last used + presets). Pure C, no game calls. See pvp_rules.h. */
#include "pvp_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Order = the order of the file and of the host's order payload; ids never change (pvpfix reads them). Appending is fine. */
static const pvpr_def defs[] = {
    { "mode", 0, 0, 1 },              /* Fight / Capture the Flag */
    { "win", 1, 0, 2 },               /* best of 1 / 3 / 5 (CTF: captures to win) */
    { "round_time", 2, 0, 4 },        /* 60 / 120 / 180 / 300 / 480 s */
    { "pause", 0, 0, 1 },             /* no pause / planning pause */
    { "vanilla", -1, -1, 1 },         /* -1 = the mod's default (pvp_mods_default_vanilla) */
    { "map", 0, 0, PVP_MAP_N - 1 },
    { "floor", 0, 0, PVP_FLOOR_N - 1 },
    { "enemy_vision", 0, 0, 1 },
    { "enemy_windows", 0, 0, 1 },
    { "enemy_portraits", 0, 0, 1 },
    { "enemy_hpbars", 0, 0, 1 },
    { "enemy_status", 0, 0, 1 },
    { "enemy_viewrange", 0, 0, 1 },
};
#define NDEFS ((int)(sizeof defs / sizeof defs[0]))
_Static_assert(NDEFS <= PVPR_MAX, "PVPR_MAX too small");

int pvpr_count(void) { return NDEFS; }
const pvpr_def *pvpr_def_at(int i) { return i >= 0 && i < NDEFS ? &defs[i] : NULL; }
int pvpr_index(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < NDEFS; i++) if (!strcmp(defs[i].id, id)) return i;
    return -1;
}
void pvpr_defaults(pvpr_vals *v) { memset(v, 0, sizeof *v); for (int i = 0; i < NDEFS; i++) v->v[i] = defs[i].dflt; }
int pvpr_vals_get(const pvpr_vals *v, const char *id) { int i = pvpr_index(id); return i < 0 ? -1 : v->v[i]; }
int pvpr_vals_set(pvpr_vals *v, const char *id, int x) {
    int i = pvpr_index(id);
    if (i < 0 || x < defs[i].lo || x > defs[i].hi) return -1;
    v->v[i] = x; return 0;
}
int pvpr_vals_equal(const pvpr_vals *a, const pvpr_vals *b) {
    for (int i = 0; i < NDEFS; i++) if (a->v[i] != b->v[i]) return 0;
    return 1;
}
size_t pvpr_format(const pvpr_vals *v, char *out, size_t cap) {
    size_t o = 0;
    for (int i = 0; i < NDEFS; i++) {
        int k = snprintf(out + o, cap > o ? cap - o : 0, "%s=%d\n", defs[i].id, v->v[i]);
        if (k < 0 || o + (size_t)k >= cap) { if (cap) out[0] = 0; return 0; }
        o += (size_t)k;
    }
    return o;
}
/* one "id=value" line (no newline); 1 = set */
static int parse_line(pvpr_vals *v, const char *s, size_t n) {
    const char *eq = memchr(s, '=', n);
    if (!eq || eq == s) return 0;
    char id[48]; size_t il = (size_t)(eq - s);
    while (il && (s[il - 1] == ' ' || s[il - 1] == '\t')) il--;
    if (il >= sizeof id) return 0;
    memcpy(id, s, il); id[il] = 0;
    char num[24]; size_t nl = n - (size_t)(eq + 1 - s);
    const char *p = eq + 1; while (nl && (*p == ' ' || *p == '\t')) { p++; nl--; }
    while (nl && (p[nl - 1] == ' ' || p[nl - 1] == '\t' || p[nl - 1] == '\r')) nl--;
    if (!nl || nl >= sizeof num) return 0;
    memcpy(num, p, nl); num[nl] = 0;
    char *end; long x = strtol(num, &end, 10);
    if (*end || x < -1000 || x > 1000) return 0;
    return pvpr_vals_set(v, id, (int)x) == 0;
}
int pvpr_parse(pvpr_vals *v, const char *text, size_t n) {
    int k = 0; size_t i = 0;
    while (i < n) {
        size_t j = i; while (j < n && text[j] != '\n') j++;
        k += parse_line(v, text + i, j - i);
        i = j + 1;
    }
    return k;
}
uint64_t pvpr_hash(const pvpr_vals *v) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < NDEFS; i++) { uint32_t x = (uint32_t)v->v[i]; for (int b = 0; b < 4; b++) { h ^= (x >> (8 * b)) & 0xFF; h *= 1099511628211ULL; } }
    return h;
}

/* ---- names */
void pvpr_clean_name(const char *in, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0; if (!in) return;
    size_t lim = cap - 1 < PVPR_NAME_MAX ? cap - 1 : PVPR_NAME_MAX, o = 0;
    while (*in == ' ' || *in == '\t') in++;
    for (const unsigned char *p = (const unsigned char *)in; *p && o < lim; p++) {
        unsigned char ch = *p;
        if (ch < 0x20 || ch == 0x7F) ch = ' ';
        if (ch == ' ' && (o == 0 || out[o - 1] == ' ')) continue;      /* no double spaces */
        out[o++] = (char)ch;
    }
    out[o] = 0;
    /* never end inside a UTF-8 character: drop a trailing lead byte + its incomplete continuation bytes */
    size_t L = strlen(out), k = L;
    while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
    if (k > 0 && ((unsigned char)out[k - 1] & 0x80)) {
        unsigned char lead = (unsigned char)out[k - 1];
        size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
        if (L - (k - 1) < need) out[k - 1] = 0;
    }
    L = strlen(out); while (L && out[L - 1] == ' ') out[--L] = 0;
}

/* ---- store */
void pvpr_store_init(pvpr_store *s) { memset(s, 0, sizeof *s); pvpr_defaults(&s->last); }
size_t pvpr_store_text(const pvpr_store *s, char *out, size_t cap) {
    size_t o = 0; char b[1024];
    #define ADD(str) do { size_t l_ = strlen(str); if (o + l_ >= cap) { if (cap) out[0] = 0; return 0; } memcpy(out + o, str, l_); o += l_; out[o] = 0; } while (0)
    ADD("# PvP rules of the multiplayer mod: the last used rules, then the saved presets. Local to this computer.\n[last]\n");
    if (!pvpr_format(&s->last, b, sizeof b)) return 0;
    ADD(b);
    for (int i = 0; i < s->npresets; i++) {
        ADD("[preset]\nname="); ADD(s->presets[i].name); ADD("\n");
        if (!pvpr_format(&s->presets[i].v, b, sizeof b)) return 0;
        ADD(b);
    }
    #undef ADD
    return o;
}
int pvpr_store_read(pvpr_store *s, const char *text, size_t n) {
    pvpr_store_init(s);
    pvpr_vals *cur = NULL; int in_preset = 0; size_t i = 0;
    while (i < n) {
        size_t j = i; while (j < n && text[j] != '\n') j++;
        const char *l = text + i; size_t ln = j - i;
        while (ln && (l[ln - 1] == '\r' || l[ln - 1] == ' ')) ln--;
        if (ln >= 6 && !memcmp(l, "[last]", 6)) { cur = &s->last; in_preset = 0; }
        else if (ln >= 8 && !memcmp(l, "[preset]", 8)) {
            if (s->npresets < PVPR_PRESETS_MAX) { pvpr_preset *p = &s->presets[s->npresets++]; memset(p, 0, sizeof *p); pvpr_defaults(&p->v); cur = &p->v; in_preset = 1; }
            else { cur = NULL; in_preset = 0; }
        }
        else if (ln && l[0] == '#') {}
        else if (in_preset && ln >= 5 && !memcmp(l, "name=", 5)) {
            char raw[256]; size_t rl = ln - 5 < sizeof raw - 1 ? ln - 5 : sizeof raw - 1; memcpy(raw, l + 5, rl); raw[rl] = 0;
            pvpr_clean_name(raw, s->presets[s->npresets - 1].name, sizeof s->presets[0].name);
        }
        else if (cur) parse_line(cur, l, ln);
        i = j + 1;
    }
    /* drop presets without a name, and a second preset of the same name (the first one stays) */
    int k = 0;
    for (int a = 0; a < s->npresets; a++) {
        if (!s->presets[a].name[0]) continue;
        int dup = 0; for (int b = 0; b < k; b++) if (!strcmp(s->presets[b].name, s->presets[a].name)) dup = 1;
        if (dup) continue;
        if (k != a) s->presets[k] = s->presets[a];
        k++;
    }
    s->npresets = k;
    return 0;
}
int pvpr_store_load(pvpr_store *s, const char *path) {
    pvpr_store_init(s);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 1;
    static char b[64 * 1024]; size_t n = fread(b, 1, sizeof b - 1, f); int err = ferror(f); fclose(f);
    if (err) return -1;
    b[n] = 0;
    return pvpr_store_read(s, b, n);
}
int pvpr_store_save(const pvpr_store *s, const char *path) {
    if (!path || !*path) return -1;
    static char b[64 * 1024]; size_t n = pvpr_store_text(s, b, sizeof b);
    if (!n) return -1;
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    int bad = fwrite(b, 1, n, f) != n; bad |= fclose(f) != 0;
    if (bad) return -1;
    return rename(tmp, path) ? -1 : 0;
}
int pvpr_preset_find(const pvpr_store *s, const char *name) {
    char c[PVPR_NAME_MAX + 1]; pvpr_clean_name(name, c, sizeof c);
    for (int i = 0; i < s->npresets; i++) if (!strcmp(s->presets[i].name, c)) return i;
    return -1;
}
int pvpr_preset_save(pvpr_store *s, const char *name, const pvpr_vals *v) {
    char c[PVPR_NAME_MAX + 1]; pvpr_clean_name(name, c, sizeof c);
    if (!c[0]) return -1;
    int i = pvpr_preset_find(s, c);
    if (i < 0) { if (s->npresets >= PVPR_PRESETS_MAX) return -2; i = s->npresets++; }
    snprintf(s->presets[i].name, sizeof s->presets[i].name, "%s", c); s->presets[i].v = *v;
    return i;
}
int pvpr_preset_delete(pvpr_store *s, int i) {
    if (i < 0 || i >= s->npresets) return -1;
    memmove(&s->presets[i], &s->presets[i + 1], sizeof s->presets[0] * (size_t)(s->npresets - i - 1));
    s->npresets--;
    return 0;
}
int pvpr_preset_match(const pvpr_store *s, const pvpr_vals *v) {
    for (int i = 0; i < s->npresets; i++) if (pvpr_vals_equal(&s->presets[i].v, v)) return i;
    return -1;
}
