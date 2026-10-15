/* pvp_modcheck - Vanilla check (pure C). See pvp_modcheck.h. */
#include "pvp_modcheck.h"
#include "fe/fe_json.h"
#include <stdio.h>
#include <string.h>

static void raw(const jv *v, char *out, size_t cap) {
    if (!cap) return;
    size_t k = v && v->t == JN_STR ? v->n : 0; if (k >= cap) k = cap - 1;
    if (k) memcpy(out, v->s, k);
    out[k] = 0;
}
int pvp_modset_parse(const char *json, size_t n, pvp_modset *m) {
    memset(m, 0, sizeof *m); m->rule = -1;
    if (!json || !n) return 0;
    char err[120]; jv *o = jparse(json, n, err, sizeof err);
    if (!o || o->t != JN_OBJ) { jfree(o); return -1; }
    jv *r = jget(o, "rule"), *h = jget(o, "modset"), *l = jget(o, "modset_lines"), *c = jget(o, "client_mods"), *k = jget(o, "known");
    m->known = k ? jtruthy(k) : 1;
    m->rule = jstreq(r, "vanilla") ? 1 : jstreq(r, "mods") ? 0 : -1;
    raw(h, m->hash, sizeof m->hash);
    for (size_t i = 0; jis(l, JN_ARR) && i < l->cnt && m->ngame < PVP_MC_MAX; i++)
        if (jis(l->v[i], JN_STR)) raw(l->v[i], m->game[m->ngame++], sizeof m->game[0]);
    for (size_t i = 0; jis(c, JN_ARR) && i < c->cnt && m->nclient < PVP_MC_MAX; i++) {
        char id[64], ver[32]; raw(jget(c->v[i], "id"), id, sizeof id); raw(jget(c->v[i], "version"), ver, sizeof ver);
        if (id[0]) snprintf(m->client[m->nclient++], sizeof m->client[0], "%s %s", id, ver);
    }
    jfree(o);
    return 0;
}
static size_t put(char *out, size_t cap, size_t at, const char *s, int quote) {
    if (at >= cap) return at;
    if (quote && at + 1 < cap) out[at++] = '"';
    for (; *s && at + 2 < cap; s++) { if (quote && (*s == '"' || *s == '\\')) out[at++] = '\\'; out[at++] = *s; }
    if (quote && at + 1 < cap) out[at++] = '"';
    out[at < cap ? at : cap - 1] = 0;
    return at;
}
size_t pvp_modset_json(const pvp_modset *m, char *out, size_t cap) {
    size_t a = 0; char t[64];
    snprintf(t, sizeof t, "{\"known\":%d,\"rule\":", m->known); a = put(out, cap, a, t, 0);
    a = put(out, cap, a, m->rule == 1 ? "vanilla" : m->rule == 0 ? "mods" : "", 1);
    a = put(out, cap, a, ",\"modset\":", 0); a = put(out, cap, a, m->hash, 1);
    a = put(out, cap, a, ",\"modset_lines\":[", 0);
    for (int i = 0; i < m->ngame; i++) { if (i) a = put(out, cap, a, ",", 0); a = put(out, cap, a, m->game[i], 1); }
    a = put(out, cap, a, "],\"client_mods\":[", 0);
    for (int i = 0; i < m->nclient; i++) {
        char id[80]; snprintf(id, sizeof id, "%s", m->client[i]); char *sp = strchr(id, ' '); const char *ver = "";
        if (sp) { *sp = 0; ver = sp + 1; }
        a = put(out, cap, a, i ? ",{\"id\":" : "{\"id\":", 0); a = put(out, cap, a, id, 1);
        a = put(out, cap, a, ",\"version\":", 0); a = put(out, cap, a, ver, 1); a = put(out, cap, a, "}", 0);
    }
    a = put(out, cap, a, "]}", 0);
    return a + 1 < cap ? a : 0;
}
void pvp_modset_name(const char *line, char *out, size_t cap) {
    size_t k = 0;
    for (; line[k] && line[k] != ':' && k + 1 < cap; k++) out[k] = line[k] == '@' ? ' ' : line[k];
    out[k] = 0;
}
static int key_len(const char *line) { const char *c = strchr(line, ':'); return c ? (int)(c - line) : (int)strlen(line); }
static int has_key(const pvp_modset *m, const char *line) {      /* same <id>@<version> */
    int kl = key_len(line);
    for (int i = 0; i < m->ngame; i++) if (key_len(m->game[i]) == kl && !strncmp(m->game[i], line, (size_t)kl)) return i;
    return -1;
}
static void names(const pvp_modset *a, const pvp_modset *notin, char *out, size_t cap) {   /* a's mods not in notin (all if NULL) */
    out[0] = 0; size_t at = 0;
    for (int i = 0; i < a->ngame; i++) {
        if (notin && has_key(notin, a->game[i]) >= 0) continue;
        char nm[160]; pvp_modset_name(a->game[i], nm, sizeof nm);
        at += (size_t)snprintf(out + at, at < cap ? cap - at : 0, "%s%s", at ? ", " : "", nm);
        if (at >= cap) { out[cap - 1] = 0; return; }
    }
}
int pvp_modset_check(int vanilla, const pvp_modset *host, const pvp_modset *peer, const char *who, int self, char *why, size_t n) {
    char l[600];
    why[0] = 0;
    if (vanilla) {
        if (!peer->ngame) return 0;
        names(peer, NULL, l, sizeof l);
        if (self) snprintf(why, n, "Vanilla check is on: turn off your gameplay mod%s %s, or set Vanilla check to Off.", peer->ngame > 1 ? "s" : "", l);
        else snprintf(why, n, "Vanilla check is on: %s has the gameplay mod%s %s. %s must turn %s off (Switch to vanilla profile in the launcher).",
                      who, peer->ngame > 1 ? "s" : "", l, who, peer->ngame > 1 ? "them" : "it");
        return 1;
    }
    if (self) return 0;                                          /* mods allowed: the host's set is the reference */
    if (host->known && peer->known && host->hash[0] && !strcmp(host->hash, peer->hash)) return 0;
    char miss[600], extra[600]; names(host, peer, miss, sizeof miss); names(peer, host, extra, sizeof extra);
    if (!miss[0] && !extra[0]) {
        for (int i = 0; i < host->ngame; i++) {                 /* same names and versions: compare the files */
            int j = has_key(peer, host->game[i]);
            if (j >= 0 && strcmp(peer->game[j], host->game[i])) {
                char nm[160]; pvp_modset_name(host->game[i], nm, sizeof nm);
                snprintf(why, n, "%s's copy of %s is not the same as the host's (other files). %s must start the game from the launcher again.", who, nm, who);
                return 1;
            }
        }
        return 0;
    }
    if (!peer->known && miss[0]) { snprintf(why, n, "%s did not start the game from the launcher, so %s cannot have the host's gameplay mods: %s.", who, who, miss); return 1; }
    size_t at = (size_t)snprintf(why, n, "%s's gameplay mods are not the host's.", who);
    if (miss[0] && at < n) at += (size_t)snprintf(why + at, n - at, " Missing: %s.", miss);
    if (extra[0] && at < n) at += (size_t)snprintf(why + at, n - at, " Not allowed: %s.", extra);
    if (at < n) snprintf(why + at, n - at, " %s must join from the launcher, which gets the host's mods.", who);
    return 1;
}
