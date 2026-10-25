/* mp_solo.c - the single-player copy of a world (mp_solo.h). */
#include "mp_solo.h"
#include "fe_json.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------------- which world a save file is */
static int hex32(const char *h) { for (int i = 0; i < 32; i++) if (!isxdigit((unsigned char)h[i])) return 0; return 1; }
static void dashed(const char *h, char g[37]) {                  /* 32 hex -> lower-case guid with dashes */
    snprintf(g, 37, "%.8s-%.4s-%.4s-%.4s-%.12s", h, h + 8, h + 12, h + 16, h + 20);
    for (char *c = g; *c; c++) *c = (char)tolower((unsigned char)*c);
}
static void undashed(const char *g, char h[33]) {                /* guid -> 32 hex, case kept */
    int k = 0; for (const char *c = g; *c && k < 32; c++) if (*c != '-') h[k++] = *c;
    h[k] = 0;
}
/* the stock autosave name "<clan>__AutoSave_<32 hex>_<n>" carries the guid; any other save: its header */
static int save_guid(const char *dir, const char *file, char g[37]) {
    const char *a = strstr(file, "__AutoSave_");
    if (a && hex32(a + 11)) { dashed(a + 11, g); return 1; }
    char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, file);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = n > 0 ? malloc((size_t)n + 1) : NULL;
    size_t got = b ? fread(b, 1, (size_t)n, f) : 0; fclose(f);
    if (!b) return 0;
    b[got] = 0;
    int ok = 0; const char *h = strstr(b, "\"CampaignSaveHeader\"");
    const char *k = h ? strstr(h, "\"CampaignGuid\"") : NULL;
    const char *q = k ? strchr(k + 14, '"') : NULL;                 /* the value's opening quote */
    if (q && strlen(q + 1) >= 36 && q[37] == '"') {
        snprintf(g, 37, "%.36s", q + 1);
        for (char *c = g; *c; c++) *c = (char)tolower((unsigned char)*c);
        ok = 1;
    }
    free(b);
    return ok;
}
static int is_save_file(const char *f) {
    size_t l = strlen(f);
    if (l < 6 || strcmp(f + l - 5, ".json") || f[0] == '.') return 0;
    if (!strncasecmp(f, "mp_", 3) || strstr(f, "SaveHeaderCache")) return 0;   /* multiplayer temporary saves, the game's cache */
    return 1;
}
int mps_newest(const char *const *dirs, int nd, const char *guid, mps_save *out) {
    int found = 0;
    for (int d = 0; d < nd; d++) {
        if (!dirs[d] || !dirs[d][0]) continue;
        DIR *dp = opendir(dirs[d]); if (!dp) continue;
        for (struct dirent *e; (e = readdir(dp)); ) {
            if (!is_save_file(e->d_name)) continue;
            char p[1400], g[37]; struct stat st;
            snprintf(p, sizeof p, "%s/%s", dirs[d], e->d_name);
            if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
            if (found && (long long)st.st_mtime < out->mtime) continue;  /* only a newer one needs its guid */
            if (!save_guid(dirs[d], e->d_name, g) || strcasecmp(g, guid)) continue;
            if (found && (long long)st.st_mtime == out->mtime && strcmp(p, out->path) > 0) continue;   /* same time: by name */
            snprintf(out->path, sizeof out->path, "%s", p); out->mtime = (long long)st.st_mtime; found = 1;
        }
        closedir(dp);
    }
    return found;
}

/* ---------------------------------------------------------------- the copy */
static void esc_into(char *o, size_t cap, const char *s) {       /* plain text -> JSON string body */
    size_t k = strlen(o);
    for (; *s && k + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[k++] = '\\'; o[k++] = (char)c; }
        else if (c < 0x20) k += (size_t)snprintf(o + k, cap - k, "\\u%04x", c);
        else o[k++] = (char)c;
    }
    o[k] = 0;
}
static void rename_str(jv *o, const char *key, const char *pre, const char *post) {
    jv *v = jget(o, key); if (!jis(v, JN_STR)) return;
    char b[1200] = ""; esc_into(b, sizeof b, pre);
    size_t k = strlen(b); snprintf(b + k, sizeof b - k, "%.*s", (int)v->n, v->s);
    esc_into(b, sizeof b, post);
    jset(o, key, jstrv(b));
}
/* the copy's identity: campaign guid (header + clan record, the only two places the game keeps it), names, SaveName */
static int apply(jv *w, const char *ng, const char *pre, const char *post, const char *stem) {
    jv *h = jget(w, "CampaignSaveHeader"), *cl = jget(w, "ClanSaveData");
    if (!jis(h, JN_OBJ) || !jis(cl, JN_OBJ)) return -1;
    jset(h, "CampaignGuid", jstrv(ng)); jset(cl, "CampaignGuid", jstrv(ng));
    rename_str(h, "ClanName", pre, post); rename_str(cl, "ClanName", pre, post);
    jv *css = jpath(w, "CampaignSettingSaveData.CampaignStartSettingSaveData");
    if (jis(css, JN_OBJ)) rename_str(css, "CampaignName", pre, post);
    jset(h, "SaveName", jstrv(stem));
    return 0;
}
int mps_copy(const char *src, const char *ng, const char *pre, const char *post, char *out_path, size_t on, char *err, size_t errn) {
    jv *w = jload(src, err, errn); if (!w) return -1;
    jv *og = jpath(w, "CampaignSaveHeader.CampaignGuid");
    if (!jis(og, JN_STR) || og->n != 36 || !jis(jget(w, "ClanSaveData"), JN_OBJ)) { jfree(w); snprintf(err, errn, "not a world save"); return -1; }
    /* folder + stem of the source; the copy's stem: the source's with the new guid in place of the old one (the stock
       autosave name), else "<stem>_solo"; a free name ("_2".. when taken) */
    char dir[1300], stem[300], oh[33], nh[33];
    snprintf(dir, sizeof dir, "%s", src); char *sl = strrchr(dir, '/'); if (!sl) { jfree(w); snprintf(err, errn, "bad path"); return -1; }
    *sl = 0; snprintf(stem, sizeof stem, "%s", sl + 1);
    size_t sn = strlen(stem); if (sn > 5 && !strcmp(stem + sn - 5, ".json")) stem[sn - 5] = 0;
    char og_s[40]; snprintf(og_s, sizeof og_s, "%.*s", (int)og->n, og->s);
    undashed(og_s, oh); undashed(ng, nh);
    char base[300] = "";
    for (char *p = stem; *p; p++) if (!strncasecmp(p, oh, 32)) { snprintf(base, sizeof base, "%.*s%s%s", (int)(p - stem), stem, nh, p + 32); break; }
    if (!base[0]) snprintf(base, sizeof base, "%s_solo", stem);
    char st2[320], p[1700]; snprintf(st2, sizeof st2, "%s", base);
    snprintf(p, sizeof p, "%s/%s.json", dir, st2);
    for (int k = 2; access(p, F_OK) == 0 && k < 100; k++) { snprintf(st2, sizeof st2, "%s_%d", base, k); snprintf(p, sizeof p, "%s/%s.json", dir, st2); }
    if (access(p, F_OK) == 0) { jfree(w); snprintf(err, errn, "no free file name"); return -1; }
    if (apply(w, ng, pre, post, st2) || jsave_new(w, p)) { jfree(w); snprintf(err, errn, "could not write %s", p); return -1; }
    jfree(w);
    /* read back: the copy = the source with the same identity changes, nothing else (numbers by value, keys in order) */
    jv *c = jload(p, err, errn), *o = jload(src, err, errn);
    int same = c && o && !apply(o, ng, pre, post, st2) && jequal(c, o);
    jfree(c); jfree(o);
    if (!same) { unlink(p); snprintf(err, errn, "the copy read back differs from the save"); return -1; }   /* our own new file */
    snprintf(out_path, on, "%s", p);
    return 0;
}

/* ---------------------------------------------------------------- asked once per world */
int mps_asked(const char *file, const char *guid) {
    FILE *f = fopen(file, "r"); if (!f) return 0;
    char line[256]; int hit = 0;
    while (!hit && fgets(line, sizeof line, f)) { size_t n = strcspn(line, "\t\r\n"); hit = n == strlen(guid) && !strncasecmp(line, guid, n); }
    fclose(f);
    return hit;
}
int mps_asked_put(const char *file, const char *guid, int yes, const char *copy_guid) {
    FILE *f = fopen(file, "a"); if (!f) return -1;
    fprintf(f, "%s\t%s\t%s\n", guid, yes ? "yes" : "no", copy_guid && *copy_guid ? copy_guid : "-");
    return fclose(f) ? -1 : 0;
}
