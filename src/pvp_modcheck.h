/* pvp_modcheck - the host rule "Vanilla check" for PvP (owner 2026-10-14 ~20:00; design board PvPVanillaCheck), pure C.
 *   On  = no GAMEPLAY mod for anyone (the host included); CLIENT mods are fine and never compared.
 *   Off = "Allow mods": every player must run exactly the host's gameplay mods (the launcher shares them).
 * A player's mods come from the launcher's session file (DSML_SESSION -> <profile>/dsml_session.json, written by
 * ~/ds_launcher core/src/flow.rs Session): "rule", "modset" (gameplay mod-set hash), "modset_lines" (one line per gameplay
 * mod: "<id>@<version>:<file sha256s>[:<patch hash>]"), "client_mods" ([{id, version}]). The report a guest mails to the
 * host has the same keys, so one parser reads both. No session file = not started by the launcher (known = 0): no mod
 * is known, the player counts as having no gameplay mod.
 * Tests: pvp_test/ (make -C pvp_test test). */
#pragma once
#include <stddef.h>

#define PVP_MC_MAX 16
typedef struct pvp_modset {
    int known;                          /* 1 = read from a launcher session (or a report of one) */
    int rule;                           /* the launcher's world rule: 1 vanilla, 0 mods, -1 not given */
    char hash[72];
    int ngame;  char game[PVP_MC_MAX][160];     /* gameplay: the modset_lines, sorted as the launcher wrote them */
    int nclient; char client[PVP_MC_MAX][80];   /* client: "<id> <version>" (shown only, never compared) */
} pvp_modset;

/* session file text or report mail -> m (all zero + rule -1 on empty / bad text: known = 0). 0 ok, -1 bad JSON */
int    pvp_modset_parse(const char *json, size_t n, pvp_modset *m);
/* the report mail (same keys as the session file). Returns the length written (0 = no room) */
size_t pvp_modset_json(const pvp_modset *m, char *out, size_t cap);
/* "<id> <version>" of a gameplay line (for messages) */
void   pvp_modset_name(const char *line, char *out, size_t cap);
/* A refusal as a code + its arguments, so each player shows it in its own language (the mail PVP_M_MODS_REFUSED carries
 * this, plus the host's text as "why" for older guests). who = the player's name ("" = unnamed: "A player"),
 * a = the mod list / mod name, b = the second list (PVP_WHY_MISMATCH: not allowed), n = how many mods (plurals). */
enum { PVP_WHY_NONE, PVP_WHY_VANILLA_SELF, PVP_WHY_VANILLA, PVP_WHY_FILES, PVP_WHY_NO_LAUNCHER, PVP_WHY_MISMATCH, PVP_WHY_N };
typedef struct pvp_why { int code, n; char who[64], a[600], b[600]; } pvp_why;
/* The host's check before a match starts, one player at a time. host = the host's mods, peer = this player's,
 * who = the player's name, self = 1 when the player is the host. 0 = allowed; 1 = refused, w = why (code + args). */
int    pvp_modset_check_why(int vanilla, const pvp_modset *host, const pvp_modset *peer, const char *who, int self, pvp_why *w);
/* the same, with the message in the current language (i18n/ds_i18n.h) */
int    pvp_modset_check(int vanilla, const pvp_modset *host, const pvp_modset *peer, const char *who, int self, char *why, size_t n);
/* w as text in the current language ("" for PVP_WHY_NONE or an unknown code) */
void   pvp_why_text(const pvp_why *w, char *out, size_t n);
/* the refusal mail: {"code":N,"n":N,"who":"..","a":"..","b":"..","why":"<text>"} (why = the host's text, for guests
   before codes). Returns the length (0 = no room). */
size_t pvp_why_json(const pvp_why *w, const char *why_text, char *out, size_t cap);
/* the mail read back: w + the text to show (in the current language; from an older host without "code": its "why"
   as it is). 0 ok, -1 bad JSON. */
int    pvp_why_parse(const char *json, size_t len, pvp_why *w, char *text, size_t n);
