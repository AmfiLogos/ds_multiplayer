/* mp_solo.h - the single-player copy of a world (owner 2026-10-25: "when logging in multiplayer into a world for the first
 * time, i should have the option to create a backup of it which stays single player"). Pure C on the save files (fe_json),
 * no game calls: mpmenu.c asks and calls it, mpsolo_test/ tests it.
 * The copy is a NEW world: the newest save of the world, with a new campaign guid (header + clan record), the clan / campaign
 * name "<pre><name><post>" (the player's language: "Moonlight Brigade (solo)"), its own SaveName / file name, in the same
 * folder. Everything else stays byte for byte in value (fe_json keeps raw text), so the copy loads as the world was. */
#pragma once
#include <stddef.h>

typedef struct { char path[1400]; long long mtime; } mps_save;

/* the newest save (file time) of the world `guid` in dirs (mp_* temporary saves and the header cache left out). 1 found. */
int mps_newest(const char *const *dirs, int nd, const char *guid, mps_save *out);
/* write the copy of the save `src` as world `new_guid` next to it; pre / post: plain UTF-8 text around the old name.
   out_path: the new file. 0 ok; -1 err says why (nothing is left behind). The copy is read back and compared with the
   source before it counts. */
int mps_copy(const char *src, const char *new_guid, const char *pre, const char *post, char *out_path, size_t on, char *err, size_t errn);
/* <app data>/mp_solo.txt, one line per world asked: "<guid>\t<yes|no>\t<copy guid or ->". 1 = asked before. */
int mps_asked(const char *file, const char *guid);
int mps_asked_put(const char *file, const char *guid, int yes, const char *copy_guid);   /* 0 ok */
