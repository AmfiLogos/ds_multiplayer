/* pvp_units.c - PvP with each player's OWN units, copied from his own save (M3c; plan research/plan_pvp.md, owner 04:20).
 * Reuses the Foreign Expedition library and transport (ext/foreign_expedition: fe/fe_save.c, fe/fe_json.c and dsnet's mail
 * + DSNET_FE_START, copied here unchanged):
 *  1. pvp_units_bring {file, guids}: every player (host too) exports the chosen units from a save file ON HIS COMPUTER
 *     (fe_export_entry: unit closure + its items + a manifest); a guest mails them to the host (mail kind PVP_M_UNITS).
 *  2. pvp_units_start (host, at the settlement): the world snapshot and the owner tables are kept first (the match end
 *     brings both back: nothing stays); then DSNET_FE_START with the new guid of every brought unit / item and its
 *     owner's uid. Every peer replaces its owner tables at that step and the server asks for a join point.
 *  3. The host's join-point save is rebuilt (fe_build_hub: the host's clan units out, the brought units in, at the
 *     settlement) and every peer loads it through the normal join path. Then the PvP match runs as usual (SETUP, ARENA,
 *     START); each player's team is made of the units he brought.
 *  4. Match end: the world snapshot is reloaded (pvp_game.c) and the host re-imports the owner tables kept in step 2. */
#include "rig.h"
#include "mp.h"
#include "pvp_core.h"
#include "fe/fe_save.h"
#include "pvp_build.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>

#define PVP_M_UNITS 11               /* mail kinds (FE uses 1-4) */
#define PVP_M_UNITS_ACK 12
#define PVP_M_ROSTER 13              /* host -> every player: who brought which units (the PvP window shows them) */

mp_session *mp_game_session(void) __attribute__((weak_import));
static dsnet_client *cli(void) { mp_session *s = mp_game_session ? mp_game_session() : NULL; return s ? mp_client(s) : NULL; }
static void hex_of(const uint8_t *u, char o[33]) { for (int i = 0; i < 16; i++) snprintf(o + 2 * i, 3, "%02x", u[i]); }
static int unhex(const char *s, uint8_t u[16]) {
    for (int i = 0; i < 16; i++) { unsigned v; if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1; u[i] = (uint8_t)v; }
    return 0;
}
static int guid_bytes(const char *s, uint8_t g[16]) { Guid x; if (!guid_parse(s, &x)) return -1; memcpy(g, &x, 16); return 0; }
static char last_msg[300];
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(last_msg, sizeof last_msg, fmt, ap); va_end(ap); rig_log("pvp units: %s", last_msg); }

static jv *recv_entries;             /* host: brought units (closure + manifest), any player, by sender uid */
static jv *pending;                  /* host: run id, entries, maps of the start in flight */
static int armed;                    /* host: the next join point builds the PvP world */
static int n_received, n_built;
static uint8_t *owners_kept; static size_t owners_len;   /* host: owner tables before the match */
static int owners_restore_due;

/* pvp_mods.c (Vanilla check): its mail kinds come through the loop below */
int  pvp_mods_mail(dsnet_client *c, const uint8_t from[16], uint8_t kind, const uint8_t *data, size_t len);
void pvp_mods_frame(void);
/* pvp_game.c */
int pvp_snap_world_lock(void);       /* take the world snapshot now and keep it for this match (0 ok) */

/* the roster: the brought units that will fight, per player uid (host: from recv_entries; guests: from the host's mail) */
typedef struct { char uid[33], name[64], port[64]; } roster_e;
static roster_e roster[16]; static int nroster;
static void roster_from_entries(void) {
    nroster = 0;
    for (size_t i = 0; recv_entries && i < recv_entries->cnt && nroster < 16; i++) {
        jv *e = recv_entries->v[i], *m = jget(e, "manifest"), *pu = jget(m, "player_uid"), *nk = jget(m, "name_key");
        jv *pr = fe_comp(jpath(e, "closure.unit.components"), 10), *pid = pr ? jget(pr, "UnitPortraitID") : NULL;
        roster_e *r = &roster[nroster++];
        snprintf(r->uid, sizeof r->uid, "%.*s", pu && pu->t == JN_STR ? (int)pu->n : 0, pu && pu->t == JN_STR ? pu->s : "");
        snprintf(r->name, sizeof r->name, "%.*s", nk && nk->t == JN_STR ? (int)nk->n : 0, nk && nk->t == JN_STR ? nk->s : "");
        snprintf(r->port, sizeof r->port, "%.*s", pid && pid->t == JN_STR ? (int)pid->n : 0, pid && pid->t == JN_STR ? pid->s : "");
    }
}
static void roster_send(dsnet_client *c) {             /* host: to every other player present */
    Buf b = { 0 }; bput(&b, "{\"roster\":[");
    for (int i = 0; i < nroster; i++) { bput(&b, "%s[", i ? "," : ""); bstr(&b, roster[i].uid); bput(&b, ","); bstr(&b, roster[i].name); bput(&b, ","); bstr(&b, roster[i].port); bput(&b, "]"); }
    bput(&b, "]}");
    uint32_t pm = dsnet_present_mask(c); uint8_t me[16], u[16]; dsnet_my_uid(c, me);
    for (int p = 0; p < 32; p++) if (pm >> p & 1 && !dsnet_player_uid(c, p, u) && memcmp(u, me, 16)) dsnet_mail_send(c, u, PVP_M_ROSTER, b.p, b.n);
    free(b.p);
}
int pvp_units_roster(int i, char uid[33], char name[64], char port[64]) {
    if (i < 0 || i >= nroster) return nroster;
    memcpy(uid, roster[i].uid, 33); memcpy(name, roster[i].name, 64); memcpy(port, roster[i].port, 64); return nroster;
}
static void host_take(const char *uid_hex, jv *entries) {
    if (!recv_entries) recv_entries = jarr();
    for (size_t i = 0; i < recv_entries->cnt; ) {                           /* a new send replaces that player's old one */
        if (jstreq(jpath(recv_entries->v[i], "manifest.player_uid"), uid_hex)) jarr_del(recv_entries, i); else i++;
    }
    for (size_t i = 0; i < entries->cnt; i++) {
        jv *e = jcopy(entries->v[i]);
        jset(jget(e, "manifest"), "player_uid", jstrv(uid_hex));          /* the server-stamped sender, not his claim */
        jpush(recv_entries, e);
    }
    n_received = (int)recv_entries->cnt;
    roster_from_entries(); dsnet_client *c = cli(); if (c) roster_send(c);
}
static char *cmd_bring(const char *line) {
    char file[1024] = "", gl[2048] = "", err[400] = "", me[33];
    Buf b = { 0 };
    if (!jstr(line, "file", file, sizeof file) || !jstr(line, "guids", gl, sizeof gl)) { bput(&b, "{\"ok\":false,\"error\":\"need file and guids\"}"); return b.p; }
    dsnet_client *c = cli(); uint8_t u[16];
    if (!c || dsnet_my_uid(c, u)) { bput(&b, "{\"ok\":false,\"error\":\"not in a multiplayer session\"}"); return b.p; }
    hex_of(u, me);
    jv *save = jload(file, err, sizeof err);
    if (!save) { bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
    jv *entries = jarr(); int n = 0;
    char *sp = NULL;
    for (char *t = strtok_r(gl, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
        while (*t == ' ') t++;
        if (n >= DSNET_PARTY_MAX) break;
        jv *e = fe_export_entry(save, t, me, err, sizeof err);
        if (!e) { jfree(entries); jfree(save); bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
        jpush(entries, e); n++;
    }
    jfree(save);
    int rc = 0; size_t sz = 0;
    if (dsnet_is_host(c)) host_take(me, entries);
    else { jv *m = jobj(); jset(m, "entries", jcopy(entries)); char *s = jdump(m, 0, &sz); jfree(m); rc = dsnet_mail_send(c, NULL, PVP_M_UNITS, s, sz); free(s); }
    say("brought %zu unit(s) from %s (%s, %zu bytes)", entries->cnt, file, dsnet_is_host(c) ? "kept on the host" : rc ? "MAIL FAILED" : "mailed to the host", sz);
    bput(&b, "{\"ok\":%s,\"n\":%zu,\"bytes\":%zu}", rc ? "false" : "true", entries->cnt, sz);
    jfree(entries);
    return b.p;
}
/* pvp_units_wiki {link, max}: the units of a DS wiki builder team link (M3b), rebuilt here as save closures by
   pvp_build.c (never the wiki's own save writer), then sent like brought units. At most `max` units (the player's share). */
static char *cmd_wiki(const char *line) {
    static char link[8192]; char err[400] = "", me[33], notes[2000] = "";
    Buf b = { 0 };
    if (!jstr(line, "link", link, sizeof link) || !link[0]) { bput(&b, "{\"ok\":false,\"error\":\"need link\"}"); return b.p; }
    double mxv = 0; int max = jnum(line, "max", &mxv) ? (int)mxv : DSNET_PARTY_MAX; if (max < 1 || max > DSNET_PARTY_MAX) max = DSNET_PARTY_MAX;
    dsnet_client *c = cli(); uint8_t u[16];
    if (!c || dsnet_my_uid(c, u)) { bput(&b, "{\"ok\":false,\"error\":\"not in a multiplayer session\"}"); return b.p; }
    hex_of(u, me);
    static wk_build wb[WK_MAX_UNITS]; int fpm = 0;
    int n = wk_decode(link, wb, WK_MAX_UNITS, &fpm, err, sizeof err);
    if (n < 1) { bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
    if (fpm) { bput(&b, "{\"ok\":false,\"error\":\"this link was made with other wiki data (another game version): make it again on the wiki\"}"); return b.p; }
    if (n > max) n = max;
    jv *entries = jarr();
    for (int i = 0; i < n; i++) {
        char nt[600] = "";
        jv *e = pb_entry(&wb[i], me, nt, sizeof nt, err, sizeof err);
        if (!e) { jfree(entries); bput(&b, "{\"ok\":false,\"error\":"); char m2[480]; snprintf(m2, sizeof m2, "unit %d: %s", i + 1, err); bstr(&b, m2); bput(&b, "}"); return b.p; }
        if (nt[0] && strlen(notes) + strlen(nt) + 16 < sizeof notes) { size_t L = strlen(notes); snprintf(notes + L, sizeof notes - L, "unit %d: %s", i + 1, nt); }
        jpush(entries, e);
    }
    int rc = 0; size_t sz = 0;
    if (dsnet_is_host(c)) host_take(me, entries);
    else { jv *m = jobj(); jset(m, "entries", jcopy(entries)); char *s = jdump(m, 0, &sz); jfree(m); rc = dsnet_mail_send(c, NULL, PVP_M_UNITS, s, sz); free(s); }
    say("wiki link: %zu unit(s) built (%s, %zu bytes)%s%s", entries->cnt, dsnet_is_host(c) ? "kept on the host" : rc ? "MAIL FAILED" : "mailed to the host", sz, notes[0] ? "; notes: " : "", notes);
    bput(&b, "{\"ok\":%s,\"n\":%zu,\"bytes\":%zu,\"names\":[", rc ? "false" : "true", entries->cnt, sz);
    for (size_t i = 0; i < entries->cnt; i++) { jv *nk = jpath(entries->v[i], "manifest.name_key"); bput(&b, "%s", i ? "," : ""); bstr(&b, nk && nk->t == JN_STR ? nk->s : "?"); }
    bput(&b, "],\"guids\":[");
    for (size_t i = 0; i < entries->cnt; i++) { jv *g = jpath(entries->v[i], "manifest.unit_guid"); bput(&b, "%s\"%s\"", i ? "," : "", g && g->t == JN_STR ? g->s : ""); }
    bput(&b, "],\"notes\":"); bstr(&b, notes); bput(&b, "}");
    jfree(entries);
    return b.p;
}
static char *cmd_start(void) {
    Buf b = { 0 }; dsnet_client *c = cli();
    if (!c || !dsnet_is_host(c)) { bput(&b, "{\"ok\":false,\"error\":\"host only\"}"); return b.p; }
    if (!recv_entries || !recv_entries->cnt) { bput(&b, "{\"ok\":false,\"error\":\"nobody brought units\"}"); return b.p; }
    if (cur_map() != 0) { bput(&b, "{\"ok\":false,\"error\":\"start from the settlement (the brought units arrive there)\"}"); return b.p; }
    /* what the match end brings back: the world and the owner tables */
    if (pvp_snap_world_lock()) { bput(&b, "{\"ok\":false,\"error\":\"world snapshot failed\"}"); return b.p; }
    free(owners_kept); owners_len = dsnet_export_owners(c, NULL, 0);
    owners_kept = owners_len ? malloc(owners_len) : NULL;
    if (owners_kept) dsnet_export_owners(c, owners_kept, owners_len);
    char rid[33]; fe_new_id(rid);
    jv *maps = fe_hub_maps(recv_entries);
    /* DSNET_FE_START payload: run_id[16], u16 nu, (guid, uid)..., u16 ni, (guid, uid)... */
    size_t cap = 20 + recv_entries->cnt * 32; for (size_t i = 0; i < maps->cnt; i++) cap += maps->v[i]->cnt * 32;
    uint8_t *pl = calloc(1, cap), *q = pl;
    unhex(rid, q); q += 16;
    uint8_t *nu_at = q; q += 2; int nu = 0, ni = 0;
    for (size_t i = 0; i < recv_entries->cnt; i++) {
        jv *m = jget(recv_entries->v[i], "manifest"); uint8_t uid[16]; unhex(jget(m, "player_uid")->s, uid);
        if (guid_bytes(jget(maps->v[i], jget(m, "unit_guid")->s)->s, q)) continue;
        memcpy(q + 16, uid, 16); q += 32; nu++;
    }
    uint8_t *ni_at = q; q += 2;
    for (size_t i = 0; i < recv_entries->cnt; i++) {
        jv *m = jget(recv_entries->v[i], "manifest"), *mp = maps->v[i]; uint8_t uid[16]; unhex(jget(m, "player_uid")->s, uid);
        for (size_t k = 0; k < mp->cnt; k++) {
            if (!strcmp(mp->k[k], jget(m, "unit_guid")->s) || guid_bytes(mp->v[k]->s, q)) continue;
            memcpy(q + 16, uid, 16); q += 32; ni++;
        }
    }
    nu_at[0] = (uint8_t)(nu & 255); nu_at[1] = (uint8_t)(nu >> 8); ni_at[0] = (uint8_t)(ni & 255); ni_at[1] = (uint8_t)(ni >> 8);
    size_t len = (size_t)(q - pl);
    if (len > DSNET_MAX_PAYLOAD) { free(pl); jfree(maps); bput(&b, "{\"ok\":false,\"error\":\"too many units / items\"}"); return b.p; }
    int rc = dsnet_submit(c, DSNET_FE_START, 0, pl, (uint16_t)len);
    free(pl);
    jfree(pending); pending = jobj();
    jset(pending, "run_id", jstrv(rid)); jset(pending, "entries", jcopy(recv_entries)); jset(pending, "maps", maps);
    say("start: run %.8s, %d unit(s), %d item(s), owners kept (%zu bytes), FE_START rc %d", rid, nu, ni, owners_len, rc);
    bput(&b, "{\"ok\":%s,\"run\":\"%s\",\"units\":%d,\"items\":%d}", rc ? "false" : "true", rid, nu, ni);
    return b.p;
}
/* mp_core: DSNET_FE_START executed (every peer, same step) */
void pvp_set_hold(int on);
void pvp_on_units_start(const uint8_t *payload, size_t len) {
    char rid[33] = ""; if (len >= 16) hex_of(payload, rid);
    pvp_set_hold(1);                                         /* story events held until the old world is back */
    dsnet_client *c = cli();
    if (c && dsnet_is_host(c) && pending && jstreq(jget(pending, "run_id"), rid)) armed = 1;
    say("units start executed (run %.8s)%s", rid, armed ? ": the next join point builds the PvP world" : "");
}
/* mp_game.c g_save (host), right after the join-point save was written to `path`: armed = that file becomes the world
   with the brought units (same save name: the host reloads it and every guest loads it) */
int pvp_units_join_point(const char *path) {
    if (!armed) return 0;
    armed = 0;
    char err[400];
    jv *live = jload(path, err, sizeof err);
    if (!live) { say("build: cannot read the join-point save: %s", err); return -1; }
    jv *run = NULL;
    jv *w = fe_build_hub(live, jget(pending, "entries"), jget(pending, "run_id")->s, jget(pending, "maps"), &run, err, sizeof err);
    jfree(live);
    if (!w) { say("build REFUSED: %s", err); return -1; }
    char probs[400];
    if (fe_check_refs(w, jget(run, "removed_host_guids"), probs, sizeof probs)) { say("build: reference check failed: %s", probs); jfree(w); jfree(run); return -1; }
    /* keep the host's save name (the reload and every guest load this file under it) */
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.pvp", path);
    unlink(tmp);
    if (jsave_new(w, tmp) || rename(tmp, path)) { say("build: write failed"); jfree(w); jfree(run); return -1; }
    n_built++;
    say("PvP world built: %d unit(s) in the clan", fe_clan_count(w));
    jfree(w); jfree(run);
    return 1;
}
int pvp_units_received(void) { dsnet_client *c = cli(); return c && dsnet_is_host(c) ? n_received : 0; }
int pvp_units_built(void) { return n_built; }
/* pvp_game.c: the world snapshot was reloaded after the match (host): put the owner tables back */
void pvp_units_after_match(void) { if (owners_kept) owners_restore_due = 1; }

/* one mail of ours (kinds 11-16; FE uses 1-5). 1 = taken. In the merged build fe/fe_game.c fe_poll reads the mailbox and
   hands other kinds here; the lane build (no FE glue) polls itself in pvp_units_frame. */
int pvp_mail_take(dsnet_client *c, const uint8_t from[16], uint8_t kind, const uint8_t *data, size_t len) {
    if (pvp_mods_mail(c, from, kind, data, len)) return 1;
    if (kind < PVP_M_UNITS || kind > PVP_M_ROSTER) return 0;
    char uh[33]; hex_of(from, uh);
    if (kind == PVP_M_UNITS && dsnet_is_host(c)) {
        char err[200]; jv *m = jparse((const char *)data, len, err, sizeof err), *e = jget(m, "entries"); char ack[120];
        if (jis(e, JN_ARR)) { host_take(uh, e); snprintf(ack, sizeof ack, "{\"ok\":1,\"n\":%zu}", e->cnt); say("%zu unit(s) received from %.8s", e->cnt, uh); }
        else snprintf(ack, sizeof ack, "{\"ok\":0,\"error\":\"bad data\"}");
        dsnet_mail_send(c, from, PVP_M_UNITS_ACK, ack, strlen(ack));
        jfree(m);
    } else if (kind == PVP_M_UNITS_ACK) say("host answered: %.*s", (int)(len < 100 ? len : 100), (const char *)data);
    else if (kind == PVP_M_ROSTER && !dsnet_is_host(c)) {
        char err[200]; jv *m = jparse((const char *)data, len, err, sizeof err), *r = jget(m, "roster");
        nroster = 0;
        for (size_t i = 0; r && r->t == JN_ARR && i < r->cnt && nroster < 16; i++) {
            jv *x = r->v[i]; if (!jis(x, JN_ARR) || x->cnt < 3) continue;
            roster_e *e = &roster[nroster++];
            snprintf(e->uid, sizeof e->uid, "%.*s", (int)x->v[0]->n, x->v[0]->s ? x->v[0]->s : "");
            snprintf(e->name, sizeof e->name, "%.*s", (int)x->v[1]->n, x->v[1]->s ? x->v[1]->s : "");
            snprintf(e->port, sizeof e->port, "%.*s", (int)x->v[2]->n, x->v[2]->s ? x->v[2]->s : "");
        }
        say("roster from the host: %d brought unit(s)", nroster);
        jfree(m);
    }
    return 1;
}
static int fe_reads_mail(void) {                         /* FE's glue is in this dylib (merged build): it reads the mailbox */
    static int v = -1; if (v < 0) v = dlsym(RTLD_DEFAULT, "fe_poll") != NULL;
    return v;
}
void pvp_units_frame(void) {
    dsnet_client *c = cli(); if (!c) return;
    uint8_t from[16], kind, *data; size_t len;
    if (!fe_reads_mail()) while (dsnet_mail_poll(c, from, &kind, &data, &len)) { pvp_mail_take(c, from, kind, data, len); free(data); }
    pvp_mods_frame();
    if (owners_restore_due && dsnet_is_host(c) && cur_map() >= 0 && !busy()) {
        owners_restore_due = 0;
        int rc = dsnet_import_owners(c, owners_kept, owners_len);
        say("owner tables from before the match restored (%zu bytes): rc %d", owners_len, rc);
        free(owners_kept); owners_kept = NULL; owners_len = 0;
        jfree(recv_entries); recv_entries = NULL; n_received = 0;
        nroster = 0; roster_send(c);                                   /* the match is over: nobody's brought units any more */
    }
}
char *pvp_units_exec(const char *cmd, const char *line) {
    if (!strcmp(cmd, "pvp_units_bring")) return cmd_bring(line);
    if (!strcmp(cmd, "pvp_units_start")) return cmd_start();
    if (!strcmp(cmd, "pvp_units_wiki")) return cmd_wiki(line);
    if (!strcmp(cmd, "pvp_units_status")) {
        Buf b = { 0 };
        bput(&b, "{\"ok\":true,\"received\":%d,\"built\":%d,\"armed\":%d,\"owners_kept\":%zu,\"msg\":", n_received, n_built, armed, owners_len);
        bstr(&b, last_msg); bput(&b, ",\"names\":[");
        for (size_t i = 0; recv_entries && i < recv_entries->cnt; i++) {
            jv *m = jget(recv_entries->v[i], "manifest"), *nk = jget(m, "name_key"), *pu = jget(m, "player_uid");
            bput(&b, "%s[\"%s\",\"%.8s\"]", i ? "," : "", nk && nk->t == JN_STR ? nk->s : "?", pu && pu->t == JN_STR ? pu->s : "");
        }
        bput(&b, "]}");
        return b.p;
    }
    return NULL;
}
