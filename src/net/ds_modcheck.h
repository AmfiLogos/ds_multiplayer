/* In-game wrong-modset message (launcher lane, 2026-10-15).
 * The host and the guest each have a list of GAMEPLAY mod lines, the same lines the launcher hashes
 * (core/src/join.rs modset_lines, loader dsload_modset.json): `<id>@<version>[:<file sha256s>[:<patch hash>]]`,
 * one per line ('\n'), any order. Client mods are never in the list (never required from others).
 * Mod settings (loader API 5): one more line per gameplay setting of a gameplay mod, `<mod id>/<setting id>@<value>`
 * (value: on / off, an integer or a choice id). Those lines are their own kind ("Other setting values"); a setting of a
 * mod that is missing, extra or of another version is not listed again (the mod line says it all).
 * Pure C, no game code, header only (static functions: the Mac build and the Windows port both just include it;
 * no build-script change). The multiplayer mod calls it after a DSNET_REJ_MODSET refusal. */
#ifndef DS_MODCHECK_H
#define DS_MODCHECK_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* i18n: the multiplayer mod defines DSMC_TR as TR (ds_i18n.h) before the include, so the guest text is in the player's
 * language; any other user of this header gets English. TR_NOOP marks the keys for the extractor. */
#ifndef DSMC_TR
#define DSMC_TR(s) (s)
#endif
#ifndef TR_NOOP
#define TR_NOOP(s) (s)
#endif

enum { DSMC_MAX_MODS = 256 };

typedef struct {
    int missing, extra, version, files;   /* counts per kind (mod lines) */
    int settings;                         /* setting lines with another value (or on one side only) */
} dsmc_counts;

typedef struct { char id[112], ver[64], rest[200]; int set; } dsmc_mod;   /* set: a setting line (id "<mod>/<setting>") */

/* optional words of a setting line in the player's language (the loader's dsl_setting_text, found with dlsym by the
 * multiplayer mod): label = the setting's label, vtext = the value as the player reads it. Not set: the raw ids. */
typedef int (*dsmc_setting_fn)(const char *mod, const char *setting, const char *value, char *label, size_t lcap,
                               char *vtext, size_t vcap);
static dsmc_setting_fn dsmc_setting_text;
__attribute__((unused)) static void dsmc_set_setting_text(dsmc_setting_fn fn) { dsmc_setting_text = fn; }

static int dsmc_parse(const char *s, dsmc_mod *m, int max) {
    int k = 0;
    while (s && *s && k < max) {
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        char line[300]; if (l >= sizeof line) l = sizeof line - 1;
        memcpy(line, s, l); line[l] = 0;
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        s = e ? e + 1 : NULL;
        if (!*line) continue;
        char *at = strchr(line, '@'), *co = at ? strchr(at, ':') : strchr(line, ':');
        if (co) { snprintf(m[k].rest, sizeof m[k].rest, "%s", co + 1); *co = 0; } else m[k].rest[0] = 0;
        if (at) { snprintf(m[k].ver, sizeof m[k].ver, "%s", at + 1); *at = 0; } else m[k].ver[0] = 0;
        snprintf(m[k].id, sizeof m[k].id, "%s", line);
        m[k].set = strchr(m[k].id, '/') != NULL;
        k++;
    }
    return k;
}

static const dsmc_mod *dsmc_find(const dsmc_mod *m, int n, const char *id) {
    for (int i = 0; i < n; i++) if (!strcmp(m[i].id, id)) return &m[i];
    return NULL;
}

/* display name of an id from the "id\tName\n" table, else the id */
static void dsmc_name_of(const char *names, const char *id, char *out, size_t n) {
    size_t il = strlen(id);
    for (const char *p = names; p && *p;) {
        const char *e = strchr(p, '\n'), *t = strchr(p, '\t');
        if (t && (!e || t < e) && (size_t)(t - p) == il && !strncmp(p, id, il)) {
            size_t l = e ? (size_t)(e - t - 1) : strlen(t + 1); if (l >= n) l = n - 1;
            memcpy(out, t + 1, l); out[l] = 0; return;
        }
        p = e ? e + 1 : NULL;
    }
    snprintf(out, n, "%s", id);
}

typedef struct { char *p; size_t n, len; } dsmc_buf;
static void dsmc_put(dsmc_buf *b, const char *fmt, ...) {
    if (b->len >= b->n) return;
    va_list a; va_start(a, fmt);
    int w = vsnprintf(b->p + b->len, b->n - b->len, fmt, a);
    va_end(a);
    if (w > 0) b->len += (size_t)w;
    if (b->len >= b->n) b->len = b->n - 1;
}

/* one kind of difference as "A 1.0, B 2.0" (sep between entries); returns how many */
enum { DSMC_K_MISSING, DSMC_K_EXTRA, DSMC_K_VERSION, DSMC_K_FILES };
static int dsmc_list(dsmc_buf *b, int kind, const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *names, int with_versions_pair) {
    int c = 0; char nm[96];
    const dsmc_mod *src = kind == DSMC_K_EXTRA ? g : h; int ns = kind == DSMC_K_EXTRA ? ng : nh;
    for (int i = 0; i < ns; i++) {
        if (src[i].set) continue;                                     /* setting lines: dsmc_slist */
        const dsmc_mod *o = dsmc_find(kind == DSMC_K_EXTRA ? h : g, kind == DSMC_K_EXTRA ? nh : ng, src[i].id);
        int hit = kind == DSMC_K_MISSING ? !o : kind == DSMC_K_EXTRA ? !o
                : kind == DSMC_K_VERSION ? (o && strcmp(o->ver, src[i].ver))
                : (o && !strcmp(o->ver, src[i].ver) && strcmp(o->rest, src[i].rest));
        if (!hit) continue;
        dsmc_name_of(names, src[i].id, nm, sizeof nm);
        if (c++) dsmc_put(b, "%s", DSMC_TR(TR_NOOP(", ")));
        if (kind == DSMC_K_VERSION && with_versions_pair) dsmc_put(b, DSMC_TR(TR_NOOP("%s (host %s, you %s)")), nm, src[i].ver, o->ver);
        else if (kind == DSMC_K_VERSION) dsmc_put(b, "%s (host %s, player %s)", nm, src[i].ver, o->ver);
        else dsmc_put(b, "%s%s%s", nm, *src[i].ver ? " " : "", src[i].ver);
    }
    return c;
}

/* setting lines whose value differs (or that one side lacks) while the mod itself is the same version on both sides;
 * guest = 1: "<Mod name>: <Label> (host X, you Y)" in the player's words, 0: "<mod>/<setting> (host X, player Y)" */
static int dsmc_mod_same(const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *mod, size_t ml) {
    char id[112]; if (ml >= sizeof id) return 0;
    memcpy(id, mod, ml); id[ml] = 0;
    const dsmc_mod *a = dsmc_find(h, nh, id), *b = dsmc_find(g, ng, id);
    return a && b && !strcmp(a->ver, b->ver);
}
static int dsmc_slist(dsmc_buf *b, const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *names, int guest) {
    int c = 0;
    for (int pass = 0; pass < 2; pass++) {                               /* host's setting lines, then the guest's own ones */
        const dsmc_mod *src = pass ? g : h, *oth = pass ? h : g; int ns = pass ? ng : nh, no = pass ? nh : ng;
        for (int i = 0; i < ns; i++) {
            if (!src[i].set) continue;
            const dsmc_mod *o = dsmc_find(oth, no, src[i].id);
            if (pass ? o != NULL : (o && !strcmp(o->ver, src[i].ver))) continue;   /* same value; pass 1: only guest-only lines */
            const char *sl = strchr(src[i].id, '/');
            if (!dsmc_mod_same(h, nh, g, ng, src[i].id, (size_t)(sl - src[i].id))) continue;
            const char *hv = pass ? "-" : src[i].ver, *gv = pass ? src[i].ver : o ? o->ver : "-";
            if (c++) dsmc_put(b, "%s", guest ? DSMC_TR(TR_NOOP(", ")) : ", ");
            if (!guest) { dsmc_put(b, "%s (host %s, player %s)", src[i].id, hv, gv); continue; }
            char mod[112], nm[96], lab[160], hx[160], gx[160];
            snprintf(mod, sizeof mod, "%.*s", (int)(sl - src[i].id), src[i].id);
            dsmc_name_of(names, mod, nm, sizeof nm);
            snprintf(lab, sizeof lab, "%s", sl + 1); snprintf(hx, sizeof hx, "%s", hv); snprintf(gx, sizeof gx, "%s", gv);
            if (dsmc_setting_text) {
                if (strcmp(hv, "-")) dsmc_setting_text(mod, sl + 1, hv, lab, sizeof lab, hx, sizeof hx);
                if (strcmp(gv, "-")) dsmc_setting_text(mod, sl + 1, gv, lab, sizeof lab, gx, sizeof gx);
            }
            dsmc_put(b, DSMC_TR(TR_NOOP("%s: %s (host %s, you %s)")), nm, lab, hx, gx);
        }
    }
    return c;
}

/* Builds the player text for the GUEST (the one who cannot join) into out (always 0-terminated).
 * vanilla = 1 when the host's rule is "Vanilla check" (no gameplay mod allowed; host_lines is then empty).
 * names (optional, may be NULL): "id\tDisplay name\n..." to show names instead of ids.
 * Returns the number of differences (0 = same set: out is ""). */
__attribute__((unused)) static int dsmc_guest_text(const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                    char *out, size_t n, dsmc_counts *counts) {
    static dsmc_mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : dsmc_parse(host_lines, h, DSMC_MAX_MODS), ng = dsmc_parse(guest_lines, g, DSMC_MAX_MODS);
    dsmc_buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; dsmc_buf z = {scratch, sizeof scratch, 0};
    dsmc_counts c = {
        dsmc_list(&z, DSMC_K_MISSING, h, nh, g, ng, names, 1), dsmc_list(&z, DSMC_K_EXTRA, h, nh, g, ng, names, 1),
        dsmc_list(&z, DSMC_K_VERSION, h, nh, g, ng, names, 1), dsmc_list(&z, DSMC_K_FILES, h, nh, g, ng, names, 1),
        vanilla ? 0 : dsmc_slist(&z, h, nh, g, ng, names, 1) };
    if (counts) *counts = c;
    int mods = c.missing + c.extra + c.version + c.files, total = mods + c.settings;
    if (!total || !n) return total;
    if (vanilla) {
        char l[1000]; dsmc_buf lb = {l, sizeof l, 0}; l[0] = 0;               /* the list goes inside one sentence (word order) */
        dsmc_list(&lb, DSMC_K_EXTRA, h, nh, g, ng, names, 1);
        dsmc_put(&b, DSMC_TR(TR_NOOP("This host allows no gameplay mods. Turn off: %s. Client mods (comfort mods) are fine.")), l);
    } else {
        dsmc_put(&b, "%s", mods ? DSMC_TR(TR_NOOP("Your gameplay mods are not the same as the host's."))
                                : DSMC_TR(TR_NOOP("Your mod settings are not the same as the host's.")));
        if (c.missing) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Missing (the host uses them):"))); dsmc_list(&b, DSMC_K_MISSING, h, nh, g, ng, names, 1); }
        if (c.extra) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Extra (the host does not use them):"))); dsmc_list(&b, DSMC_K_EXTRA, h, nh, g, ng, names, 1); }
        if (c.version) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Other version:"))); dsmc_list(&b, DSMC_K_VERSION, h, nh, g, ng, names, 1); }
        if (c.files) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Same version, different files (reinstall):"))); dsmc_list(&b, DSMC_K_FILES, h, nh, g, ng, names, 1); }
        if (c.settings) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Other setting values:"))); dsmc_slist(&b, h, nh, g, ng, names, 1); }
        if (mods) dsmc_put(&b, "\n%s", DSMC_TR(TR_NOOP("Join from the launcher: it gets the host's mods for this game.")));
        else dsmc_put(&b, "\n%s", DSMC_TR(TR_NOOP("Set the same values in the Mods window (Settings of that mod), then join again.")));
    }
    return total;
}

/* The same differences seen from the host (one short line for the host's chat / log), e.g.
 * "The owner cannot join: missing Hell difficulty 1.0.0; extra Cheaper recruits 1.0.0". */
__attribute__((unused)) static int dsmc_host_text(const char *player, const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                   char *out, size_t n) {
    static dsmc_mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : dsmc_parse(host_lines, h, DSMC_MAX_MODS), ng = dsmc_parse(guest_lines, g, DSMC_MAX_MODS);
    dsmc_buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; dsmc_buf z = {scratch, sizeof scratch, 0};
    int k[5], total = 0;
    for (int i = 0; i < 4; i++) total += k[i] = dsmc_list(&z, i, h, nh, g, ng, names, 0);
    total += k[4] = vanilla ? 0 : dsmc_slist(&z, h, nh, g, ng, names, 0);
    if (!total || !n) return total;
    dsmc_put(&b, "%s cannot join: ", player && *player ? player : "A player");
    static const char *lab[5] = {"missing ", "extra ", "other version ", "different files ", "other setting "};
    int first = 1;
    for (int i = 0; i < 5; i++) {
        if (!k[i]) continue;
        if (!first) dsmc_put(&b, "; ");
        first = 0; dsmc_put(&b, "%s", vanilla && i == DSMC_K_EXTRA ? "gameplay mods on in a vanilla game: " : lab[i]);
        if (i == 4) dsmc_slist(&b, h, nh, g, ng, names, 0); else dsmc_list(&b, i, h, nh, g, ng, names, 0);
    }
    return total;
}
__attribute__((unused)) static int dsmc_hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* a plan row's host_set / my_set for the player: the loader's text as is, separators ('\n', '&', ';') as ", " */
__attribute__((unused)) static void dsmc_set_show(const char *s, char *out, size_t n) {
    size_t o = 0; if (!n) return;
    for (const char *p = s ? s : ""; *p && o + 3 < n; p++) {
        if (*p == '\n' || *p == '&' || *p == ';') { if (o && p[1]) { out[o++] = ','; out[o++] = ' '; } }
        else out[o++] = *p;
    }
    out[o] = 0;
}
/* ================================================================ "Get the host's mods" (v0.1.14, hostmods CONTRACT)
 * The loader's dsl_hostmods_plan answers JSON {"rows":[{"id","name","host_ver","my_ver","my_on","official","action",
 * "host_set","my_set"}...],"can_fix":0|1,"downloads":N,"names_pending":0|1}; the box text is built from it here (pure C, unit-tested).
 * Mod settings are their own lines (`<mod>/<setting>@<value>`, lane modsettings) in the lines given to the loader. */
typedef struct { char id[64], name[96], host_ver[32], my_ver[32], action[16], host_set[256], my_set[256]; int my_on, official; } dsmc_prow;
typedef struct { int n, can_fix, downloads, names_pending; dsmc_prow row[DSMC_MAX_MODS]; } dsmc_plan;   /* names_pending: the loader still
                                     fetches the name of a mod this copy does not have (ask again shortly) */

__attribute__((unused)) static const char *dsmc_j_ws(const char *p) { while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }
__attribute__((unused)) static const char *dsmc_j_after_str(const char *p) {          /* p at '"': just after the closing quote (NULL: broken) */
    for (p++; *p; p++) { if (*p == '\\') { if (!*++p) return NULL; } else if (*p == '"') return p + 1; }
    return NULL;
}
__attribute__((unused)) static const char *dsmc_j_after(const char *p) {              /* p at any value: just after it (NULL: broken) */
    p = dsmc_j_ws(p);
    if (*p == '"') return dsmc_j_after_str(p);
    if (*p == '{' || *p == '[') {
        int d = 0;
        while (*p) {
            if (*p == '"') { p = dsmc_j_after_str(p); if (!p) return NULL; continue; }
            if (*p == '{' || *p == '[') d++;
            else if ((*p == '}' || *p == ']') && --d == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
/* in the object starting at o ('{'): the value of "key" (NULL: absent) */
__attribute__((unused)) static const char *dsmc_j_get(const char *o, const char *key) {
    o = dsmc_j_ws(o); if (!o || *o != '{') return NULL;
    const char *p = dsmc_j_ws(o + 1); size_t kl = strlen(key);
    while (p && *p == '"') {
        const char *ke = dsmc_j_after_str(p); if (!ke) return NULL;
        int hit = (size_t)(ke - p - 2) == kl && !strncmp(p + 1, key, kl);
        p = dsmc_j_ws(ke); if (*p != ':') return NULL;
        const char *v = dsmc_j_ws(p + 1);
        if (hit) return v;
        p = dsmc_j_after(v); if (!p) return NULL;
        p = dsmc_j_ws(p); if (*p == ',') p = dsmc_j_ws(p + 1);
    }
    return NULL;
}
__attribute__((unused)) static void dsmc_j_utf8(unsigned c, char *out, size_t n, size_t *o) {
    char b[4]; int k = 0;
    if (c < 0x80) b[k++] = (char)c;
    else if (c < 0x800) { b[k++] = (char)(0xC0 | c >> 6); b[k++] = (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { b[k++] = (char)(0xE0 | c >> 12); b[k++] = (char)(0x80 | (c >> 6 & 63)); b[k++] = (char)(0x80 | (c & 63)); }
    else { b[k++] = (char)(0xF0 | c >> 18); b[k++] = (char)(0x80 | (c >> 12 & 63)); b[k++] = (char)(0x80 | (c >> 6 & 63)); b[k++] = (char)(0x80 | (c & 63)); }
    if (*o + (size_t)k < n) { memcpy(out + *o, b, (size_t)k); *o += (size_t)k; }
}
__attribute__((unused)) static unsigned dsmc_j_hex4(const char *p) { unsigned v = 0; for (int i = 0; i < 4; i++) { int h = dsmc_hexv(p[i]); if (h < 0) return 0xFFFD; v = v * 16 + (unsigned)h; } return v; }
/* a JSON string value -> UTF-8 (whole characters only); 0 = not a string ("" written) */
__attribute__((unused)) static int dsmc_j_str(const char *v, char *out, size_t n) {
    size_t o = 0; if (n) out[0] = 0;
    if (!v || *v != '"' || !n) return 0;
    for (const char *p = v + 1; *p && *p != '"'; p++) {
        unsigned c = (unsigned char)*p;
        if (c == '\\') {
            p++;
            if (*p == 'n') c = '\n'; else if (*p == 't') c = '\t'; else if (*p == 'r') c = '\r'; else if (*p == 'b') c = 8; else if (*p == 'f') c = 12;
            else if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
                c = dsmc_j_hex4(p + 1); p += 4;
                if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
                    unsigned lo = dsmc_j_hex4(p + 3);
                    if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                }
                dsmc_j_utf8(c, out, n, &o); continue;
            } else if (*p) c = (unsigned char)*p; else break;
        }
        if (o + 1 < n) out[o++] = (char)c;
        else break;
    }
    {                                                                     /* never half a character at a cut */
        size_t i = o; while (i && ((unsigned char)out[i - 1] & 0xC0) == 0x80) i--;
        if (i) { unsigned char c0 = (unsigned char)out[i - 1]; size_t need = c0 >= 0xF0 ? 4 : c0 >= 0xE0 ? 3 : c0 >= 0xC0 ? 2 : 1;
                 if (o - (i - 1) < need) o = i - 1; }
    }
    out[o] = 0;
    return 1;
}
__attribute__((unused)) static int dsmc_j_int(const char *v, int def) {
    if (!v) return def;
    if (*v == 't') return 1;
    if (*v == 'f') return 0;
    return (*v == '-' || (*v >= '0' && *v <= '9')) ? atoi(v) : def;
}
/* dsl_hostmods_plan JSON -> pl. Returns the number of rows, -1 = not that JSON. */
__attribute__((unused)) static int dsmc_plan_parse(const char *json, dsmc_plan *pl) {
    memset(pl, 0, sizeof *pl);
    const char *r = json ? dsmc_j_get(json, "rows") : NULL;
    if (!r || *r != '[') return -1;
    pl->can_fix = dsmc_j_int(dsmc_j_get(json, "can_fix"), 0);
    pl->downloads = dsmc_j_int(dsmc_j_get(json, "downloads"), 0);
    pl->names_pending = dsmc_j_int(dsmc_j_get(json, "names_pending"), 0);
    const char *p = dsmc_j_ws(r + 1);
    while (p && *p == '{' && pl->n < DSMC_MAX_MODS) {
        dsmc_prow *w = &pl->row[pl->n++];
        dsmc_j_str(dsmc_j_get(p, "id"), w->id, sizeof w->id);
        dsmc_j_str(dsmc_j_get(p, "name"), w->name, sizeof w->name);
        if (!w->name[0]) snprintf(w->name, sizeof w->name, "%s", w->id);
        dsmc_j_str(dsmc_j_get(p, "host_ver"), w->host_ver, sizeof w->host_ver);
        dsmc_j_str(dsmc_j_get(p, "my_ver"), w->my_ver, sizeof w->my_ver);
        dsmc_j_str(dsmc_j_get(p, "action"), w->action, sizeof w->action);
        dsmc_j_str(dsmc_j_get(p, "host_set"), w->host_set, sizeof w->host_set);
        dsmc_j_str(dsmc_j_get(p, "my_set"), w->my_set, sizeof w->my_set);
        w->my_on = dsmc_j_int(dsmc_j_get(p, "my_on"), 0);
        w->official = dsmc_j_int(dsmc_j_get(p, "official"), 1);
        p = dsmc_j_after(p); if (!p) return -1;
        p = dsmc_j_ws(p); if (*p == ',') p = dsmc_j_ws(p + 1);
    }
    for (int i = 0; i < pl->n; i++) if (!strcmp(pl->row[i].action, "not_official")) pl->can_fix = 0;   /* never a button then */
    return pl->n;
}
/* what a row's action means for the player (status words, not what the button does) */
__attribute__((unused)) static const char *dsmc_action_text(const char *a) {
    if (!strcmp(a, "turn_on")) return DSMC_TR(TR_NOOP("you have it, turned off"));
    if (!strcmp(a, "download")) return DSMC_TR(TR_NOOP("you do not have it"));
    if (!strcmp(a, "update")) return DSMC_TR(TR_NOOP("your version is older"));
    if (!strcmp(a, "reinstall")) return DSMC_TR(TR_NOOP("same version, different files"));
    if (!strcmp(a, "host_older")) return DSMC_TR(TR_NOOP("the host has an older version: the host should update"));
    if (!strcmp(a, "turn_off")) return DSMC_TR(TR_NOOP("the host does not use it"));
    if (!strcmp(a, "settings")) return DSMC_TR(TR_NOOP("other gameplay settings"));
    if (!strcmp(a, "not_official")) return DSMC_TR(TR_NOOP("not one of our official mods: it cannot be downloaded"));
    return "";
}
/* The box text for the guest: title, the differing rows (host's version next to the guest's, on / off, settings),
 * the mods that are the same in one line, and what the button does (or why there is none). max_rows: rows shown
 * before "More differences: N" (the stock box is small). Returns the number of differing rows. */
__attribute__((unused)) static int dsmc_hostmods_text(const dsmc_plan *pl, int vanilla, char *out, size_t n, int max_rows) {
    dsmc_buf b = {out, n, 0}; if (n) out[0] = 0;
    int diff = 0, same = 0, shown = 0, older = 0;
    for (int i = 0; i < pl->n; i++) { if (strcmp(pl->row[i].action, "same")) diff++; else same++; if (!strcmp(pl->row[i].action, "host_older")) older++; }
    dsmc_put(&b, "<size=130%%>%s</size>\n%s", DSMC_TR(TR_NOOP("Could not join")),
             vanilla ? DSMC_TR(TR_NOOP("This host allows no gameplay mods. Client mods (comfort mods) are fine."))
                     : DSMC_TR(TR_NOOP("Your gameplay mods are not the same as the host's.")));
    dsmc_put(&b, "\n<size=85%%>");
    for (int i = 0; i < pl->n; i++) {
        const dsmc_prow *w = &pl->row[i];
        if (!strcmp(w->action, "same")) continue;
        if (shown >= max_rows) continue;
        shown++;
        char mine[96];
        if (!w->my_ver[0]) snprintf(mine, sizeof mine, "-");
        else if (!w->my_on) snprintf(mine, sizeof mine, "%s %s", w->my_ver, DSMC_TR(TR_NOOP("(off)")));
        else snprintf(mine, sizeof mine, "%s", w->my_ver);
        char nm[140]; snprintf(nm, sizeof nm, "<color=#E8A23A>%s</color>", w->name);
        dsmc_put(&b, "\n");
        dsmc_put(&b, DSMC_TR(TR_NOOP("%s (host %s, you %s)")), nm, w->host_ver[0] ? w->host_ver : "-", mine);
        const char *st = dsmc_action_text(w->action);
        if (*st) dsmc_put(&b, ": %s", st);
        if (strcmp(w->host_set, w->my_set) && (w->host_set[0] || w->my_set[0])) {
            char hs[200], ms[200]; dsmc_set_show(w->host_set, hs, sizeof hs); dsmc_set_show(w->my_set, ms, sizeof ms);
            dsmc_put(&b, "\n   <color=#a69085>");
            dsmc_put(&b, DSMC_TR(TR_NOOP("settings: host %s, you %s")), hs[0] ? hs : DSMC_TR(TR_NOOP("default")), ms[0] ? ms : DSMC_TR(TR_NOOP("default")));
            dsmc_put(&b, "</color>");
        }
    }
    if (diff > shown) { dsmc_put(&b, "\n"); dsmc_put(&b, DSMC_TR(TR_NOOP("More differences: %d")), diff - shown); }
    if (same) {
        dsmc_put(&b, "\n<color=#a69085>%s ", DSMC_TR(TR_NOOP("Same as the host:")));
        int k = 0;
        for (int i = 0; i < pl->n; i++) if (!strcmp(pl->row[i].action, "same")) dsmc_put(&b, "%s%s", k++ ? DSMC_TR(TR_NOOP(", ")) : "", pl->row[i].name);
        dsmc_put(&b, "</color>");
    }
    dsmc_put(&b, "</size>\n");
    if (pl->can_fix) {
        dsmc_put(&b, "%s", DSMC_TR(TR_NOOP("\"Get the host's mods\" downloads what is missing from our official releases, sets your gameplay mods like the host's, then joins the host again.")));
        if (older) dsmc_put(&b, " %s", DSMC_TR(TR_NOOP("Where the host has an older version, you get the host's version.")));
    } else
        dsmc_put(&b, "%s", DSMC_TR(TR_NOOP("The host uses a mod that is not one of our official mods, so the game cannot get the host's mods for you. Ask the host for it, or ask the host to turn it off.")));
    return diff;
}

/* ---- DSMP_REJOIN (the new process joins the same host once): `ip|<addr>|<port>` or `steam|<host steamid64>`, then
 * optional flags: `|watch` (spectator), `|world=<campaign id>` (a Recent worlds row: the join is for that world).
 * Unknown flags are skipped (a newer writer). */
typedef struct { char how[8], addr[96]; unsigned port; unsigned long long steamid; int watch; char world[40]; } dsmc_rejoin;
__attribute__((unused)) static int dsmc_rejoin_format(const dsmc_rejoin *r, char *out, size_t n) {
    int w;
    if (!strcmp(r->how, "ip")) {
        if (!r->addr[0] || !r->port || r->port > 65535 || strpbrk(r->addr, "|\n\r=")) return -1;
        w = snprintf(out, n, "ip|%s|%u", r->addr, r->port);
    } else if (!strcmp(r->how, "steam")) {
        if (!r->steamid) return -1;
        w = snprintf(out, n, "steam|%llu", r->steamid);
    } else return -1;
    if (w < 0 || (size_t)w >= n) return -1;
    if (r->watch) { size_t l = strlen(out); if (snprintf(out + l, n - l, "|watch") >= (int)(n - l)) return -1; }
    if (r->world[0] && !strpbrk(r->world, "|\n\r")) { size_t l = strlen(out); if (snprintf(out + l, n - l, "|world=%s", r->world) >= (int)(n - l)) return -1; }
    return 0;
}
__attribute__((unused)) static int dsmc_rejoin_parse(const char *s, dsmc_rejoin *r) {
    memset(r, 0, sizeof *r);
    if (!s) return -1;
    char t[300]; snprintf(t, sizeof t, "%s", s);
    size_t l = strlen(t); while (l && (t[l - 1] == '\n' || t[l - 1] == '\r' || t[l - 1] == ' ')) t[--l] = 0;
    char *f[8]; int nf = 0;
    for (char *p = t; nf < 8;) { f[nf++] = p; char *e = strchr(p, '|'); if (!e) break; *e = 0; p = e + 1; }
    int k;
    if (nf >= 3 && !strcmp(f[0], "ip")) {
        char *end; unsigned long pt = strtoul(f[2], &end, 10);
        if (!f[1][0] || *end || !pt || pt > 65535 || strlen(f[1]) >= sizeof r->addr) return -1;
        snprintf(r->how, sizeof r->how, "ip"); snprintf(r->addr, sizeof r->addr, "%s", f[1]); r->port = (unsigned)pt; k = 3;
    } else if (nf >= 2 && !strcmp(f[0], "steam")) {
        char *end; unsigned long long id = strtoull(f[1], &end, 10);
        if (!id || *end) return -1;
        snprintf(r->how, sizeof r->how, "steam"); r->steamid = id; k = 2;
    } else return -1;
    for (; k < nf; k++) {
        if (!strcmp(f[k], "watch")) r->watch = 1;
        else if (!strncmp(f[k], "world=", 6)) snprintf(r->world, sizeof r->world, "%s", f[k] + 6);
    }
    return 0;
}
#endif
