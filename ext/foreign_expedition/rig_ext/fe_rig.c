/* Foreign Expedition test commands, layered on the rig dylib WITHOUT editing src/:
   build.sh compiles a copy of src/rig_cmd.c whose rig_exec is renamed rig_exec_orig; this file is the new rig_exec.
   fe_stats {guid}: every StatType (1..122) of a unit, read through StatusReader like list_units' hp. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "rig.h"

char *rig_exec_orig(const char *line);

static char *fe_stats(const char *line) {
    char gs[64] = ""; Guid g; Buf b = {0};
    if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) { bput(&b, "{\"ok\":false,\"error\":\"need guid\"}"); return b.p; }
    int map = cur_map(); Obj u = map < 0 ? NULL : find_unit(map, g);
    if (!u) { bput(&b, "{\"ok\":false,\"error\":\"unit not found on the current map\"}"); return b.p; }
    bput(&b, "{\"ok\":true,\"guid\":\"%s\",\"map\":%d,\"dead\":%s,\"stats\":[", gs, map, unit_dead(u) ? "true" : "false");
    for (int i = 1; i <= 122; i++) bput(&b, "%s%.6g", i > 1 ? "," : "", unit_stat(u, i));
    bput(&b, "]}");
    return b.p;
}

char *rig_exec(const char *line) {
    char cmd[64] = "";
    jstr(line, "cmd", cmd, sizeof cmd);
    if (!strcmp(cmd, "fe_stats")) return fe_stats(line);
    if (!strcmp(cmd, "fe_save")) {                     /* fe_save {name}: CampaignSaveLoader.Save, no mp session needed */
        char nm[200] = "fe_test"; jstr(line, "name", nm, sizeof nm);
        Buf b = {0}; bput(&b, "{\"ok\":%s}", rig_save_campaign(nm) ? "true" : "false"); return b.p;
    }
    return rig_exec_orig(line);
}
