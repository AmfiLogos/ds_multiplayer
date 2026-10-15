// win_test_cmds.c - Windows-only rig commands for the cross-play tests (dispatched from rig_exec, see gen_port.py).
// win_exp {"do":"state"|"next"|"confirm"}: the stock expedition panel (ExpeditionPlanPresenter) on this peer.
//   next    = the panel's Next/Go button handler (OnClickNext) -> at the last step the stock TryStartExpedition
//   confirm = run the Action the stock "critical risks" confirmation was opened with (its Confirm button)
#include "dswin.h"
#include "rig.h"

#define W_EXP_ONACTIVATE  0xBE6010   /* ExpeditionPlanPresenter$$OnActivateView */
#define W_EXP_ONCLICKNEXT 0xBE69C0   /* ExpeditionPlanPresenter$$OnClickNext */
#define W_EXP_SHOWRISK    0xBECA90   /* ExpeditionPlanView$$ShowCriticalRiskConfirm(Action onConfirm) */
#define W_ACTION_INVOKE   0x5D2640   /* System.Action$$Invoke */
static uintptr_t h_pres, h_confirm;  /* GC handles */
static int risk_shown, norisk;
#define W_RISK_ISCRITICAL 0xBF28D0  /* ExpeditionPlanRiskData$$get_IsCritical (struct this -> pointer on x64) */
static bool (*o_iscrit)(void *, void *);
/* test switch (win_exp {"norisk":1}): every expedition risk reads as not critical, so the stock TryStartExpedition
   takes its no-risk branch (the MSVC-inlined start) instead of the confirmation */
static bool h_iscrit(void *risk, void *mi) { return norisk ? false : o_iscrit(risk, mi); }
static void (*o_onact)(Obj, void *), (*o_showrisk)(Obj, Obj, void *);
static uintptr_t gch_new(Obj o) { return ((uint32_t (*)(Obj, bool))(ds_base + 0x49DA40))(o, false); }      /* il2cpp_gchandle_new */
static Obj gch_get(uintptr_t h) { return h ? ((Obj (*)(uint32_t))(ds_base + 0x45CF40))((uint32_t)h) : NULL; } /* il2cpp_gchandle_get_target */
static void h_onact(Obj self, void *mi) { h_pres = gch_new(self); rig_log("win_exp: presenter %p active", self); o_onact(self, mi); }
static void h_showrisk(Obj view, Obj on_confirm, void *mi) {
    risk_shown++; h_confirm = gch_new(on_confirm);
    rig_log("win_exp: stock critical-risks confirmation shown");
    o_showrisk(view, on_confirm, mi);
}
/* the stock risk confirmation of TryStartExpedition is a CommonNotice_ShowYesNo UIInput (34): Yes = ParamCallback1 */
void win_note_yesno(Obj yes) {
    risk_shown++; h_confirm = yes ? gch_new(yes) : 0;
    rig_log("win_exp: stock critical-risks confirmation (Yes/No) shown");
}
void win_test_install(void) {
    rig_hook(W_EXP_ONACTIVATE, (void *)h_onact, (void **)&o_onact, "win_exp ExpeditionPlanPresenter.OnActivateView");
    rig_hook(W_RISK_ISCRITICAL, (void *)h_iscrit, (void **)&o_iscrit, "win_exp ExpeditionPlanRiskData.get_IsCritical (test switch)");
    rig_hook(W_EXP_SHOWRISK, (void *)h_showrisk, (void **)&o_showrisk, "win_exp ExpeditionPlanView.ShowCriticalRiskConfirm");
}
char *win_exec(const char *cmd, const char *line) {
    if (!strcmp(cmd, "win_wrapstat") || !strcmp(cmd, "win_scname")) {   /* test: counters of the Windows-only wrappers */
        extern int win_sc_names_calls, win_sc_names_tagged, win_keystream_calls, win_keystream_blocked, ui_win_sc_names_test(void);
        int rc = !strcmp(cmd, "win_scname") ? ui_win_sc_names_test() : 0;
        char *r = malloc(200); snprintf(r, 200, "{\"ok\":true,\"rc\":%d,\"sc_calls\":%d,\"sc_tagged\":%d,\"key_calls\":%d,\"key_blocked\":%d}",
                                        rc, win_sc_names_calls, win_sc_names_tagged, win_keystream_calls, win_keystream_blocked);
        return r;
    }
    if (!strcmp(cmd, "win_evlambda")) {          /* test: the stock choice button's lambda (OnClickChoice inlined), idx */
        extern int ui_win_ev_lambda_test(int idx); const char *p = strstr(line, "\"idx\":"); int idx = p ? atoi(p + 6) : 0;
        char *r = malloc(64); snprintf(r, 64, "{\"ok\":true,\"rc\":%d}", ui_win_ev_lambda_test(idx)); return r;
    }
    if (strcmp(cmd, "win_exp")) return NULL;
    char d[32] = "state"; jstr(line, "do", d, sizeof d);
    double v; if (jnum(line, "norisk", &v)) norisk = v != 0;
    Obj p = gch_get(h_pres);
    Buf b = {0};
    if (!strcmp(d, "next") && p) F(W_EXP_ONCLICKNEXT, void (*)(Obj, void *))(p, NULL);
    if (!strcmp(d, "confirm")) { Obj a = gch_get(h_confirm); if (a) F(W_ACTION_INVOKE, void (*)(Obj, void *))(a, NULL); }
    bput(&b, "{\"ok\":true,\"norisk\":%d,\"presenter\":%s,\"popup_state\":%d,\"started\":%d,\"risk_shown\":%d}",
         norisk, p ? "true" : "false", p ? AT(p, 0x38, int32_t) : -1, p ? (int)AT(p, 0x60, bool) : -1, risk_shown);
    return b.p;
}
