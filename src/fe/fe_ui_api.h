/* Foreign Expedition data for the menus (fe_game.c -> mpmenu.c): plain structs, no JSON. Text keys stay raw (the menu
 * translates them with the game's TextKeyExtensions.GetText). */
#pragma once
#include <stddef.h>
typedef struct { char path[1024], clan[64], folder[24], version[32]; int day, units, ok; } fe_save_info;
typedef struct { char guid[40], name_key[80], trees[120]; char gear_keys[6][64]; int level, ngear; } fe_unit_info;
typedef struct { int on, death, xp, loot, units, recruit; char host[32], version[32]; } fe_mode_info;
typedef struct { char run[33], host[32], date[24], applied_date[24], applied_file[400]; int units, applied; } fe_result_info;
typedef struct { char name_key[80]; double lv0, lv1, exp0, exp1; int dead, from_snapshot, removed; } fe_res_row;
typedef struct { char key[64]; int amount; } fe_res_item;

int  fe_list_saves(fe_save_info *out, int max);                  /* own Saves + the game's Saves (read only), newest first */
int  fe_list_units(const char *path, fe_unit_info *out, int max);
int  fe_bring_units(const char *path, const char (*guids)[40], int n, char *err, size_t errn);   /* 0 ok */
/* My Units (the unit library, src/myunits.h; only read here). portrait = a key, empty when the library has none */
#define FE_LIB_TEAM_MAX 8
typedef struct { char id[17], name[64], race[64], portrait[96]; int level; } fe_lib_unit;
typedef struct { char name[64]; char ids[FE_LIB_TEAM_MAX][17]; int n; } fe_lib_team;
/* units (count) and teams (*nteams); -1 + err when the library file is damaged / newer. An absent file = 0 units */
int  fe_lib_list(fe_lib_unit *units, int max, fe_lib_team *teams, int tmax, int *nteams, char *err, size_t errn);
int  fe_bring_lib(const char (*ids)[17], int n, char *err, size_t errn);                       /* 0 ok: copies sent */
int  fe_result_all_custom(const char *run);                      /* 1 = every unit of this result is a My Units / wiki copy */
int  fe_mode_get(fe_mode_info *m);                               /* 1 = this session is a Foreign Expedition lobby / hub */
void fe_host_mode(int on, int death, int xp, int loot, int units, int recruit, const char *host_name);   /* host, each frame */
int  fe_brought_of(int slot);                                    /* units this player brought (-1 unknown) */
int  fe_my_brought(void);                                        /* units I brought this session (host's ack), -1 none */
int  fe_list_results(fe_result_info *out, int max);              /* data/fe/results/<run>.json, newest first */
int  fe_result_rows(const char *run, fe_res_row *rows, int max, fe_res_item *items, int imax, int *nitems, fe_mode_info *rules);
int  fe_result_targets(const char *run, fe_save_info *out, int max);   /* saves of the home campaign, newest first */
/* apply (backup first). target "" = default. choice: 0 ask, 1 expedition version, 2 home version for units changed at home.
   0 ok (out_file), 1 = some units changed at home (err names them; call again with a choice), -1 refused (err). */
int  fe_apply_run(const char *run, const char *target, int choice, char *out_file, size_t on, char *err, size_t errn);
