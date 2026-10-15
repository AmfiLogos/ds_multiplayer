// dsmath_win.c - Windows side of the deterministic math swap: every UCRT math entry point inside
// GameAssembly.dll 25846389 that game code reaches is redirected to the shared dsm_ functions.
// Entry list and how each was found and checked: crt_patch_list.tsv, win/NOTES.md. Enabled with DSMP_MATH=1
// (off until the Mac side rebinds its libm imports the same way, otherwise Mac and Windows would differ more).
#include "dswin.h"
#include "rig.h"
#include "dsmath.h"

typedef struct { uintptr_t rva; void *fn; const char *name; } MathPatch;
static const MathPatch PATCHES[] = {
    { 0x4DB470, (void *)dsm_atan2f, "atan2f" }, { 0x4DC190, (void *)dsm_atan2, "atan2" },
    { 0x4DD6D0, (void *)dsm_atanf, "atanf" },   { 0x4DF3C0, (void *)dsm_atan, "atan" },
    { 0x4DBF50, (void *)dsm_acosf, "acosf" },   { 0x4DBC90, (void *)dsm_acos, "acos" },
    { 0x4DD8D0, (void *)dsm_asin, "asin" },
    { 0x4DA140, (void *)dsm_sinf, "sinf" },     { 0x4D9850, (void *)dsm_cosf, "cosf" },
    { 0x4DADA0, (void *)dsm_sin, "sin" },       { 0x4DA820, (void *)dsm_cos, "cos" },
    { 0x4DDE80, (void *)dsm_tanf, "tanf" },     { 0x4DE430, (void *)dsm_tan, "tan" },
    { 0x4DE2D0, (void *)dsm_expf, "expf" },     { 0x4DFA00, (void *)dsm_exp, "exp" },
    { 0x4E0D00, (void *)dsm_logf, "logf" },     { 0x4DEE90, (void *)dsm_log, "log" },
    { 0x4DBA90, (void *)dsm_log10f, "log10f" }, { 0x4DFE20, (void *)dsm_log10, "log10" },
    { 0x4DC810, (void *)dsm_powf, "powf" },     { 0x4E88A0, (void *)dsm_pow, "pow" },
    { 0x4E03D0, (void *)dsm_sinh, "sinh" },     { 0x4DF610, (void *)dsm_cosh, "cosh" },
    { 0x4E0800, (void *)dsm_tanh, "tanh" },
};

int dsmath_win_install(void) {
    int bad = 0;
    for (size_t i = 0; i < sizeof PATCHES / sizeof *PATCHES; i++) {
        void *orig;
        bad |= rig_hook(PATCHES[i].rva, PATCHES[i].fn, &orig, PATCHES[i].name);
    }
    rig_log("dsmath %s: %d CRT math entries redirected%s", DSMATH_VERSION, (int)(sizeof PATCHES / sizeof *PATCHES), bad ? " (SOME FAILED)" : "");
    return bad;
}
