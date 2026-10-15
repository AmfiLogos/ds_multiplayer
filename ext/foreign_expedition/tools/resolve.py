#!/usr/bin/env python3
"""Resolve the git merge-file conflicts of the FE merge (merge/tree), one rule per conflict, in file order."""
import os, re
T = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/merge/tree/')
def resolve(f, rules):
    t = open(T + f).read()
    parts = re.split(r'(<<<<<<< src\n.*?\n=======\n.*?>>>>>>> fe\n)', t, flags=re.S)
    k = 0
    for i, p in enumerate(parts):
        m = re.match(r'<<<<<<< src\n(.*?)=======\n(.*?)>>>>>>> fe\n', p, re.S)
        if not m: continue
        parts[i] = rules[k](m.group(1), m.group(2)); k += 1
    assert k == len(rules), (f, k, len(rules))
    open(T + f, 'w').write(''.join(parts)); print('resolved', f, k)
both = lambda s, e: s + e
resolve('mp_core.c', [both])
resolve('net/dsnet_internal.h', [lambda s, e: s.replace('enum { BLOB_SAVE = 1, BLOB_PATCH = 2, BLOB_REPORT = 3 };',
    'enum { BLOB_SAVE = 1, BLOB_PATCH = 2, BLOB_REPORT = 3, BLOB_MAIL = 4 };   /* MAIL data: to_uid[16], from_uid[16] (server-stamped), u8 kind, payload */')])
resolve('net/dsnet_client.c', [lambda s, e: s.replace('        if (r == 0) return;\n',
    '        if (r == 0) return;\n        if (c->rx.kind == BLOB_MAIL) {           /* Foreign Expedition mail: queued for the game thread */\n'
    '            if (c->rx.total >= 33 && c->nmail < 64) { c->mail[c->nmail].data = c->rx.buf; c->mail[c->nmail].len = c->rx.total; c->nmail++; }\n'
    '            else free(c->rx.buf);\n            c->rx.buf = NULL;\n            return;\n        }\n', 1)])
def server(s, e):
    mail = e[e.index('        if (r == 1 && c->rx.kind == BLOB_MAIL) {'):e.index('        if (!c->is_host) return;')]
    head = ('        if (!c->is_host && n >= 6 && p[5] == BLOB_MAIL) {   /* Foreign Expedition mail from a guest (spectators send none) */\n'
            '            if (c->spectator) return;\n'
            '            int r = dsnet_blob_feed(&c->rx, p + 1, n - 1);\n'
            '            if (r < 0) { dsnet_logf("server: bad mail fragment from player %d", c->id); return; }\n'
            + mail.replace('        if (r == 1 && c->rx.kind == BLOB_MAIL) {', '            if (r == 1 && c->rx.kind == BLOB_MAIL) {').replace('\n        ', '\n            ')
            + '            return;\n        }\n')
    hostmail = mail.replace('if (r == 1 && c->rx.kind == BLOB_MAIL)', 'if (c->rx.kind == BLOB_MAIL)')
    s2 = s.replace('        if (r == 0) return;\n        if (c->rx.kind == BLOB_PATCH) {', '        if (r == 0) return;\n' + hostmail + '        if (c->rx.kind == BLOB_PATCH) {', 1)
    assert s2 != s
    return head + s2
resolve('net/dsnet_server.c', [server])
resolve('mpmenu.h', [
    lambda s, e: ('typedef struct mpm_settings { int pause_mode, speedup, gear, units, admin, events, population, fog;\n'
                  '                              int spectators, watching;\n'
                  '                              int fe, fe_death, fe_xp, fe_loot, fe_recruit; } mpm_settings;   /* fog: shared fog of war 0/1 (default 0); spectators: host allows them (1 default); watching: how many watch now (game side, display only); fe: World = Foreign Expedition (boards FESettingsA, ForeignHost) */\n'),
    both,
    lambda s, e: s + e.replace('1 << 18', '1 << 21').replace('1 << 19', '1 << 22').replace('1 << 20', '1 << 23'),
    both])
resolve('mpmenu_model.c', [
    lambda s, e: s + e.splitlines(True)[1],
    lambda s, e: e.splitlines(True)[0] + s,
    lambda s, e: s.rstrip(';\n') + '\n' + e.replace('                              | (m->fe_results', '                              | (m->fe_results')])
resolve('mpmenu.c', [
    lambda s, e: s.replace('MPB_FRIEND5, MPB_SETTINGS,', 'MPB_FRIEND5, MPB_WORLD, MPB_SETTINGS,').replace('MPB_JOIN_FRIEND, MPB_LOAD_SAVE', 'MPB_JOIN_FRIEND, MPB_FE_RESLIST, MPB_LOAD_SAVE'),
    both,
    lambda s, e: s.replace('\\"spectators\\":%d,\\"chat_fade', '\\"spectators\\":%d,\\"fe\\":%d,\\"fe_death\\":%d,\\"fe_xp\\":%d,\\"fe_loot\\":%d,\\"fe_recruit\\":%d,\\"chat_fade').replace(
        'M.set.fog, M.set.spectators, chat_fade_s', 'M.set.fog, M.set.spectators, M.set.fe, M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.fe_recruit, chat_fade_s'),
    lambda s, e: s.splitlines(True)[0] + e + ''.join(s.splitlines(True)[1:]),
    both,
    lambda s, e: s.replace('\\"watching\\":%d},', '\\"watching\\":%d,\\"fe\\":%d,\\"fe_death\\":%d,\\"fe_xp\\":%d,\\"fe_loot\\":%d,\\"fe_recruit\\":%d},').replace(
        'M.set.watching, M.watch, M.spect);', 'M.set.watching, M.set.fe, M.set.fe_death, M.set.fe_xp, M.set.fe_loot, M.set.fe_recruit, M.watch, M.spect);')])
