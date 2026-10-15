/* Foreign Expedition save surgery (C port of ../../fe_save.py, same rules; that file stays the reference and
 * tests/test_c_vs_py.py runs both on the same fixtures). Pure C + fe_json, no game dependency. */
#pragma once
#include "fe_json.h"

#define FE_ZERO "00000000-0000-0000-0000-000000000000"
#define FE_MARK "mp.fe."

typedef struct fe_rules { int death_permanent, xp_counts, items_back; } fe_rules;

void  fe_new_guid(char out[37]);
void  fe_new_id(char out[33]);                       /* 32 hex */
void  fe_guid_seed(uint64_t seed);                   /* tests: deterministic guids (0 = random again) */
jv   *fe_comp(const jv *comps, int type);            /* component Data of that Type */
const char *fe_side_of(const jv *save, const char *g);
int   fe_clan_count(const jv *save);

/* {closure, manifest}: closure = {fe_closure, unit{...}, items{...}}; manifest = export_id, player_uid, home campaign,
   save name, version, unit_guid, closure_hash, progress_sig, name_key, level, exp */
jv   *fe_export_unit(const jv *save, const char *g, char *err, size_t errn);
jv   *fe_export_entry(const jv *save, const char *g, const char *player_uid, char *err, size_t errn);
void  fe_progress_sig(const jv *closure, char out[17]);
void  fe_remove_unit(jv *save, const char *g, int with_gear, jv *gone);   /* gone: array to append removed guids (or NULL) */
jv   *fe_difficulty_keys(const jv *save);
int   fe_retarget_difficulty(jv *comps, const jv *want);
/* maps: one object per entry {old guid: new guid} (unit + items) made before the build, or NULL = fresh guids.
   fe_hub_maps makes them (the host needs the new guids before the build, for the owner table). */
jv   *fe_hub_maps(const jv *entries);
jv   *fe_build_hub(const jv *host, const jv *entries, const char *run_id, const jv *maps, jv **run_out, char *err, size_t errn);
/* last_alive: {hub guid: closure} kept by the host from its saves during the run (death not permanent) */
jv   *fe_extract_results(const jv *hub_end, const jv *run, fe_rules r, const char *player_uid, const jv *loot,
                         const jv *last_alive, char *err, size_t errn);
int   fe_result_hash_ok(const jv *result);
/* owner rule: a custom unit (My Units library / wiki link) never enters a regular save. Custom = manifest source "wiki"
   or "library", a library_id, or a home campaign guid of all zeros (or none). fe_apply_results skips such units (report
   line FE_CUSTOM_LINE) and refuses with FE_CUSTOM_REFUSE when every unit of the result is custom. */
#define FE_CUSTOM_LINE "custom unit (My Units / wiki link): stays out of your saves"
#define FE_CUSTOM_REFUSE "These units came from My Units or a wiki link: nothing to bring home (your saves are not changed)."
int   fe_manifest_custom(const jv *manifest);
/* returns the new file's path (malloc) or NULL with err; report: array of strings (or NULL) */
char *fe_apply_results(const jv *home, const jv *result, const jv *manifests, const char *ledger_path, const char *out_dir,
                       const jv *choices, jv *report, char *err, size_t errn);
int   fe_check_refs(const jv *save, const jv *gone, char *out, size_t outn);   /* number of problems; first ones in out */
