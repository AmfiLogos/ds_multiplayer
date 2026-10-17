#!/usr/bin/env python3
"""i18n lane (menu side): exact text replacements in src/, as (file, old, new). Re-runnable on a newer main: a pair whose
new text is already there is skipped, a pair whose old text is gone is reported (fix it by hand). Usage: tools/i18n_menu_edits.py"""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
E = []
def e(f, old, new): E.append((f, old, new))

M = "mpmenu.c"
e(M, 'field_make(src, parent, "Address, like 192.168.1.20:47777", is_addr ? clip : "")', 'field_make(src, parent, TR("Address, like 192.168.1.20:47777"), is_addr ? clip : "")')
e(M, 'Obj f = field_make(src, parent, "Your name", my_name);', 'Obj f = field_make(src, parent, TR("Your name"), my_name);')
e(M, '(f, u_str(text), NULL) : (void)0);          /* SetTextWithoutNotify */', '(f, u_str_ok(text), NULL) : (void)0);       /* SetTextWithoutNotify */')
e(M, 'Obj t = mk_txt(tpl_txt, R, 1, 24, 514, 0, 0, PY(wide ? 24 : 27), 400, 40, C_MAIN); tmp_set(t, "Rules");', 'Obj t = mk_txt(tpl_txt, R, 1, 24, 514, 0, 0, PY(wide ? 24 : 27), 400, 40, C_MAIN); tmp_set(t, TR("Rules"));')
e(M, 'Obj d = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(pw - 88), PY(end + (wide ? 40 : 59)), 140, 34, 16, "Done", C_MAIN);', 'Obj d = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(pw - 88), PY(end + (wide ? 40 : 59)), 140, 34, 16, TR("Done"), C_MAIN);')
e(M, 'tmp_set(t, "Expedition Rules");', 'tmp_set(t, TR("Expedition Rules"));')
e(M, 'tmp_set(nt, "Visitors see these rules before they choose units.");', 'tmp_set(nt, TR("Visitors see these rules before they choose units."));')
e(M, 'Obj d = mk_button(src, R, 0, S_NBTN, C_WHITE, PX(pw - 88), PY(end + 55), 140, 34, 16, "Done", C_MAIN);', 'Obj d = mk_button(src, R, 0, S_NBTN, C_WHITE, PX(pw - 88), PY(end + 55), 140, 34, 16, TR("Done"), C_MAIN);')
e(M, '368 - 40, -16, 62, 26, 14, "Watch", C_MAIN);', '368 - 40, -16, 62, 26, 14, TR("Watch"), C_MAIN);')
e(M, 'if (nt) tmp_set(nt, "Click a world to reconnect the same way. x removes it from this list.");', 'if (nt) tmp_set(nt, TR("Click a world to reconnect the same way. x removes it from this list."));')
e(M, '100, 26, 14, "\\xE2\\x80\\xB9 Newer", C_MAIN);', '100, 26, 14, TR("\\xE2\\x80\\xB9 Newer"), C_MAIN);')
e(M, '100, 26, 14, "Older \\xE2\\x80\\xBA", C_MAIN);', '100, 26, 14, TR("Older \\xE2\\x80\\xBA"), C_MAIN);')
e(M, 'rp_build(&rp_title, u_tr(v), src, 0, tpl_img, tpl_txt, -78, -89 + 26, "Saved with this world. New worlds start with your last rules.", h_tb, 0);',
     'rp_build(&rp_title, u_tr(v), src, 0, tpl_img, tpl_txt, -78, -89 + 26, TR("Saved with this world. New worlds start with your last rules."), h_tb, 0);')
e(M, 'tmp_set(l, "<margin-left=46>Rules</margin>"); }', '{ char rl[96]; snprintf(rl, sizeof rl, "<margin-left=46>%s</margin>", TR("Rules")); tmp_set(l, rl); } }')
e(M, '''snprintf(t, sizeof t, "%s  <size=72%%><color=#A69085>Day %lld</color></size>", h->world[0] ? h->world : "A world", h->day);''',
     '''{ char dy[64]; snprintf(dy, sizeof dy, TR("Day %lld"), h->day);
              snprintf(t, sizeof t, "%s  <size=72%%><color=#A69085>%s</color></size>", h->world[0] ? h->world : TR("A world"), dy); }''')
e(M, '''"%s<color=%s>%s</color>%s", o ? ", " : "", col, tok,
                                      !strcmp(h->how, "host") || strcmp(tok, h->host) ? "" : " (host)");''',
     '''"%s<color=%s>%s</color>%s", o ? TR(", ") : "", col, tok,
                                      !strcmp(h->how, "host") || strcmp(tok, h->host) ? "" : TR(" (host)"));''')
e(M, '''            if (!strcmp(h->how, "host")) snprintf(t, sizeof t, "You hosted%s%s", pl[0] ? " \\xC2\\xB7 with " : "", pl);
            else if (!strcmp(h->how, "steam")) snprintf(t, sizeof t, "Steam%s%s", pl[0] ? " \\xC2\\xB7 with " : "", pl);
            else snprintf(t, sizeof t, "IP %s%s%s", h->addr, pl[0] ? " \\xC2\\xB7 with " : "", pl);''',
     '''            if (!strcmp(h->how, "host")) { if (pl[0]) snprintf(t, sizeof t, TR("You hosted \\xC2\\xB7 with %s"), pl); else snprintf(t, sizeof t, "%s", TR("You hosted")); }
            else if (!strcmp(h->how, "steam")) { if (pl[0]) snprintf(t, sizeof t, TR("Steam \\xC2\\xB7 with %s"), pl); else snprintf(t, sizeof t, "%s", TR("Steam")); }
            else { if (pl[0]) snprintf(t, sizeof t, TR("IP %s \\xC2\\xB7 with %s"), h->addr, pl); else snprintf(t, sizeof t, TR("IP %s"), h->addr); }''')
e(M, 'if (cnt > 3) snprintf(lab, sizeof lab, "Your units (+%d)", cnt - 3); else if (cnt) snprintf(lab, sizeof lab, "Your units");',
     'if (cnt > 3) snprintf(lab, sizeof lab, TR("Your units (+%d)"), cnt - 3); else if (cnt) snprintf(lab, sizeof lab, "%s", TR("Your units"));')
e(M, 'char lab[32] = "";', 'char lab[96] = "";')
e(M, 'tmp_set(tt, "Recent Worlds"); }', 'tmp_set(tt, TR("Recent Worlds")); }')
e(M, '''tmp_set(r, "<color=#A69085>Leave multiplayer to load another world</color>");''',
     '''{ char lr[200]; snprintf(lr, sizeof lr, "<color=#A69085>%s</color>", TR("Leave multiplayer to load another world")); tmp_set(r, lr); }''')
e(M, '396, 40, 16, "Steam invite", C_MAIN); KEEP(h_lwb[LW_INVITE], b);', '396, 40, 16, TR("Steam invite"), C_MAIN); KEEP(h_lwb[LW_INVITE], b);')
e(M, '96, 40, 16, "Copy", C_MAIN); KEEP(h_lwb[LW_COPY], b);', '96, 40, 16, TR("Copy"), C_MAIN); KEEP(h_lwb[LW_COPY], b);')
e(M, '206, 34, 16, host ? "Leave multiplayer" : "Leave", C_MAIN);', '206, 34, 16, host ? TR("Leave multiplayer") : TR("Leave"), C_MAIN);')
e(M, '232, 44, 18, "Start", C_START);', '232, 44, 18, mpm_word("TEXTKEY_Common_Start", "Start"), C_START);')
e(M, '206, 34, 16, "Bring units", C_MAIN);', '206, 34, 16, TR("Bring units"), C_MAIN);')
e(M, 'if (host) rp_build(&rp_lobby, R, src, 1, tpl_img, tpl_txt, 0, 0, "Saved with this world. New worlds start with your last rules.", h_lwr, 1);',
     'if (host) rp_build(&rp_lobby, R, src, 1, tpl_img, tpl_txt, 0, 0, TR("Saved with this world. New worlds start with your last rules."), h_lwr, 1);')
e(M, '''    char b[256]; const char *hn = M.host[0] ? M.host : "the host";
    if (host) tmp_set(lwt(T_TITLE), "Multiplayer Lobby"); else { snprintf(b, sizeof b, "%s's Lobby", hn); tmp_set(lwt(T_TITLE), b); }
    tmp_set(lwt(T_HINT), "Share your address, then press Start when everyone is in.");
    snprintf(b, sizeof b, "Waiting for %s to start", hn); tmp_set(lwt(T_GUEST), b);''',
     '''    char b[256]; const char *hn = M.host[0] ? M.host : TR("the host");
    if (host) tmp_set(lwt(T_TITLE), TR("Multiplayer Lobby")); else { snprintf(b, sizeof b, TR("%s's Lobby"), hn); tmp_set(lwt(T_TITLE), b); }
    tmp_set(lwt(T_HINT), TR("Share your address, then press Start when everyone is in."));
    snprintf(b, sizeof b, TR("Waiting for %s to start"), hn); tmp_set(lwt(T_GUEST), b);''')
e(M, 'snprintf(b, sizeof b, "Players (%d)", n); tmp_set(lwt(T_PLAYERS), b); tmp_set(lwt(T_RULES), "Rules"); tmp_set(lwt(T_INV), "Invite");',
     'snprintf(b, sizeof b, TR("Players (%d)"), n); tmp_set(lwt(T_PLAYERS), b); tmp_set(lwt(T_RULES), TR("Rules")); tmp_set(lwt(T_INV), TR("Invite"));')
e(M, 'in.players[i].name : in.nplayers > 0 ? "Player" : my_name;', 'in.players[i].name : in.nplayers > 0 ? TR("Player") : my_name;')
e(M, 'tmp_set(st, tag == 1 ? "Host" : tag == 2 ? "You" : "");', 'tmp_set(st, tag == 1 ? TR("Host") : tag == 2 ? TR("You") : "");')
e(M, '''          snprintf(b, sizeof b, "Friends outside your home: open port %s on your router%s.%s%s", port ? port + 1 : "47777",
                   in.steam ? ", or use Steam invite" : "", M.internet[0] ? " Internet: " : "", M.internet); tmp_set(lwt(T_INV_NOTE), b); }''',
     '''          snprintf(b, sizeof b, in.steam ? TR("Friends outside your home: open port %s on your router, or use Steam invite.") : TR("Friends outside your home: open port %s on your router."),
                   port ? port + 1 : "47777");
          if (M.internet[0]) { size_t bl = strlen(b); snprintf(b + bl, sizeof b - bl, " "); bl = strlen(b); snprintf(b + bl, sizeof b - bl, TR("Internet: %s"), M.internet); }
          tmp_set(lwt(T_INV_NOTE), b); }''')
e(M, 'if (cb) tmp_set(AT(cb, 0x28, Obj), frames < copied_until ? "Copied" : "Copy");', 'if (cb) tmp_set(AT(cb, 0x28, Obj), frames < copied_until ? TR("Copied") : TR("Copy"));')
e(M, '''if (ib2) tmp_set(AT(ib2, 0x28, Obj), in.steam ? "Steam invite" : "<color=#A69085>Steam invite <size=80%>\\xC2\\xB7 Needs the Steam version</size></color>");''',
     '''if (ib2) { char iv[200]; if (in.steam) snprintf(iv, sizeof iv, "%s", TR("Steam invite"));
                  else snprintf(iv, sizeof iv, "<color=#A69085>%s <size=80%%>\\xC2\\xB7 %s</size></color>", TR("Steam invite"), TR("Needs the Steam version"));
                  tmp_set(AT(ib2, 0x28, Obj), iv); }''')
e(M, 'tmp_set(lwt(T_RULES_NOTE), host ? "Only the host can change the rules." : "Set by the host.");', 'tmp_set(lwt(T_RULES_NOTE), host ? TR("Only the host can change the rules.") : TR("Set by the host."));')
e(M, 'if (loading[0]) snprintf(b, sizeof b, "%s is still loading", loading); else b[0] = 0;', 'if (loading[0]) snprintf(b, sizeof b, TR("%s is still loading"), loading); else b[0] = 0;')
# load list: the save row's tag (shown after the world's own title)
e(M, '''    if (strstr(cur, "[Multiplayer]")) return;
    char nt[320]; snprintf(nt, sizeof nt, "%s  <color=#8FC4FF>[Multiplayer]</color>", cur);''',
     '''    char tg[96]; snprintf(tg, sizeof tg, "  <color=#8FC4FF>[%s]</color>", TR("Multiplayer"));
    if (strstr(cur, tg) || strstr(cur, "<color=#8FC4FF>[")) return;
    char nt[320]; snprintf(nt, sizeof nt, "%s%s", cur, tg);''')
e(M, '''if (strstr(cur, "Leave multiplayer") && load_label[0]) tmp_set(t, load_label);   /* label of an older build */''',
     '''if ((strstr(cur, "Leave multiplayer") || strstr(cur, TR("Leave multiplayer"))) && load_label[0]) tmp_set(t, load_label);   /* label of an older build (English then) */''')
e(M, '''    snprintf(M.msg, sizeof M.msg, "%s is playing another world now%s%s%s, not %s.", host[0] ? host : "The host", w && w[0] ? " (" : "", w && w[0] ? w : "", w && w[0] ? ")" : "",
             hist_world[0] ? hist_world : "this one");''',
     '''    const char *hh = host[0] ? host : TR("The host"), *nw = hist_world[0] ? hist_world : TR("this one");
    if (w && w[0]) snprintf(M.msg, sizeof M.msg, TR("%s is playing another world now (%s), not %s."), hh, w, nw);
    else snprintf(M.msg, sizeof M.msg, TR("%s is playing another world now, not %s."), hh, nw);''')
e(M, '''    if (r == DSNET_REJ_MODSET) { snprintf(M.msg, sizeof M.msg, "Could not join: your gameplay mods are not the same as the host's"); modset_notice(); }
    else if (*w) snprintf(M.msg, sizeof M.msg, "Could not join: %s", w);
    else if (rc) snprintf(M.msg, sizeof M.msg, "%s answered, but the game did not start. Try again.", addr);''',
     '''    if (r == DSNET_REJ_MODSET) { snprintf(M.msg, sizeof M.msg, "%s", TR("Could not join: your gameplay mods are not the same as the host's")); modset_notice(); }
    else if (*w) snprintf(M.msg, sizeof M.msg, TR("Could not join: %s"), w);
    else if (rc) snprintf(M.msg, sizeof M.msg, TR("%s answered, but the game did not start. Try again."), addr);''')
e(M, '''snprintf(M.msg, sizeof M.msg, "\\"%.40s\\" is not an address. It looks like 192.168.1.20:47777", txt); return;''',
     '''char tq[64]; snprintf(tq, sizeof tq, "%.40s", txt); dsi_utf8_fix_end(tq);
        snprintf(M.msg, sizeof M.msg, TR("\\"%s\\" is not an address. It looks like 192.168.1.20:47777"), tq); return;''')
e(M, '''    if (r == DSNET_CONN_TIMEOUT) snprintf(M.msg, sizeof M.msg, "No answer from %s. Check the address, and ask the host to open port %u on the router.", addr, conn_cfg.port);
    else if (r == DSNET_CONN_REFUSED) snprintf(M.msg, sizeof M.msg, "Nobody is hosting at %s. Check the address and the port.", addr);
    else if (r == DSNET_CONN_BAD_ADDRESS) snprintf(M.msg, sizeof M.msg, "Unknown address: %s. Check the address.", addr);
    else if (r < 0) snprintf(M.msg, sizeof M.msg, "Cannot reach %s. Check the address and your network.", addr);
    if (r != 1 && hist_host[0]) { snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", hist_host); hist_host[0] = 0; }   /* a Recent worlds row */''',
     '''    if (r == DSNET_CONN_TIMEOUT) snprintf(M.msg, sizeof M.msg, TR("No answer from %s. Check the address, and ask the host to open port %u on the router."), addr, conn_cfg.port);
    else if (r == DSNET_CONN_REFUSED) snprintf(M.msg, sizeof M.msg, TR("Nobody is hosting at %s. Check the address and the port."), addr);
    else if (r == DSNET_CONN_BAD_ADDRESS) snprintf(M.msg, sizeof M.msg, TR("Unknown address: %s. Check the address."), addr);
    else if (r < 0) snprintf(M.msg, sizeof M.msg, TR("Cannot reach %s. Check the address and your network."), addr);
    if (r != 1 && hist_host[0]) { snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_host); hist_host[0] = 0; }   /* a Recent worlds row */''')
e(M, '''    if (rc && hist_host[0] && !other_world_text(hist_host)) snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", hist_host);''',
     '''    if (rc && hist_host[0] && !other_world_text(hist_host)) snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_host);''')
e(M, '''    tmp_set(AT(v, 0x40, Obj), "OK");''', '''    tmp_set(AT(v, 0x40, Obj), TR("OK"));''')

# ---- part 2: boxes, messages, Recent worlds, chat, actions, tooltips, tags
e(M, '''        o += (size_t)snprintf(names + o, o < sizeof names ? sizeof names - o : 0, "%s<color=%s>%s</color>", n ? ", " : "", ui_player_color(p), nm && nm[0] ? nm : "A player");''',
     '''        o += (size_t)snprintf(names + o, o < sizeof names ? sizeof names - o : 0, "%s<color=%s>%s</color>", n ? TR(", ") : "", ui_player_color(p), nm && nm[0] ? nm : TR("A player"));''')
e(M, '''static void leave_box_text(char *out, size_t cap) {''', '''static int leave_box_text(char *out, size_t cap) {             /* returns how many other players are in (they go back to the title) */''')
e(M, '''    if (n) snprintf(out, cap, "<size=150%%>End the game for everyone?</size>\\n%s %s back to the title.\\n<size=88%%><color=#a69085>The world stays on your computer. You keep playing it alone.</color></size>",
                    names, n > 1 ? "go" : "goes");
    else snprintf(out, cap, "<size=150%%>Leave multiplayer?</size>\\nNobody else is in this world now. Friends can no longer join.\\n<size=88%%><color=#a69085>The world stays on your computer. You keep playing it alone.</color></size>");''',
     '''    char who[400];
    if (n) snprintf(who, sizeof who, TRN("%s goes back to the title.", "%s go back to the title.", n), names);
    snprintf(out, cap, "<size=150%%>%s</size>\\n%s\\n<size=88%%><color=#a69085>%s</color></size>", n ? TR("End the game for everyone?") : TR("Leave multiplayer?"),
             n ? who : TR("Nobody else is in this world now. Friends can no longer join."), TR("The world stays on your computer. You keep playing it alone."));
    return n;''')
e(M, '''    leave_box_text(lbox_text, sizeof lbox_text);''', '''    int others = leave_box_text(lbox_text, sizeof lbox_text);''')
e(M, """    tmp_set(AT(v, 0x40, Obj), strstr(lbox_text, "End the game") ? "End for everyone" : "Leave");
    tmp_set(AT(v, 0x50, Obj), "Cancel");""",
     """    tmp_set(AT(v, 0x40, Obj), others ? TR("End for everyone") : TR("Leave"));          /* from the box's own case, not its text */
    tmp_set(AT(v, 0x50, Obj), mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"));""")
e(M, '''    if (n == 1) snprintf(t, sizeof t, "<size=150%%>Remove the spectator now?</size>\\nThey go back to the title. Nobody can watch until you allow spectators again.");
    else snprintf(t, sizeof t, "<size=150%%>Remove the %d spectators now?</size>\\nThey go back to the title. Nobody can watch until you allow spectators again.", n);
    box_show(BX_SPECT_OFF, t, "Remove", "Cancel");''',
     '''    char hd[160]; snprintf(hd, sizeof hd, TRN("Remove the spectator now?", "Remove the %d spectators now?", n), n);
    snprintf(t, sizeof t, "<size=150%%>%s</size>\\n%s", hd, TR("They go back to the title. Nobody can watch until you allow spectators again."));
    box_show(BX_SPECT_OFF, t, TR("Remove"), mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"));''')
e(M, '''            snprintf(M.notice, sizeof M.notice, "<size=150%%>Could not save your game</size>\\nSo you did not join %s's game. Save from the Esc menu, then ask %s to invite you again.", inv_host, inv_host);''',
     '''            char bd[400]; snprintf(bd, sizeof bd, TR("So you did not join %s's game. Save from the Esc menu, then ask %s to invite you again."), inv_host, inv_host);
            snprintf(M.notice, sizeof M.notice, "<size=150%%>%s</size>\\n%s", TR("Could not save your game"), bd);''')
e(M, '''            snprintf(t, sizeof t, "<size=150%%>Your game could not be saved</size>\\nJoin %s's game anyway?\\n<size=88%%><color=#a69085>Unsaved progress in this world will be lost.</color></size>", inv_host);
            box_show(BX_NOSAVE, t, "Join anyway", "Cancel");''',
     '''            char q[200]; snprintf(q, sizeof q, TR("Join %s's game anyway?"), inv_host);
            snprintf(t, sizeof t, "<size=150%%>%s</size>\\n%s\\n<size=88%%><color=#a69085>%s</color></size>", TR("Your game could not be saved"), q, TR("Unsaved progress in this world will be lost."));
            box_show(BX_NOSAVE, t, TR("Join anyway"), mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"));''')
e(M, '''        snprintf(t, sizeof t, "<size=150%%>Join %s?</size>\\n%s invited you to their world.\\n<size=88%%><color=#a69085>Your progress here is saved first, then this world closes.</color></size>", inv_host, inv_host);
        box_show(BX_INVITE, t, "Save and join", "Cancel");''',
     '''        char hd[160], bd[200]; snprintf(hd, sizeof hd, TR("Join %s?"), inv_host); snprintf(bd, sizeof bd, TR("%s invited you to their world."), inv_host);
        snprintf(t, sizeof t, "<size=150%%>%s</size>\\n%s\\n<size=88%%><color=#a69085>%s</color></size>", hd, bd, TR("Your progress here is saved first, then this world closes."));
        box_show(BX_INVITE, t, TR("Save and join"), mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"));''')
e(M, '''        const char *h = in.host_name[0] ? in.host_name : inv_host[0] ? inv_host : "the host";
        char t[300]; snprintf(t, sizeof t, "<size=150%%>Joining %s's game...</size>\\n<size=88%%><color=#a69085>%s</color></size>", h,
                              in.phase == MP_PH_LOADING ? "Loading the world" : in.phase == MP_PH_WAITING_SAVE ? "Waiting for the world" : "Connecting");
        box_show(BX_JOINING, t, "Cancel", NULL);''',
     '''        const char *h = in.host_name[0] ? in.host_name : inv_host[0] ? inv_host : TR("the host");
        char hd[160]; snprintf(hd, sizeof hd, TR("Joining %s's game..."), h);
        char t[300]; snprintf(t, sizeof t, "<size=150%%>%s</size>\\n<size=88%%><color=#a69085>%s</color></size>", hd,
                              in.phase == MP_PH_LOADING ? TR("Loading the world") : in.phase == MP_PH_WAITING_SAVE ? TR("Waiting for the world") : TR("Connecting"));
        box_show(BX_JOINING, t, mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"), NULL);''')
e(M, '''hn && hn[0] ? hn : "The host");
    if (adm != adm0) say(adm ? "%s turned Admin mode on: the host can control every unit." : "%s turned Admin mode off: the host controls only their own units.", host);''',
     '''hn && hn[0] ? hn : TR("The host"));
    if (adm != adm0) say(adm ? TR("%s turned Admin mode on: the host can control every unit.") : TR("%s turned Admin mode off: the host controls only their own units."), host);''')
e(M, '''        say("Units per player is now %d (host rule).", r.units_per_player);
    if (r.pause_mode != r0.pause_mode) say("Pause is now: %s (host rule).", r.pause_mode == MPM_PAUSE_NONE ? "no pause" : r.pause_mode == MPM_PAUSE_ALL_VOTE ? "pause by vote" : "anyone can pause");
    if ((r.allow_speedup != 0) != (r0.allow_speedup != 0)) say("Speed-up is now %s (host rule).", r.allow_speedup ? "on" : "off");
    if (r.gear_mode != r0.gear_mode) say("Gear is now: %s (host rule).", r.gear_mode == MPM_GEAR_SHARED_ANY ? "shared, anyone equips any unit" : r.gear_mode == MPM_GEAR_OWN_STORAGE ? "own storage per player" : "shared, equip your own units");
    if (r.event_choice != r0.event_choice) { mpm_settings t = { 0 }; t.events = r.event_choice; char v[40]; mpm_rule_value(&t, 5, v, sizeof v); say("Event choices: %s (host rule).", v); }
    if (r.population != r0.population) { mpm_settings t = { 0 }; t.population = r.population; char v[40]; mpm_rule_value(&t, 4, v, sizeof v); say("Population: %s (host rule).", v); }
    if (r.shared_fog != r0.shared_fog) say("Shared fog of war: %s (host rule).", r.shared_fog ? "On" : "Off");''',
     '''        say(TR("Units per player is now %d (host rule)."), r.units_per_player);
    if (r.pause_mode != r0.pause_mode) say(TR("Pause is now: %s (host rule)."), r.pause_mode == MPM_PAUSE_NONE ? TR("no pause") : r.pause_mode == MPM_PAUSE_ALL_VOTE ? TR("pause by vote") : TR("anyone can pause"));
    if ((r.allow_speedup != 0) != (r0.allow_speedup != 0)) say(TR("Speed-up is now %s (host rule)."), r.allow_speedup ? TR("on") : TR("off"));
    if (r.gear_mode != r0.gear_mode) say(TR("Gear is now: %s (host rule)."), r.gear_mode == MPM_GEAR_SHARED_ANY ? TR("shared, anyone equips any unit") : r.gear_mode == MPM_GEAR_OWN_STORAGE ? TR("own storage per player") : TR("shared, equip your own units"));
    if (r.event_choice != r0.event_choice) { mpm_settings t = { 0 }; t.events = r.event_choice; char v[64]; mpm_rule_value(&t, 5, v, sizeof v); say(TR("Event choices: %s (host rule)."), v); }
    if (r.population != r0.population) { mpm_settings t = { 0 }; t.population = r.population; char v[64]; mpm_rule_value(&t, 4, v, sizeof v); say(TR("Population: %s (host rule)."), v); }
    if (r.shared_fog != r0.shared_fog) say(TR("Shared fog of war: %s (host rule)."), r.shared_fog ? TR("On") : TR("Off"));''')
e(M, '''        char cur[128] = ""; str_utf8(tmp_get(t), cur, sizeof cur); if (strcmp(cur, "Host a world")) tmp_set(t, "Host a world");
        if (bt) { str_utf8(tmp_get(bt), cur, sizeof cur); if (strcmp(cur, "Host")) tmp_set(bt, "Host"); }''',
     '''        char cur[128] = ""; str_utf8(tmp_get(t), cur, sizeof cur); if (strcmp(cur, TR("Host a world"))) tmp_set(t, TR("Host a world"));   /* our own text read back */
        if (bt) { str_utf8(tmp_get(bt), cur, sizeof cur); if (strcmp(cur, TR("Host"))) tmp_set(bt, TR("Host")); }''')
e(M, '''        if (!M.steam) { snprintf(M.msg, sizeof M.msg, "This world was joined through Steam: needs the Steam version."); return; }''',
     '''        if (!M.steam) { snprintf(M.msg, sizeof M.msg, "%s", TR("This world was joined through Steam: needs the Steam version.")); return; }''')
e(M, '''          if (M.watch && !next_spect) { snprintf(M.msg, sizeof M.msg, "Spectators cannot join through Steam."); return; }''',
     '''          if (M.watch && !next_spect) { snprintf(M.msg, sizeof M.msg, "%s", TR("Spectators cannot join through Steam.")); return; }''')
e(M, '''        snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", h->host); return;''',
     '''        snprintf(M.msg, sizeof M.msg, TR("%s is not hosting this world right now."), hist_shown_host(h->host)); return;''')
e(M, '''{ snprintf(M.msg, sizeof M.msg, "Bad address in this row: %s", h->addr); return; }''', '''{ snprintf(M.msg, sizeof M.msg, TR("Bad address in this row: %s"), h->addr); return; }''')
e(M, '''conn_cfg = cc; conn_t0 = now_ms_(); snprintf(hist_host, sizeof hist_host, "%s", h->host);''', '''conn_cfg = cc; conn_t0 = now_ms_(); snprintf(hist_host, sizeof hist_host, "%s", hist_shown_host(h->host));''')
e(M, '''static void hist_join(int k) {''', '''/* hist_t.host "the host" is stored data (name unknown): shown in the player's language */
static const char *hist_shown_host(const char *h) { return !strcmp(h, "the host") ? TR("the host") : h; }
static void hist_join(int k) {''')
e(M, '''            snprintf(t, sizeof t, "<color=%s>%s (watching):</color> %s", UI_SPECTATOR_COLOR, m.name[0] ? m.name : "Spectator", body);
        else if (m.kind == DSNET_CHAT_TEXT) snprintf(t, sizeof t, "<color=%s>%s:</color> %s", col, m.name[0] ? m.name : "Player", body);''',
     '''        { char wn[96]; snprintf(wn, sizeof wn, TR("%s (watching):"), m.name[0] ? m.name : TR("Spectator"));
            snprintf(t, sizeof t, "<color=%s>%s</color> %s", UI_SPECTATOR_COLOR, wn, body); }
        else if (m.kind == DSNET_CHAT_TEXT) snprintf(t, sizeof t, "<color=%s>%s:</color> %s", col, m.name[0] ? m.name : TR("Player"), body);''')
e(M, '''            snprintf(t, sizeof t, "<i><color=#A69085>%s %s</color></i>", m.name[0] ? m.name : "A spectator",
                     m.kind == DSNET_CHAT_WATCHING ? "is watching" : "stopped watching");''',
     '''            char ln[200]; snprintf(ln, sizeof ln, m.kind == DSNET_CHAT_WATCHING ? TR("%s is watching") : TR("%s stopped watching"), m.name[0] ? m.name : TR("A spectator"));
            snprintf(t, sizeof t, "<i><color=#A69085>%s</color></i>", ln);''')
e(M, '''            snprintf(t, sizeof t, "<i><color=#A69085>%s %s</color></i>", m.name[0] ? m.name : "A player",
                     m.kind == DSNET_CHAT_LEFT ? "left the game" : m.kind == DSNET_CHAT_RECONNECTED ? "reconnected" : "joined the game");''',
     '''            char ln[200]; snprintf(ln, sizeof ln, m.kind == DSNET_CHAT_LEFT ? TR("%s left the game") : m.kind == DSNET_CHAT_RECONNECTED ? TR("%s reconnected") : TR("%s joined the game"),
                                   m.name[0] ? m.name : TR("A player"));
            snprintf(t, sizeof t, "<i><color=#A69085>%s</color></i>", ln);''')
e(M, '''if (ti) tmp_set(ti, "Chat");''', '''if (ti) tmp_set(ti, TR("Chat"));''')
e(M, '''if (hi) tmp_set(hi, "Right-click and drag to move");''', '''if (hi) tmp_set(hi, TR("Right-click and drag to move"));''')
e(M, '''if (say) tmp_set(say, "Say:");''', '''if (say) tmp_set(say, TR("Say:"));''')
e(M, '''if (rc) chat_add("<i><color=#F16071>Message not sent: no connection to the other players.</color></i>"); }   /* F18 */''',
     '''if (rc) { char ln[300]; snprintf(ln, sizeof ln, "<i><color=#F16071>%s</color></i>", TR("Message not sent: no connection to the other players.")); chat_add(ln); } }   /* F18 */''')
e(M, '''          o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s<color=%s>%s</color>", k ? ", " : "", ui_player_color(p), nm); k++;
      }
      if (k && o < sizeof line) snprintf(line + o, sizeof line - o, "%s", more ? " and others are typing..." : k > 1 ? " are typing..." : " is typing...");''',
     '''          o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s<color=%s>%s</color>", k ? TR(", ") : "", ui_player_color(p), nm); k++;
      }
      if (k) { char who[200]; snprintf(who, sizeof who, "%s", line);
               snprintf(line, sizeof line, more ? TR("%s and others are typing...") : TRN("%s is typing...", "%s are typing...", k), who); }''')
e(M, '''if (tip_ready()) game_tip_show("Ping", "Click to move your camera there.");''', '''if (tip_ready()) game_tip_show(TR("Ping"), TR("Click to move your camera there."));''')
e(M, '''        if (!hc || !hc->on_start) snprintf(err, sizeof err, "No mod offers this choice.");''', '''        if (!hc || !hc->on_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));''')
e(M, '''        if (!hc || !hc->page_start) snprintf(err, sizeof err, "No mod offers this choice.");''', '''        if (!hc || !hc->page_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));''')
e(M, '''        if (!hc || !hc->lobby_start) snprintf(err, sizeof err, "No mod offers this choice.");''', '''        if (!hc || !hc->lobby_start) snprintf(err, sizeof err, "%s", TR("No mod offers this choice."));''')
e(M, '''else if (ui_notice) ui_notice(err[0] ? err : "This mode could not start.");''', '''else if (ui_notice) ui_notice(err[0] ? err : TR("This mode could not start."));''')
e(M, '''if (rc) say("The Steam invite window did not open (Steam overlay off?). Use Copy direct address instead.");''', '''if (rc) say("%s", TR("The Steam invite window did not open (Steam overlay off?). Use Copy direct address instead."));''')
e(M, '''if (host_alone) say("Multiplayer ended. You are playing alone now; the world stays on your computer.");''', '''if (host_alone) say("%s", TR("Multiplayer ended. You are playing alone now; the world stays on your computer."));''')
e(M, '''        if (rc) snprintf(M.msg, sizeof M.msg, "Could not join %s's world (Steam). Try again, or use Join by address.", k >= 0 && k < 6 ? M.friends[k] : "a friend");''',
     '''        if (rc) snprintf(M.msg, sizeof M.msg, TR("Could not join %s's world (Steam). Try again, or use Join by address."), k >= 0 && k < 6 ? M.friends[k] : TR("a friend"));''')
e(M, '''    char t[600]; snprintf(t, sizeof t, "%s  <size=75%%><color=#8FC4FF>Multiplayer</color><color=#A69085>  with %s</color></size>", clan, who);''',
     '''    char wt[300]; snprintf(wt, sizeof wt, TR("with %s"), who);
    char t[600]; snprintf(t, sizeof t, "%s  <size=75%%><color=#8FC4FF>%s</color><color=#A69085>  %s</color></size>", clan, TR("Multiplayer"), wt);''')
e(M, '''        tmp_set(rm, "<u>Remove tag</u>");''', '''        { char rt[96]; snprintf(rt, sizeof rt, "<u>%s</u>", TR("Remove tag")); tmp_set(rm, rt); }''')
e(M, '''        char *k = strstr(cur, "  <color=#8FC4FF>[Multiplayer]</color>"); if (k) { *k = 0; tmp_set(tt, cur); }''',
     '''        char *k = strstr(cur, "  <color=#8FC4FF>["); if (k) { *k = 0; tmp_set(tt, cur); }   /* our tag (h_slot_set), any language */''')
e(M, '''    snprintf(t, sizeof t, "<size=150%%>Remove the multiplayer tag?</size>\\n%s was played with %s.\\n<size=88%%><color=#a69085>The world itself does not change. The tag comes back if a friend joins it again.</color></size>", clan, who);
    box_show(BX_UNTAG, t, "Remove", "Cancel");''',
     '''    char bd[400]; snprintf(bd, sizeof bd, TR("%s was played with %s."), clan, who);
    snprintf(t, sizeof t, "<size=150%%>%s</size>\\n%s\\n<size=88%%><color=#a69085>%s</color></size>", TR("Remove the multiplayer tag?"), bd, TR("The world itself does not change. The tag comes back if a friend joins it again."));
    box_show(BX_UNTAG, t, TR("Remove"), mpm_word("TEXTKEY_CommonPopUp_Cancel", "Cancel"));''')

def run():
    miss = done = skip = 0
    cache = {}
    for f, old, new in E:
        p = os.path.join(ROOT, f)
        t = cache.get(p) or open(p, encoding="utf-8").read()
        if t.count(old) == 1: t = t.replace(old, new); done += 1
        elif new in t: skip += 1
        else: miss += 1; print(f"MISS {f}: {old[:100]!r} (found {t.count(old)} times)")
        cache[p] = t
    for p, t in cache.items(): open(p, "w", encoding="utf-8").write(t)
    print(f"i18n edits: {done} applied, {skip} already there, {miss} missing")
    return 1 if miss else 0
if __name__ == "__main__": sys.exit(run())
