/* pvp_mods.c - the host rule "Vanilla check" in the game (owner 2026-10-14 ~20:00; rules and messages: pvp_modcheck.h).
 *  - Each copy reads its own mods once from the launcher's session file (env DSML_SESSION; tests: DSRIG_DSML_SESSION,
 *    or the rig command pvp_mods {"session": path}). No file = not started by the launcher: no mod known.
 *  - The host asks every player for its mods (mail PVP_M_MODS_ASK, which also carries the host's current choice, so a
 *    guest's PvP window shows it) when the PvP window opens, when the rule changes and when a player joins; each
 *    player answers with its report (PVP_M_MODS, the session file's keys).
 *  - Start Match (pvp_game.c pvp_ui_start_match, and the rig pvp_start): pvp_mods_gate checks the host and every player
 *    present; a refusal names the player and the mod, is shown in the host's window and mailed to every player
 *    (PVP_M_MODS_REFUSED). A player whose report has not come yet = refused ("waiting"), asked again.
 * Display and transport only: nothing here is sim state (the rule itself rides in SETUP, pvp_core). */
#include "rig.h"
#include "mp.h"
#include "pvp_modcheck.h"
#include "fe/fe_json.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PVP_M_MODS_ASK 14            /* host -> player: {"vanilla":0|1}; the player answers with PVP_M_MODS */
#define PVP_M_MODS 15                /* player -> host: its mods (pvp_modset_json) */
#define PVP_M_MODS_REFUSED 16        /* host -> every player: {"why":"..."} Start Match was refused */

mp_session *mp_game_session(void) __attribute__((weak_import));
static dsnet_client *cli(void) { mp_session *s = mp_game_session ? mp_game_session() : NULL; return s ? mp_client(s) : NULL; }

static pvp_modset mine; static int mine_loaded; static char mine_from[600];
static struct { uint8_t uid[16]; int have; pvp_modset m; } rep[32];   /* host: per player slot */
static uint32_t seen_mask; static uint8_t seen_uid[32][16];          /* host: who was present last frame */
static int asked_vanilla = 1;        /* host: the choice sent with the last ask */
static int told_vanilla = -1;        /* guest: the host's choice from its last ask (-1 = none yet) */
static char last_msg[700];           /* last gate result / refusal received */

static void load_session(const char *path) {
    memset(&mine, 0, sizeof mine); mine.rule = -1; mine_loaded = 1;
    if (!path || !*path) path = getenv("DSML_SESSION");
    if (!path || !*path) path = getenv("DSRIG_DSML_SESSION");
    snprintf(mine_from, sizeof mine_from, "%s", path ? path : "");
    FILE *f = path && *path ? fopen(path, "rb") : NULL;
    if (!f) { rig_log("pvp mods: no launcher session (%s): no mod known", path && *path ? path : "game not started by the launcher"); return; }
    char *t = malloc(1 << 20); size_t n = t ? fread(t, 1, (1 << 20) - 1, f) : 0; fclose(f);
    int rc = t ? pvp_modset_parse(t, n, &mine) : -1; free(t);
    rig_log("pvp mods: session %s: rc %d, rule %d, %d gameplay mod(s), %d client mod(s), mod set %.12s", path, rc, mine.rule, mine.ngame, mine.nclient, mine.hash);
}
const pvp_modset *pvp_mods_mine(void) { if (!mine_loaded) load_session(NULL); return &mine; }
/* the window's first choice: the world's rule in the launcher; none given = On (design board PvPVanillaCheck) */
int pvp_mods_default_vanilla(void) { return pvp_mods_mine()->rule == 0 ? 0 : 1; }
int pvp_mods_told_vanilla(void) { return told_vanilla; }
const char *pvp_mods_last(void) { return last_msg; }

static void ask_one(dsnet_client *c, const uint8_t uid[16]) {
    char b[32]; int n = snprintf(b, sizeof b, "{\"vanilla\":%d}", asked_vanilla);
    dsnet_mail_send(c, uid, PVP_M_MODS_ASK, b, (size_t)n);
}
/* host: tell every other player the choice and ask for their mods (their old reports stay until the new ones come) */
void pvp_mods_ask(int vanilla) {
    dsnet_client *c = cli(); if (!c || !dsnet_is_host(c)) return;
    asked_vanilla = vanilla ? 1 : 0;
    uint32_t pm = dsnet_present_mask(c); uint8_t me[16], u[16]; dsnet_my_uid(c, me);
    for (int p = 0; p < 32; p++) if (pm >> p & 1 && !dsnet_player_uid(c, p, u) && memcmp(u, me, 16)) ask_one(c, u);
}
static void send_report(dsnet_client *c) {
    char b[4096]; size_t n = pvp_modset_json(pvp_mods_mine(), b, sizeof b);
    if (n) dsnet_mail_send(c, NULL, PVP_M_MODS, b, n);
    rig_log("pvp mods: my mods sent to the host (%zu bytes, %d gameplay)", n, mine.ngame);
}
static int slot_of(dsnet_client *c, const uint8_t uid[16]) {
    uint32_t pm = dsnet_present_mask(c); uint8_t u[16];
    for (int p = 0; p < 32; p++) if (pm >> p & 1 && !dsnet_player_uid(c, p, u) && !memcmp(u, uid, 16)) return p;
    return -1;
}
/* from pvp_units.c's mail loop: 1 = this mail was ours */
int pvp_mods_mail(dsnet_client *c, const uint8_t from[16], uint8_t kind, const uint8_t *data, size_t len) {
    char err[120];
    if (kind == PVP_M_MODS_ASK && !dsnet_is_host(c)) {
        jv *m = jparse((const char *)data, len, err, sizeof err), *v = jget(m, "vanilla");
        if (v) told_vanilla = jnumv(v) != 0;
        last_msg[0] = 0;                                              /* a new ask: an old refusal is stale */
        jfree(m);
        send_report(c);
        return 1;
    }
    if (kind == PVP_M_MODS && dsnet_is_host(c)) {
        int p = slot_of(c, from);
        if (p < 0) return 1;
        pvp_modset m;
        if (pvp_modset_parse((const char *)data, len, &m)) { rig_log("pvp mods: bad report from player %d", p); return 1; }
        memcpy(rep[p].uid, from, 16); rep[p].m = m; rep[p].have = 1;
        rig_log("pvp mods: player %d: %s, %d gameplay mod(s), %d client mod(s), mod set %.12s", p, m.known ? "launcher" : "no launcher", m.ngame, m.nclient, m.hash);
        return 1;
    }
    if (kind == PVP_M_MODS_REFUSED && !dsnet_is_host(c)) {
        jv *m = jparse((const char *)data, len, err, sizeof err), *w = jget(m, "why");
        char raw[700] = ""; if (w && w->t == JN_STR) snprintf(raw, sizeof raw, "%.*s", (int)w->n, w->s);
        /* the host wrote it with JSON escapes for quotes only */
        size_t o = 0; for (size_t i = 0; raw[i] && o + 1 < sizeof last_msg; i++) { if (raw[i] == '\\' && raw[i + 1]) i++; last_msg[o++] = raw[i]; } last_msg[o] = 0;
        rig_log("pvp mods: the host refused Start Match: %s", last_msg);
        jfree(m);
        return 1;
    }
    return 0;
}
/* every frame (pvp_units_frame): host: a new player in a slot = its old report goes, it is asked */
void pvp_mods_frame(void) {
    dsnet_client *c = cli(); if (!c) { seen_mask = 0; return; }
    if (!dsnet_is_host(c)) return;
    uint32_t pm = dsnet_present_mask(c); uint8_t me[16], u[16]; dsnet_my_uid(c, me);
    for (int p = 0; p < 32; p++) {
        int here = pm >> p & 1 && !dsnet_player_uid(c, p, u);
        if (!here) { if (seen_mask >> p & 1) { rep[p].have = 0; seen_mask &= ~(1u << p); } continue; }
        if (seen_mask >> p & 1 && !memcmp(seen_uid[p], u, 16)) continue;
        memcpy(seen_uid[p], u, 16); seen_mask |= 1u << p; rep[p].have = 0;
        if (memcmp(u, me, 16)) ask_one(c, u);
    }
}
static void refuse_all(dsnet_client *c, const char *why) {
    char b[1600]; size_t o = (size_t)snprintf(b, sizeof b, "{\"why\":\"");
    for (const char *s = why; *s && o + 4 < sizeof b; s++) { if (*s == '"' || *s == '\\') b[o++] = '\\'; b[o++] = *s; }
    o += (size_t)snprintf(b + o, sizeof b - o, "\"}");
    uint32_t pm = dsnet_present_mask(c); uint8_t me[16], u[16]; dsnet_my_uid(c, me);
    for (int p = 0; p < 32; p++) if (pm >> p & 1 && !dsnet_player_uid(c, p, u) && memcmp(u, me, 16)) dsnet_mail_send(c, u, PVP_M_MODS_REFUSED, b, o);
}
/* host, before the match is set up: 0 = everyone may play under this rule; else why = the message */
int pvp_mods_gate(int vanilla, char *why, size_t n) {
    why[0] = 0;
    dsnet_client *c = cli(); if (!c || !dsnet_is_host(c)) return 0;
    const pvp_modset *host = pvp_mods_mine();
    int me = dsnet_player_id(c), refused = pvp_modset_check(vanilla, host, host, "You", 1, why, n);
    uint32_t pm = dsnet_present_mask(c); char waiting[300] = ""; size_t wl = 0;
    for (int p = 0; p < 32 && !refused; p++) {
        if (!(pm >> p & 1) || p == me) continue;
        const char *nm = dsnet_player_name(c, p); char who[40]; snprintf(who, sizeof who, "%s", nm && nm[0] ? nm : "A player");
        if (!rep[p].have) { uint8_t u[16]; if (!dsnet_player_uid(c, p, u)) ask_one(c, u);
                            wl += (size_t)snprintf(waiting + wl, wl < sizeof waiting ? sizeof waiting - wl : 0, "%s%s", wl ? ", " : "", who); continue; }
        refused = pvp_modset_check(vanilla, host, &rep[p].m, who, 0, why, n);
    }
    if (!refused && waiting[0]) { snprintf(why, n, "Waiting for the mod list of %s. Press Start Match again in a moment.", waiting); refused = 2; }
    snprintf(last_msg, sizeof last_msg, "%s", refused ? why : "");
    rig_log("pvp mods: gate (Vanilla check %s): %s", vanilla ? "on" : "off", refused ? why : "everyone may play");
    if (refused == 1) refuse_all(c, why);
    return refused;
}
/* window note: what the rule means for these players right now (host: from the reports; guest: its own mods) */
void pvp_mods_summary(int vanilla, int host, char *out, size_t n) {
    const pvp_modset *m = pvp_mods_mine();
    char l[500] = ""; size_t at = 0;
    for (int i = 0; i < m->ngame; i++) { char nm[160]; pvp_modset_name(m->game[i], nm, sizeof nm); at += (size_t)snprintf(l + at, at < sizeof l ? sizeof l - at : 0, "%s%s", at ? ", " : "", nm); }
    if (vanilla) {
        if (m->ngame) snprintf(out, n, "Client mods are fine. Gameplay mods: none allowed; turn off %s.", l);
        else snprintf(out, n, "Client mods are fine. Gameplay mods: none allowed.");
    } else if (host) snprintf(out, n, m->ngame ? "Every player needs the host's gameplay mods: %s." : "The host has no gameplay mod: nobody may have one.", l);
    else snprintf(out, n, "Every player needs the host's gameplay mods (the launcher gets them).");
}
char *pvp_mods_exec(const char *cmd, const char *line) {   /* tests: pvp_mods {"session": path, "ask": 0|1, "gate": 0|1} */
    if (strcmp(cmd, "pvp_mods")) return NULL;
    char path[600] = ""; double v;
    if (jstr(line, "session", path, sizeof path)) load_session(path);
    if (jnum(line, "ask", &v)) pvp_mods_ask((int)v);
    int gate = -1; char why[700] = "";
    if (jnum(line, "gate", &v)) gate = pvp_mods_gate((int)v, why, sizeof why);
    const pvp_modset *m = pvp_mods_mine(); dsnet_client *c = cli();
    Buf b = { 0 };
    bput(&b, "{\"ok\":true,\"from\":"); bstr(&b, mine_from);
    bput(&b, ",\"known\":%d,\"rule\":%d,\"gameplay\":%d,\"client\":%d,\"modset\":", m->known, m->rule, m->ngame, m->nclient); bstr(&b, m->hash);
    bput(&b, ",\"told_vanilla\":%d,\"gate\":%d,\"why\":", told_vanilla, gate); bstr(&b, why);
    bput(&b, ",\"last\":"); bstr(&b, last_msg); bput(&b, ",\"reports\":[");
    int first = 1;
    for (int p = 0; c && p < 32; p++) if (rep[p].have) { bput(&b, "%s[%d,%d,%d,", first ? "" : ",", p, rep[p].m.known, rep[p].m.ngame); bstr(&b, rep[p].m.hash); bput(&b, "]"); first = 0; }
    bput(&b, "]}");
    return b.p;
}
