// mpmenu_model.c - Multiplayer menu state machine, no game code (tests: mpmenu_test/).
// i18n: every text goes through TR / TRN (ds_i18n.h); the logic never compares a translated text (session ends: mpm_obs.end).
#define MPM_NO_GAME
#include "mpmenu.h"
#include "ds_i18n.h"
#include <stdio.h>
#include <string.h>
_Static_assert(MPB_FE_RESLIST < 64 && MPB_POP_NEXT < 80, "mpm_visible: title / lobby buttons need a bit below 64");

/* game words (mpmenu.h): the game side's lookup, else our English through TR */
static mpm_word_fn word_fn;
void mpm_set_word_fn(mpm_word_fn fn) { word_fn = fn; }
const char *mpm_word(const char *textkey, const char *english) {
    const char *w = word_fn && textkey ? word_fn(textkey, english) : NULL;
    return w && w[0] ? w : TR(english);
}
static const char *on_off(int v) { return v ? TR("on") : TR("off"); }
static const char *On_Off(int v) { return v ? TR("On") : TR("Off"); }

/* short labels (ux #17): at the stock button size 22 (design #8) the host page buttons fit on one line at 960 and 1512 */
void mpm_settings_summary(const mpm_settings *s, char *out, size_t cap) {
    if (s->fe) {
        snprintf(out, cap, TR("Foreign Expedition \xC2\xB7 Death: %s \xC2\xB7 Experience: %s \xC2\xB7 Dungeon loot: %s \xC2\xB7 Units: %d \xC2\xB7 Recruiting: %s"),
                 s->fe_death ? TR("permanent") : TR("not permanent"), s->fe_xp ? TR("counts") : TR("does not count"), s->fe_loot ? TR("comes back") : TR("stays"),
                 s->units, on_off(s->fe_recruit));
        return;
    }
    static const char *ev[] = { TR_NOOP("owner"), TR_NOOP("host"), TR_NOOP("first"), TR_NOOP("majority") },
                      *po[] = { TR_NOOP("shared"), TR_NOOP("per player"), TR_NOOP("split"), TR_NOOP("multiplied") };
    snprintf(out, cap, TR("Pause: %s \xC2\xB7 Speed-up: %s \xC2\xB7 Gear: %s \xC2\xB7 Units: %d \xC2\xB7 Population: %s \xC2\xB7 Events: %s \xC2\xB7 Admin: %s \xC2\xB7 Shared fog: %s \xC2\xB7 Spectators: %s"), mpm_pause_text(s->pause_mode),
             on_off(s->speedup), mpm_gear_text(s->gear), s->units, TR(po[s->population >= 0 && s->population < 4 ? s->population : 0]),
             TR(ev[s->events >= 0 && s->events < 4 ? s->events : 0]), on_off(s->admin), on_off(s->fog), on_off(s->spectators));
}
const char *mpm_population_desc(int mode) {           /* owner's wording (lead 2026-10-13) */
    return mode == 1 ? TR("Each player can recruit up to the full clan limit")
         : mode == 2 ? TR("The clan limit is divided between the players")
         : mode == 3 ? TR("Clan limit x players connected, shared by all") : TR("One clan limit for everyone, first come");
}
const char *mpm_rule_tip(int row) {                     /* tooltip bodies (research/tooltips_v9.md 3.3, uxdesign) */
    static const char *t[MPM_RULE_ROWS] = {
        TR_NOOP("Who can pause the game. Anyone can pause: one player pauses it for everyone. Pause by vote: it pauses only when every player votes for it. No pause: nobody can pause it. Events, the Portal screen and a lost player still pause in every mode."),
        TR_NOOP("On: players can play at Fast speed when every player asks for it. Off: the game stays at normal speed for everyone."),
        TR_NOOP("Who may change gear. Shared, own units: everyone uses the settlement storage, and each player equips only their own units. Shared, any unit: anyone equips any unit. Own storage: each player has their own items."),
        TR_NOOP("The most units each player can control at the same time (1, 2, 3, 4 or 6). The host can change it during play."),
        TR_NOOP("How the clan's population limit is shared. Shared: one limit for everyone, first come. Per player: each player has the full limit. Split equally: the limit is divided between players. Multiplied: limit times players, shared. A hire needs room here and under Units per Player."),
        TR_NOOP("Who answers an event. Owner of the unit: the player who controls the unit in the event. Host only. First to answer. Majority vote: the host breaks a tie."),
        TR_NOOP("On: the host can order every unit and release the units of absent players. Off: the host is a normal player for other players' units."),
        TR_NOOP("Off: each player sees only what their own units see; places explored by others stay dark until a reload. On: everyone sees what any unit has revealed. The world is the same for everyone either way."),
        TR_NOOP("Allowed: anyone you invite can join to watch (at most 8). They see everything, can chat and ping, own no units, and cannot pause or vote. Not allowed: nobody can watch; turning it off during play removes the spectators.") };
    return row >= 0 && row < MPM_RULE_ROWS ? TR(t[row]) : "";
}
/* row 4 "Population" is the game's own word (TEXTKEY_Shared_Population_NAME) */
const char *mpm_rule_tip_title(int row) {
    static const char *t[MPM_RULE_ROWS] = { TR_NOOP("Pause"), TR_NOOP("Speed-up"), TR_NOOP("Gear"), TR_NOOP("Units per Player"), "Population", TR_NOOP("Event Choices"), TR_NOOP("Admin Mode"), TR_NOOP("Shared Fog of War"), TR_NOOP("Spectators") };
    if (row == MPM_RULE_POP) return mpm_word("TEXTKEY_Shared_Population_NAME", TR_NOOP("Population"));
    return row >= 0 && row < MPM_RULE_ROWS ? TR(t[row]) : "";
}
const char *mpm_rule_label(int row) {
    static const char *l[MPM_RULE_ROWS] = { TR_NOOP("Pause"), TR_NOOP("Speed-up"), TR_NOOP("Gear"), TR_NOOP("Units per player"), "Population", TR_NOOP("Event choices"), TR_NOOP("Admin mode"), TR_NOOP("Shared fog"), TR_NOOP("Spectators") };
    if (row == MPM_RULE_POP) return mpm_word("TEXTKEY_Shared_Population_NAME", TR_NOOP("Population"));
    return row >= 0 && row < MPM_RULE_ROWS ? TR(l[row]) : "";
}
void mpm_rule_value(const mpm_settings *s, int row, char *out, size_t cap) {
    switch (row) {
    case 0: snprintf(out, cap, "%s", s->pause_mode == MPM_PAUSE_NONE ? TR("No pause") : s->pause_mode == MPM_PAUSE_ALL_VOTE ? TR("Pause by vote") : TR("Anyone can pause")); break;
    case 1: snprintf(out, cap, "%s", On_Off(s->speedup)); break;
    case 2: snprintf(out, cap, "%s", s->gear == MPM_GEAR_SHARED_ANY ? TR("Shared, any unit") : s->gear == MPM_GEAR_OWN_STORAGE ? TR("Own storage") : TR("Shared, own units")); break;
    case 3: snprintf(out, cap, "%d", s->units); break;
    case 4: snprintf(out, cap, "%s", s->population == 1 ? TR("Per player") : s->population == 2 ? TR("Split equally") : s->population == 3 ? TR("Multiplied") : TR("Shared")); break;
    case 5: snprintf(out, cap, "%s", s->events == 1 ? TR("Host only") : s->events == 2 ? TR("First to answer") : s->events == 3 ? TR("Majority vote") : TR("Owner of the unit")); break;
    case 6: snprintf(out, cap, "%s", On_Off(s->admin)); break;
    case 7: snprintf(out, cap, "%s", On_Off(s->fog)); break;
    case 8:
        if (s->spectators && s->watching > 0) snprintf(out, cap, TRN("Allowed \xC2\xB7 %d watching", "Allowed \xC2\xB7 %d watching", s->watching), s->watching);
        else snprintf(out, cap, "%s", s->spectators ? TR("Allowed") : TR("Not allowed"));
        break;
    default: out[0] = 0;
    }
}
/* Foreign Expedition rules (owner 2026-10-14: death, XP, dungeon loot only; recruiting in the hub is a host option, default Off).
   Row 1 "Experience" is the game's own word (TEXTKEY_Shared_EXP_NAME). */
const char *mpm_fe_label(int row) {
    static const char *l[MPM_FE_ROWS] = { TR_NOOP("Death"), "Experience", TR_NOOP("Dungeon loot"), TR_NOOP("Units per player"), TR_NOOP("Gear"), TR_NOOP("Recruiting") };
    if (row == 1) return mpm_word("TEXTKEY_Shared_EXP_NAME", TR_NOOP("Experience"));
    return row >= 0 && row < MPM_FE_ROWS ? TR(l[row]) : "";
}
void mpm_fe_value(const mpm_settings *s, int row, char *out, size_t cap) {
    switch (row) {
    case 0: snprintf(out, cap, "%s", s->fe_death ? TR("Permanent") : TR("Not permanent")); break;
    case 1: snprintf(out, cap, "%s", s->fe_xp ? TR("Counts") : TR("Does not count")); break;
    case 2: snprintf(out, cap, "%s", s->fe_loot ? TR("Comes back") : TR("Stays in the hub")); break;
    case 3: snprintf(out, cap, "%d", s->units); break;
    case 4: snprintf(out, cap, "%s", TR("Own storage")); break;
    case 5: snprintf(out, cap, "%s", On_Off(s->fe_recruit)); break;
    default: out[0] = 0;
    }
}
const char *mpm_fe_desc(const mpm_settings *s, int row) {
    switch (row) {
    case 0: return s->fe_death ? TR("A unit that dies is removed at home, with its gear.") : TR("A unit that dies goes home as it was at the last save.");
    case 1: return s->fe_xp ? TR("XP and levels gained here go home.") : TR("Units go home as they left.");
    case 2: return s->fe_loot ? TR("Items found in dungeons go home. Hub stock stays.") : TR("Everything found stays in the hub.");
    case 3: return TR("Units each player may bring.");
    case 4: return TR("Fixed in this mode: items must know their owner.");
    case 5: return s->fe_recruit ? TR("Recruits belong to the recruiter and stay in the hub.") : TR("Recruiting is not offered in the hub.");
    }
    return "";
}
const char *mpm_fe_tip(int row) {
    static const char *t[MPM_FE_ROWS] = {
        TR_NOOP("Permanent: a unit that dies in the expedition is removed from its home save, with the gear it wears at home. Not permanent: it goes home in its state from the last multiplayer save where it was alive."),
        TR_NOOP("Counts: levels, skills and experience gained in the hub go home. Does not count: units go home exactly as they left."),
        TR_NOOP("Comes back: items your units find in dungeons go into your home storage. Items made from the hub's stock always stay in the hub."),
        TR_NOOP("How many units each player may bring into the hub (1, 2, 3, 4 or 6). Up to 4 units go into each dive."),
        TR_NOOP("Fixed in a Foreign Expedition: each player has their own storage, so every item knows its owner when results go home."),
        TR_NOOP("Off: recruiting is not offered in the hub. On: players can recruit in the hub; recruits belong to the player who hired them and stay in the hub (they never go home).") };
    return row >= 0 && row < MPM_FE_ROWS ? TR(t[row]) : "";
}
static int fe_arrow(mpm_model *m, int c) {
    switch (c) {
    case MPB_FED_PREV: case MPB_FED_NEXT: m->set.fe_death = !m->set.fe_death; return 1;
    case MPB_FEX_PREV: case MPB_FEX_NEXT: m->set.fe_xp = !m->set.fe_xp; return 1;
    case MPB_FEL_PREV: case MPB_FEL_NEXT: m->set.fe_loot = !m->set.fe_loot; return 1;
    case MPB_FER_PREV: case MPB_FER_NEXT: m->set.fe_recruit = !m->set.fe_recruit; return 1;
    }
    return 0;
}
const char *mpm_sel_value(const mpm_model *m, int sel) {
    return sel == MPB_SEL_PAUSE ? mpm_pause_text(m->set.pause_mode) : sel == MPB_SEL_SPEED ? on_off(m->set.speedup)
         : sel == MPB_SEL_GEAR ? mpm_gear_text(m->set.gear) : sel == MPB_SEL_UNITS ? (const char *[]){ "?", "1", "2", "3", "4", "?", "6" }[m->set.units >= 1 && m->set.units <= 6 ? m->set.units : 0] : "";
}
const char *mpm_pause_text(int mode) {
    return mode == MPM_PAUSE_NONE ? TR("off") : mode == MPM_PAUSE_ALL_VOTE ? TR("by vote") : TR("anyone");
}
const char *mpm_gear_text(int gear) {
    return gear == MPM_GEAR_SHARED_ANY ? TR("any unit") : gear == MPM_GEAR_OWN_STORAGE ? TR("own storage") : TR("own units");
}

void mpm_init(mpm_model *m) {
    memset(m, 0, sizeof *m);
    m->state = MPM_IDLE;
    m->set.pause_mode = MPM_PAUSE_ANYONE; m->set.speedup = 1; m->set.gear = MPM_GEAR_SHARED_OWN; m->set.units = 3; m->set.spectators = 1;
    m->set.fe_death = 0; m->set.fe_xp = 1; m->set.fe_loot = 1; m->set.fe_recruit = 0;   /* board FESettingsA defaults; death Off (owner 2026-10-15) */
}

static int go(mpm_model *m, int st) {
    m->state = st; m->confirm_leave = 0;
    if (st != MPM_TITLE_HOST && st != MPM_PICK_SAVE && st != MPM_PICK_NEW && st != MPM_HOST_LOADING && st != MPM_LOBBY && st != MPM_PLAYING)
        m->choice = 0;                                   /* the host choice lasts from its host page to the end of its session */
    return MPA_REFRESH;
}
/* ---- host choices (MPF API for lane fepvp: register_host_choice2). nchoice 0 + host_choice = one old-style choice. */
static int nch(const mpm_model *m) { return m->nchoice ? m->nchoice : m->host_choice ? 1 : 0; }
static int ch_v2(const mpm_model *m, int k) { return m->nchoice && k >= 0 && k < m->nchoice ? m->choice_v2[k] : 0; }
static int ch_rules(const mpm_model *m, int k) { return m->nchoice && k >= 0 && k < m->nchoice ? m->choice_rules[k] : 1; }
static int cur_v2(const mpm_model *m) { return m->choice > 0 && ch_v2(m, m->choice - 1); }
int mpm_choice_btn(int k) { return k == 0 ? MPB_HOST_FE : k > 0 && k < MPM_CHOICES ? MPB_HOST_C1 + k - 1 : MPB_NONE; }
int mpm_choice_of_btn(int b) { return b == MPB_HOST_FE ? 0 : b >= MPB_HOST_C1 && b <= MPB_HOST_C3 ? b - MPB_HOST_C1 + 1 : -1; }
/* owner 2026-10-17: the Multiplayer page did not say where PvP and the game modes start. Under the buttons of the two
   modes Custom Modes registers (ids of ds_custommodes cm_mp.c), one small line says what the button leads to: Foreign
   Expedition = co-op in the neutral hub with the units each player brings (fe/fe_hub.c); PvP = the PvP v1 match, mode
   Fight or Capture the Flag (pvp/pvp_ui.c). Another mod's choice: no line. */
const char *mpm_choice_note(const char *id) {
    if (!id) return NULL;
    if (!strcmp(id, "foreign-expedition")) return TR("Co-op: your units in a shared hub");
    if (!strcmp(id, "pvp")) return TR("Team battle or Capture the Flag");
    return NULL;
}
int mpm_choice_register(mpm_choices *l, const char *id, const char *label, const char *tip, const mpm_choice *fn, int check_len) {
    if (!l || !id || !id[0] || !label || !label[0] || !fn) return -1;
    if (check_len && (strlen(id) > 31 || strlen(label) > 47 || (tip && strlen(tip) > 299))) return -1;
    int k = 0;
    while (k < l->n && strcmp(l->c[k].id, id)) k++;
    if (k == l->n) { if (l->n >= MPM_CHOICES) return -1; l->n++; }
    mpm_choice *c = &l->c[k];
    *c = *fn;
    snprintf(c->id, sizeof c->id, "%s", id); snprintf(c->label, sizeof c->label, "%s", label); snprintf(c->tip, sizeof c->tip, "%s", tip ? tip : "");
    return k;
}
void mpm_choices_to_model(const mpm_choices *l, mpm_model *m) {
    m->nchoice = l->n; m->host_choice = l->n > 0;
    snprintf(m->host_choice_label, sizeof m->host_choice_label, "%s", l->n ? l->c[0].label : "");
    for (int k = 0; k < MPM_CHOICES; k++) {
        snprintf(m->choice_label[k], sizeof m->choice_label[k], "%s", k < l->n ? l->c[k].label : "");
        m->choice_v2[k] = k < l->n && l->c[k].v2;
        m->choice_rules[k] = k < l->n && (!l->c[k].v2 || !l->c[k].draw_page);
    }
}
const char *mpm_choice_now(const mpm_choices *l, const mpm_model *m) { return m->choice > 0 && m->choice <= l->n ? l->c[m->choice - 1].id : ""; }
void mpm_page_started(mpm_model *m, int rc, const char *err) {
    if (m->state != MPM_TITLE_HOST) return;
    if (rc == 0) { m->msg[0] = 0; m->closed_frames = 0; go(m, MPM_HOST_LOADING); return; }   /* waits like "Your save" */
    snprintf(m->msg, sizeof m->msg, "%s", err && err[0] ? err : TR("This mode could not start."));
    dsi_utf8_fix_end(m->msg);                                               /* a mod's text may end inside a character */
}
static const mpm_obs *cur;
/* a failed start or join (lan_v10, ux_flow F8): the reason goes to the stock notice box (game side), not in small type
 * in brackets on the Multiplayer button; the button stays plain. head and why: already translated. */
static int fail(mpm_model *m, const char *head, const char *why) {
    char r[200]; snprintf(r, sizeof r, "%s", cur && cur->msg[0] ? cur->msg : why);
    /* the title line says "Could not join" already: drop that start of a refusal. Both texts come from the same
       TR("Could not join: %s") (mp_game), so this compares one translated text with itself, in any language */
    const char *f = TR("Could not join: %s"), *ps = strstr(f, "%s");
    size_t pl = ps ? (size_t)(ps - f) : 0;
    if (pl && !strncmp(r, f, pl)) memmove(r, r + pl, strlen(r + pl) + 1);
    if (r[0] >= 'a' && r[0] <= 'z') r[0] = (char)(r[0] - 32);
    m->msg[0] = 0;
    size_t rl = strlen(r);
    int dot = rl && r[rl - 1] != '.' && (unsigned char)r[rl - 1] < 0x80;          /* "." only after a Latin letter ("。" ends CJK) */
    snprintf(m->notice, sizeof m->notice, "<size=150%%>%s</size>\n%s%s", head, r, dot ? "." : "");
    return go(m, MPM_FAILED);
}
static int join_failed(mpm_model *m) {
    char h[160];
    if (m->host[0]) snprintf(h, sizeof h, TR("Could not join %s's game"), m->host);
    else snprintf(h, sizeof h, "%s", TR("Could not join the game"));
    return fail(m, h, TR("The connection to the host failed. Try again, or ask the host to invite you again"));
}
/* the session ended without our own Leave: a stock notice box on the title says why (ux #9), not the button line.
 * Which case it is comes from o->end (MPM_END_*), never from the text. */
static int ended(mpm_model *m, const mpm_obs *o) {
    char a[200], b[400]; const char *h = m->host;
    m->msg[0] = 0;
    if (o->end == MPM_END_HOST_LEFT) {
        if (h[0]) { snprintf(a, sizeof a, TR("%s left the game"), h);
                    snprintf(b, sizeof b, TR("The world is saved on %s's computer. To play again, %s hosts it and invites you."), h, h); }
        else { snprintf(a, sizeof a, "%s", TR("The host left the game"));
               snprintf(b, sizeof b, "%s", TR("The world is saved on the host's computer. To play again, the host hosts it and invites you.")); }
        snprintf(m->notice, sizeof m->notice, "<size=150%%>%s</size>\n%s", a, b);
    } else if (o->end == MPM_END_SPECT_OFF) {                                 /* a spectator dropped by the host's rule */
        if (h[0]) snprintf(a, sizeof a, TR("%s turned spectators off"), h); else snprintf(a, sizeof a, "%s", TR("The host turned spectators off"));
        snprintf(m->notice, sizeof m->notice, "<size=150%%>%s</size>\n%s", a, TR("You can no longer watch this world."));
    } else if (o->end == MPM_END_LINK_LOST) {                                 /* our own link dropped (no host BYE): the host waits */
        if (h[0]) { snprintf(a, sizeof a, TR("Connection to %s lost"), h); snprintf(b, sizeof b, TR("%s is waiting for you: rejoin from Recent worlds."), h); }
        else { snprintf(a, sizeof a, "%s", TR("Connection to the host lost")); snprintf(b, sizeof b, "%s", TR("The host is waiting for you: rejoin from Recent worlds.")); }
        snprintf(m->notice, sizeof m->notice, "<size=150%%>%s</size>\n%s", a, b);
    } else snprintf(m->notice, sizeof m->notice, "%s", o->msg);
    return go(m, MPM_IDLE);
}

/* selector arrows (host page and lobby): previous / next value of a rule; 1 if it changed */
static const int units_list[] = { 1, 2, 3, 4, 6 };
static int units_step(int u, int d) {
    int i = 0; while (i < 5 && units_list[i] != u) i++;
    if (i == 5) i = 2;
    return units_list[(i + d + 5) % 5];
}
static int rule_arrow(mpm_model *m, int c) {
    switch (c) {
    case MPB_PAUSE_PREV: m->set.pause_mode = (m->set.pause_mode + 2) % 3; return 1;
    case MPB_PAUSE_NEXT: case MPB_PAUSE_MODE: m->set.pause_mode = (m->set.pause_mode + 1) % 3; return 1;
    case MPB_SPEED_PREV: case MPB_SPEED_NEXT: case MPB_SPEEDUP: m->set.speedup = !m->set.speedup; return 1;   /* two values */
    case MPB_GEAR_PREV: m->set.gear = (m->set.gear + 2) % 3; return 1;
    case MPB_GEAR_NEXT: case MPB_GEAR: m->set.gear = (m->set.gear + 1) % 3; return 1;
    case MPB_UNITS_PREV: m->set.units = units_step(m->set.units, -1); return 1;
    case MPB_UNITS_NEXT: m->set.units = units_step(m->set.units, 1); return 1;
    case MPB_ADMIN_PREV: case MPB_ADMIN_NEXT: m->set.admin = !m->set.admin; return 1;
    case MPB_EVENTS_PREV: m->set.events = (m->set.events + 3) % 4; return 1;
    case MPB_EVENTS_NEXT: m->set.events = (m->set.events + 1) % 4; return 1;
    case MPB_POP_PREV: m->set.population = (m->set.population + 3) % 4; return 1;
    case MPB_POP_NEXT: m->set.population = (m->set.population + 1) % 4; return 1;
    case MPB_FOG_PREV: case MPB_FOG_NEXT: m->set.fog = !m->set.fog; return 1;
    case MPB_SPECT_PREV: case MPB_SPECT_NEXT: m->set.spectators = !m->set.spectators; return 1;   /* title page: no session yet */
    }
    return 0;
}
int mpm_step(mpm_model *m, const mpm_obs *o, int c) {
    int a = 0; cur = o;
    m->steam = o->steam; m->spect = o->spectator;
    /* Escape = Back on every title page. Host page: the Rules are always open (owner v12 item 5), nothing to close first */
    if (o->escape && c == MPB_NONE && o->scene == 1) {
        /* one rule (ux_flow F16): Escape = the visible Cancel if there is one, else Back */
        if (m->state == MPM_TITLE_MENU || m->state == MPM_TITLE_JOIN) c = m->connecting[0] ? MPB_CANCEL_JOIN : MPB_BACK;
        else if (m->state == MPM_TITLE_HOST || m->state == MPM_TITLE_FRIENDS) c = MPB_BACK;
        else if (m->state == MPM_GUEST_JOINING || m->state == MPM_GUEST_LOADING) c = MPB_CANCEL_JOIN;
    }
    if (o->escape && c == MPB_NONE && m->confirm_leave) c = MPB_LEAVE_CANCEL;   /* Escape = Cancel in the Leave box */
    if (o->escape && c == MPB_NONE && m->state == MPM_LOBBY && m->lobby_settings_open) c = MPB_RULES_DONE;   /* lobby: Escape closes the Rules */
    if (o->host_name[0]) snprintf(m->host, sizeof m->host, "%s", o->host_name);
    switch (m->state) {
    case MPM_IDLE: case MPM_FAILED:
        if (o->role == 2) { m->msg[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->role == 1 && o->scene == 2) return go(m, MPM_PLAYING);          /* a session started another way (mp.json) */
        if (c == MPB_MULTIPLAYER && o->scene == 1) { m->msg[0] = 0; return go(m, MPM_TITLE_MENU); }
        if (o->esc_open && c == MPB_NONE) return 0;
        break;
    case MPM_TITLE_MENU:
        if (o->role == 2) return go(m, MPM_GUEST_JOINING);
        if (o->scene != 1) return go(m, MPM_IDLE);
        /* the three host choices under "Host a game" (owner 2026-10-15); MPB_HOST (the line itself) = Your save, as before.
           An expedition is only ever chosen on purpose (left_prompt regression 2026-10-15) */
        if (c == MPB_HOST || c == MPB_HOST_SAVE) { m->set.fe = 0; m->host_new = 0; m->choice = 0; return go(m, MPM_TITLE_HOST); }
        if (c == MPB_HOST_NEW) { m->set.fe = 0; m->host_new = 1; m->choice = 0; return go(m, MPM_TITLE_HOST); }
        { int k = mpm_choice_of_btn(c);                                        /* a registered host choice (only once registered) */
          if (k >= 0 && k < nch(m)) { m->set.fe = ch_rules(m, k); m->host_new = 0; m->msg[0] = 0; m->choice = k + 1; return go(m, MPM_TITLE_HOST); } }
        if (c == MPB_JOIN_ADDR) { m->msg[0] = 0; return go(m, MPM_TITLE_JOIN); }
        if (c == MPB_FE_RESLIST && m->fe_results) return MPA_FE_RESULTS;
        if (c >= MPB_HIST0 && c <= MPB_HIST5 && m->hist_page * 6 + (c - MPB_HIST0) < m->nhist) { m->hist_pick = m->hist_page * 6 + (c - MPB_HIST0); m->msg[0] = 0; m->watch = 0; return MPA_HIST_JOIN | MPA_REFRESH; }
        if (c >= MPB_HISTW0 && c <= MPB_HISTW5 && m->hist_page * 6 + (c - MPB_HISTW0) < m->nhist) { m->hist_pick = m->hist_page * 6 + (c - MPB_HISTW0); m->msg[0] = 0; m->watch = 1; return MPA_HIST_JOIN | MPA_REFRESH; }
        if (c >= MPB_HISTX0 && c <= MPB_HISTX5 && m->hist_page * 6 + (c - MPB_HISTX0) < m->nhist) { m->hist_pick = m->hist_page * 6 + (c - MPB_HISTX0); return MPA_HIST_DEL | MPA_REFRESH; }
        if (c == MPB_HIST_PREV && m->hist_page > 0) { m->hist_page--; return MPA_REFRESH; }
        if (c == MPB_HIST_NEXT && (m->hist_page + 1) * 6 < m->nhist) { m->hist_page++; return MPA_REFRESH; }
        if (c == MPB_JOIN_FRIEND && o->steam) { m->msg[0] = 0; m->nfriends = 0; m->watch = 0; return go(m, MPM_TITLE_FRIENDS) | MPA_FRIENDS; }   /* LAN app: greyed, no action */
        if (c == MPB_CANCEL_JOIN && m->connecting[0]) { m->connecting[0] = 0; m->msg[0] = 0; return MPA_JOIN_CANCEL | MPA_REFRESH; }   /* a Recent worlds reconnect: stay here */
        if (c == MPB_BACK) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->connecting[0] = 0; m->msg[0] = 0; return go(m, MPM_IDLE) | a; }
        break;
    case MPM_TITLE_FRIENDS:
        if (o->role == 2) { m->msg[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->scene != 1) return go(m, MPM_IDLE);
        if (c >= MPB_FRIEND0 && c <= MPB_FRIEND5 && c - MPB_FRIEND0 < m->nfriends) { m->friend_pick = c - MPB_FRIEND0; return MPA_JOIN_FRIEND | MPA_REFRESH; }
        if (c == MPB_BACK) return go(m, MPM_TITLE_MENU);
        break;
    case MPM_TITLE_JOIN:
        if (o->role == 2) { m->msg[0] = m->connecting[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->scene != 1) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->connecting[0] = 0; return go(m, MPM_IDLE) | a; }
        if (o->edited && !m->connecting[0]) { m->msg[0] = 0; a |= MPA_REFRESH; }   /* an old error goes when the text changes */
        if (c == MPB_CONNECT && !m->connecting[0]) { m->msg[0] = 0; m->watch = 0; return MPA_JOIN_ADDR | MPA_REFRESH; }   /* game side sets connecting or msg */
        if (c == MPB_WATCH && !m->connecting[0]) { m->msg[0] = 0; m->watch = 1; return MPA_JOIN_ADDR | MPA_REFRESH; }       /* the same, as a spectator */
        if (c == MPB_CANCEL_JOIN && m->connecting[0]) { m->connecting[0] = 0; m->msg[0] = 0; return MPA_JOIN_CANCEL | MPA_REFRESH; }
        if (c == MPB_BACK) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->msg[0] = m->connecting[0] = 0; return go(m, MPM_TITLE_MENU) | a; }
        break;
    case MPM_TITLE_HOST:
        if (o->role == 2) return go(m, MPM_GUEST_JOINING);
        if (o->scene != 1) return go(m, MPM_IDLE);
        if (o->edited) m->msg[0] = 0;
        if (c == MPB_SETTINGS) { m->settings_open = !m->settings_open; return MPA_REFRESH; }
        if (c == MPB_RULES_DONE || c == MPB_FE_DONE) { m->settings_open = 0; return MPA_REFRESH; }
        if (c == MPB_FEU_PREV) { m->set.units = units_step(m->set.units, -1); return MPA_REFRESH; }
        if (c == MPB_FEU_NEXT) { m->set.units = units_step(m->set.units, 1); return MPA_REFRESH; }
        if (fe_arrow(m, c)) return MPA_REFRESH;
        if (rule_arrow(m, c)) return MPA_REFRESH;
        if (c == MPB_PAUSE_MODE) { m->set.pause_mode = (m->set.pause_mode + 1) % 3; return MPA_REFRESH; }
        if (c == MPB_SPEEDUP) { m->set.speedup = !m->set.speedup; return MPA_REFRESH; }
        if (c == MPB_GEAR) { m->set.gear = (m->set.gear + 1) % 3; return MPA_REFRESH; }
        if (c == MPB_HOST_START && cur_v2(m)) return MPA_CHOICE_PAGE | MPA_REFRESH;   /* the mod loads its world (mpm_page_started) */
        if ((c == MPB_LOAD_SAVE || c == MPB_NEW_CAMPAIGN) && cur_v2(m)) break;   /* not on such a page */
        if (c == MPB_LOAD_SAVE) { m->picker_seen = 0; m->closed_frames = 0; return go(m, MPM_PICK_SAVE) | MPA_OPEN_LOAD_LIST; }
        if (c == MPB_NEW_CAMPAIGN) { m->picker_seen = 0; m->closed_frames = 0; return go(m, MPM_PICK_NEW) | MPA_OPEN_NEW; }
        if (c == MPB_BACK) return go(m, MPM_TITLE_MENU);
        break;
    case MPM_PICK_SAVE: case MPM_PICK_NEW:
        if (o->scene != 1 || o->loading) { m->closed_frames = 0; return go(m, MPM_HOST_LOADING); }     /* the campaign is loading */
        if (o->picker_open || !o->title_visible) { m->picker_seen = 1; m->closed_frames = 0; }
        else if (m->picker_seen && ++m->closed_frames > 90) return go(m, MPM_TITLE_HOST);   /* closed, nothing loads: back */
        break;
    case MPM_HOST_LOADING:
        if (o->campaign_ready) { m->closed_frames = 0; return go(m, MPM_LOBBY) | MPA_HOST_START | MPA_LOBBY_SHOW; }
        if (o->scene == 1 && o->title_visible && !o->loading && ++m->closed_frames > 300) return fail(m, TR("Multiplayer could not start"), TR("Loading the campaign failed"));
        break;
    case MPM_LOBBY:
        if (o->scene == 1 && o->title_visible && !o->loading) {              /* back on the title (quit): end the session */
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        } else m->closed_frames = 0;
        if (o->failed) return fail(m, TR("Multiplayer could not start"), TR("The world stays open as a single-player game")) | MPA_LOBBY_HIDE | (m->confirm_leave ? MPA_LEAVE_BOX_CLOSE : 0);
        if (o->role == 0 && c == MPB_NONE) { snprintf(m->msg, sizeof m->msg, "%s", o->msg); return go(m, MPM_IDLE) | MPA_LOBBY_HIDE; }
        if (c == MPB_INVITE) return o->steam ? MPA_INVITE : 0;            /* LAN app: greyed "Needs the Steam version" */
        if (c == MPB_COPY_ADDR) return MPA_COPY_ADDR;
        if (c == MPB_FE_BRINGWIN) return MPA_FE_BRING;
        if (c == MPB_START && cur_v2(m)) return MPA_CHOICE_LOBBY;            /* the choice's lobby_start (game side) */
        if (c == MPB_START && m->set.fe) return MPA_FE_START;              /* game side builds the hub, then starts (or says why not) */
        if (c == MPB_START) return go(m, MPM_PLAYING) | MPA_LOBBY_HIDE | MPA_START_GAME;
        if (c == MPB_LOBBY_SETTINGS) { m->lobby_settings_open = !m->lobby_settings_open; return MPA_REFRESH; }
        if (c == MPB_ADMIN || ((c == MPB_ADMIN_PREV || c == MPB_ADMIN_NEXT) && m->lobby_settings_open)) return MPA_ADMIN | MPA_REFRESH;   /* dsnet admin mode */
        if (c == MPB_RULES_DONE) { m->lobby_settings_open = 0; return MPA_REFRESH; }
        if (m->lobby_settings_open && (c == MPB_SPECT_PREV || c == MPB_SPECT_NEXT)) return MPA_SPECTATORS | MPA_REFRESH;   /* game side: confirm box / mp_set_spectators */
        if (m->lobby_settings_open && rule_arrow(m, c)) return MPA_RULES | MPA_REFRESH;   /* host changes the rules in the lobby */
        /* host Leave ends everyone's session (ux_flow F7): ask first */
        if (c == MPB_LEAVE && !m->confirm_leave) { m->confirm_leave = 1; return MPA_CONFIRM_LEAVE | MPA_REFRESH; }
        if (c == MPB_LEAVE_CONFIRM && m->confirm_leave) return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE;
        if (c == MPB_LEAVE_CANCEL && m->confirm_leave) { m->confirm_leave = 0; return MPA_LEAVE_BOX_CLOSE | MPA_REFRESH; }
        break;
    case MPM_PLAYING:
        if (o->scene == 1 && o->title_visible && !o->loading) {              /* back on the title (quit): end the session */
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE; }
        } else m->closed_frames = 0;
        if (c == MPB_ESC_INVITE && o->role == 1) return MPA_INVITE;
        if (c == MPB_LEAVE && o->role == 1 && !m->confirm_leave) { m->confirm_leave = 1; return MPA_CONFIRM_LEAVE | MPA_REFRESH; }   /* host: ask first (F7) */
        if (c == MPB_LEAVE_CANCEL && m->confirm_leave) { m->confirm_leave = 0; return MPA_LEAVE_BOX_CLOSE | MPA_REFRESH; }
        if ((c == MPB_LEAVE && o->role != 1) || (c == MPB_LEAVE_CONFIRM && m->confirm_leave)) { m->msg[0] = 0; return go(m, MPM_IDLE) | MPA_LEAVE; }
        if (o->role == 0 && m->confirm_leave) a |= MPA_LEAVE_BOX_CLOSE;    /* the session ended under the box */
        if (o->role == 0) {                                                  /* steamnet's reason ("The host left the game"); own quit: none */
            if (!o->msg[0]) { m->msg[0] = 0; return go(m, MPM_IDLE) | a; }
            if (o->end == MPM_END_REFUSED) return join_failed(m) | a;        /* a join from the player's own world was refused (F9, F17) */
            return ended(m, o) | a;
        }
        if (o->role == 2 && o->host_lobby && o->campaign_ready) return go(m, MPM_GUEST_LOBBY) | MPA_LOBBY_SHOW;
        break;
    case MPM_GUEST_LOBBY:                                                    /* host still in the lobby: waiting panel */
        if (o->scene == 1 && o->title_visible && !o->loading) {
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        } else m->closed_frames = 0;
        if (c == MPB_LEAVE) { m->msg[0] = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        if (c == MPB_FE_BRINGWIN) return MPA_FE_BRING;
        if (o->role == 0) return (o->msg[0] ? ended(m, o) : go(m, MPM_IDLE)) | MPA_LOBBY_HIDE;
        if (!o->host_lobby) return go(m, MPM_PLAYING) | MPA_LOBBY_HIDE;
        break;
    case MPM_GUEST_JOINING: case MPM_GUEST_LOADING:
        if (c == MPB_CANCEL_JOIN) return go(m, MPM_IDLE) | MPA_LEAVE;
        if (o->failed || o->role == 0) {
            if (o->end == MPM_END_HOST_LEFT || o->end == MPM_END_LINK_LOST || o->end == MPM_END_SPECT_OFF) return ended(m, o);
            return join_failed(m);
        }
        if (o->guest_loading && m->state == MPM_GUEST_JOINING) a |= go(m, MPM_GUEST_LOADING);
        if (o->campaign_ready && !o->guest_loading) { m->closed_frames = 0; return go(m, MPM_PLAYING); }
        break;
    }
    return a;
}

int mpm_is_visible(const mpm_model *m, const mpm_obs *o, int b) {
    if (b == MPB_WATCH) return m->state == MPM_TITLE_JOIN && !m->connecting[0];
    if (b == MPB_HOST_SAVE || b == MPB_HOST_NEW) return m->state == MPM_TITLE_MENU;
    { int k = mpm_choice_of_btn(b); if (k >= 0) return m->state == MPM_TITLE_MENU && k < nch(m); }   /* only once a mod registered it */
    if (b == MPB_HOST_START) return m->state == MPM_TITLE_HOST && cur_v2(m);
    if (b == MPB_JOIN_DIV || b == MPB_JOIN_HDR) return m->state == MPM_TITLE_MENU;   /* Host a game ... | divider | Join a game ... */
    if (b < 0 || b >= 64) return 0;
    return (int)(mpm_visible(m, o) >> b & 1u);
}
uint64_t mpm_visible(const mpm_model *m, const mpm_obs *o) {
#define B(x) (1ull << (x))
    switch (m->state) {
    case MPM_IDLE: case MPM_FAILED:
        return o->scene == 1 ? B(MPB_MULTIPLAYER) : 0;
    case MPM_TITLE_MENU: return B(MPB_HOST) | B(MPB_JOIN_ADDR) | B(MPB_JOIN_FRIEND) | B(MPB_BACK) | (m->msg[0] || m->connecting[0] ? B(MPB_INFO) : 0)   /* INFO: reconnect status */
                              | (m->connecting[0] ? B(MPB_CANCEL_JOIN) : 0)                          /* a Recent worlds reconnect can be stopped (F16) */
                              | (m->fe_results ? B(MPB_FE_RESLIST) : 0);
    case MPM_TITLE_FRIENDS: {
        uint64_t v = B(MPB_HEADER) | B(MPB_INFO) | B(MPB_DIVIDER) | B(MPB_BACK);
        for (int i = 0; i < m->nfriends && i < 6; i++) v |= B(MPB_FRIEND0 + i);
        return v;
    }
    case MPM_TITLE_JOIN:                                                     /* INFO = connecting / error / no-field hint */
        return B(MPB_HEADER) | B(MPB_LABEL_ADDR) | B(MPB_LABEL_NAME) | B(MPB_DIVIDER) | (m->connecting[0] ? B(MPB_CANCEL_JOIN) : B(MPB_CONNECT)) | B(MPB_BACK)
             | (m->connecting[0] || m->msg[0] || m->no_field ? B(MPB_INFO) : 0);
    case MPM_TITLE_HOST:                                                     /* layout v2 §2a */
        return B(MPB_HEADER) | B(MPB_LABEL_NAME) | (m->msg[0] ? B(MPB_INFO) : 0)
             /* the Rules panel is drawn by the game side next to the column, always open (owner v12 item 5: no cog button) */
             | B(MPB_DIVIDER) | (cur_v2(m) ? 0 : m->host_new ? B(MPB_NEW_CAMPAIGN) : B(MPB_LOAD_SAVE)) | B(MPB_BACK);   /* the choice made on the Multiplayer page; a v2 choice: Start (MPB_HOST_START, id >= 64) */
    case MPM_LOBBY: return B(MPB_INVITE) | B(MPB_COPY_ADDR) | B(MPB_START) | B(MPB_LEAVE);
    case MPM_GUEST_LOBBY: return B(MPB_LEAVE);
    case MPM_PLAYING: return o->esc_open ? ((o->role == 1 ? B(MPB_ESC_INVITE) : 0) | B(MPB_LEAVE)) : 0;
    case MPM_GUEST_JOINING: case MPM_GUEST_LOADING:
        return o->scene == 1 ? B(MPB_INFO) | B(MPB_CANCEL_JOIN) : 0;          /* INFO = status line */
    }
    return 0;
#undef B
}

void mpm_label(const mpm_model *m, int b, char *out, size_t cap) {
    char tmp[200];
    switch (b) {
    case MPB_MULTIPLAYER:
        if (m->msg[0]) snprintf(out, cap, "%s <size=60%%>(%s)</size>", TR("Multiplayer"), m->msg);
        else snprintf(out, cap, "%s", TR("Multiplayer"));
        break;
    case MPB_LABEL_ADDR: snprintf(out, cap, "%s", TR("Host address")); break;
    case MPB_HEADER:
        if (m->state == MPM_TITLE_HOST && cur_v2(m)) { snprintf(out, cap, TR("Host: %s"), m->choice_label[m->choice - 1]); break; }
        snprintf(out, cap, "%s", m->state == MPM_TITLE_JOIN ? TR("Join a multiplayer world") : m->state == MPM_TITLE_FRIENDS ? TR("Join a friend on Steam")
                                          : m->set.fe ? TR("Host a Foreign Expedition") : m->host_new ? TR("Host a new save") : TR("Host your save")); break;
    case MPB_JOIN_FRIEND:
        if (m->steam) snprintf(out, cap, "%s", TR("Join a friend on Steam"));
        else snprintf(out, cap, "<color=#A69085><line-height=80%%>%s\n<size=65%%>%s</size></line-height></color>", TR("Join a friend on Steam"), TR("Needs the Steam version"));
        break;
    case MPB_FRIEND0: case MPB_FRIEND1: case MPB_FRIEND2: case MPB_FRIEND3: case MPB_FRIEND4: case MPB_FRIEND5:
        if (m->friends[b - MPB_FRIEND0][0]) snprintf(out, cap, TR("Join %s's world"), m->friends[b - MPB_FRIEND0]);
        else snprintf(out, cap, "%s", TR("Join a friend's world"));
        break;
    case MPB_SETTINGS: snprintf(out, cap, "%s", TR("Rules")); break;        /* cog left, "›" right, summary under it */
    case MPB_SUMMARY: mpm_settings_summary(&m->set, out, cap); break;
    case MPB_SEL_PAUSE: snprintf(out, cap, "%s", TR("Pause")); break;
    case MPB_SEL_SPEED: snprintf(out, cap, "%s", TR("Speed-up")); break;
    case MPB_SEL_GEAR: snprintf(out, cap, "%s", TR("Gear")); break;
    case MPB_SEL_UNITS: snprintf(out, cap, "%s", TR("Units per player")); break;
    case MPB_UNITS_PREV: case MPB_UNITS_NEXT: out[0] = 0; break;
    case MPB_DIVIDER: case MPB_PAUSE_PREV: case MPB_PAUSE_NEXT: case MPB_SPEED_PREV: case MPB_SPEED_NEXT:
    case MPB_GEAR_PREV: case MPB_GEAR_NEXT: out[0] = 0; break;
    case MPB_LABEL_NAME: snprintf(out, cap, "%s", TR("Your name")); break;
    case MPB_INFO:                                                          /* small type, errors in red */
        if (m->state == MPM_TITLE_FRIENDS)
            snprintf(out, cap, "%s", m->msg[0] ? m->msg : m->nfriends ? TR("Friends hosting a world now:") : TR("No Steam friend is hosting right now. Ask them to host, or use Join by address."));
        else if (m->state == MPM_GUEST_JOINING || m->state == MPM_GUEST_LOADING) {
            const char *f = m->spect ? (m->state == MPM_GUEST_LOADING ? TR("Joining %s's game as a spectator... loading") : TR("Joining %s's game as a spectator..."))
                                     : (m->state == MPM_GUEST_LOADING ? TR("Joining %s's game... loading") : TR("Joining %s's game..."));
            snprintf(tmp, sizeof tmp, f, m->host[0] ? m->host : TR("the host"));
            snprintf(out, cap, "<size=75%%>%s</size>", tmp);
        }
        else if (m->connecting[0]) { snprintf(tmp, sizeof tmp, m->watch ? TR("Connecting to %s to watch...") : TR("Connecting to %s..."), m->connecting);
                                     snprintf(out, cap, "<size=70%%>%s</size>", tmp); }
        else if (m->msg[0]) snprintf(out, cap, "<color=#F16071>%s</color>", m->msg);   /* Lora 14 (game side) */
        else if (m->no_field) snprintf(out, cap, "<size=60%%>%s</size>", TR("Copy the host's address, then press Connect"));
        else out[0] = 0;
        break;
    case MPB_HOST: snprintf(out, cap, "%s", TR("Host a game")); break;
    case MPB_JOIN_HDR: snprintf(out, cap, "%s", TR("Join a game")); break;
    case MPB_JOIN_DIV: out[0] = 0; break;
    case MPB_HOST_SAVE: snprintf(out, cap, "%s", TR("Your save")); break;
    case MPB_HOST_NEW: snprintf(out, cap, "%s", TR("New save")); break;
    /* host choices: the registering mod's own text (Custom Modes registers it translated, again on a language change) */
    case MPB_HOST_FE: snprintf(out, cap, "%s", m->nchoice ? m->choice_label[0] : m->host_choice_label[0] ? m->host_choice_label : TR("Foreign Expedition")); break;
    case MPB_HOST_C1: case MPB_HOST_C2: case MPB_HOST_C3: snprintf(out, cap, "%s", m->choice_label[mpm_choice_of_btn(b)]); break;
    case MPB_HOST_START: snprintf(out, cap, "%s", mpm_word("TEXTKEY_Common_Start", TR_NOOP("Start"))); break;
    case MPB_FE_RESLIST: snprintf(out, cap, "%s", TR("Expedition results")); break;
    case MPB_BACK: snprintf(out, cap, "%s", mpm_word("TEXTKEY_Common_Back", TR_NOOP("Back"))); break;
    case MPB_PAUSE_MODE: snprintf(out, cap, TR("Pause: %s"), mpm_pause_text(m->set.pause_mode)); break;
    case MPB_SPEEDUP: snprintf(out, cap, TR("Speed-up: %s"), on_off(m->set.speedup)); break;
    case MPB_GEAR: snprintf(out, cap, TR("Gear: %s"), mpm_gear_text(m->set.gear)); break;
    case MPB_LOAD_SAVE: snprintf(out, cap, "%s", TR("Host from a save")); break;
    case MPB_NEW_CAMPAIGN: snprintf(out, cap, "%s", TR("Host a new campaign")); break;
    case MPB_INVITE: case MPB_ESC_INVITE: snprintf(out, cap, "%s", TR("Steam invite")); break;
    case MPB_JOIN_ADDR: snprintf(out, cap, "%s", TR("Join by address")); break;
    case MPB_CONNECT: snprintf(out, cap, "%s", m->no_field ? TR("Connect with the copied address") : TR("Connect")); break;
    case MPB_COPY_ADDR: snprintf(out, cap, "%s", TR("Copy direct address")); break;
    case MPB_START: snprintf(out, cap, "%s", mpm_word("TEXTKEY_Common_Start", TR_NOOP("Start"))); break;
    case MPB_LEAVE: snprintf(out, cap, "%s", TR("Leave multiplayer")); break;
    case MPB_CANCEL_JOIN: snprintf(out, cap, "%s", mpm_word("TEXTKEY_CommonPopUp_Cancel", TR_NOOP("Cancel"))); break;
    case MPB_WATCH: case MPB_HISTW0: case MPB_HISTW1: case MPB_HISTW2: case MPB_HISTW3: case MPB_HISTW4: case MPB_HISTW5: snprintf(out, cap, "%s", TR("Watch")); break;
    case MPB_SPECT_PREV: case MPB_SPECT_NEXT: out[0] = 0; break;
    default: snprintf(out, cap, "?"); break;
    }
}

/* lobby panel (design #6): a title line (Cinzel, set by the game side) and a body: next step, one line per player ("· name": Lora has no U+25CF),
 * settings in the secondary colour, addresses. No <b> (TMP swaps in a font that is not the game's). */
void mpm_lobby_title(const mpm_model *m, char *out, size_t cap) {
    if (m->state == MPM_GUEST_LOBBY) snprintf(out, cap, TR("In %s's lobby"), m->host[0] ? m->host : TR("the host"));
    else snprintf(out, cap, "%s", TR("Multiplayer lobby"));
}
void mpm_lobby_text(const mpm_model *m, const char *const *names, int n, char *out, size_t cap) {
    size_t o;
    if (m->state == MPM_GUEST_LOBBY) {                                     /* guest waiting panel */
        const char *h = m->host[0] ? m->host : TR("the host");
        o = (size_t)snprintf(out, cap, TR("The game starts when %s presses Start."), h);
    } else o = (size_t)snprintf(out, cap, "%s", TR("Share your address, then press Start when everyone is in."));
    if (n <= 0 && o < cap) o += (size_t)snprintf(out + o, cap - o, "\n\xC2\xB7 %s", TR("You (host)"));
    for (int i = 0; i < n && o < cap; i++) {
        const char *nm = names[i] && names[i][0] ? names[i] : TR("Player");
        if (i == 0) o += (size_t)snprintf(out + o, cap - o, "\n\xC2\xB7 ");
        if (i == 0 && o < cap) o += (size_t)snprintf(out + o, cap - o, TR("%s (host)"), nm);
        else if (i) o += (size_t)snprintf(out + o, cap - o, "\n\xC2\xB7 %s", nm);
    }
    /* guest: no settings line (m->set holds this copy's own host settings, not the host's) */
    if (o < cap && m->state != MPM_GUEST_LOBBY) {
        char s[200]; snprintf(s, sizeof s, TR("Pause: %s  \xC2\xB7  Speed-up: %s  \xC2\xB7  Gear: %s"), mpm_pause_text(m->set.pause_mode), on_off(m->set.speedup), mpm_gear_text(m->set.gear));
        o += (size_t)snprintf(out + o, cap - o, "\n<size=87%%><color=#A69085>%s</color></size>", s);
    }
    if (o < cap && m->direct[0] && m->state != MPM_GUEST_LOBBY) {
        const char *port = strrchr(m->direct, ':');
        o += (size_t)snprintf(out + o, cap - o, "\n");
        if (o < cap) o += (size_t)snprintf(out + o, cap - o, TR("Local: %s"), m->direct);
        if (o < cap && m->internet[0]) { o += (size_t)snprintf(out + o, cap - o, "\n"); if (o < cap) o += (size_t)snprintf(out + o, cap - o, TR("Internet: %s"), m->internet); }
        if (o < cap) o += (size_t)snprintf(out + o, cap - o, "\n");
        if (o < cap) snprintf(out + o, cap - o, TR("Friends outside your home: open port %s on your router, or use Steam invite"), port ? port + 1 : "47777");
    }
}

/* ---------------------------------------------------------------- load list filter */
int mpm_list_mode(const mpm_model *m) {
    if (m->state == MPM_PICK_SAVE) return m->set.fe ? MPM_LIST_ALL : MPM_LIST_MP;   /* hosting: Your save (an expedition copies any world) */
    return MPM_LIST_SOLO;
}
int mpm_list_show(int mode, int show_other, int tagged, int current) {
    if (mode == MPM_LIST_ALL || show_other || current) return 1;
    return mode == MPM_LIST_MP ? tagged != 0 : tagged == 0;
}
const char *mpm_list_toggle_label(int mode) {
    return mode == MPM_LIST_MP ? TR("Show single-player worlds") : mode == MPM_LIST_SOLO ? TR("Show multiplayer worlds") : "";
}

/* ---------------------------------------------------------------- Multiplayer world tag (mp_tags.txt) */
static void tag_clean(const char *in, char out[32]) {                  /* one name: no tab / newline / control byte */
    size_t o = 0;
    for (; *in && o < 31; in++) if ((unsigned char)*in >= 0x20) out[o++] = *in;
    out[o] = 0;
}
/* the fields of one line: guid then names (tab separated); returns the number of names, -1 for an empty / bad line */
static int tag_split(char *line, char **guid, char names[][32], int max) {
    line[strcspn(line, "\r\n")] = 0;
    char *f = strtok(line, "\t"); if (!f || !*f) return -1;
    *guid = f; int n = 0;
    while ((f = strtok(NULL, "\t"))) { if (!*f || n >= max) continue; tag_clean(f, names[n]); if (names[n][0]) n++; }
    return n;
}
int mpm_tag_get(const char *path, const char *guid, char names[][32], int max) {
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char line[1024], tmp[MPM_TAG_NAMES][32]; int got = -1;
    while (fgets(line, sizeof line, f)) {
        char *g; int n = tag_split(line, &g, tmp, MPM_TAG_NAMES);
        if (n < 0 || strcmp(g, guid)) continue;
        got = n < max ? n : max;
        for (int i = 0; i < got; i++) memcpy(names[i], tmp[i], 32);
    }
    fclose(f); return got;
}
/* rewrite the file without guid's line; keep != NULL: then append guid with these names. 0 written, -1 failed */
static int tag_write(const char *path, const char *guid, char keep[][32], int nkeep, int *had) {
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return -1; }
    char line[1024], copy[1024]; *had = 0;
    while (in && fgets(line, sizeof line, in)) {
        memcpy(copy, line, sizeof line); char *g, nm[MPM_TAG_NAMES][32];
        if (tag_split(copy, &g, nm, MPM_TAG_NAMES) < 0) continue;
        if (!strcmp(g, guid)) { *had = 1; continue; }
        fputs(line, out); if (!strchr(line, '\n')) fputc('\n', out);
    }
    if (keep) { fputs(guid, out); for (int i = 0; i < nkeep; i++) fprintf(out, "\t%s", keep[i]); fputc('\n', out); }
    if (in) fclose(in);
    if (fclose(out)) return -1;
    return rename(tmp, path) ? -1 : 0;
}
int mpm_tag_add(const char *path, const char *guid, const char *const *names, int n) {
    if (!guid || !guid[0] || strpbrk(guid, "\t\r\n ")) return 0;
    char cur[MPM_TAG_NAMES][32]; int had = mpm_tag_get(path, guid, cur, MPM_TAG_NAMES), k = had < 0 ? 0 : had, changed = had < 0;
    for (int i = 0; i < n && k < MPM_TAG_NAMES; i++) {
        char c[32]; tag_clean(names[i] ? names[i] : "", c);
        if (!c[0]) continue;
        int dup = 0; for (int j = 0; j < k; j++) if (!strcmp(cur[j], c)) dup = 1;
        if (!dup) { memcpy(cur[k++], c, 32); changed = 1; }
    }
    if (!changed) return 0;
    int was; return tag_write(path, guid, cur, k, &was) == 0;
}
int mpm_tag_remove(const char *path, const char *guid) {
    char cur[1][32]; if (mpm_tag_get(path, guid, cur, 1) < 0) return 0;
    int had; return tag_write(path, guid, NULL, 0, &had) == 0 && had;
}
void mpm_tag_names(char names[][32], int n, int shown, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    int k = n < shown ? n : shown, last_and = n == k && k >= 2;               /* "A, B and C" / "A, B +2" */
    char head[MPM_TAG_NAMES * 40]; size_t o = 0; head[0] = 0;
    for (int i = 0; i < (last_and ? k - 1 : k) && o < sizeof head; i++)
        o += (size_t)snprintf(head + o, sizeof head - o, "%s%s", i ? TR(", ") : "", names[i]);
    if (last_and) snprintf(out, cap, TR("%s and %s"), head, names[k - 1]);
    else snprintf(out, cap, "%s", head);
    size_t l = strlen(out);
    if (n > k && l < cap) snprintf(out + l, cap - l, " +%d", n - k);
}
