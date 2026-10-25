/* pvp_rules.c - multiplayer glue of the PvP rules (lane pvprules): the rules file, the host's rules for the match (one
 * stamped order + a join-point block), pvp_rule_get, the texts of the new rows, test commands. See pvp_rules.h. */
#include "pvp_rules.h"
#include "mpmenu_kit.h"
#include "mp_modes_text.h"
#include "mp_api.h"
#include "mp.h"
#include "mp_game.h"
#include "net/dsnet.h"
#include "dsl_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PVPR_ORDER_TYPE 56           /* mp order type (used elsewhere: 1-7, 11, 20-22, 30-37, 48-50) */
#define PVPR_WIRE 1

const DSLHost *mu_host(void);        /* mu_ui.c: the loader host of this module (NULL in the rig's own dylib) */
DSLPlugin *mu_plugin(void);

static pvpr_store store; static int loaded;
static char path[1400];
static pvpr_vals match_v; static pvp_map_pick match_map; static int synced;
static int n_recv, n_sent, n_saves, n_save_fail;

/* ---- the file */
static void mkdirs(char *p) {
    for (char *s = p + 1; *s; s++) if (*s == '/' || *s == '\\') { char c = *s; *s = 0; mkdir(p, 0755); *s = c; }
    mkdir(p, 0755);
}
const char *pvp_rules_path(void) {
    if (path[0]) return path;
    char dir[1300] = "";
    const DSLHost *h = mu_host();
    const char *d = h ? dsl_mod_data_dir(h, mu_plugin()) : NULL;   /* <data>/ModData/multiplayer/ (loader, SDK API 6) */
    if (d && *d) snprintf(dir, sizeof dir, "%s", d);
    else {                                                           /* older loader / the rig's own dylib: the same place */
        Obj ps = F(0x507B74C, Obj (*)(void *))(NULL);                /* Application.persistentDataPath */
        char base[1100] = ""; str_utf8(ps, base, sizeof base);
        if (!base[0]) return "";
        snprintf(dir, sizeof dir, "%s/ModData/multiplayer", base);
    }
    mkdirs(dir);
    snprintf(path, sizeof path, "%s/pvp_rules.txt", dir);
    return path;
}
static void load_once(void) {
    if (loaded) return;
    const char *p = pvp_rules_path(); if (!p[0]) { pvpr_store_init(&store); return; }   /* il2cpp not up yet: try later */
    loaded = 1;
    int rc = pvpr_store_load(&store, p);
    rig_log("pvp rules: %s %s (%d preset(s))", rc == 0 ? "read" : rc == 1 ? "no file yet:" : "could not read", p, store.npresets);
}
pvpr_store *pvp_rules_store(void) { load_once(); return &store; }
pvpr_vals *pvp_rules_local(void) { load_once(); return &store.last; }
static void save_now(const char *why) {
    load_once(); if (!loaded) return;
    int rc = pvpr_store_save(&store, pvp_rules_path());
    if (rc) n_save_fail++; else n_saves++;
    rig_log("pvp rules: saved (%s): rc %d, %d preset(s)", why, rc, store.npresets);
}
void pvp_rules_changed(void) { save_now("a rule changed"); }

/* ---- the host's rules for the match */
static mp_session *reg_sess;
static mp_session *sess(void) { return mp_game_session(); }
static int apply_wire(const uint8_t *a, size_t n, const char *from) {
    if (n < 4 + 1 + 4 * 3 + 1 || memcmp(a, "PVR", 3) || a[3] != PVPR_WIRE) return -1;
    pvp_map_pick m; memset(&m, 0, sizeof m);
    uint8_t fl = a[4];
    memcpy(&m.region, a + 5, 4); memcpy(&m.floor, a + 9, 4); memcpy(&m.seed, a + 13, 4);
    m.here = fl & 1; m.boss = fl >> 1 & 1; m.random_starts = fl >> 2 & 1;
    pvpr_vals v; pvpr_defaults(&v); pvpr_parse(&v, (const char *)a + 17, n - 17);
    match_v = v; match_map = m; synced = 1;
    char t[600]; pvpr_format(&v, t, sizeof t); for (char *q = t; *q; q++) if (*q == '\n') *q = ' ';
    rig_log("pvp rules: the host's rules (%s): map %s region %d floor %d seed %d%s%s; %s", from, m.here ? "here" : "dungeon", m.region, m.floor, m.seed,
            m.boss ? " boss room" : "", m.random_starts ? " random starts" : "", t);
    return 0;
}
static size_t wire(uint8_t *out, size_t cap, const pvpr_vals *v, const pvp_map_pick *m) {
    if (cap < 17 + 600) return 0;
    memcpy(out, "PVR", 3); out[3] = PVPR_WIRE;
    out[4] = (uint8_t)((m->here ? 1 : 0) | (m->boss ? 2 : 0) | (m->random_starts ? 4 : 0));
    memcpy(out + 5, &m->region, 4); memcpy(out + 9, &m->floor, 4); memcpy(out + 13, &m->seed, 4);
    size_t t = pvpr_format(v, (char *)out + 17, cap - 17);
    return t ? 17 + t : 0;
}
static int rules_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (player != 0) { rig_log("pvp rules: an order from player %d refused (only the host sets the rules)", player); return 0; }
    if (!apply_wire(a, n, "order")) n_recv++;
    return 0;
}
static pvpr_vals view_v; static uint8_t view_team[PVPR_VIEW_TEAMS]; static int view_have, view_asked, view_sent, view_recv;   /* P1 */
static void session_check(void) {
    mp_session *s = sess();
    if (!s) { if (synced) rig_log("pvp rules: session over: the host's rules are forgotten"); synced = 0; reg_sess = NULL; view_have = 0; return; }
    if (s == reg_sess) return;
    reg_sess = s; mp_set_order_handler(s, PVPR_ORDER_TYPE, rules_order); view_have = 0;
}

/* ---- the host's PvP window for the guests (UX review 2026-10-25, P1: a guest's window showed its own default rules until
   Start). Mail outside the sim, display only: pvp_rule_get, the fingerprint and the join-point block never read it. */
#define PVP_M_RULES_VIEW 17                  /* host -> a player: pvpr_view_format text (pvp_units.c mail loop; PvP uses 11-18) */
#define PVP_M_RULES_ASK 18                   /* player -> host: "send me your window" (its PvP window opened) */
static dsnet_client *cli(void) { mp_session *s = sess(); return s ? mp_client(s) : NULL; }
int pvp_rules_view_send(const pvpr_vals *v, const uint8_t teams[PVPR_VIEW_TEAMS]) {
    session_check();
    dsnet_client *c = cli(); if (!c || !dsnet_is_host(c)) return -1;
    char b[1024]; size_t n = pvpr_view_format(v, teams, b, sizeof b); if (!n) return -2;
    uint32_t pm = dsnet_present_mask(c); uint8_t me[16], u[16]; dsnet_my_uid(c, me); int k = 0, bad = 0;
    int my = dsnet_player_id(c); if (my < 0 || my > 31 || !(pm & ~(1u << my))) return 0;   /* nobody else here: nothing to send (a joiner changes the mask) */
    for (int p = 0; p < 32; p++) if (pm >> p & 1 && !dsnet_player_uid(c, p, u) && memcmp(u, me, 16)) { if (dsnet_mail_send(c, u, PVP_M_RULES_VIEW, b, n)) bad++; else k++; }
    view_sent++;
    char t[1100]; snprintf(t, sizeof t, "%s", b); for (char *q = t; *q; q++) if (*q == '\n') *q = ' ';
    rig_log("pvp rules: window sent to %d player(s)%s: %s", k, bad ? " (a mail failed)" : "", t);
    return bad ? -3 : 0;
}
int pvp_rules_view_ask(void) {
    session_check();
    dsnet_client *c = cli(); if (!c || dsnet_is_host(c)) return -1;
    return dsnet_mail_send(c, NULL, PVP_M_RULES_ASK, "?", 1);
}
int pvp_rules_view_asked(void) { int a = view_asked; view_asked = 0; return a; }
const pvpr_vals *pvp_rules_view(uint8_t teams[PVPR_VIEW_TEAMS]) {
    session_check();
    if (!view_have) return NULL;
    if (teams) memcpy(teams, view_team, PVPR_VIEW_TEAMS);
    return &view_v;
}
/* from pvp_units.c's mail loop: 1 = this mail was ours */
int pvp_rules_view_mail(dsnet_client *c, const uint8_t from[16], uint8_t kind, const uint8_t *data, size_t len) {
    if (kind == PVP_M_RULES_ASK) { if (dsnet_is_host(c)) view_asked = 1; return 1; }
    if (kind != PVP_M_RULES_VIEW) return 0;
    session_check();
    uint8_t hu[16];
    if (dsnet_is_host(c) || dsnet_player_uid(c, 0, hu) || memcmp(hu, from, 16)) { rig_log("pvp rules: a window mail not from the host: ignored"); return 1; }
    pvpr_vals v; uint8_t t[PVPR_VIEW_TEAMS];
    if (pvpr_view_parse((const char *)data, len, &v, t)) { rig_log("pvp rules: bad window mail from the host (%zu bytes)", len); return 1; }
    int changed = !view_have || !pvpr_vals_equal(&v, &view_v) || memcmp(t, view_team, sizeof t);
    view_v = v; memcpy(view_team, t, sizeof t); view_have = 1; view_recv++;
    if (changed) { char s[700]; pvpr_format(&v, s, sizeof s); for (char *q = s; *q; q++) if (*q == '\n') *q = ' ';
                   char tt[PVPR_VIEW_TEAMS + 1]; for (int k = 0; k < PVPR_VIEW_TEAMS; k++) tt[k] = (char)('0' + t[k]); tt[PVPR_VIEW_TEAMS] = 0;
                   rig_log("pvp rules: the host's window: %s teams %s", s, tt); }
    return 1;
}
int pvp_rules_send(const pvp_map_pick *m) {
    session_check();
    mp_session *s = sess(); if (!s || !dsnet_is_host(mp_client(s))) return -1;
    static uint8_t b[1024]; static const uint8_t zero[16];
    size_t n = wire(b, sizeof b, pvp_rules_local(), m);
    if (!n) return -2;
    int rc = mp_order(s, PVPR_ORDER_TYPE, zero, b, (uint16_t)n);
    if (!rc) n_sent++;
    save_now("match start");
    rig_log("pvp rules: sent to every player: rc %d (%zu bytes)", rc, n);
    return rc;
}
int pvp_rules_synced(void) { return synced; }
const pvp_map_pick *pvp_map_now(void) {
    static pvp_map_pick d; if (synced) return &match_map;
    pvp_map_resolve(0, 0, 0, &d); return &d;
}
int pvp_rule_get(const char *id) {
    int i = pvpr_index(id); if (i < 0) return -1;
    if (synced) return match_v.v[i];
    return pvpr_def_at(i)->dflt;
}
/* join-point block "PVPR": the host's rules travel with every join-point save (late joiner, round / match reloads) */
static uint8_t jp_buf[1024]; static size_t jp_len;
static size_t jp_size(void) { if (!synced) return 0; jp_len = wire(jp_buf, sizeof jp_buf, &match_v, &match_map); return jp_len; }
static size_t jp_write(uint8_t *out, size_t cap) { if (!synced || cap < jp_len) return 0; memcpy(out, jp_buf, jp_len); return jp_len; }
static void jp_read(const uint8_t *p, size_t n) {
    if (!p || !n) { synced = 0; return; }
    if (apply_wire(p, n, "join point")) { synced = 0; rig_log("pvp rules: bad join-point block (%zu bytes)", n); }
}
static uint64_t fp_mix(void) { return synced ? pvpr_hash(&match_v) ^ ((uint64_t)(uint32_t)match_map.seed << 1) ^ ((uint64_t)match_map.floor << 40) ^ ((uint64_t)(match_map.region + 1) << 48) : 0; }
static void frame(void) { session_check(); }

/* ---- test commands: pvpr_status, pvpr_set {id,v}, pvpr_preset_save {name}, pvpr_preset_pick {i}, pvpr_preset_delete {i},
   pvpr_reload (read the file again), pvpr_resolve {map,floor,seed} */
static void put_vals(Buf *b, const pvpr_vals *v) {
    bput(b, "{");
    for (int i = 0; i < pvpr_count(); i++) bput(b, "%s\"%s\":%d", i ? "," : "", pvpr_def_at(i)->id, v->v[i]);
    bput(b, "}");
}
static char *exec(const char *cmd, const char *line) {
    double x = 0, y = 0, z = 0; char id[64] = "", nm[300] = "";
    pvpr_store *s = pvp_rules_store();
    if (!strcmp(cmd, "pvpr_set")) { jstr(line, "id", id, sizeof id); jnum(line, "v", &x);
        if (pvpr_vals_set(&s->last, id, (int)x)) return strdup("{\"ok\":false,\"error\":\"unknown id or value\"}");
        pvp_rules_changed(); }
    else if (!strcmp(cmd, "pvpr_preset_save")) { jstr(line, "name", nm, sizeof nm); int r = pvpr_preset_save(s, nm, &s->last);
        if (r < 0) return strdup("{\"ok\":false,\"error\":\"empty name or full\"}"); save_now("preset saved"); }
    else if (!strcmp(cmd, "pvpr_preset_pick")) { jnum(line, "i", &x); if ((int)x < 0 || (int)x >= s->npresets) return strdup("{\"ok\":false,\"error\":\"no preset\"}");
        s->last = s->presets[(int)x].v; save_now("preset picked"); }
    else if (!strcmp(cmd, "pvpr_preset_delete")) { jnum(line, "i", &x); if (pvpr_preset_delete(s, (int)x)) return strdup("{\"ok\":false,\"error\":\"no preset\"}"); save_now("preset deleted"); }
    else if (!strcmp(cmd, "pvpr_reload")) { loaded = 0; load_once(); }
    else if (!strcmp(cmd, "pvpr_resolve")) { jnum(line, "map", &x); jnum(line, "floor", &y); jnum(line, "seed", &z);
        pvp_map_pick m; int r = pvp_map_resolve((int)x, (int)y, (uint32_t)z, &m); Buf b = { 0 };
        bput(&b, "{\"ok\":%s,\"here\":%d,\"region\":%d,\"floor\":%d,\"seed\":%d,\"boss\":%d,\"random_starts\":%d}", r ? "false" : "true", m.here, m.region, m.floor, m.seed, m.boss, m.random_starts);
        return b.p; }
    else if (strcmp(cmd, "pvpr_status")) return NULL;
    Buf b = { 0 };
    bput(&b, "{\"ok\":true,\"file\":"); bstr(&b, pvp_rules_path());
    bput(&b, ",\"saves\":%d,\"save_fail\":%d,\"sent\":%d,\"received\":%d,\"synced\":%d,\"view_sent\":%d,\"view_recv\":%d,\"view_have\":%d,\"local\":", n_saves, n_save_fail, n_sent, n_recv, synced, view_sent, view_recv, view_have);
    put_vals(&b, &s->last); bput(&b, ",\"match\":"); put_vals(&b, synced ? &match_v : &s->last);
    const pvp_map_pick *m = pvp_map_now();
    bput(&b, ",\"map\":{\"here\":%d,\"region\":%d,\"floor\":%d,\"seed\":%d,\"boss\":%d,\"random_starts\":%d},\"get\":{", m->here, m->region, m->floor, m->seed, m->boss, m->random_starts);
    for (int i = 0; i < pvpr_count(); i++) bput(&b, "%s\"%s\":%d", i ? "," : "", pvpr_def_at(i)->id, pvp_rule_get(pvpr_def_at(i)->id));
    bput(&b, "},\"presets\":[");
    for (int i = 0; i < s->npresets; i++) { bput(&b, "%s{\"name\":", i ? "," : ""); bstr(&b, s->presets[i].name); bput(&b, ",\"v\":"); put_vals(&b, &s->presets[i].v); bput(&b, "}"); }
    bput(&b, "],\"preset_now\":%d}", pvpr_preset_match(s, &s->last));
    return b.p;
}

void pvp_rules_start(void) {
    const mp_api_table *A = mp_api_get(MP_API_VERSION); if (!A) return;
    int bad = 0;
    mp_joinpoint jp = { .tag = { 'P', 'V', 'P', 'R' }, .size = jp_size, .write = jp_write, .read = jp_read };
    bad |= A->register_joinpoint(&jp);
    bad |= A->register_fingerprint(fp_mix);
    bad |= A->register_frame(MP_FRAME_ALWAYS, frame);
    bad |= A->register_rig_cmd("pvpr_", exec);
    rig_log("pvp rules: registered%s", bad ? " (a registration was refused)" : "");
}

/* ---- texts of the rows this lane added to the PvP Rules box (pvp_ui.c draws them) */
static const char *vis_ids[6] = { "enemy_vision", "enemy_windows", "enemy_portraits", "enemy_hpbars", "enemy_status", "enemy_viewrange" };
static const char *vis_label[6] = { TR_NOOP("Enemy Vision"), TR_NOOP("Enemy Windows"), TR_NOOP("Enemy Portraits"), TR_NOOP("Enemy Health Bars"),
                                    TR_NOOP("Enemy Status"), TR_NOOP("Enemy View Range") };
static const char *vis_tip[6] = {
    TR_NOOP("Shown: everyone sees the enemy team's units everywhere on the map.\n\nHidden: you see enemy units only where your own units can see."),
    TR_NOOP("Shown: clicking an enemy unit opens its full unit window.\n\nHidden: clicking an enemy unit shows only what you see of a monster."),
    TR_NOOP("Shown: enemy units are in your portrait bar.\n\nHidden: your portrait bar shows only your own team."),
    TR_NOOP("Shown: health bars above enemy units.\n\nHidden: no health bars on enemy units."),
    TR_NOOP("Shown: status, mood and work icons above enemy units.\n\nHidden: none of these icons on enemy units."),
    TR_NOOP("Shown: the area the enemy team can see is lit on the map. Their units stay hidden.\n\nHidden: you do not see where the enemy can see.") };
static const char *dungeon_name(int k) {      /* the game's own region names */
    return k == 0 ? cmt_game("TEXTKEY_ExpeditionPlanUI_VeilSilence_NAME", "The Veil of Silence") : cmt_game("TEXTKEY_ExpeditionPlanUI_FesteringBurrow_NAME", "Festering Burrow");
}
int pvpr_ui_rows(void) { return 2 + 6; }      /* Map, Floor, then the six visibility rules */
const char *pvpr_ui_id(int r) { return r == 0 ? "map" : r == 1 ? "floor" : r - 2 < 6 ? vis_ids[r - 2] : ""; }
const char *pvpr_ui_label(int r) { return r == 0 ? TR("Map") : r == 1 ? TR("Floor") : r - 2 < 6 ? TR(vis_label[r - 2]) : ""; }
const char *pvpr_ui_tip(int r) {
    if (r == 0) return TR("This settlement: the hub base you are in; teams start at its two ends.\n\nRandom map: a random dungeon floor, random start points for each team.\n\nDungeon 1 or 2: a floor of that dungeon, made by the game from a new seed. The same map for every player, with no monsters, no items and no portal.");
    if (r == 1) return TR("Which floor of the dungeon: a random one, floor 1 to 4, or the boss room (a small arena with no boss in it).");
    return r - 2 < 6 ? TR(vis_tip[r - 2]) : "";
}
/* the value text; "here" = this world is a settlement for PvP (PvP world / hub) */
void pvpr_ui_value(int r, const pvpr_vals *v, int here, char *out, size_t cap) {
    int x = pvpr_vals_get(v, pvpr_ui_id(r));
    if (r == 0) {
        if (x == PVP_MAP_HERE) { if (here) snprintf(out, cap, "%s", TR("This settlement")); else snprintf(out, cap, TR("Dungeon floor %d"), 3); }
        else if (x == PVP_MAP_RANDOM) snprintf(out, cap, "%s", TR("Random map"));
        else snprintf(out, cap, TR("Dungeon %d: %s"), x == PVP_MAP_DUNGEON1 ? 1 : 2, dungeon_name(x == PVP_MAP_DUNGEON1 ? 0 : 1));
    } else if (r == 1) {
        int m = pvpr_vals_get(v, "map");
        if (m != PVP_MAP_DUNGEON1 && m != PVP_MAP_DUNGEON2) snprintf(out, cap, "%s", m == PVP_MAP_RANDOM ? TR("Random floor") : "-");
        else if (x == PVP_FLOOR_RANDOM) snprintf(out, cap, "%s", TR("Random floor"));
        else if (x == PVP_FLOOR_BOSS) snprintf(out, cap, "%s", TR("Boss room"));
        else snprintf(out, cap, TR("Floor %d"), x);
    } else snprintf(out, cap, "%s", x ? TR("Shown") : TR("Hidden"));
    dsi_utf8_fix_end(out);
}
int pvpr_ui_steppable(int r, const pvpr_vals *v) {   /* the Floor row only means something for Dungeon 1 / 2 */
    if (r != 1) return 1;
    int m = pvpr_vals_get(v, "map"); return m == PVP_MAP_DUNGEON1 || m == PVP_MAP_DUNGEON2;
}
void pvpr_ui_step(int r, pvpr_vals *v, int d) {
    const char *id = pvpr_ui_id(r); int i = pvpr_index(id); if (i < 0) return;
    const pvpr_def *df = pvpr_def_at(i); int n = df->hi - df->lo + 1;
    int x = v->v[i] - df->lo; x = ((x + d) % n + n) % n; v->v[i] = x + df->lo;
}
