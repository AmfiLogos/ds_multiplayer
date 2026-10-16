// Windows bootstrap of the rig (replaces src/rig_main.c + src/rig_hook.c + src/rig_win.c).
// Built into baselib.dll, a proxy of the game's own baselib.dll: every export forwards to baselib_orig.dll
// (baselib_exports.def). GameAssembly.dll imports baselib, so our DllMain runs while GameAssembly is mapped
// but before any of its code. Does nothing unless DSRIG_DIR is set (a stock copy behaves like the original).
// Hooks: MinHook (x64 inline, trampolines). Control channel: TCP 127.0.0.1 (rig_sock_win.c).
// DSMP_MODULE: the same file builds multiplayer.dll, a module of the mod loader (~/ds_launcher loader, start/win):
// no DllMain; the loader calls dsl_module_load / dsl_module_early, hooks go through the loader's one engine
// (host->hook_rva), and the loader owns the save-folder redirect (persistentDataPath) and the Steam choice.
#include "dswin.h"
#include "rig.h"
#ifdef DSMP_MODULE
#include "dsl_sdk.h"
static const DSLHost *DH;
static DSLPlugin *DME;
#else
#include "MinHook.h"
#endif
#include <stdarg.h>

uintptr_t ds_base;
char rig_dir[1024];
static FILE *lf;
static CRITICAL_SECTION loglock;

#define WIN_GA_TIMESTAMP 0x6AC9E3E7u      /* GameAssembly.dll 25846389 (PE TimeDateStamp) */
#define WIN_GA_SIZE      0x5951000u       /* SizeOfImage */
#define W_MAINFLOW_UPDATE 0xA3B1D0        /* Refactor.Main.MainFlow$$Update */
#define W_PERSISTENT_PATH 0x38E9DD0       /* UnityEngine.Application$$get_persistentDataPath */
#define W_STRING_NEW      0x49E320        /* il2cpp_string_new (export) */
#define W_STORE_START     0xA3F240        /* Refactor.Main.StoreRuntimeHandler$$Start (Steam gate) */
#define W_CRT_FMA3_FLAG   0x520E4B8       /* UCRT: (flag & 3) == 3 -> FMA3 variants of atan2f, acos, sin ... */

void rig_log(const char *fmt, ...) {
    if (!lf) return;
    EnterCriticalSection(&loglock);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(lf, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap);
    fputc('\n', lf); fflush(lf);
    LeaveCriticalSection(&loglock);
}

/* ---------------------------------------------------------------- hooks (MinHook) */
/* MSVC folds identical method bodies, so two Mac methods can share one Windows address (for example
   Random.Range(int,int) and Random.RandomRangeInt). The second hook on an address is skipped and gets the
   first original, which is the same code. */
typedef struct { uintptr_t rva; void *orig; const char *who; } HookRec;
/* Class-filtered hooks for bodies MSVC folded with other methods. The filter is an asm thunk: it saves the
   argument registers (rcx rdx r8 r9 xmm0-3), asks filt_pick() whether `this` (rcx) is of the expected class,
   restores them and tail-jumps to our detour or to the original, so any signature (stack args too) passes through. */
typedef struct { const char *who, *cls; void *fn, *repl, *orig; const char *ns; void *volatile klass; volatile int tried; } Filt;
static Filt FILT[];
__attribute__((used)) void *filt_pick(uintptr_t self, int i) {
    void *k = self ? *(void **)self : NULL;                 /* Il2CppObject.klass; Il2CppClass.name at +0x10 */
    /* entries with a namespace: the class pointer was resolved at install, compare pointers only. Their bodies are
       also shared by value-type methods, whose `this` is raw struct data: never dereference it */
    if (FILT[i].ns) {                                     /* resolved at the first call (managed code runs: il2cpp is up) */
        extern void *ord_debug_class(const char *, const char *);
        if (!FILT[i].tried) { FILT[i].klass = ord_debug_class(FILT[i].ns, FILT[i].cls); FILT[i].tried = 1;
                              rig_log("filter %s: class %s.%s = %p", FILT[i].who, FILT[i].ns, FILT[i].cls, FILT[i].klass); }
        return k && k == FILT[i].klass ? FILT[i].repl : FILT[i].orig;
    }
    const volatile char *n = k ? *(const char **)((char *)k + 0x10) : NULL, *c = FILT[i].cls;
    int eq = n != NULL;
    for (int j = 0; eq; j++) { if (n[j] != c[j]) eq = 0; else if (!c[j]) break; }
    return eq ? FILT[i].repl : FILT[i].orig;
}
#define FILTFN(i) __attribute__((naked)) static void filt##i(void) { __asm__ volatile( \
    "push %rcx\n push %rdx\n push %r8\n push %r9\n sub $0x68, %rsp\n" \
    "movdqu %xmm0, 0x20(%rsp)\n movdqu %xmm1, 0x30(%rsp)\n movdqu %xmm2, 0x40(%rsp)\n movdqu %xmm3, 0x50(%rsp)\n" \
    "mov $" #i ", %edx\n call filt_pick\n mov %rax, %r11\n" \
    "movdqu 0x20(%rsp), %xmm0\n movdqu 0x30(%rsp), %xmm1\n movdqu 0x40(%rsp), %xmm2\n movdqu 0x50(%rsp), %xmm3\n" \
    "add $0x68, %rsp\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n jmp *%r11\n"); }
FILTFN(0) FILTFN(1) FILTFN(2) FILTFN(3) FILTFN(4) FILTFN(5) FILTFN(6)
static Filt FILT[] = {
    { "QuitToTitleSceneRequested..ctor", "QuitToTitleSceneRequested", (void *)filt0, 0, 0 },   /* 18 methods share it */
    { "CombatInfoComponent.SetCombatTarget", "CombatInfoComponent", (void *)filt1, 0, 0 },    /* 253 */
    { "CombatTargetApplyData..ctor", "CombatTargetApplyData", (void *)filt2, 0, 0 },          /* 137 */
    { "ui UnitObject.SetHUD", "UnitObject", (void *)filt3, 0, 0 },                             /* 158 */
    /* lan_v8 population getters: plain field getters, shared with value-type getters too (pointer compare) */
    { "ui ClanDataContainer.get_CurrentPopulation", "ClanDataContainer", (void *)filt4, 0, 0, "Refactor", 0, 0 },   /* 65 */
    { "ui ClanDataContainer.get_MaxPopulation", "ClanDataContainer", (void *)filt5, 0, 0, "Refactor", 0, 0 },       /* 29 */
    /* core_next fast mp load (mp_game.c h_stabilize): the campaign load's wait coroutine is one body with the map
       transition finalization / abort waits (d__13, d__9); only the load's iterator class gets the skip */
    { "WaitUntilGameStabilizes.MoveNext", "<WaitUntilGameStabilizes>d__103", (void *)filt6, 0, 0 },                /* 3 */
};
#define NFILT 7
static HookRec hooks[512]; static int nhooks;
/* one site: MinHook (own baselib.dll) or the loader's engine (module); 0 = ok */
static int hook_raw(uintptr_t rva, void *repl, void **orig, const char *who) {
#ifdef DSMP_MODULE
    int r = DH->hook_rva(DME, rva, repl, orig, who);
    if (r) rig_log("hook %s at %#llx refused by the loader engine (see dsload.log)", who, (unsigned long long)rva);
    return r;
#else
    static int inited;
    if (!inited) { MH_STATUS s = MH_Initialize(); if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { rig_log("MH_Initialize %d", s); return -1; } inited = 1; }
    void *target = (void *)(ds_base + rva);
    MH_STATUS s = MH_CreateHook(target, repl, orig);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) { rig_log("hook %s at %#llx failed (MinHook %d)", who, (unsigned long long)rva, s); return -1; }
    return 0;
#endif
}
int rig_hook(uintptr_t rva, void *repl, void **orig, const char *who) {
    for (int i = 0; i < nhooks; i++) if (hooks[i].rva == rva) {
        *orig = hooks[i].orig;
        rig_log("hook %s: address %#llx already hooked by %s (identical-code folding); sharing", who, (unsigned long long)rva, hooks[i].who);
        return 0;
    }
    /* trace-only detours whose Mac prototypes split a Guid / Vector2 into registers (arm64 ABI): on Windows x64 the
       struct is a pointer or one packed register, so their argument copies would be wrong. Not installed here
       (lost: these trace kinds in trace.bin). See win/abi_audit.tsv. */
    static const char *const WIN_SKIP[] = { "EntityDataContainer.MoveThing", "EntityDataContainer.OnSpawnThing",
        "InMapDataInterface.GetWorldPosition", "InMapDataInterface.GetCenterOfWorldOccupies",
        "HostileEntityData.AddHostile", "HostileEntityData.RemoveHostile", NULL };
    for (int i = 0; WIN_SKIP[i]; i++) if (!strcmp(who, WIN_SKIP[i])) {
        *orig = (void *)(ds_base + rva);                 /* callers of o_* (none outside the detour) get the original */
        rig_log("hook %s: not installed on Windows (arm64-only argument layout, trace only)", who);
        return 0;
    }
    /* MSVC folded these bodies with other methods (QuitToTitleSceneRequested..ctor shares its code with
       TickRequested..ctor and 16 others). The detour only runs when `this` is of the expected class; every
       other caller goes straight to the original. All three take only integer/pointer arguments. */
    for (int i = 0; i < NFILT; i++) if (!strcmp(who, FILT[i].who)) {
        if (hook_raw(rva, FILT[i].fn, &FILT[i].orig, who)) { rig_log("hook %s (class-filtered) failed", who); return -1; }
        FILT[i].repl = repl; *orig = FILT[i].orig;
        if (nhooks < 512) hooks[nhooks++] = (HookRec){ rva, *orig, who };
        rig_log("hook %s: shared body on Windows, detour filtered to class %s; hooked %#llx", who, FILT[i].cls, (unsigned long long)rva);
        return 0;
    }
    if (hook_raw(rva, repl, orig, who)) return -1;
    if (nhooks < 512) hooks[nhooks++] = (HookRec){ rva, *orig, who };
    rig_log("%s: hooked %#llx", who, (unsigned long long)rva);   /* the full hook list (win/inline_audit.py --log) */
    return 0;
}

/* ---------------------------------------------------------------- game hooks */
static void (*o_update)(void *, void *);
int rig_hooks_late;                     /* Mac: late stub page; MinHook needs no such split */
static void zero_fma3_flag(const char *when);
static void my_update(void *self, void *mi) {
    if (!rig_main_tid) { rig_main_tid = GetCurrentThreadId(); zero_fma3_flag("first MainFlow.Update"); }
    rig_hooks_late = 1;
    rig_frame(self);
    { extern void win_kit_frame(void); win_kit_frame(); }
    if (!getenv("DSRIG_MINIMAL")) rig_det_frame(self);
    o_update(self, mi);
}
static Obj (*o_pdp)(void *);
static Obj my_pdp(void *mi) {
    (void)mi; (void)o_pdp;
    static char p[1100];
    if (!p[0]) snprintf(p, sizeof p, "%s/data", rig_dir);
    return ((Obj (*)(const char *))(ds_base + W_STRING_NEW))(p);
}
/* Steam gate: the Mac copy patches SteamRuntime.get_IsEnabled to false. On Windows that getter's body is
   folded with every "return true" method, so the caller is hooked instead: Start() returns Disabled (0). */
static int (*o_store_start)(Obj, Obj, void *);
static int my_store_start(Obj self, Obj on_signal, void *mi) {
    (void)self; (void)on_signal; (void)mi; (void)o_store_start;
    rig_log("StoreRuntimeHandler.Start -> Disabled (Steam gate)");
    return 0;
}

/* DSW_DEBUG_ORD=1: log the right-click chain (diagnostic for the Windows order-capture gap) */
static void (*o_dbg_oe)(Obj, Obj, void *); static void (*o_dbg_rc)(Obj, uint64_t, void *); static void (*o_dbg_cf)(Obj, uint64_t, Obj, void *);
static void dbg_oe(Obj s, Obj m, void *mi) { rig_log("DBG OnEvent(InteractGridRequested) msg=%p", m); o_dbg_oe(s, m, mi); rig_log("DBG OnEvent done"); }
static void dbg_rc(Obj s, uint64_t p, void *mi) { rig_log("DBG DefaultCommand.RightClick %d,%d", (int)(uint32_t)p, (int)(p >> 32)); o_dbg_rc(s, p, mi); }
static void dbg_cf(Obj s, uint64_t p, Obj sel, void *mi) { rig_log("DBG CommandFlow.Run %d,%d", (int)(uint32_t)p, (int)(p >> 32)); o_dbg_cf(s, p, sel, mi); }

/* MSVC inlined MoveOrder.All and WaypointOrder.ClearWayPoint into CommandFlow.Move (they have NO direct callers
   in GameAssembly.dll; on the Mac, CommandFlow.Move is just these two calls). ord captures player moves on those
   two functions, so a right-click move was never captured on Windows. CommandFlow.Move is replaced by the
   same two calls to the stand-alone copies (same C# code), which run through ord's hooks as on the Mac.
   Not fixed the same way yet: TaskFinishEventHandler.TryCraft (inlined into CraftingTaskFinished): crafted items
   are not owner-tagged on Windows. */
#define W_CF_MOVE   0x91D620      /* Refactor.Control.CommandFlow$$Move(Vector2Int position, ISelectReader select) */
#define W_MOVE_ALL  0x935980      /* Refactor.Control.MoveOrder$$All(ISelectReader, Vector2Int) */
#define W_WP_CLEAR  0x939B30      /* Refactor.Control.WaypointOrder$$ClearWayPoint(ISelectReader) */
static void (*o_cf_move)(Obj, uint64_t, Obj, void *);
static void h_cf_move(Obj self, uint64_t pos, Obj select, void *mi) {
    Obj wp = self ? AT(self, 0x20, Obj) : NULL, mo = self ? AT(self, 0x18, Obj) : NULL;   /* _waypoint, _move */
    if (!wp || !mo) { o_cf_move(self, pos, select, mi); return; }                          /* original throws as before */
    F(W_WP_CLEAR, void (*)(Obj, Obj, void *))(wp, select, NULL);
    F(W_MOVE_ALL, void (*)(Obj, Obj, uint64_t, void *))(mo, select, pos, NULL);
}

/* Same problem for the Teams flow: ExpeditionPlanView.InputOnAccept (hooked by ui_teams) is inlined into
   ExpeditionPlanPresenter.StartExpedition / TryStartExpedition on Windows. On the Mac StartExpedition is:
   if (_isExpeditionStart) return; _isExpeditionStart = true; _view.InputOnAccept(_selectedRegion, _selectedUnits).
   Windows: StartExpedition is replaced by exactly that, calling the stand-alone (hooked) InputOnAccept.
   TryStartExpedition: see h_exp_try below (stock risk confirmation kept). */
#define W_EXP_START     0xBE77E0  /* Refactor.UI.ExpeditionPlanPresenter$$StartExpedition */
#define W_EXP_TRYSTART  0xBE7B20  /* Refactor.UI.ExpeditionPlanPresenter$$TryStartExpedition */
#define W_EXP_ACCEPT    0xBEBC90  /* Refactor.UI.ExpeditionPlanView$$InputOnAccept(int region, List<Guid>) */
extern int mp_game_active(void);
static void (*o_exp_start)(Obj, void *), (*o_exp_try)(Obj, void *);
static void h_exp_start(Obj self, void *mi) {
    Obj view = self ? AT(self, 0x10, Obj) : NULL;
    if (!self || !view) { o_exp_start(self, mi); return; }
    if (AT(self, 0x60, bool)) return;                                   /* _isExpeditionStart */
    AT(self, 0x60, bool) = true;
    F(W_EXP_ACCEPT, void (*)(Obj, int32_t, Obj, void *))(view, AT(self, 0x3C, int32_t), AT(self, 0x48, Obj), NULL);
}
/* TryStartExpedition keeps the stock flow (critical-risk check, confirmation popup whose Action calls the hooked
   StartExpedition). Only its inlined no-risk start is redirected: while it runs, the UIInputs of the inlined
   InputOnAccept (102 = start the map change, then 95) are swallowed in UIInputStream.AddInput and the hooked
   stand-alone InputOnAccept is called instead, so ui_teams decides as on the Mac. */
#define W_UIINPUT_ADD   0xA67CF0  /* Refactor.Main.InputModule.UIInputStream$$AddInput(UIInput input): struct -> by pointer on x64 */
static int in_try, try_started;
static void (*o_uiadd)(Obj, const int32_t *, void *);
static void h_uiadd(Obj stream, const int32_t *input, void *mi) {
    if (in_try && input) rig_log("expedition: TryStartExpedition sends UIInput %d", input[0]);
    if (in_try && input && input[0] == 34) {                         /* CommonNotice_ShowYesNo = stock critical-risks confirmation */
        extern void win_note_yesno(Obj yes); win_note_yesno(*(Obj *)((char *)input + 0x78));   /* UIInput.ParamCallback1 */
    }
    if (in_try && input && (input[0] == 102 || (try_started && input[0] == 95))) {   /* UIInput.UIInputType @0x0 */
        if (input[0] == 102) try_started = 1;
        rig_log("expedition: inlined InputOnAccept UIInput %d swallowed (redirected to the hooked InputOnAccept)", input[0]);
        return;
    }
    o_uiadd(stream, input, mi);
}
static void h_exp_try(Obj self, void *mi) {
    if (!mp_game_active() || !self) { o_exp_try(self, mi); return; }
    in_try = 1; try_started = 0;
    rig_log("expedition: TryStartExpedition (stock, multiplayer) started=%d", (int)AT(self, 0x60, bool));
    o_exp_try(self, mi);                                                  /* stock: risks popup or inlined start */
    in_try = 0;
    if (try_started) {                                                    /* no-risk path: start as on the Mac */
        Obj view = AT(self, 0x10, Obj);
        if (view) F(W_EXP_ACCEPT, void (*)(Obj, int32_t, Obj, void *))(view, AT(self, 0x3C, int32_t), AT(self, 0x48, Obj), NULL);
    }
}

/* rig_win.c stand-ins: -batchmode has no window on Windows either */
void rig_win_install(void) {}
void rig_win_space_install(void) {}
void rig_win_frame(void) {}
void rig_win_place(void) {}
void rig_win_space_frame(void) {}

static int check_ga(HMODULE m) {
    IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)m;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)m + d->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != WIN_GA_TIMESTAMP || nt->OptionalHeader.SizeOfImage != WIN_GA_SIZE) {
        rig_log("GameAssembly timestamp %#x size %#x is not build %s: rig inactive",
                (unsigned)nt->FileHeader.TimeDateStamp, (unsigned)nt->OptionalHeader.SizeOfImage, RIG_BUILD);
        return 0;
    }
    return 1;
}

/* Same CRT math path on every CPU. GameAssembly's CRT sets the flag in its own init, which runs after our
   DllMain, so it is cleared again on the first MainFlow.Update. */
static void zero_fma3_flag(const char *when) {
    if (getenv("DSRIG_KEEP_FMA3")) return;
    DWORD old; uint32_t *flag = (uint32_t *)(ds_base + W_CRT_FMA3_FLAG);
    if (VirtualProtect(flag, 4, PAGE_READWRITE, &old)) { rig_log("CRT FMA3 flag was %u -> 0 (%s)", *flag, when); *flag = 0; VirtualProtect(flag, 4, old, &old); }
}

static void install(HMODULE ga) {
    ds_base = (uintptr_t)ga;
    rig_log("GameAssembly mapped at %p (build %s)", (void *)ds_base, RIG_BUILD);
    zero_fma3_flag("load");
    int bad = 0;
    bad |= rig_hook(W_MAINFLOW_UPDATE, (void *)my_update, (void **)&o_update, "MainFlow.Update");
#ifndef DSMP_MODULE                                                /* module: the loader's start part does both */
    bad |= rig_hook(W_PERSISTENT_PATH, (void *)my_pdp, (void **)&o_pdp, "Application.get_persistentDataPath");
    { extern int kit_mode;                                          /* the test kit runs on the player's real Steam */
      if (!kit_mode || getenv("DSMP_KIT_NOSTEAM")) bad |= rig_hook(W_STORE_START, (void *)my_store_start, (void **)&o_store_start, "StoreRuntimeHandler.Start"); }
#else
    (void)my_pdp; (void)my_store_start;
#endif
    if (bad) rig_log("hook install failed: the rig will not answer game commands");
    if (getenv("DSW_DEBUG_ORD")) {
        rig_hook(0xAC7F00, (void *)dbg_oe, (void **)&o_dbg_oe, "DBG PlayerControlEventHandler.OnEvent(InteractGridRequested)");
        rig_hook(0x927E50, (void *)dbg_rc, (void **)&o_dbg_rc, "DBG DefaultCommand.RightClick");
        rig_hook(0x91D940, (void *)dbg_cf, (void **)&o_dbg_cf, "DBG CommandFlow.Run");
    }
    { extern int dsmath_win_install(void); const char *m = getenv("DSMP_MATH");   /* shared math ON by default since v9 */
      char off[1100]; snprintf(off, sizeof off, "%s/dsmp_math.off", rig_dir);       /* off: DSMP_MATH=0 or <rig dir>/dsmp_math.off */
      if (m && *m == '0') rig_log("dsmath: off (DSMP_MATH=0)");
      else if (GetFileAttributesA(off) != INVALID_FILE_ATTRIBUTES) rig_log("dsmath: off (%s)", off);
      else dsmath_win_install(); }
    const char *port = getenv("DSRIG_PORT");
    if (getenv("DSRIG_MINIMAL")) { rig_log("DSRIG_MINIMAL: det / trn / mp / ord / ui hooks NOT installed"); rig_sock_start(port && *port ? port : "47811"); return; }
    /* same order as src/rig_main.c on_image */
    extern void trn_install(void), mp_game_install(void), ord_install(void), ui_install(void), ui_teams_install(void), mpmenu_install(void);
    rig_det_install();
    trn_install();
    mp_game_install();
    ord_install();
    ui_install();
    ui_teams_install();
    mpmenu_install();
#ifndef DSMP_MODULE
    { /* rig baselib.dll: Custom Modes (custom/cm_mp.c, same DLL here: -DCM_SAME_IMAGE): FE + PvP register through mp_api, PvP
         hooks (src/rig_main.c loads custom-modes.dylib at this place). As a loader module: the custom-modes module starts it. */
      extern const void *mp_api_get(uint32_t version); extern int cm_mp_start(const void *host, void *self, const void *api);
      int r = cm_mp_start(NULL, NULL, mp_api_get(1)); rig_log("custom-modes: started in this DLL (rc %d)", r); }
#endif
    { extern void ord_win_craft_install(void); ord_win_craft_install(); }
    { extern void ui_win_install(void), mpmenu_win_install(void); ui_win_install(); mpmenu_win_install(); }
    { extern void win_test_install(void); win_test_install(); }
    rig_hook(W_EXP_START, (void *)h_exp_start, (void **)&o_exp_start, "ExpeditionPlanPresenter.StartExpedition (un-inlined)");
    rig_hook(W_EXP_TRYSTART, (void *)h_exp_try, (void **)&o_exp_try, "ExpeditionPlanPresenter.TryStartExpedition (inlined start redirected)");
    rig_hook(W_UIINPUT_ADD, (void *)h_uiadd, (void **)&o_uiadd, "UIInputStream.AddInput (expedition start redirect)");
    rig_hook(W_CF_MOVE, (void *)h_cf_move, (void **)&o_cf_move, "CommandFlow.Move (un-inlined for ord capture)");
    rig_sock_start(port && *port ? port : "47811");
}

#ifdef DSMP_MODULE
/* ---------------------------------------------------------------- loader module entry points */
DSL_PLUGIN();

/* crash note (Windows): the first access violation / illegal instruction of the process goes to rig.log with the faulting
   address and every stack value that points into a module (name + offset), then the normal handling goes on. */
static void crash_where(char *out, size_t n, uintptr_t a) {
    HMODULE m = NULL; char nm[MAX_PATH] = "?";
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, nm, sizeof nm); const char *b = strrchr(nm, '\\');
        snprintf(out, n, "%s+%#llx", b ? b + 1 : nm, (unsigned long long)(a - (uintptr_t)m));
    } else out[0] = 0;
}
static LONG WINAPI crash_note(EXCEPTION_POINTERS *e) {
    static volatile LONG once;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if ((c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_ILLEGAL_INSTRUCTION) || InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    char w[300]; crash_where(w, sizeof w, (uintptr_t)e->ContextRecord->Rip);
    rig_log("CRASH %#lx at %p %s (access %llu to %p), thread %lu; stack:", (unsigned long)c, (void *)e->ContextRecord->Rip, w,
            (unsigned long long)e->ExceptionRecord->ExceptionInformation[0], (void *)e->ExceptionRecord->ExceptionInformation[1], GetCurrentThreadId());
    uintptr_t *sp = (uintptr_t *)e->ContextRecord->Rsp; int k = 0;
    for (int i = 0; i < 256 && k < 24; i++) {
        MEMORY_BASIC_INFORMATION mi; if (!VirtualQuery(sp + i, &mi, sizeof mi) || mi.State != MEM_COMMIT) break;
        crash_where(w, sizeof w, sp[i]);
        if (w[0]) { rig_log("  [rsp+%#x] %p %s", i * 8, (void *)sp[i], w); k++; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DSL_EXPORT int dsl_module_load(const DSLHost *host, DSLPlugin *self) {
    AddVectoredExceptionHandler(0, crash_note);
    DH = host; DME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d || !*d) { host->log(self, "DSRIG_DIR not set: multiplayer inactive"); return 1; }
    if (!host->game_build || strcmp(host->game_build, RIG_BUILD)) { host->log(self, "game build %s is not %s", host->game_build ? host->game_build : "?", RIG_BUILD); return 2; }
    snprintf(rig_dir, sizeof rig_dir, "%s", d);
    for (char *p = rig_dir; *p; p++) if (*p == '\\') *p = '/';
    InitializeCriticalSection(&loglock);
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    lf = fopen(lp, "a");
    rig_log("---- rig start pid %lu (windows, loader module %s)", (unsigned long)GetCurrentProcessId(), host->plugin_id(self));
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    dsw_net_init();
    host->log(self, "rig log %s", lp);
    return 0;
}

DSL_EXPORT int dsl_module_early(const DSLHost *host, DSLPlugin *self) {
    HMODULE m = GetModuleHandleA("GameAssembly.dll");
    if (!m || (uintptr_t)m != host->ga_base || !check_ga(m)) { host->log(self, "GameAssembly is not build %s: multiplayer off", RIG_BUILD); return 3; }
    install(m);
    host->log(self, "multiplayer started (control port %s)", getenv("DSRIG_PORT") ? getenv("DSRIG_PORT") : "47811");
    return 0;
}

/* World check (dsload, mods per world): a guest loading the host's world is checked by the join (HELLO) instead. */
#include "mp_game.h"
DSL_EXPORT int dsl_world_check_skip(void) {
    mp_session *s = mp_game_session();
    return s && mp_server_port(s) == 0;
}
#else
/* GameAssembly is normally already in the module list when our DllMain runs (we are its import);
   if not, a thread waits for it (the first game code we care about runs seconds later). */
static DWORD WINAPI wait_ga(LPVOID arg) {
    (void)arg;
    for (int i = 0; i < 60000; i++) {
        HMODULE m = GetModuleHandleA("GameAssembly.dll");
        if (m) { if (check_ga(m)) install(m); return 0; }
        Sleep(1);
    }
    rig_log("GameAssembly.dll never loaded");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)h; (void)res;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    extern int win_kit_detect(char *dir, size_t n);
    const char *d = getenv("DSRIG_DIR");
    if (d && *d) snprintf(rig_dir, sizeof rig_dir, "%s", d);
    else if (!win_kit_detect(rig_dir, sizeof rig_dir)) return TRUE;   /* neither the rig nor the test kit: stock game */
    InitializeCriticalSection(&loglock);
    for (char *p = rig_dir; *p; p++) if (*p == '\\') *p = '/';
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    lf = fopen(lp, "a");
    { extern int kit_mode; rig_log("---- rig start pid %lu (windows, baselib proxy%s)", (unsigned long)GetCurrentProcessId(), kit_mode ? ", TEST KIT recorder mode" : ""); }
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    dsw_net_init();
    HMODULE m = GetModuleHandleA("GameAssembly.dll");
    if (m) { if (check_ga(m)) install(m); }
    else CloseHandle(CreateThread(NULL, 0, wait_ga, NULL, 0, NULL));
    return TRUE;
}
#endif
