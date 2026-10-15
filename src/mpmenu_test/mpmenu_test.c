// mpmenu_test.c - tests for the Multiplayer menu state machine (mpmenu_model.c). make -C mpmenu_test test
#define MPM_NO_GAME
#include "../mpmenu.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define B(x) (1ull << (x))

static mpm_obs title(void) { mpm_obs o = {0}; o.scene = 1; o.title_visible = 1; return o; }

int main(void) {
    mpm_model m; mpm_init(&m); char l[160];
    mpm_obs o = title();
    /* title: only the Multiplayer button */
    CHECK(mpm_step(&m, &o, MPB_NONE) == 0);
    CHECK(mpm_visible(&m, &o) == B(MPB_MULTIPLAYER));
    /* Multiplayer -> Host -> settings */
    mpm_step(&m, &o, MPB_MULTIPLAYER); CHECK(m.state == MPM_TITLE_MENU);
    CHECK(mpm_visible(&m, &o) == (B(MPB_HOST) | B(MPB_JOIN_ADDR) | B(MPB_JOIN_FRIEND) | B(MPB_BACK)));
    /* LAN app (no Steam): "Join a friend" is greyed and does nothing; Steam: the friends page, Escape = Back */
    mpm_label(&m, MPB_JOIN_FRIEND, l, sizeof l); CHECK(strstr(l, "Needs the Steam version") != NULL);
    CHECK(mpm_step(&m, &o, MPB_JOIN_FRIEND) == 0 && m.state == MPM_TITLE_MENU);
    o.steam = 1; mpm_step(&m, &o, MPB_NONE); mpm_label(&m, MPB_JOIN_FRIEND, l, sizeof l); CHECK(!strcmp(l, "Join a friend on Steam"));
    CHECK((mpm_step(&m, &o, MPB_JOIN_FRIEND) & MPA_FRIENDS) && m.state == MPM_TITLE_FRIENDS);
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "No Steam friend is hosting") != NULL);
    m.nfriends = 2; strcpy(m.friends[0], "The owner"); strcpy(m.friends[1], "Ana");
    CHECK((mpm_visible(&m, &o) & B(MPB_FRIEND1)) && !(mpm_visible(&m, &o) & B(MPB_FRIEND2)));
    mpm_label(&m, MPB_FRIEND1, l, sizeof l); CHECK(!strcmp(l, "Join Ana's world"));
    CHECK((mpm_step(&m, &o, MPB_FRIEND1) & MPA_JOIN_FRIEND) && m.friend_pick == 1);
    o.escape = 1; mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(m.state == MPM_TITLE_MENU);
    mpm_step(&m, &o, MPB_JOIN_ADDR); o.escape = 1; mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(m.state == MPM_TITLE_MENU);   /* join page: Escape = Back */
    o.steam = 0;
    mpm_step(&m, &o, MPB_JOIN_ADDR); CHECK(m.state == MPM_TITLE_JOIN && mpm_visible(&m, &o) == (B(MPB_HEADER) | B(MPB_LABEL_ADDR) | B(MPB_LABEL_NAME) | B(MPB_DIVIDER) | B(MPB_CONNECT) | B(MPB_BACK)));
    mpm_label(&m, MPB_HEADER, l, sizeof l); CHECK(!strcmp(l, "Join a multiplayer world"));
    CHECK(mpm_step(&m, &o, MPB_CONNECT) & MPA_JOIN_ADDR);
    /* connecting (ux #1): the game side sets `connecting`; Cancel replaces Connect, INFO says so; Cancel stops it */
    strcpy(m.connecting, "10.255.255.1:47777");
    CHECK(mpm_visible(&m, &o) == (B(MPB_HEADER) | B(MPB_LABEL_ADDR) | B(MPB_LABEL_NAME) | B(MPB_DIVIDER) | B(MPB_INFO) | B(MPB_CANCEL_JOIN) | B(MPB_BACK)));
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "Connecting to 10.255.255.1:47777...") != NULL);
    CHECK(mpm_step(&m, &o, MPB_CONNECT) == 0);                                   /* no second connect while one runs */
    CHECK((mpm_step(&m, &o, MPB_CANCEL_JOIN) & MPA_JOIN_CANCEL) && !m.connecting[0] && m.state == MPM_TITLE_JOIN);
    /* timeout message, then an edit of the address clears it (ux #16) */
    strcpy(m.msg, "No answer from 10.255.255.1:47777");
    CHECK(mpm_visible(&m, &o) & B(MPB_INFO));
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "No answer") && strstr(l, "<color="));
    o.edited = 1; mpm_step(&m, &o, MPB_NONE); o.edited = 0; CHECK(!m.msg[0] && !(mpm_visible(&m, &o) & B(MPB_INFO)));
    /* Back while connecting cancels it */
    strcpy(m.connecting, "1.2.3.4:47777");
    CHECK((mpm_step(&m, &o, MPB_BACK) & MPA_JOIN_CANCEL) && !m.connecting[0] && m.state == MPM_TITLE_MENU);
    mpm_step(&m, &o, MPB_JOIN_ADDR);
    mpm_step(&m, &o, MPB_BACK); CHECK(m.state == MPM_TITLE_MENU);
    mpm_step(&m, &o, MPB_BACK); CHECK(m.state == MPM_IDLE);
    mpm_step(&m, &o, MPB_MULTIPLAYER); mpm_step(&m, &o, MPB_HOST); CHECK(m.state == MPM_TITLE_HOST && (mpm_visible(&m, &o) & B(MPB_LABEL_NAME)) && !(mpm_visible(&m, &o) & B(MPB_MULTIPLAYER)));
    mpm_label(&m, MPB_LABEL_NAME, l, sizeof l); CHECK(!strcmp(l, "Your name"));
    /* layout v2 §2a: header, settings row + summary; the selectors replace the summary when open */
    { uint64_t v = mpm_visible(&m, &o);
      CHECK((v & B(MPB_HEADER)) && (v & B(MPB_SETTINGS)) && (v & B(MPB_DIVIDER)) && !(v & B(MPB_SUMMARY)) && !(v & B(MPB_SEL_PAUSE)) && !(v & B(MPB_PAUSE_MODE)));
      mpm_label(&m, MPB_HEADER, l, sizeof l); CHECK(!strcmp(l, "Host your save"));
      mpm_label(&m, MPB_SETTINGS, l, sizeof l); CHECK(!strcmp(l, "Rules"));
      mpm_settings_summary(&m.set, l, sizeof l); CHECK(!strcmp(l, "Pause: anyone \xC2\xB7 Speed-up: on \xC2\xB7 Gear: own units \xC2\xB7 Units: 3 \xC2\xB7 Population: shared \xC2\xB7 Events: owner \xC2\xB7 Admin: off \xC2\xB7 Shared fog: off \xC2\xB7 Spectators: on"));
      { char rv[64]; mpm_rule_value(&m.set, 0, rv, sizeof rv); CHECK(!strcmp(rv, "Anyone can pause") && !strcmp(mpm_rule_label(3), "Units per player") && !strcmp(mpm_rule_label(5), "Event choices") && !strcmp(mpm_rule_label(MPM_RULE_POP), "Population")); }
      mpm_step(&m, &o, MPB_SETTINGS); mpm_step(&m, &o, MPB_POP_PREV); CHECK(m.set.population == 3);
      { char rv[64]; mpm_rule_value(&m.set, MPM_RULE_POP, rv, sizeof rv); CHECK(!strcmp(rv, "Multiplied") && !strcmp(mpm_population_desc(3), "Clan limit x players connected, shared by all")); }
      mpm_step(&m, &o, MPB_POP_NEXT); CHECK(m.set.population == 0);
      { char rv[64]; CHECK(m.set.fog == 0); mpm_step(&m, &o, MPB_FOG_NEXT); mpm_rule_value(&m.set, 7, rv, sizeof rv);   /* Shared fog: default Off */
        CHECK(m.set.fog == 1 && !strcmp(rv, "On") && !strcmp(mpm_rule_label(7), "Shared fog") && mpm_rule_tip(7)[0]); mpm_step(&m, &o, MPB_FOG_PREV); CHECK(m.set.fog == 0); }
      mpm_step(&m, &o, MPB_RULES_DONE);
      mpm_step(&m, &o, MPB_SETTINGS); mpm_step(&m, &o, MPB_EVENTS_NEXT); CHECK(m.set.events == 1);
      { char rv[64]; mpm_rule_value(&m.set, 5, rv, sizeof rv); CHECK(!strcmp(rv, "Host only")); }
      mpm_step(&m, &o, MPB_EVENTS_PREV); mpm_step(&m, &o, MPB_EVENTS_PREV); CHECK(m.set.events == 3);
      mpm_step(&m, &o, MPB_EVENTS_NEXT); CHECK(m.set.events == 0);
      o.escape = 1; mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(!m.settings_open && m.state == MPM_TITLE_HOST);   /* Escape closes the Rules panel first */
      mpm_step(&m, &o, MPB_SETTINGS); v = mpm_visible(&m, &o);
      CHECK(m.settings_open);
      mpm_step(&m, &o, MPB_ADMIN_NEXT); CHECK(m.set.admin == 1); mpm_step(&m, &o, MPB_ADMIN_PREV); CHECK(m.set.admin == 0);
      mpm_step(&m, &o, MPB_PAUSE_PREV); CHECK(m.set.pause_mode == MPM_PAUSE_NONE && !strcmp(mpm_sel_value(&m, MPB_SEL_PAUSE), "off"));
      mpm_step(&m, &o, MPB_PAUSE_NEXT); CHECK(m.set.pause_mode == MPM_PAUSE_ANYONE);
      mpm_step(&m, &o, MPB_SPEED_PREV); CHECK(m.set.speedup == 0); mpm_step(&m, &o, MPB_SPEED_NEXT); CHECK(m.set.speedup == 1);
      mpm_step(&m, &o, MPB_GEAR_PREV); CHECK(m.set.gear == MPM_GEAR_OWN_STORAGE && !strcmp(mpm_sel_value(&m, MPB_SEL_GEAR), "own storage"));
      mpm_step(&m, &o, MPB_GEAR_NEXT); CHECK(m.set.gear == MPM_GEAR_SHARED_OWN);
      mpm_step(&m, &o, MPB_UNITS_NEXT); CHECK(m.set.units == 4); mpm_step(&m, &o, MPB_UNITS_NEXT); CHECK(m.set.units == 6 && !strcmp(mpm_sel_value(&m, MPB_SEL_UNITS), "6"));
      mpm_step(&m, &o, MPB_UNITS_NEXT); CHECK(m.set.units == 1); mpm_step(&m, &o, MPB_UNITS_PREV); CHECK(m.set.units == 6);
      for (int k = 0; k < 3; k++) mpm_step(&m, &o, MPB_UNITS_PREV);
      CHECK(m.set.units == 2); mpm_step(&m, &o, MPB_UNITS_NEXT); CHECK(m.set.units == 3);
      mpm_step(&m, &o, MPB_RULES_DONE); CHECK(!m.settings_open); }
    mpm_label(&m, MPB_PAUSE_MODE, l, sizeof l); CHECK(!strcmp(l, "Pause: anyone"));
    mpm_label(&m, MPB_SPEEDUP, l, sizeof l); CHECK(!strcmp(l, "Speed-up: on"));
    mpm_label(&m, MPB_GEAR, l, sizeof l); CHECK(!strcmp(l, "Gear: own units"));
    mpm_step(&m, &o, MPB_PAUSE_MODE); CHECK(m.set.pause_mode == MPM_PAUSE_ALL_VOTE);
    mpm_step(&m, &o, MPB_PAUSE_MODE); CHECK(m.set.pause_mode == MPM_PAUSE_NONE);
    mpm_label(&m, MPB_PAUSE_MODE, l, sizeof l); CHECK(!strcmp(l, "Pause: off"));
    mpm_step(&m, &o, MPB_SPEEDUP); CHECK(m.set.speedup == 0);
    mpm_step(&m, &o, MPB_GEAR); mpm_step(&m, &o, MPB_GEAR); CHECK(m.set.gear == MPM_GEAR_OWN_STORAGE);
    mpm_step(&m, &o, MPB_GEAR); CHECK(m.set.gear == MPM_GEAR_SHARED_OWN);
    /* load list opened then closed without loading: back to settings */
    CHECK(mpm_step(&m, &o, MPB_LOAD_SAVE) & MPA_OPEN_LOAD_LIST); CHECK(m.state == MPM_PICK_SAVE);
    CHECK(mpm_step(&m, &o, MPB_NONE) == 0 && m.state == MPM_PICK_SAVE);        /* list not seen yet: wait */
    o.picker_open = 1; o.title_visible = 0; mpm_step(&m, &o, MPB_NONE);
    o.picker_open = 0; o.title_visible = 1; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_PICK_SAVE);   /* a load may start */
    for (int i = 0; i < 95; i++) mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_TITLE_HOST);
    /* list closes and the load starts a few frames later: no cancel */
    mpm_step(&m, &o, MPB_LOAD_SAVE); o.picker_open = 1; mpm_step(&m, &o, MPB_NONE); o.picker_open = 0;
    for (int i = 0; i < 20; i++) mpm_step(&m, &o, MPB_NONE);
    o.loading = 1; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_HOST_LOADING); o.loading = 0;
    m.state = MPM_TITLE_HOST;
    /* new campaign -> scene changes -> loading -> ready -> lobby with host start */
    CHECK(mpm_step(&m, &o, MPB_NEW_CAMPAIGN) & MPA_OPEN_NEW);
    o.title_visible = 0; mpm_step(&m, &o, MPB_NONE);
    o.scene = 0; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_HOST_LOADING);
    o.scene = 2; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_HOST_LOADING);
    o.campaign_ready = 1;
    int a = mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_LOBBY && (a & MPA_HOST_START) && (a & MPA_LOBBY_SHOW));
    o.role = 1;
    CHECK(mpm_visible(&m, &o) == (B(MPB_INVITE) | B(MPB_COPY_ADDR) | B(MPB_START) | B(MPB_LEAVE)));
    CHECK(mpm_step(&m, &o, MPB_COPY_ADDR) == MPA_COPY_ADDR);
    CHECK((mpm_step(&m, &o, MPB_LOBBY_SETTINGS) & MPA_REFRESH) && m.lobby_settings_open);
    CHECK((mpm_step(&m, &o, MPB_ADMIN) & MPA_ADMIN) && m.state == MPM_LOBBY);
    /* lobby (host, settings open): the arrows change the rules and ask to send them */
    CHECK((mpm_step(&m, &o, MPB_UNITS_PREV) & MPA_RULES) && m.set.units == 2);
    { int pm = m.set.pause_mode; CHECK((mpm_step(&m, &o, MPB_PAUSE_NEXT) & MPA_RULES) && m.set.pause_mode == (pm + 1) % 3); }
    mpm_step(&m, &o, MPB_PAUSE_PREV); mpm_step(&m, &o, MPB_UNITS_NEXT);
    CHECK(mpm_step(&m, &o, MPB_ADMIN_NEXT) & MPA_ADMIN);
    mpm_step(&m, &o, MPB_RULES_DONE); CHECK(!m.lobby_settings_open && !(mpm_step(&m, &o, MPB_UNITS_NEXT) & MPA_RULES));   /* closed: no change */
    mpm_step(&m, &o, MPB_LOBBY_SETTINGS); o.escape = 1; mpm_step(&m, &o, MPB_NONE); o.escape = 0;
    CHECK(!m.lobby_settings_open && m.state == MPM_LOBBY);                    /* lan_v9: Escape closes the lobby Rules, the lobby stays */
    o.escape = 1; mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(m.state == MPM_LOBBY);   /* Escape with the Rules closed: nothing */
    CHECK(mpm_rule_tip(0)[0] && mpm_rule_tip(MPM_RULE_ROWS - 1)[0] && !mpm_rule_tip(MPM_RULE_ROWS)[0]);
    mpm_label(&m, MPB_INVITE, l, sizeof l); CHECK(!strcmp(l, "Steam invite"));
    CHECK(mpm_step(&m, &o, MPB_INVITE) == 0 && m.state == MPM_LOBBY);           /* no Steam: greyed, nothing */
    o.steam = 1; CHECK(mpm_step(&m, &o, MPB_INVITE) == MPA_INVITE && m.state == MPM_LOBBY); o.steam = 0;
    /* lobby Leave (host): the same confirm box */
    { mpm_model q = m; int aa = mpm_step(&q, &o, MPB_LEAVE); CHECK(q.state == MPM_LOBBY && (aa & MPA_CONFIRM_LEAVE) && !(aa & MPA_LEAVE));
      aa = mpm_step(&q, &o, MPB_LEAVE_CONFIRM); CHECK(q.state == MPM_IDLE && (aa & MPA_LEAVE) && (aa & MPA_LOBBY_HIDE)); }
    a = mpm_step(&m, &o, MPB_START); CHECK(m.state == MPM_PLAYING && (a & MPA_LOBBY_HIDE) && (a & MPA_START_GAME));
    /* Esc menu: host sees Invite + Leave, guest only Leave */
    CHECK(mpm_visible(&m, &o) == 0);
    o.esc_open = 1; CHECK(mpm_visible(&m, &o) == (B(MPB_ESC_INVITE) | B(MPB_LEAVE)));
    CHECK(mpm_step(&m, &o, MPB_ESC_INVITE) == MPA_INVITE);
    /* host Leave (lan_v10, ux_flow F7): a confirm box first; Cancel / Escape keep the session, End leaves */
    a = mpm_step(&m, &o, MPB_LEAVE); CHECK(m.state == MPM_PLAYING && (a & MPA_CONFIRM_LEAVE) && !(a & MPA_LEAVE) && m.confirm_leave);
    a = mpm_step(&m, &o, MPB_LEAVE_CANCEL); CHECK(m.state == MPM_PLAYING && !(a & MPA_LEAVE) && !m.confirm_leave);
    mpm_step(&m, &o, MPB_LEAVE); o.escape = 1; a = mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(m.state == MPM_PLAYING && !m.confirm_leave && !(a & MPA_LEAVE));
    a = mpm_step(&m, &o, MPB_LEAVE_CONFIRM); CHECK(m.state == MPM_PLAYING && !(a & MPA_LEAVE));   /* no box up: nothing */
    mpm_step(&m, &o, MPB_LEAVE); a = mpm_step(&m, &o, MPB_LEAVE_CONFIRM); CHECK(m.state == MPM_IDLE && (a & MPA_LEAVE) && !m.confirm_leave);
    /* quit to the title while playing: the session is ended after 90 title frames, then the Multiplayer button shows */
    { mpm_model q; mpm_init(&q); q.state = MPM_PLAYING; mpm_obs t2 = title(); t2.role = 1; int aa = 0;
      for (int i = 0; i < 95; i++) aa |= mpm_step(&q, &t2, MPB_NONE);
      CHECK(q.state == MPM_IDLE && (aa & MPA_LEAVE)); t2.role = 0; CHECK(mpm_visible(&q, &t2) == B(MPB_MULTIPLAYER)); }
    /* session lost while playing */
    mpm_init(&m); o = title(); o.scene = 2; o.campaign_ready = 1; o.role = 1;
    mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_PLAYING);                  /* mp.json session */
    /* host left (ux #9): a notice box text, nothing in the Multiplayer button */
    strcpy(m.host, "The owner");
    o.role = 0; strcpy(o.msg, "The host left the game"); mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_IDLE && !m.msg[0] && !strcmp(m.notice, "<size=150%>The owner left the game</size>\nThe world is saved on the owner's computer. To play again, the owner hosts it and invites you."));
    mpm_label(&m, MPB_MULTIPLAYER, l, sizeof l); CHECK(!strcmp(l, "Multiplayer")); o.msg[0] = 0;
    /* lan_v10: a join started from the player's own world and refused (F9 / F17): the reason box, shown in the world */
    { mpm_model q; mpm_init(&q); mpm_obs t2 = title(); t2.scene = 2; t2.campaign_ready = 1; t2.role = 2; strcpy(t2.host_name, "The owner");
      mpm_step(&q, &t2, MPB_NONE); mpm_step(&q, &t2, MPB_NONE); CHECK(q.state == MPM_PLAYING);
      t2.role = 0; strcpy(t2.msg, "Could not join: the host is playing another world now (Old World)"); mpm_step(&q, &t2, MPB_NONE);
      CHECK(q.state == MPM_FAILED && strstr(q.notice, "Could not join the owner's game</size>\nThe host is playing another world now (Old World).")); }
    /* host start failed in the lobby */
    mpm_init(&m); m.state = MPM_LOBBY; o.failed = 1;
    a = mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_FAILED && (a & MPA_LOBBY_HIDE) && strstr(m.notice, "Multiplayer could not start</size>") && !m.msg[0]);
    /* guest: invite accepted -> joining -> loading -> playing; labels */
    mpm_init(&m); o = title(); o.role = 2;
    mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_JOINING);
    CHECK(mpm_visible(&m, &o) == (B(MPB_INFO) | B(MPB_CANCEL_JOIN)));
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "Joining the host's game...") != NULL);
    strcpy(o.host_name, "The owner"); mpm_step(&m, &o, MPB_NONE); mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "Joining the owner's game...") != NULL);
    o.guest_loading = 1; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_LOADING);
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "loading") != NULL);
    o.scene = 2; o.campaign_ready = 1; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_LOADING);
    o.guest_loading = 0; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_PLAYING);
    o.esc_open = 1; CHECK(mpm_visible(&m, &o) == B(MPB_LEAVE));
    CHECK(mpm_step(&m, &o, MPB_ESC_INVITE) == 0);                               /* guest cannot invite */
    /* host still in the lobby (ux #14): the guest gets the waiting panel; Start on the host ends it */
    o.esc_open = 0; o.host_lobby = 1; a = mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_GUEST_LOBBY && (a & MPA_LOBBY_SHOW) && mpm_visible(&m, &o) == B(MPB_LEAVE));
    { const char *gn[] = { "The owner", "Ben" }; char gt[512]; mpm_lobby_text(&m, gn, 2, gt, sizeof gt);
      char tl[64]; mpm_lobby_title(&m, tl, sizeof tl);
      CHECK(!strcmp(tl, "In the owner's lobby") && strstr(gt, "The game starts when the owner presses Start.") && strstr(gt, "\xC2\xB7 the owner (host)\n\xC2\xB7 Ben") && !strstr(gt, "Pause:") && !strstr(gt, "<b>")); }
    o.host_lobby = 0; a = mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_PLAYING && (a & MPA_LOBBY_HIDE));
    o.host_lobby = 1; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_LOBBY);
    { mpm_model m2 = m; mpm_obs o2 = o; o2.role = 0; strcpy(o2.msg, "Connection to the host lost"); mpm_step(&m2, &o2, MPB_NONE);   /* own link lost */
      CHECK(m2.state == MPM_IDLE && strstr(m2.notice, "Connection to the owner lost</size>") && strstr(m2.notice, "The owner is waiting for you: rejoin from Recent worlds.")); }
    o.role = 0; strcpy(o.msg, "The host left the game"); a = mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_IDLE && (a & MPA_LOBBY_HIDE) && strstr(m.notice, "The owner left the game</size>")); o.msg[0] = 0; o.role = 2; o.host_lobby = 0;
    /* guest cancel and guest failure */
    mpm_init(&m); o = title(); o.role = 2; mpm_step(&m, &o, MPB_NONE);
    a = mpm_step(&m, &o, MPB_CANCEL_JOIN); CHECK(m.state == MPM_IDLE && (a & MPA_LEAVE));
    mpm_init(&m); o = title(); o.role = 2; mpm_step(&m, &o, MPB_NONE);         /* Escape = Cancel while joining (F16) */
    o.escape = 1; a = mpm_step(&m, &o, MPB_NONE); o.escape = 0; CHECK(m.state == MPM_IDLE && (a & MPA_LEAVE)); o.role = 0;
    /* Multiplayer page: a Recent worlds reconnect shows Cancel; Escape and Cancel stop it and stay on the page */
    mpm_init(&m); o = title(); mpm_step(&m, &o, MPB_MULTIPLAYER); strcpy(m.connecting, "1.2.3.4:47777");
    CHECK(mpm_visible(&m, &o) & B(MPB_CANCEL_JOIN));
    a = mpm_step(&m, &o, MPB_CANCEL_JOIN); CHECK(m.state == MPM_TITLE_MENU && (a & MPA_JOIN_CANCEL) && !m.connecting[0] && !(mpm_visible(&m, &o) & B(MPB_CANCEL_JOIN)));
    strcpy(m.connecting, "1.2.3.4:47777"); o.escape = 1; a = mpm_step(&m, &o, MPB_NONE); o.escape = 0;
    CHECK(m.state == MPM_TITLE_MENU && (a & MPA_JOIN_CANCEL) && !m.connecting[0]);
    mpm_init(&m); o = title(); o.role = 2; mpm_step(&m, &o, MPB_NONE);
    strcpy(o.host_name, "The owner"); mpm_step(&m, &o, MPB_NONE); o.host_name[0] = 0;
    o.role = 0; o.failed = 1; strcpy(o.msg, "Could not join: your game version is different from the host's: update both games"); mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_FAILED);
    /* lan_v10 (ux_flow F8): the reason in the notice box, the Multiplayer button plain */
    CHECK(!strcmp(m.notice, "<size=150%>Could not join the owner's game</size>\nYour game version is different from the host's: update both games."));
    mpm_label(&m, MPB_MULTIPLAYER, l, sizeof l); CHECK(!strcmp(l, "Multiplayer")); o.msg[0] = 0;
    o.failed = 0; mpm_step(&m, &o, MPB_MULTIPLAYER); CHECK(m.state == MPM_TITLE_MENU && !m.msg[0]);
    /* lobby text */
    const char *names[] = { "The owner", "Ben" }; char t[512];
    mpm_init(&m); mpm_lobby_text(&m, names, 2, t, sizeof t);
    CHECK(!strncmp(t, "Share your address, then press Start when everyone is in.\n", 58));
    { char tl[64]; mpm_lobby_title(&m, tl, sizeof tl); CHECK(!strcmp(tl, "Multiplayer lobby")); }
    CHECK(strstr(t, "\xC2\xB7 the owner (host)\n\xC2\xB7 Ben\n") && strstr(t, "Pause: anyone  \xC2\xB7  Speed-up: on  \xC2\xB7  Gear: own units") && strstr(t, "<color=#A69085>") && !strstr(t, "<b>"));
    mpm_lobby_text(&m, names, 0, t, sizeof t); CHECK(strstr(t, "\xC2\xB7 You (host)"));
    CHECK(!strstr(t, "Local:"));
    strcpy(m.direct, "192.168.1.20:47777"); mpm_lobby_text(&m, names, 2, t, sizeof t);
    CHECK(strstr(t, "\nLocal: 192.168.1.20:47777\nFriends outside your home: open port 47777 on your router, or use Steam invite") && !strstr(t, "needs router"));
    strcpy(m.internet, "81.2.3.4:47777"); mpm_lobby_text(&m, names, 2, t, sizeof t); CHECK(strstr(t, "\nInternet: 81.2.3.4:47777"));
    /* guest joins by address from the join page */
    mpm_init(&m); o = title(); mpm_step(&m, &o, MPB_MULTIPLAYER); mpm_step(&m, &o, MPB_JOIN_ADDR);
    o.role = 2; mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_JOINING);
    /* spectators (owner 2026-10-14): Join page Watch, Recent worlds Watch, the Spectators rule, the end notice */
    mpm_init(&m); o = title(); CHECK(m.set.spectators == 1);
    mpm_step(&m, &o, MPB_MULTIPLAYER); mpm_step(&m, &o, MPB_JOIN_ADDR);
    CHECK(mpm_is_visible(&m, &o, MPB_WATCH) && mpm_is_visible(&m, &o, MPB_CONNECT) && !mpm_is_visible(&m, &o, MPB_HISTW0));
    mpm_label(&m, MPB_WATCH, l, sizeof l); CHECK(!strcmp(l, "Watch"));
    CHECK((mpm_step(&m, &o, MPB_WATCH) & MPA_JOIN_ADDR) && m.watch == 1);
    strcpy(m.connecting, "1.2.3.4:47777"); CHECK(!mpm_is_visible(&m, &o, MPB_WATCH) && mpm_step(&m, &o, MPB_WATCH) == 0);
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "Connecting to 1.2.3.4:47777 to watch...") != NULL);
    mpm_step(&m, &o, MPB_CANCEL_JOIN);
    CHECK((mpm_step(&m, &o, MPB_CONNECT) & MPA_JOIN_ADDR) && m.watch == 0);
    o.role = 2; o.spectator = 1; strcpy(o.host_name, "The owner"); mpm_step(&m, &o, MPB_NONE); CHECK(m.state == MPM_GUEST_JOINING);
    mpm_label(&m, MPB_INFO, l, sizeof l); CHECK(strstr(l, "Joining the owner's game as a spectator...") != NULL);
    o.role = 0; strcpy(o.msg, "The host turned spectators off"); o.failed = 1; mpm_step(&m, &o, MPB_NONE);
    CHECK(m.state == MPM_IDLE && strstr(m.notice, "The owner turned spectators off") && strstr(m.notice, "You can no longer watch this world."));
    { mpm_model q; mpm_init(&q); mpm_obs po = title(); strcpy(q.host, "Ana"); q.state = MPM_PLAYING; po.scene = 2; po.role = 0; strcpy(po.msg, "The host turned spectators off");
      mpm_step(&q, &po, MPB_NONE); CHECK(q.state == MPM_IDLE && strstr(q.notice, "Ana turned spectators off")); }
    mpm_init(&m); o = title(); mpm_step(&m, &o, MPB_MULTIPLAYER); m.nhist = 8; m.hist_page = 1;
    CHECK((mpm_step(&m, &o, MPB_HISTW1) & MPA_HIST_JOIN) && m.hist_pick == 7 && m.watch == 1);
    CHECK(mpm_step(&m, &o, MPB_HISTW2) == 0);                                  /* past the list */
    CHECK((mpm_step(&m, &o, MPB_HIST0) & MPA_HIST_JOIN) && m.hist_pick == 6 && m.watch == 0);
    { mpm_settings ss = m.set; char rv[64];
      CHECK(!strcmp(mpm_rule_label(8), "Spectators") && !strcmp(mpm_rule_tip_title(8), "Spectators") && strstr(mpm_rule_tip(8), "at most 8"));
      mpm_rule_value(&ss, 8, rv, sizeof rv); CHECK(!strcmp(rv, "Allowed"));
      ss.watching = 2; mpm_rule_value(&ss, 8, rv, sizeof rv); CHECK(!strcmp(rv, "Allowed \xC2\xB7 2 watching"));
      ss.spectators = 0; mpm_rule_value(&ss, 8, rv, sizeof rv); CHECK(!strcmp(rv, "Not allowed")); }
    mpm_init(&m); o = title(); mpm_step(&m, &o, MPB_MULTIPLAYER); mpm_step(&m, &o, MPB_HOST); mpm_step(&m, &o, MPB_SETTINGS);
    mpm_step(&m, &o, MPB_SPECT_NEXT); CHECK(m.set.spectators == 0); mpm_step(&m, &o, MPB_SPECT_PREV); CHECK(m.set.spectators == 1);
    m.state = MPM_LOBBY; o.scene = 2; o.campaign_ready = 1; o.role = 1; m.lobby_settings_open = 1;
    a = mpm_step(&m, &o, MPB_SPECT_NEXT); CHECK((a & MPA_SPECTATORS) && !(a & MPA_RULES) && m.set.spectators == 1);   /* the game side decides (confirm box) */
    m.lobby_settings_open = 0; CHECK(!(mpm_step(&m, &o, MPB_SPECT_NEXT) & MPA_SPECTATORS));
    {   /* host choices under "Host a game" (uifix11): Your save / New save / Foreign Expedition, each its own Host page */
        mpm_model h; mpm_init(&h); mpm_obs t = title(); char s[200];
        mpm_step(&h, &t, MPB_MULTIPLAYER);
        /* the third choice only once a mod registered it (MASTER_PLAN 2.2 B register_host_choice) */
        CHECK(mpm_is_visible(&h, &t, MPB_HOST_SAVE) && mpm_is_visible(&h, &t, MPB_HOST_NEW) && !mpm_is_visible(&h, &t, MPB_HOST_FE));
        { mpm_model n = h; mpm_step(&n, &t, MPB_HOST_FE); CHECK(n.state == MPM_TITLE_MENU && !n.set.fe); }   /* not registered: no page */
        h.host_choice = 1; snprintf(h.host_choice_label, sizeof h.host_choice_label, "Expedition X");
        mpm_label(&h, MPB_HOST_FE, s, sizeof s); CHECK(!strcmp(s, "Expedition X"));
        h.host_choice_label[0] = 0;
        CHECK(mpm_is_visible(&h, &t, MPB_HOST_SAVE) && mpm_is_visible(&h, &t, MPB_HOST_NEW) && mpm_is_visible(&h, &t, MPB_HOST_FE));
        mpm_label(&h, MPB_HOST_SAVE, s, sizeof s); CHECK(!strcmp(s, "Your save"));
        mpm_label(&h, MPB_HOST_NEW, s, sizeof s); CHECK(!strcmp(s, "New save"));
        mpm_label(&h, MPB_HOST_FE, s, sizeof s); CHECK(!strcmp(s, "Foreign Expedition"));
        mpm_model k = h; mpm_step(&k, &t, MPB_HOST_SAVE); uint64_t v = mpm_visible(&k, &t);
        CHECK(k.state == MPM_TITLE_HOST && !k.set.fe && (v & B(MPB_LOAD_SAVE)) && !(v & B(MPB_NEW_CAMPAIGN)) && !mpm_is_visible(&k, &t, MPB_HOST_SAVE));
        mpm_label(&k, MPB_HEADER, s, sizeof s); CHECK(!strcmp(s, "Host your save"));
        CHECK(mpm_step(&k, &t, MPB_LOAD_SAVE) & MPA_OPEN_LOAD_LIST);
        k = h; mpm_step(&k, &t, MPB_HOST_NEW); v = mpm_visible(&k, &t);
        CHECK(k.state == MPM_TITLE_HOST && !k.set.fe && (v & B(MPB_NEW_CAMPAIGN)) && !(v & B(MPB_LOAD_SAVE)));
        mpm_label(&k, MPB_HEADER, s, sizeof s); CHECK(!strcmp(s, "Host a new save"));
        CHECK(mpm_step(&k, &t, MPB_NEW_CAMPAIGN) & MPA_OPEN_NEW);
        k = h; mpm_step(&k, &t, MPB_HOST_FE); v = mpm_visible(&k, &t);
        CHECK(k.state == MPM_TITLE_HOST && k.set.fe == 1 && (v & B(MPB_LOAD_SAVE)) && !(v & B(MPB_NEW_CAMPAIGN)));
        mpm_label(&k, MPB_HEADER, s, sizeof s); CHECK(!strcmp(s, "Host a Foreign Expedition"));
        mpm_step(&k, &t, MPB_BACK); mpm_step(&k, &t, MPB_HOST); CHECK(k.state == MPM_TITLE_HOST && !k.set.fe && !k.host_new);   /* the old path: Your save */
    }
    {   /* Foreign Expedition (ext lane, board FESettingsA): chosen on the Multiplayer page, FE rules, summary, Start builds the hub first */
        mpm_model f; mpm_init(&f); mpm_obs t = title(); char s[400];
        f.host_choice = 1; mpm_step(&f, &t, MPB_MULTIPLAYER); mpm_step(&f, &t, MPB_HOST_FE);
        CHECK(f.state == MPM_TITLE_HOST && f.set.fe == 1);
        { mpm_model g2 = f; g2.state = MPM_TITLE_MENU; mpm_step(&g2, &t, MPB_HOST_SAVE); CHECK(g2.set.fe == 0); }   /* Your save: no expedition */
        CHECK(f.set.fe_death == 0 && f.set.fe_xp == 1 && f.set.fe_loot == 1 && f.set.fe_recruit == 0);   /* defaults: death Off (owner 2026-10-15) */
        CHECK(strstr(mpm_fe_desc(&f.set, 0), "last save") != NULL);
        mpm_step(&f, &t, MPB_FED_NEXT); CHECK(f.set.fe_death == 1);
        mpm_step(&f, &t, MPB_FER_NEXT); CHECK(f.set.fe_recruit == 1);
        mpm_fe_value(&f.set, 5, s, sizeof s); CHECK(!strcmp(s, "On"));
        CHECK(strstr(mpm_fe_desc(&f.set, 5), "stay in the hub") != NULL);
        mpm_step(&f, &t, MPB_FED_PREV); CHECK(f.set.fe_death == 0 && strstr(mpm_fe_desc(&f.set, 0), "last save") != NULL);
        mpm_step(&f, &t, MPB_FEU_NEXT); CHECK(f.set.units == 4);
        mpm_settings_summary(&f.set, s, sizeof s); CHECK(strstr(s, "Foreign Expedition") && strstr(s, "Recruiting: on") && strstr(s, "Death: not permanent"));
        mpm_step(&f, &t, MPB_SETTINGS); CHECK(f.settings_open); mpm_step(&f, &t, MPB_FE_DONE); CHECK(!f.settings_open);
        f.state = MPM_LOBBY; mpm_obs g = {0}; g.scene = 2; g.campaign_ready = 1; g.role = 1;
        int a = mpm_step(&f, &g, MPB_START); CHECK(a == MPA_FE_START && f.state == MPM_LOBBY);   /* the game side starts it */
        f.set.fe = 0; a = mpm_step(&f, &g, MPB_START); CHECK((a & MPA_START_GAME) && f.state == MPM_PLAYING);
        for (int r = 0; r < MPM_FE_ROWS; r++) CHECK(mpm_fe_label(r)[0] && mpm_fe_tip(r)[0]);
    }
    {   /* load list filter (owner 2026-10-15): solo list = single-player worlds, Your save = multiplayer worlds, toggle shows both */
        mpm_model l; mpm_init(&l); mpm_obs t = title(); char s[64];
        CHECK(mpm_list_mode(&l) == MPM_LIST_SOLO);
        CHECK(mpm_list_show(MPM_LIST_SOLO, 0, 0, 0) && !mpm_list_show(MPM_LIST_SOLO, 0, 1, 0) && mpm_list_show(MPM_LIST_SOLO, 1, 1, 0));
        CHECK(mpm_list_show(MPM_LIST_SOLO, 0, 1, 1));                      /* the world being played stays */
        CHECK(mpm_list_show(MPM_LIST_MP, 0, 1, 0) && !mpm_list_show(MPM_LIST_MP, 0, 0, 0) && mpm_list_show(MPM_LIST_MP, 1, 0, 0));
        CHECK(mpm_list_show(MPM_LIST_ALL, 0, 0, 0) && mpm_list_show(MPM_LIST_ALL, 0, 1, 0));
        snprintf(s, sizeof s, "%s", mpm_list_toggle_label(MPM_LIST_SOLO)); CHECK(!strcmp(s, "Show multiplayer worlds"));
        snprintf(s, sizeof s, "%s", mpm_list_toggle_label(MPM_LIST_MP)); CHECK(!strcmp(s, "Show single-player worlds"));
        CHECK(!mpm_list_toggle_label(MPM_LIST_ALL)[0]);
        mpm_step(&l, &t, MPB_MULTIPLAYER); mpm_step(&l, &t, MPB_HOST_SAVE); CHECK(mpm_list_mode(&l) == MPM_LIST_SOLO);   /* host page: no list yet */
        mpm_step(&l, &t, MPB_LOAD_SAVE); CHECK(l.state == MPM_PICK_SAVE && mpm_list_mode(&l) == MPM_LIST_MP);
        mpm_model e; mpm_init(&e); e.host_choice = 1; mpm_step(&e, &t, MPB_MULTIPLAYER); mpm_step(&e, &t, MPB_HOST_FE); mpm_step(&e, &t, MPB_LOAD_SAVE);
        CHECK(e.state == MPM_PICK_SAVE && mpm_list_mode(&e) == MPM_LIST_ALL);   /* an expedition copies any world */
    }
    {   /* Multiplayer world tag file (mp_tags.txt) */
        char tp[512]; const char *td = getenv("TMPDIR"); snprintf(tp, sizeof tp, "%s/mp_tags_test_%d.txt", td && *td ? td : "/tmp", (int)getpid());
        FILE *z = fopen(tp, "w"); if (z) fclose(z);                         /* empty file (left in TMPDIR: no rm) */
        char nm[MPM_TAG_NAMES][32], s[160];
        CHECK(mpm_tag_get(tp, "g1", nm, MPM_TAG_NAMES) == -1);
        const char *a[] = { "The owner", "Ana" }; CHECK(mpm_tag_add(tp, "g1", a, 2) == 1);
        CHECK(mpm_tag_get(tp, "g1", nm, MPM_TAG_NAMES) == 2 && !strcmp(nm[0], "The owner") && !strcmp(nm[1], "Ana"));
        CHECK(mpm_tag_add(tp, "g1", a, 2) == 0);                      /* same names: no write */
        const char *b2[] = { "Ana", "Bo\tX\n", "", NULL }; CHECK(mpm_tag_add(tp, "g1", b2, 4) == 1);
        CHECK(mpm_tag_get(tp, "g1", nm, MPM_TAG_NAMES) == 3 && !strcmp(nm[2], "BoX"));
        const char *c1[] = { "The owner", "Zed" }; CHECK(mpm_tag_add(tp, "g2", c1, 2) == 1);
        CHECK(mpm_tag_add(tp, "bad guid", c1, 2) == 0 && mpm_tag_get(tp, "bad", nm, 4) == -1);
        mpm_tag_names(nm, 3, 3, s, sizeof s); CHECK(!strcmp(s, "The owner, Ana and BoX"));
        mpm_tag_names(nm, 3, 2, s, sizeof s); CHECK(!strcmp(s, "The owner, Ana +1"));
        mpm_tag_names(nm, 1, 3, s, sizeof s); CHECK(!strcmp(s, "The owner"));
        CHECK(mpm_tag_remove(tp, "g1") == 1 && mpm_tag_get(tp, "g1", nm, 4) == -1);
        CHECK(mpm_tag_get(tp, "g2", nm, 4) == 2);                    /* the other world keeps its tag */
        CHECK(mpm_tag_remove(tp, "g1") == 0);
        CHECK(mpm_tag_add(tp, "g1", c1, 1) == 1 && mpm_tag_get(tp, "g1", nm, 4) == 1);   /* a friend joins again: back, new names */
        const char *many[20]; char mb[20][8]; for (int i = 0; i < 20; i++) { snprintf(mb[i], 8, "p%d", i); many[i] = mb[i]; }
        mpm_tag_add(tp, "g3", many, 20); CHECK(mpm_tag_get(tp, "g3", nm, MPM_TAG_NAMES) == MPM_TAG_NAMES);
        CHECK(mpm_tag_get("./no_such_dir/x.txt", "g1", nm, 4) == -1 && mpm_tag_add("./no_such_dir/x.txt", "g1", c1, 1) == 0);
    }
    printf(fails ? "mpmenu_test: %d FAILED\n" : "mpmenu_test: all passed%.0d\n", fails);
    return fails != 0;
}
