/* Foreign Expedition, game side (plan F3-F6). Glue between the save library (fe_save.c), dsnet (mail, FE_START) and
 * the game (join-point save, loot tags, recruit refusal, delete guard). No menus yet (F4): rig commands fe_*.
 *
 * Flow:
 *   every player: fe_bring {file, guids}  -> export from a save (read only), keep the export locally, mail it to the host
 *   host:         fe_start {death, xp, items} -> gear mode Own storage, DSNET_FE_START (new owners); at the next join
 *                 point the host's save IS the hub (built from the live world + the units) and every peer loads it
 *   host:         fe_end -> hold, save, results per player, mailed (again on each rejoin until acknowledged)
 *   every player: fe_apply {run} -> backup of the whole Saves folder FIRST, then a NEW save (never the original)
 * Files: <rig_dir>/data/fe/{exports,runs/<run>,results,claims.json,protected.json}; backups: env DSRIG_FE_BACKUP or
 * <rig_dir>/data/fe/backups/<time>/. */
#include "rig.h"
#include "mp.h"
#include "mp_game.h"
#include "net/dsnet.h"
#include "fe_save.h"
#include "fe_ui_api.h"
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { FE_M_UNITS = 1, FE_M_UNITS_ACK = 2, FE_M_RESULT = 3, FE_M_RESULT_ACK = 4, FE_M_MODE = 5 };
#define FE_HUB_PREFIX "fe5ea00"        /* hub campaign guids start with this: every peer can tell it runs a hub; the 8th
                                          hex digit is the host option "Recruiting in the hub" (0 Off = not offered, 1 On) */

static jv *recv_entries;               /* host: entries received (closure + manifest), any player */
static jv *pending_start;              /* host: {run_id, rules, entries, maps} between fe_start and the join point */
static jv *run_m;                      /* host: the running hub's manifest (+ rules) */
static jv *loot;                       /* host: {item guid: {found_in_dungeon, player_uid}} */
static jv *last_alive;                 /* host: {hub guid: closure} from the last save where it was alive */
static char last_msg[300];
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

/* ---------------------------------------------------------------- files */
static void fe_path(char *o, size_t n, const char *sub) { snprintf(o, n, "%s/data/fe%s%s", rig_dir, sub && *sub ? "/" : "", sub ? sub : ""); }
static void mkdirs(const char *p) {
    char b[1500]; snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
    mkdir(b, 0755);
}
static int write_json(const char *path, const jv *v) {
    size_t n; char *s = jdump(v, 1, &n);
    char tmp[1600]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) { free(s); return -1; }
    size_t w = fwrite(s, 1, n, f); fclose(f); free(s);
    return w == n ? rename(tmp, path) : -1;
}
static jv *read_json(const char *path) { char e[200]; return jload(path, e, sizeof e); }
static int copy_file(const char *a, const char *b) {
    FILE *i = fopen(a, "rb"); if (!i) return -1;
    FILE *o = fopen(b, "wbx"); if (!o) { fclose(i); return -1; }      /* never overwrite */
    char buf[1 << 16]; size_t n; int r = 0;
    while ((n = fread(buf, 1, sizeof buf, i)) > 0) if (fwrite(buf, 1, n, o) != n) { r = -1; break; }
    fclose(i); if (fclose(o)) r = -1;
    return r;
}
/* F6 backup (lead 2026-10-14): before any apply, the WHOLE save folder is copied. Returns files copied, -1 on error. */
static int backup_folder(const char *dir, char *out, size_t on) {
    char root[1200], ts[40];
    const char *env = getenv("DSRIG_FE_BACKUP");
    if (env && *env) snprintf(root, sizeof root, "%s", env); else fe_path(root, sizeof root, "backups");
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(ts, sizeof ts, "%Y-%m-%d-%H%M%S", &tm);
    snprintf(out, on, "%s/%s", root, ts);
    for (int k = 2; access(out, F_OK) == 0 && k < 100; k++) snprintf(out, on, "%s/%s-%d", root, ts, k);
    mkdirs(out);
    DIR *d = opendir(dir); if (!d) return -1;
    int n = 0, bad = 0;
    for (struct dirent *e; (e = readdir(d)); ) {
        if (e->d_name[0] == '.') continue;
        char a[1500], b[1500]; snprintf(a, sizeof a, "%s/%s", dir, e->d_name); snprintf(b, sizeof b, "%s/%s", out, e->d_name);
        struct stat st; if (stat(a, &st) || !S_ISREG(st.st_mode)) continue;
        if (copy_file(a, b)) bad++; else n++;
    }
    closedir(d);
    return bad ? -1 : n;
}
static void hex_of(const uint8_t *u, char o[33]) { for (int i = 0; i < 16; i++) snprintf(o + 2 * i, 3, "%02x", u[i]); }
static int unhex(const char *s, uint8_t u[16]) {
    if (!s || strlen(s) != 32) return -1;
    for (int i = 0; i < 16; i++) { unsigned v; if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1; u[i] = (uint8_t)v; }
    return 0;
}
static int guid_bytes(const char *s, uint8_t g[16]) { Guid x; if (!guid_parse(s, &x)) return -1; memcpy(g, &x, 16); return 0; }
static dsnet_client *cli(void) { mp_session *s = mp_game_session(); return s ? mp_client(s) : NULL; }
static int my_uid_hex(char o[33]) { uint8_t u[16]; dsnet_client *c = cli(); if (!c || dsnet_my_uid(c, u)) return -1; hex_of(u, o); return 0; }
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(last_msg, sizeof last_msg, fmt, ap); va_end(ap); rig_log("fe: %s", last_msg); }

/* ---------------------------------------------------------------- hub world test (every peer, deterministic) */
int fe_world_is_hub(void) {
    Obj c = cdc(); Obj clan = c ? AT(c, 0x50, Obj) : NULL;
    if (!clan) return 0;
    char gs[40]; guid_str(AT(clan, 0x10, Guid), gs);
    return !strncmp(gs, FE_HUB_PREFIX, 7) && (gs[7] == '0' || gs[7] == '1') ? 1 + (gs[7] == '1') : 0;
}
/* recruiting allowed in this world: not a hub, or a hub whose host turned "Recruiting in the hub" On. Recruits then
   belong to the recruiter (normal recruit rule) and stay in the hub: they are not in the run, so nothing sends them home. */
int fe_recruit_allowed(void) { return fe_world_is_hub() != 1; }

/* ---------------------------------------------------------------- bring units (every player) */
static void host_take_entries(const char *uid_hex, jv *entries) {
    if (!recv_entries) recv_entries = jarr();
    for (size_t i = 0; i < recv_entries->cnt; ) {            /* a new send replaces the old one */
        if (jstreq(jpath(recv_entries->v[i], "manifest.player_uid"), uid_hex)) jarr_del(recv_entries, i); else i++;
    }
    for (size_t i = 0; i < entries->cnt; i++) {
        jv *e = jcopy(entries->v[i]);
        jset(jget(e, "manifest"), "player_uid", jstrv(uid_hex));                    /* the server-stamped sender, not his claim */
        jpush(recv_entries, e);
    }
}
/* the exports kept locally (data/fe/exports/<export_id>.json; source_file = the save each came from, none for My Units /
   wiki copies), then the entries go to the host (mail) or straight into the host's list. A new send replaces this
   player's last one. files: one per entry (raw path or NULL). Returns the JSON answer (malloc). */
static char *send_entries(jv *entries, const char *const *files, const char *me, const char *what) {
    Buf b = { 0 }; dsnet_client *c = cli();
    char dir[1300]; fe_path(dir, sizeof dir, "exports"); mkdirs(dir);
    for (size_t i = 0; i < entries->cnt; i++) {
        jv *keep = jcopy(entries->v[i]);
        if (files && files[i]) jset(keep, "source_file", jstrv(files[i]));     /* raw path: test paths have no quotes */
        char p[1500]; snprintf(p, sizeof p, "%s/%s.json", dir, jget(jget(entries->v[i], "manifest"), "export_id")->s);
        write_json(p, keep); jfree(keep);
    }
    int rc = 0; size_t sz = 0;
    if (dsnet_is_host(c)) host_take_entries(me, entries);
    else {
        jv *m = jobj(); jset(m, "entries", jcopy(entries));
        char *s = jdump(m, 0, &sz); jfree(m);
        rc = dsnet_mail_send(c, NULL, FE_M_UNITS, s, sz); free(s);
    }
    say("brought %zu unit(s) from %s (%s, %zu bytes)", entries->cnt, what, dsnet_is_host(c) ? "kept on the host" : rc ? "MAIL FAILED" : "mailed to the host", sz);
    bput(&b, "{\"ok\":%s,\"n\":%zu,\"bytes\":%zu}", rc ? "false" : "true", entries->cnt, sz);
    return b.p;
}
static char *cmd_bring(const char *line) {
    char file[1024] = "", gl[2048] = "", err[400] = "", me[33];
    Buf b = { 0 };
    if (!jstr(line, "file", file, sizeof file) || !jstr(line, "guids", gl, sizeof gl)) { bput(&b, "{\"ok\":false,\"error\":\"need file and guids (comma separated)\"}"); return b.p; }
    dsnet_client *c = cli();
    if (!c || my_uid_hex(me)) { bput(&b, "{\"ok\":false,\"error\":\"not in a multiplayer session\"}"); return b.p; }
    jv *save = jload(file, err, sizeof err);
    if (!save) { bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
    jv *entries = jarr();
    const char *files[64]; size_t nf = 0;
    char *sp = NULL;
    for (char *t = strtok_r(gl, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
        while (*t == ' ') t++;
        jv *e = fe_export_entry(save, t, me, err, sizeof err);
        if (!e || nf >= 64) { jfree(e); jfree(entries); jfree(save); bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, e ? "too many units" : err); bput(&b, "}"); return b.p; }
        jpush(entries, e); files[nf++] = file;
    }
    jfree(save);
    char *r = send_entries(entries, files, me, file);
    jfree(entries);
    return r;
}
#if __has_include("mu_bring.h") && __has_include("myunits.h")
/* Bring Units (custom/units/mu_bring.h, lane units): the picks of one OK = one send. Save picks are exported from their
   save (read only); My Units picks become copies through mu_entries (manifest source "library": never applied home). */
#include "mu_bring.h"
#include "myunits.h"
int fe_bring_picks(const mu_pick *picks, int n, char *err, size_t errn) {
    char me[33]; dsnet_client *c = cli();
    if (!c || my_uid_hex(me)) { snprintf(err, errn, "Not in a multiplayer session."); return -1; }
    jv *entries = jarr(); const char *files[64]; size_t nf = 0;
    static char ids[64][17]; int nl = 0;
    for (int i = 0; i < n && nf < 64; i++) {
        if (picks[i].source == MU_SRC_LIBRARY) { if (nl < 64) snprintf(ids[nl++], 17, "%s", picks[i].lib_id); continue; }
        char e1[300]; jv *save = jload(picks[i].save_file, e1, sizeof e1);
        jv *e = save ? fe_export_entry(save, picks[i].unit_guid, me, e1, sizeof e1) : NULL;
        jfree(save);
        if (!e) { jfree(entries); snprintf(err, errn, "Could not bring %s: %s", picks[i].name, e1); return -1; }
        jpush(entries, e); files[nf++] = picks[i].save_file;
    }
    if (nl) {
        char e2[300] = "", notes[1000] = ""; jv *lib = mu_load(e2, sizeof e2);
        jv *le = lib ? mu_entries(lib, ids, nl, me, notes, sizeof notes, e2, sizeof e2) : NULL;
        jfree(lib);
        if (!le) { jfree(entries); snprintf(err, errn, "Could not bring the My Units units: %s", e2); return -1; }
        if (notes[0]) say("My Units: %s", notes);
        for (size_t i = 0; i < le->cnt && nf < 64; i++) { jpush(entries, jcopy(le->v[i])); files[nf++] = NULL; }
        jfree(le);
    }
    char *r = send_entries(entries, files, me, "Bring Units");
    int ok = r && strstr(r, "\"ok\":true");
    if (!ok) snprintf(err, errn, "The units could not be sent to the host.");
    free(r); jfree(entries);
    return ok ? 0 : -1;
}
#endif
/* ---------------------------------------------------------------- start (host) */
static const char *fe_host_name(void);
static char *cmd_start(const char *line) {
    Buf b = { 0 }; dsnet_client *c = cli();
    if (!c || !dsnet_is_host(c)) { bput(&b, "{\"ok\":false,\"error\":\"host only\"}"); return b.p; }
    if (!recv_entries || !recv_entries->cnt) { bput(&b, "{\"ok\":false,\"error\":\"nobody brought units yet\"}"); return b.p; }
    double d = 1, x = 1, it = 1, rec = 0; jnum(line, "death", &d); jnum(line, "xp", &x); jnum(line, "items", &it); jnum(line, "recruit", &rec);
    char rid[33]; fe_new_id(rid);
    jv *maps = fe_hub_maps(recv_entries);
    /* payload: run_id[16], u16 nu, (guid, uid)..., u16 ni, (guid, uid)... */
    size_t cap = 20 + recv_entries->cnt * 32; for (size_t i = 0; i < maps->cnt; i++) cap += maps->v[i]->cnt * 32;
    uint8_t *pl = calloc(1, cap), *q = pl;
    unhex(rid, q); q += 16;
    uint8_t *nu_at = q; q += 2; int nu = 0, ni = 0;
    for (size_t i = 0; i < recv_entries->cnt; i++) {
        jv *m = jget(recv_entries->v[i], "manifest");
        uint8_t uid[16]; unhex(jget(m, "player_uid")->s, uid);
        if (guid_bytes(jget(maps->v[i], jget(m, "unit_guid")->s)->s, q)) continue;
        memcpy(q + 16, uid, 16); q += 32; nu++;
    }
    uint8_t *ni_at = q; q += 2;
    for (size_t i = 0; i < recv_entries->cnt; i++) {
        jv *m = jget(recv_entries->v[i], "manifest"), *mp = maps->v[i];
        uint8_t uid[16]; unhex(jget(m, "player_uid")->s, uid);
        for (size_t k = 0; k < mp->cnt; k++) {
            if (!strcmp(mp->k[k], jget(m, "unit_guid")->s)) continue;
            if (guid_bytes(mp->v[k]->s, q)) continue;
            memcpy(q + 16, uid, 16); q += 32; ni++;
        }
    }
    nu_at[0] = nu & 255; nu_at[1] = nu >> 8; ni_at[0] = ni & 255; ni_at[1] = ni >> 8;
    size_t len = (size_t)(q - pl);
    if (len > DSNET_MAX_PAYLOAD) { free(pl); jfree(maps); bput(&b, "{\"ok\":false,\"error\":\"too many units / items for one command\"}"); return b.p; }
    dsnet_rules r; dsnet_rules_get(c, &r); r.gear_mode = DSNET_GEAR_OWN_STORAGE;     /* items must know their owner */
    int rr = dsnet_set_rules(c, &r);
    int rc = dsnet_submit(c, DSNET_FE_START, 0, pl, (uint16_t)len);
    free(pl);
    jfree(pending_start); pending_start = jobj();
    jv *ru = jobj(); jset(ru, "death_permanent", jbool(d != 0)); jset(ru, "xp_counts", jbool(x != 0)); jset(ru, "items_back", jbool(it != 0)); jset(ru, "recruit_in_hub", jbool(rec != 0)); jset(ru, "host_name", jstrv(fe_host_name()));
    jset(pending_start, "run_id", jstrv(rid)); jset(pending_start, "rules", ru);
    jset(pending_start, "entries", jcopy(recv_entries)); jset(pending_start, "maps", maps);
    say("start: run %.8s, %d unit(s), %d item(s), rules rc %d, FE_START rc %d", rid, nu, ni, rr, rc);
    bput(&b, "{\"ok\":%s,\"run\":\"%s\",\"units\":%d,\"items\":%d}", rc ? "false" : "true", rid, nu, ni);
    return b.p;
}
/* host page / lobby Start (mpmenu, board FESettingsA): the same as fe_start. 0 ok, else err says why for the host. */
int fe_menu_start(int death, int xp, int loot, int recruit, char *err, size_t errn) {
    char line[160]; snprintf(line, sizeof line, "{\"cmd\":\"fe_start\",\"death\":%d,\"xp\":%d,\"items\":%d,\"recruit\":%d}", death, xp, loot, recruit);
    char *r = cmd_start(line);
    int ok = r && strstr(r, "\"ok\":true") != NULL;
    if (!ok) {
        if (r && strstr(r, "nobody brought")) snprintf(err, errn, "Nobody brought units yet: Bring units first.");
        else if (r && strstr(r, "host only")) snprintf(err, errn, "Only the host starts a Foreign Expedition.");
        else snprintf(err, errn, "The Foreign Expedition could not start.");
    }
    free(r);
    return ok ? 0 : -1;
}
/* mp_core: DSNET_FE_START executed (every peer, same step). The host arms the hub build for the next join point. */
static int armed;
void fe_on_start(const uint8_t *payload, size_t len) {
    char rid[33] = ""; if (len >= 16) hex_of(payload, rid);
    dsnet_client *c = cli();
    if (c && dsnet_is_host(c) && pending_start && jstreq(jget(pending_start, "run_id"), rid)) armed = 1;
    say("FE_START executed (run %.8s)%s", rid, armed ? ": the next join point builds the hub" : "");
}
static void snapshot_alive(const jv *save) {                  /* death not permanent: last state where each unit lived */
    if (!run_m) return;
    if (!last_alive) last_alive = jobj();
    jv *rows = jget(run_m, "units"); char e[200];
    for (size_t i = 0; rows && i < rows->cnt; i++) {
        const char *hg = jget(rows->v[i], "hub_guid")->s;
        if (!fe_side_of(save, hg)) continue;
        jv *cl = fe_export_unit(save, hg, e, sizeof e);
        if (cl) jset(last_alive, hg, cl);
    }
    char p[1300], sub[200]; snprintf(sub, sizeof sub, "runs/%s/last_alive.json", jget(run_m, "run_id")->s);
    fe_path(p, sizeof p, sub); write_json(p, last_alive);
}
int fe_hub_template_save(const jv *save);
int fe_hub_stock_json(jv *hub);
/* mp_game g_save, host, right after the join-point save was written to `path` (Saves/mp_sync.json). Armed: that file
   becomes the hub (same SaveName, so the reload and every guest load it). A running hub: keep the last-alive states. */
int fe_join_point_save(const char *path, const char *save_name) {
    if (!armed) {
        if (run_m) { jv *s = read_json(path); if (s) { snapshot_alive(s); jfree(s); } }
        return 0;
    }
    armed = 0;
    char err[400];
    jv *live = jload(path, err, sizeof err);
    if (!live) { say("hub build: cannot read the join-point save: %s", err); return -1; }
    jv *run = NULL;
    if (fe_hub_template_save(live)) fe_hub_stock_json(live);              /* a neutral hub: its stock, before the build counts the start items */
    jv *hub = fe_build_hub(live, jget(pending_start, "entries"), jget(pending_start, "run_id")->s, jget(pending_start, "maps"), &run, err, sizeof err);
    jfree(live);
    if (!hub) { say("hub build REFUSED: %s", err); return -1; }
    /* hub campaign guid: recognisable prefix (fe_world_is_hub on every peer), same id in header and clan record */
    char cg[40]; snprintf(cg, sizeof cg, "%s", jget(run, "hub_campaign_guid")->s); memcpy(cg, FE_HUB_PREFIX, 7);
    cg[7] = jtruthy(jpath(pending_start, "rules.recruit_in_hub")) ? '1' : '0';
    jset(jget(hub, "CampaignSaveHeader"), "CampaignGuid", jstrv(cg)); jset(jget(hub, "ClanSaveData"), "CampaignGuid", jstrv(cg));
    jset(run, "hub_campaign_guid", jstrv(cg));
    jset(jget(hub, "CampaignSaveHeader"), "SaveName", jstrv(save_name));
    char probs[400];
    if (fe_check_refs(hub, jget(run, "removed_host_guids"), probs, sizeof probs)) { say("hub build: reference check failed: %s", probs); jfree(hub); jfree(run); return -1; }
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.fe", path);
    unlink(tmp);
    if (jsave_new(hub, tmp) || rename(tmp, path)) { say("hub build: write failed"); jfree(hub); jfree(run); return -1; }
    jset(run, "rules", jcopy(jget(pending_start, "rules")));
    jfree(run_m); run_m = run;
    jfree(loot); loot = jobj(); jfree(last_alive); last_alive = NULL;
    snapshot_alive(hub);
    char p[1300], sub[200]; snprintf(sub, sizeof sub, "runs/%s", jget(run_m, "run_id")->s);
    fe_path(p, sizeof p, sub); mkdirs(p);
    snprintf(sub, sizeof sub, "runs/%s/run.json", jget(run_m, "run_id")->s); fe_path(p, sizeof p, sub); write_json(p, run_m);
    say("hub built: run %.8s, %d unit(s), campaign %s", jget(run_m, "run_id")->s, fe_clan_count(hub), cg);
    jfree(hub);
    return 1;
}
/* ord_mp it_tag: an item got an owner. Host + dungeon map = dungeon loot (owner 2026-10-14: only that comes back). */
/* key + amount of an item lying on a map: EntityDataContainer (cdc +0x20) +0x28 Dictionary<MapType, ItemDataContainer>;
   +0x20 _onMapItems Dictionary<Guid, ItemHolder> (entry stride 0x38: guid +8, ItemHolder {Guid, Key +0x10, Amount +0x18}
   at +0x18). Same walk as ui_items. */
static int map_item(int map, const uint8_t g[16], char *key, size_t kn, int *amount) {
    Obj c0 = cdc(), edc = c0 ? AT(c0, 0x20, Obj) : NULL;
    Obj ic = edc ? dict_int_get(AT(edc, 0x28, Obj), map) : NULL, om = ic ? AT(ic, 0x20, Obj) : NULL;
    Obj ent = om ? AT(om, 0x18, Obj) : NULL; int n = om ? AT(om, 0x20, int32_t) : 0;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x38;
        if (*(int32_t *)e < 0 || memcmp(e + 8, g, 16)) continue;
        Obj ks = *(Obj *)(e + 0x28); size_t k = 0;
        if (ks) { int32_t l = AT(ks, 0x10, int32_t); const uint16_t *ch = (const uint16_t *)((char *)ks + 0x14); for (int j = 0; j < l && k + 1 < kn; j++) key[k++] = (char)ch[j]; }
        key[k] = 0; *amount = *(int32_t *)(e + 0x30); return 0;
    }
    return -1;
}
void fe_note_loot(const uint8_t item[16], int player) {
    dsnet_client *c = cli();
    if (!run_m || !c || !dsnet_is_host(c) || cur_map() == 0 || player < 0) return;
    uint8_t uid[16]; if (dsnet_player_uid(c, player, uid)) return;
    char gs[40], uh[33]; Guid g; memcpy(&g, item, 16); guid_str(g, gs); hex_of(uid, uh);
    if (!loot) loot = jobj();
    jv *t = jobj(); jset(t, "found_in_dungeon", jbool(1)); jset(t, "player_uid", jstrv(uh)); jset(t, "map", jnum_i(cur_map()));
    /* key + amount at pick-up: a stackable item is merged into a stack (its own guid is gone by the end), so the
       results bring such loot back by key and amount (dive 2026-10-15: a picked mana stone merged into a base stack) */
    char key[80] = ""; int am = 0;
    extern int ord_last_pickup(const uint8_t g[16], char *key, size_t kn, int *amount);   /* ord_game.c: read before the pick-up */
    if ((!ord_last_pickup(item, key, sizeof key, &am) || !map_item(cur_map(), item, key, sizeof key, &am)) && key[0]) { jset(t, "key", jstrv(key)); jset(t, "amount", jnum_i(am > 0 ? am : 1)); }
    jset(loot, gs, t);
    rig_log("fe: loot %s %s x%d -> %.8s", gs, key[0] ? key : "?", am, uh);
}

/* ---------------------------------------------------------------- end (host) */
static double last_send[64]; static char sent_uid[64][33]; static int nsent;
static double *send_clock(const char *uh) {
    for (int i = 0; i < nsent; i++) if (!strcmp(sent_uid[i], uh)) return &last_send[i];
    if (nsent == 64) return &last_send[0];
    snprintf(sent_uid[nsent], 33, "%s", uh); last_send[nsent] = 0; return &last_send[nsent++];
}
static int send_result_file(const char *path, const char *uh) {
    dsnet_client *c = cli(); uint8_t uid[16];
    if (!c || unhex(uh, uid)) return -1;
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n); size_t got = fread(b, 1, (size_t)n, f); fclose(f);
    int rc = got == (size_t)n ? dsnet_mail_send(c, uid, FE_M_RESULT, b, (size_t)n) : -1;
    free(b);
    *send_clock(uh) = now_s();
    return rc;
}
static char *cmd_end(const char *line) {
    (void)line; Buf b = { 0 }; dsnet_client *c = cli();
    if (!c || !dsnet_is_host(c) || !run_m) { bput(&b, "{\"ok\":false,\"error\":\"host of a running Foreign Expedition only\"}"); return b.p; }
    if (cur_map() != 0) { bput(&b, "{\"ok\":false,\"error\":\"Return to the settlement first\"}"); return b.p; }
    int hr = dsnet_hold(c, 1, DSNET_HOLD_OTHER, NULL);
    const char *rid = jget(run_m, "run_id")->s;
    char name[64]; snprintf(name, sizeof name, "MPFE_%.8s_end", rid);
    if (!rig_save_campaign(name)) { bput(&b, "{\"ok\":false,\"error\":\"the game did not save\"}"); return b.p; }
    char sp[1400], rp[1400], sub[300], err[300];
    snprintf(sp, sizeof sp, "%s/data/Saves/%s.json", rig_dir, name);
    snprintf(sub, sizeof sub, "runs/%s/hub_end.json", rid); fe_path(rp, sizeof rp, sub);
    if (rename(sp, rp)) snprintf(rp, sizeof rp, "%s", sp);       /* out of Saves/: the load list never shows it */
    jv *end = jload(rp, err, sizeof err);
    if (!end) { bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
    snprintf(sub, sizeof sub, "runs/%s/loot.json", rid); fe_path(sp, sizeof sp, sub); if (loot) write_json(sp, loot);
    jv *ru = jget(run_m, "rules");
    fe_rules r = { jtruthy(jget(ru, "death_permanent")), jtruthy(jget(ru, "xp_counts")), jtruthy(jget(ru, "items_back")) };
    jv *players = jobj(), *rows = jget(run_m, "units");
    for (size_t i = 0; rows && i < rows->cnt; i++) jset(players, jget(rows->v[i], "player_uid")->s, jnull());
    snprintf(sub, sizeof sub, "runs/%s/results", rid); fe_path(sp, sizeof sp, sub); mkdirs(sp);
    int made = 0, sent = 0;
    for (size_t i = 0; i < players->cnt; i++) {
        const char *uh = players->k[i];
        jv *pl = jobj();                                            /* this player's dungeon loot */
        for (size_t k = 0; loot && k < loot->cnt; k++) if (jstreq(jget(loot->v[k], "player_uid"), uh)) jset(pl, loot->k[k], jcopy(loot->v[k]));
        jv *res = fe_extract_results(end, run_m, r, uh, pl, last_alive, err, sizeof err);
        jfree(pl);
        if (!res) { say("results for %.8s: %s", uh, err); continue; }
        char p[1500]; snprintf(p, sizeof p, "%s/%s.json", sp, uh);
        write_json(p, res); jfree(res); made++;
        if (!send_result_file(p, uh)) sent++;
    }
    jfree(players); jfree(end);
    say("ended run %.8s: hold %d, %d result(s), %d mailed", rid, hr, made, sent);
    bput(&b, "{\"ok\":true,\"run\":\"%s\",\"results\":%d,\"mailed\":%d}", rid, made, sent);
    return b.p;
}

/* ---------------------------------------------------------------- results + apply (every player) */
static jv *my_manifests(void) {
    jv *m = jobj(); char dir[1300]; fe_path(dir, sizeof dir, "exports");
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)); ) {
        if (!strstr(e->d_name, ".json")) continue;
        char p[1500]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        jv *x = read_json(p); if (!x) continue;
        jv *man = jcopy(jget(x, "manifest")); if (jget(x, "source_file")) jset(man, "source_file", jcopy(jget(x, "source_file")));
        jset(m, jget(man, "export_id")->s, man); jfree(x);
    }
    if (d) closedir(d);
    return m;
}
static void on_result(const uint8_t from[16], const uint8_t *data, size_t len) {
    char err[200]; jv *res = jparse((const char *)data, len, err, sizeof err);
    if (!res || !fe_result_hash_ok(res)) { say("results refused: damaged"); jfree(res); return; }
    jv *mans = my_manifests(), *u = jget(res, "units"); int mine = 1;
    for (size_t i = 0; u && i < u->cnt; i++) if (!jget(mans, jget(u->v[i], "export_id")->s)) mine = 0;
    jfree(mans);
    if (!mine) { say("These results are not for your units."); jfree(res); return; }
    char dir[1300], p[1500]; fe_path(dir, sizeof dir, "results"); mkdirs(dir);
    snprintf(p, sizeof p, "%s/%s.json", dir, jget(res, "run_id")->s);
    write_json(p, res);
    dsnet_client *c = cli();
    if (c) dsnet_mail_send(c, from, FE_M_RESULT_ACK, jget(res, "run_id")->s, strlen(jget(res, "run_id")->s));
    say("results of run %.8s received (%zu unit(s))", jget(res, "run_id")->s, u ? u->cnt : 0);
    jfree(res);
}
/* the protected originals: CampaignSaveLoader.Delete never removes them (iron-mode autosave after loading an applied save) */
int fe_is_protected(const char *stem) {
    char p[1300]; fe_path(p, sizeof p, "protected.json");
    jv *v = read_json(p); int r = v && jget(v, stem) != NULL;
    jfree(v);
    return r;
}
static void protect(const char *stem) {
    char p[1300]; fe_path(p, sizeof p, "protected.json");
    jv *v = read_json(p); if (!v) v = jobj();
    jset(v, stem, jbool(1)); write_json(p, v); jfree(v);
}
static char *newest_with_campaign(const char *dir, const char *cg) {
    DIR *d = opendir(dir); char *best = NULL; double bt = -1;
    for (struct dirent *e; d && (e = readdir(d)); ) {
        size_t l = strlen(e->d_name); if (l < 6 || strcmp(e->d_name + l - 5, ".json")) continue;
        char p[1500]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        FILE *f = fopen(p, "rb"); if (!f) continue;
        char head[8192]; size_t n = fread(head, 1, sizeof head - 1, f); fclose(f); head[n] = 0;
        char want[64]; snprintf(want, sizeof want, "\"CampaignGuid\": \"%s\"", cg);
        char *h = strstr(head, "\"CampaignSaveHeader\"");
        struct stat st;
        if (!h || !strstr(h, want) || stat(p, &st)) {
            /* the header is not always first in the file: fall back to a full parse */
            if (h) continue;
            jv *s = read_json(p); int ok = s && jstreq(jpath(s, "CampaignSaveHeader.CampaignGuid"), cg); jfree(s);
            if (!ok || stat(p, &st)) continue;
        }
        double t = st.st_mtimespec.tv_sec + st.st_mtimespec.tv_nsec / 1e9;
        if (t > bt) { bt = t; free(best); best = strdup(p); }
    }
    if (d) closedir(d);
    return best;
}
static char *cmd_apply(const char *line) {
    Buf b = { 0 }; char rid[64] = "", err[600], target[1024] = "";
    if (!jstr(line, "run", rid, sizeof rid)) { bput(&b, "{\"ok\":false,\"error\":\"need run\"}"); return b.p; }
    jstr(line, "target", target, sizeof target);
    char p[1500], sub[200]; snprintf(sub, sizeof sub, "results/%s.json", rid); fe_path(p, sizeof p, sub);
    jv *res = read_json(p);
    if (!res) { bput(&b, "{\"ok\":false,\"error\":\"no results for this run\"}"); return b.p; }
    jv *mans = my_manifests(), *u = jget(res, "units");
    const char *cg = NULL, *src = NULL; int mine = 0, custom = 0;
    for (size_t i = 0; u && i < u->cnt; i++) {
        jv *m = jget(mans, jget(u->v[i], "export_id")->s);
        if (!m) continue;
        mine++;
        if (fe_manifest_custom(m)) { custom++; continue; }        /* owner rule: never the home of a custom unit */
        if (!cg) { cg = jget(m, "home_campaign_guid")->s; src = jget(m, "source_file") ? jget(m, "source_file")->s : NULL; }
    }
    /* every unit is a My Units / wiki copy: refused BEFORE the backup and the home lookup (nothing to bring home) */
    if (mine && custom == mine) { say("apply refused: %s", FE_CUSTOM_REFUSE); jfree(res); jfree(mans); bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, FE_CUSTOM_REFUSE); bput(&b, "}"); return b.p; }
    if (!cg) { jfree(res); jfree(mans); bput(&b, "{\"ok\":false,\"error\":\"These results are not for your units.\"}"); return b.p; }
    char saves[1200]; snprintf(saves, sizeof saves, "%s/data/Saves", rig_dir);
    char *home_p = target[0] ? strdup(target) : newest_with_campaign(saves, cg);
    if (!home_p && src) home_p = strdup(src);                    /* the save the units came from (read only) */
    if (!home_p) { jfree(res); jfree(mans); bput(&b, "{\"ok\":false,\"error\":\"the home save is gone\"}"); return b.p; }
    /* F6: backup of the whole Saves folder FIRST (and the source's folder when it is elsewhere) */
    char bk[1300]; int nb = backup_folder(saves, bk, sizeof bk);
    char srcdir[1200]; snprintf(srcdir, sizeof srcdir, "%s", home_p); char *sl = strrchr(srcdir, '/'); if (sl) *sl = 0;
    int nb2 = 0; char bk2[1300] = "";
    if (strcmp(srcdir, saves)) nb2 = backup_folder(srcdir, bk2, sizeof bk2);
    if (nb < 0 || nb2 < 0) { free(home_p); jfree(res); jfree(mans); bput(&b, "{\"ok\":false,\"error\":\"backup failed: nothing applied\"}"); return b.p; }
    jv *home = jload(home_p, err, sizeof err);
    if (!home) { free(home_p); jfree(res); jfree(mans); bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, "}"); return b.p; }
    char choices_p[1024] = ""; jv *choices = NULL;
    if (jstr(line, "choices", choices_p, sizeof choices_p)) choices = read_json(choices_p);
    char led[1300]; fe_path(led, sizeof led, "claims.json");
    jv *rep = jarr();
    char *out = fe_apply_results(home, res, mans, led, saves, choices, rep, err, sizeof err);
    if (out) {
        char stem[400]; snprintf(stem, sizeof stem, "%s", jpath(home, "CampaignSaveHeader.SaveName") ? jpath(home, "CampaignSaveHeader.SaveName")->s : "");
        if (stem[0]) protect(stem);
        say("applied run %.8s -> %s (backup %s, %d files)", rid, out, bk, nb);
        bput(&b, "{\"ok\":true,\"file\":"); bstr(&b, out); bput(&b, ",\"backup\":"); bstr(&b, bk);
        bput(&b, ",\"backup_files\":%d,\"home\":", nb); bstr(&b, home_p); bput(&b, ",\"report\":[");
        for (size_t i = 0; i < rep->cnt; i++) { if (i) bput(&b, ","); bstr(&b, rep->v[i]->s); }
        bput(&b, "]}");
    } else {
        say("apply refused: %s", err);
        bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, err); bput(&b, ",\"backup\":"); bstr(&b, bk); bput(&b, "}");
    }
    free(out); free(home_p); jfree(home); jfree(res); jfree(mans); jfree(rep); jfree(choices);
    return b.p;
}

/* ---------------------------------------------------------------- every frame (mp_game_poll) */
static jv *mode_rx;                    /* guest: the host's last FE_M_MODE */
static int my_ack = -1;                /* units the host confirmed from me */
static fe_mode_info host_mode; static char mode_sent_json[600]; static uint8_t mode_sent_to[32][16]; static int nmode_sent;
static char *mode_json(void) {
    static char b[1200]; size_t o = 0;
    o += (size_t)snprintf(b + o, sizeof b - o, "{\"on\":%d,\"death\":%d,\"xp\":%d,\"loot\":%d,\"units\":%d,\"recruit\":%d,\"version\":\"%s\",\"host\":\"%s\",\"brought\":{",
                          host_mode.on, host_mode.death, host_mode.xp, host_mode.loot, host_mode.units, host_mode.recruit, host_mode.version, host_mode.host);
    jv *cnt = jobj();
    for (size_t i = 0; recv_entries && i < recv_entries->cnt; i++) {
        const char *u = jpath(recv_entries->v[i], "manifest.player_uid")->s; jv *x = jget(cnt, u);
        jset(cnt, u, jnum_i(x ? (long long)jnumv(x) + 1 : 1));
    }
    for (size_t i = 0; i < cnt->cnt && o < sizeof b - 60; i++) o += (size_t)snprintf(b + o, sizeof b - o, "%s\"%s\":%s", i ? "," : "", cnt->k[i], cnt->v[i]->s);
    jfree(cnt);
    snprintf(b + o, sizeof b - o, "}}");
    return b;
}
void fe_host_mode(int on, int death, int xp, int loot, int units, int recruit, const char *host_name) {
    if (on != host_mode.on) rig_log("fe: host mode %d -> %d (lobby World setting)", host_mode.on, on);
    host_mode.on = on; host_mode.death = death; host_mode.xp = xp; host_mode.loot = loot; host_mode.units = units; host_mode.recruit = recruit;
    snprintf(host_mode.host, sizeof host_mode.host, "%s", host_name ? host_name : "");
    if (!host_mode.version[0]) {                                  /* the save format version: "DS_" + the app version */
        Obj v = F(0x507B9CC, Obj (*)(void *))(NULL); char a[32] = "";
        if (v) { int32_t n = AT(v, 0x10, int32_t); const uint16_t *ch = (const uint16_t *)((char *)v + 0x14); int k = 0; for (int i = 0; i < n && k < 31; i++) a[k++] = (char)ch[i]; a[k] = 0; }
        snprintf(host_mode.version, sizeof host_mode.version, "%s%s", strncmp(a, "DS_", 3) ? "DS_" : "", a);
        rig_log("fe: save version of this build: %s (app version %s)", host_mode.version, a);
    }
}
static const char *fe_host_name(void) { return host_mode.host; }
int fe_mode_get(fe_mode_info *m) {
    dsnet_client *c = cli(); memset(m, 0, sizeof *m);
    if (!c) return 0;
    if (dsnet_is_host(c)) { *m = host_mode; return m->on; }
    if (!mode_rx) return 0;
    m->on = (int)jnumv(jget(mode_rx, "on")); m->death = (int)jnumv(jget(mode_rx, "death")); m->xp = (int)jnumv(jget(mode_rx, "xp"));
    m->loot = (int)jnumv(jget(mode_rx, "loot")); m->units = (int)jnumv(jget(mode_rx, "units")); m->recruit = (int)jnumv(jget(mode_rx, "recruit"));
    snprintf(m->version, sizeof m->version, "%s", jget(mode_rx, "version") ? jget(mode_rx, "version")->s : "");
    snprintf(m->host, sizeof m->host, "%s", jget(mode_rx, "host") ? jget(mode_rx, "host")->s : "");
    return m->on;
}
int fe_brought_of(int slot) {
    dsnet_client *c = cli(); uint8_t u[16]; char uh[33];
    if (!c || dsnet_player_uid(c, slot, u)) return -1;
    hex_of(u, uh);
    if (dsnet_is_host(c)) { int n = 0; for (size_t i = 0; recv_entries && i < recv_entries->cnt; i++) n += jstreq(jpath(recv_entries->v[i], "manifest.player_uid"), uh); return n; }
    jv *b = jget(jget(mode_rx, "brought"), uh);
    return b ? (int)jnumv(b) : 0;
}
int fe_my_brought(void) { dsnet_client *c = cli(); if (c && dsnet_is_host(c)) { char me[33]; if (my_uid_hex(me)) return -1; int n = 0; for (size_t i = 0; recv_entries && i < recv_entries->cnt; i++) n += jstreq(jpath(recv_entries->v[i], "manifest.player_uid"), me); return n; } return my_ack; }
static void host_mode_mail(dsnet_client *c) {                 /* on change, and to every newcomer */
    if (!host_mode.on && !mode_sent_json[0]) return;
    char *j = mode_json(); int changed = strcmp(j, mode_sent_json) != 0;
    /* dive 2026-10-15 06:51: a guest that joined while the host reloaded its join point never got the mode (the mail
       arrived before its handler was ready) and the Bring button stayed dead. Small message: sent again every 5 s. */
    static double last_all; if (now_s() - last_all > 5) { last_all = now_s(); nmode_sent = 0; }
    if (changed) { snprintf(mode_sent_json, sizeof mode_sent_json, "%s", j); nmode_sent = 0; }
    uint32_t pres = dsnet_present_mask(c); uint8_t me[16]; dsnet_my_uid(c, me);
    for (int k = 0; k < 32; k++) {
        uint8_t u[16]; if (!(pres >> k & 1) || dsnet_player_uid(c, k, u) || !memcmp(u, me, 16)) continue;
        int sent = 0; for (int i = 0; i < nmode_sent; i++) if (!memcmp(mode_sent_to[i], u, 16)) sent = 1;
        if (sent || nmode_sent >= 32) continue;
        int rc = dsnet_mail_send(c, u, FE_M_MODE, j, strlen(j)); memcpy(mode_sent_to[nmode_sent++], u, 16);
        static int nlog; if (nlog < 12 || rc) { nlog++; char uh[33]; hex_of(u, uh); rig_log("fe: mode mail to %.8s rc %d (slot %d, on %d)", uh, rc, k, host_mode.on); }
    }
    static double last_pl; if (host_mode.on && now_s() - last_pl > 10) { last_pl = now_s(); rig_log("fe: mode mail: present mask %#x", pres); }
}

/* ---- saves and units for the Bring window */
static int read_header(const char *p, fe_save_info *si) {
    jv *s = read_json(p); if (!s) return -1;
    jv *h = jget(s, "CampaignSaveHeader");
    snprintf(si->path, sizeof si->path, "%s", p);
    snprintf(si->clan, sizeof si->clan, "%s", jget(h, "ClanName") ? jget(h, "ClanName")->s : "?");
    snprintf(si->version, sizeof si->version, "%s", jget(h, "Version") ? jget(h, "Version")->s : "");
    si->day = (int)jnumv(jget(h, "PassedDay")); si->units = fe_clan_count(s);
    si->ok = jnumv(jget(h, "CurrentMap")) == 0;                   /* at the settlement */
    jfree(s); return 0;
}
static const char *game_saves_dir(char *b, size_t n) {
    const char *e = getenv("DSRIG_FE_GAME_SAVES");                /* tests: a folder of copies; never the real one */
    if (e) { snprintf(b, n, "%s", e); return b; }
    if (getenv("DSRIG_BATCH") || getenv("DSRIG_HIDEWIN") || getenv("DSMP_TEST")) return NULL;
    const char *home = getenv("HOME"); if (!home) return NULL;
    snprintf(b, n, "%s/Library/Application Support/CanOpener/Dungeon Settlers/Saves", home); return b;
}
int fe_list_saves(fe_save_info *out, int max) {
    char dirs[2][1100]; const char *lab[2] = { "Multiplayer folder", "Game folder" }; int nd = 0, n = 0;
    snprintf(dirs[nd++], sizeof dirs[0], "%s/data/Saves", rig_dir);
    if (game_saves_dir(dirs[1], sizeof dirs[1])) nd = 2;
    double mt[64];
    for (int d = 0; d < nd; d++) {
        DIR *dp = opendir(dirs[d]);
        for (struct dirent *e; dp && (e = readdir(dp)) && n < max && n < 64; ) {
            size_t l = strlen(e->d_name);
            if (l < 6 || strcmp(e->d_name + l - 5, ".json") || !strncmp(e->d_name, "mp_sync", 7) || !strncmp(e->d_name, "MPFE_", 5) || strstr(e->d_name, "SaveHeaderCache")) continue;
            char p[1400]; snprintf(p, sizeof p, "%s/%s", dirs[d], e->d_name);
            struct stat st; if (stat(p, &st)) continue;
            if (read_header(p, &out[n])) continue;
            snprintf(out[n].folder, sizeof out[n].folder, "%s", lab[d]);
            mt[n] = st.st_mtimespec.tv_sec; n++;
        }
        if (dp) closedir(dp);
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (mt[j] > mt[i]) { fe_save_info t = out[i]; out[i] = out[j]; out[j] = t; double x = mt[i]; mt[i] = mt[j]; mt[j] = x; }
    return n;
}
int fe_list_units(const char *path, fe_unit_info *out, int max) {
    jv *s = read_json(path); if (!s) return 0;
    jv *u = jpath(s, "EntityContainerSaveData.UnitContainerSaveData"), *ec = jpath(s, "EntityComponentSaveData.EntityComponents"); int n = 0;
    for (size_t i = 0; u && i < u->cnt; i++) {
        jv *pl = jget(u->v[i], "PlayerGuidList");
        for (size_t j = 0; pl && j < pl->cnt && n < max; j++) {
            fe_unit_info *x = &out[n]; memset(x, 0, sizeof *x);
            const char *g = pl->v[j]->s; jv *cm = jget(ec, g);
            snprintf(x->guid, sizeof x->guid, "%s", g);
            jv *pr = fe_comp(cm, 10), *st = fe_comp(cm, 0);
            snprintf(x->name_key, sizeof x->name_key, "%s", jget(pr, "UnitNameTextKey") ? jget(pr, "UnitNameTextKey")->s : "");
            x->level = (int)jnumv(jpath(st, "ProgressionStats.Level"));
            jv *tr = jpath(jget(jget(s, "PlayerUnitsSaveData"), "SkillTrees"), g), *mn = jpath(tr, "SkillTrees.Main"); size_t o = 0;
            for (size_t k = 0; mn && k < mn->cnt && k < 3; k++) o += (size_t)snprintf(x->trees + o, sizeof x->trees - o, "%s%s", k ? " \xC2\xB7 " : "", mn->v[k]->s);
            jv *sl = jget(fe_comp(cm, 6), "ItemSlots");
            for (size_t k = 0; sl && k < sl->cnt && x->ngear < 6; k++) { jv *key = jget(sl->v[k], "Key"); if (key && key->n) snprintf(x->gear_keys[x->ngear++], 64, "%s", key->s); }
            n++;
        }
    }
    jfree(s); return n;
}
int fe_bring_units(const char *path, const char (*guids)[40], int n, char *err, size_t errn) {
    char line[2600]; size_t o = (size_t)snprintf(line, sizeof line, "{\"cmd\":\"fe_bring\",\"file\":\"%s\",\"guids\":\"", path);
    for (int i = 0; i < n && o < sizeof line - 50; i++) o += (size_t)snprintf(line + o, sizeof line - o, "%s%s", i ? "," : "", guids[i]);
    snprintf(line + o, sizeof line - o, "\"}");
    char *r = cmd_bring(line); int ok = r && strstr(r, "\"ok\":true");
    if (!ok) snprintf(err, errn, "Could not bring these units%s", r && strstr(r, "error") ? ": the save could not be read." : ".");
    free(r); return ok ? 0 : -1;
}

/* ---- results for the results window */
int fe_list_results(fe_result_info *out, int max) {
    char dir[1300]; fe_path(dir, sizeof dir, "results"); int n = 0;
    char lp[1300]; fe_path(lp, sizeof lp, "claims.json"); jv *led = read_json(lp);
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)) && n < max; ) {
        size_t l = strlen(e->d_name); if (l != 37 || strcmp(e->d_name + 32, ".json")) continue;
        char p[1500]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        jv *r = read_json(p); if (!r) continue;
        fe_result_info *x = &out[n]; memset(x, 0, sizeof *x);
        snprintf(x->run, sizeof x->run, "%.32s", e->d_name);
        snprintf(x->host, sizeof x->host, "%s", jpath(r, "rules.host_name") ? jpath(r, "rules.host_name")->s : "the host");
        struct stat st; if (!stat(p, &st)) { struct tm tm; localtime_r(&st.st_mtimespec.tv_sec, &tm); strftime(x->date, sizeof x->date, "%d %b %Y", &tm); }
        x->units = jget(r, "units") ? (int)jget(r, "units")->cnt : 0;
        jv *cl = jget(jget(led, "claims"), x->run);
        if (cl) { x->applied = 1; snprintf(x->applied_date, sizeof x->applied_date, "%s", jget(cl, "date") ? jget(cl, "date")->s : ""); snprintf(x->applied_file, sizeof x->applied_file, "%s", jget(cl, "file") ? jget(cl, "file")->s : ""); }
        jfree(r); n++;
    }
    if (d) closedir(d);
    jfree(led);
    return n;
}
int fe_result_rows(const char *run, fe_res_row *rows, int max, fe_res_item *items, int imax, int *nitems, fe_mode_info *rules) {
    char p[1500], sub[200]; snprintf(sub, sizeof sub, "results/%s.json", run); fe_path(p, sizeof p, sub);
    jv *r = read_json(p); if (!r) return -1;
    jv *mans = my_manifests(), *u = jget(r, "units"); int n = 0;
    for (size_t i = 0; u && i < u->cnt && n < max; i++) {
        jv *x = u->v[i], *m = jget(mans, jget(x, "export_id")->s); fe_res_row *w = &rows[n++]; memset(w, 0, sizeof *w);
        snprintf(w->name_key, sizeof w->name_key, "%s", m && jget(m, "name_key") ? jget(m, "name_key")->s : "");
        w->lv0 = jnumv(jget(m, "level")); w->exp0 = jnumv(jget(m, "exp"));
        w->dead = jstreq(jget(x, "status"), "dead"); w->from_snapshot = jget(x, "from_snapshot") != NULL;
        w->lv1 = jget(x, "level_after") ? jnumv(jget(x, "level_after")) : w->lv0; w->exp1 = jget(x, "exp_after") ? jnumv(jget(x, "exp_after")) : w->exp0;
        w->removed = w->dead && jtruthy(jpath(r, "rules.death_permanent"));
    }
    *nitems = 0;
    jv *it = jget(r, "items");
    for (size_t i = 0; it && i < it->cnt && *nitems < imax; i++) {
        jv *k = jpath(it->v[i], "registry.Key"); fe_res_item *y = &items[(*nitems)++];
        snprintf(y->key, sizeof y->key, "%s", k ? k->s : ""); y->amount = jget(it->v[i], "amount") ? (int)jnumv(jget(it->v[i], "amount")) : 1;
    }
    if (rules) { memset(rules, 0, sizeof *rules); rules->death = jtruthy(jpath(r, "rules.death_permanent")); rules->xp = jtruthy(jpath(r, "rules.xp_counts")); rules->loot = jtruthy(jpath(r, "rules.items_back"));
                 snprintf(rules->host, sizeof rules->host, "%s", jpath(r, "rules.host_name") ? jpath(r, "rules.host_name")->s : ""); }
    jfree(mans); jfree(r);
    return n;
}
int fe_result_targets(const char *run, fe_save_info *out, int max) {
    char p[1500], sub[200]; snprintf(sub, sizeof sub, "results/%s.json", run); fe_path(p, sizeof p, sub);
    jv *r = read_json(p), *mans = my_manifests(); char cg[64] = ""; char src[1024] = "";
    jv *u = jget(r, "units");
    for (size_t i = 0; u && i < u->cnt && !cg[0]; i++) { jv *m = jget(mans, jget(u->v[i], "export_id")->s); if (m && !fe_manifest_custom(m)) { snprintf(cg, sizeof cg, "%s", jget(m, "home_campaign_guid")->s); if (jget(m, "source_file")) snprintf(src, sizeof src, "%s", jget(m, "source_file")->s); } }
    jfree(r); jfree(mans);
    fe_save_info all[64]; int na = fe_list_saves(all, 64), n = 0;
    for (int i = 0; i < na && n < max; i++) {
        jv *s = read_json(all[i].path); int same = s && jstreq(jpath(s, "CampaignSaveHeader.CampaignGuid"), cg); jfree(s);
        if (same) out[n++] = all[i];
    }
    if (!n && src[0] && n < max && !read_header(src, &out[n])) { snprintf(out[n].folder, sizeof out[n].folder, "where they came from"); n++; }
    return n;
}
int fe_apply_run(const char *run, const char *target, int choice, char *out_file, size_t on, char *err, size_t errn) {
    char line[1600], chp[1300] = "";
    if (choice) {                                                 /* one choice for every unit that changed at home */
        jv *mans = my_manifests(), *ch = jobj();
        for (size_t i = 0; i < mans->cnt; i++) jset(ch, mans->k[i], jstrv(choice == 1 ? "expedition" : "home"));
        fe_path(chp, sizeof chp, "choices.json"); write_json(chp, ch); jfree(ch); jfree(mans);
    }
    snprintf(line, sizeof line, "{\"cmd\":\"fe_apply\",\"run\":\"%s\",\"target\":\"%s\",\"choices\":\"%s\"}", run, target ? target : "", chp);
    char *r = cmd_apply(line); int ok = r && strstr(r, "\"ok\":true");
    if (ok) { char e[200]; jv *j = jparse(r, strlen(r), e, sizeof e); snprintf(out_file, on, "%s", j && jget(j, "file") ? jget(j, "file")->s : ""); jfree(j); free(r); return 0; }
    char e[200]; jv *j = r ? jparse(r, strlen(r), e, sizeof e) : NULL; const char *m = j && jget(j, "error") ? jget(j, "error")->s : "The results could not be applied.";
    snprintf(err, errn, "%s", m); int conflict = strstr(m, "choose per unit") != NULL;
    jfree(j); free(r);
    return conflict ? 1 : -1;
}

int pvp_mail_take(dsnet_client *c, const uint8_t from[16], uint8_t kind, const uint8_t *data, size_t len) __attribute__((weak_import));   /* pvp_units.c */
void fe_poll(void) {
    dsnet_client *c = cli(); if (!c) return;
    uint8_t from[16], kind, *data; size_t len;
    while (dsnet_mail_poll(c, from, &kind, &data, &len)) {
        char uh[33]; hex_of(from, uh);
        if (kind == FE_M_UNITS && dsnet_is_host(c)) {
            char err[200]; jv *m = jparse((const char *)data, len, err, sizeof err), *e = jget(m, "entries");
            char ack[120];
            if (jis(e, JN_ARR)) { host_take_entries(uh, e); snprintf(ack, sizeof ack, "{\"ok\":1,\"n\":%zu}", e->cnt); say("%zu unit(s) received from %.8s", e->cnt, uh); }
            else snprintf(ack, sizeof ack, "{\"ok\":0,\"error\":\"bad data\"}");
            dsnet_mail_send(c, from, FE_M_UNITS_ACK, ack, strlen(ack));
            jfree(m);
        } else if (kind == FE_M_UNITS_ACK) { say("host answered: %.*s", (int)(len < 100 ? len : 100), (const char *)data);
            double nn; if (jnum((const char *)data, "n", &nn)) my_ack = (int)nn; }
        else if (kind == FE_M_MODE && !dsnet_is_host(c)) { int first = !mode_rx; jfree(mode_rx); char e2[100]; mode_rx = jparse((const char *)data, len, e2, sizeof e2);
            if (first) rig_log("fe: host mode received (%zu bytes, parse %s)", len, mode_rx ? "ok" : e2); }
        else if (kind == FE_M_RESULT) on_result(from, data, len);
        else if (kind == FE_M_RESULT_ACK && dsnet_is_host(c) && run_m) {
            char p[1500], q[1500], sub[300];
            snprintf(sub, sizeof sub, "runs/%.*s/results/%s.json", (int)(len < 40 ? len : 40), (const char *)data, uh);
            fe_path(p, sizeof p, sub); snprintf(q, sizeof q, "%s.delivered", p);
            rename(p, q); say("results delivered to %.8s", uh);
        }
        else if (pvp_mail_take) pvp_mail_take(c, from, kind, data, len);   /* PvP's kinds (11-16): one reader of the mailbox */
        free(data);
    }
    if (dsnet_is_host(c)) host_mode_mail(c);
    /* host: results not acknowledged yet go to their player whenever he is connected (every 10 s) */
    static double last_scan;
    if (dsnet_is_host(c) && run_m && now_s() - last_scan > 2) {
        last_scan = now_s();
        char dir[1300], sub[200]; snprintf(sub, sizeof sub, "runs/%s/results", jget(run_m, "run_id")->s); fe_path(dir, sizeof dir, sub);
        DIR *d = opendir(dir);
        for (struct dirent *e; d && (e = readdir(d)); ) {
            size_t l = strlen(e->d_name); if (l != 37 || strcmp(e->d_name + 32, ".json")) continue;
            char uh[33]; memcpy(uh, e->d_name, 32); uh[32] = 0;
            uint32_t pres = dsnet_present_mask(c); int here = 0; uint8_t u[16], w[16]; unhex(uh, w);
            for (int k = 0; k < 32; k++) if ((pres >> k & 1) && !dsnet_player_uid(c, k, u) && !memcmp(u, w, 16)) here = 1;
            if (!here || now_s() - *send_clock(uh) < 10) continue;
            char p[1500]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            say("results for %.8s sent again (%d)", uh, send_result_file(p, uh));
        }
        if (d) closedir(d);
    }
}

/* ---------------------------------------------------------------- rig commands */
static char *cmd_stats(const char *line) {
    char gs[64] = ""; Guid g; Buf b = { 0 };
    if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) { bput(&b, "{\"ok\":false,\"error\":\"need guid\"}"); return b.p; }
    int map = cur_map(); Obj u = map < 0 ? NULL : find_unit(map, g);
    if (!u) { bput(&b, "{\"ok\":false,\"error\":\"unit not found on the current map\"}"); return b.p; }
    bput(&b, "{\"ok\":true,\"guid\":\"%s\",\"map\":%d,\"dead\":%s,\"stats\":[", gs, map, unit_dead(u) ? "true" : "false");
    for (int i = 1; i <= 122; i++) bput(&b, "%s%.6g", i > 1 ? "," : "", unit_stat(u, i));
    bput(&b, "]}");
    return b.p;
}
static char *cmd_status(void) {
    Buf b = { 0 }; dsnet_client *c = cli();
    bput(&b, "{\"ok\":true,\"recruit_allowed\":%d,\"session\":%s,\"host\":%s,\"hub_world\":%s,\"received\":%zu,\"pending_start\":%s,\"armed\":%d,\"run\":",
         fe_recruit_allowed(), c ? "true" : "false", c && dsnet_is_host(c) ? "true" : "false", fe_world_is_hub() ? "true" : "false",
         recv_entries ? recv_entries->cnt : 0, pending_start ? "true" : "false", armed);
    if (run_m) bstr(&b, jget(run_m, "run_id")->s); else bput(&b, "null");
    bput(&b, ",\"hub_units\":");
    if (run_m) {
        jv *rows = jget(run_m, "units"); bput(&b, "[");
        for (size_t i = 0; rows && i < rows->cnt; i++) {
            bput(&b, "%s{\"hub\":\"%s\",\"home\":\"%s\",\"player\":\"%s\"}", i ? "," : "", jget(rows->v[i], "hub_guid")->s,
                 jget(rows->v[i], "home_guid")->s, jget(rows->v[i], "player_uid")->s);
        }
        bput(&b, "]");
    } else bput(&b, "null");
    bput(&b, ",\"loot\":%zu,\"last_alive\":%zu,\"msg\":", loot ? loot->cnt : 0, last_alive ? last_alive->cnt : 0);
    bstr(&b, last_msg);
    char me[33] = ""; my_uid_hex(me); bput(&b, ",\"uid\":\"%s\"}", me);
    return b.p;
}
char *fe_exec(const char *cmd, const char *line) {
    if (!strcmp(cmd, "fe_stats")) return cmd_stats(line);
    if (!strcmp(cmd, "fe_save")) { char nm[200] = "fe_test"; jstr(line, "name", nm, sizeof nm); Buf b = { 0 }; bput(&b, "{\"ok\":%s}", rig_save_campaign(nm) ? "true" : "false"); return b.p; }
    if (!strcmp(cmd, "fe_bring")) return cmd_bring(line);
    if (!strcmp(cmd, "fe_start")) return cmd_start(line);
    if (!strcmp(cmd, "fe_end")) return cmd_end(line);
    if (!strcmp(cmd, "fe_apply")) return cmd_apply(line);
    if (!strcmp(cmd, "fe_status")) return cmd_status();
    if (!strcmp(cmd, "fe_backup")) {                            /* test: the backup alone */
        char bk[1300], dir[1200]; snprintf(dir, sizeof dir, "%s/data/Saves", rig_dir);
        int n = backup_folder(dir, bk, sizeof bk); Buf b = { 0 };
        bput(&b, "{\"ok\":%s,\"files\":%d,\"dir\":", n >= 0 ? "true" : "false", n); bstr(&b, bk); bput(&b, "}"); return b.p;
    }
    return NULL;
}
