/* ord_core.h - game-independent part of the orders lane: wire format, event table, (de)serializers.
 * The game is reached only through ord_env, so ord_test/ runs it with fake objects.
 *
 * Wire format v1 (little-endian):
 *   u8 'O', u8 ORD_VERSION, u16 kind, ctx, body
 *   ctx  = i32 map, i32 sel_group, u8 nsel, nsel x guid[16]          (issuer's selection)
 *   body = per kind, see ord_msg below and ord_msg_write().
 *   str  = u16 len + utf8 (len 0xFFFF = null);  opt guid = u8 has (+ guid[16]) */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define ORD_MAGIC    'O'
#define ORD_VERSION  2     /* 2: Set 2 (base) events, build-mode clicks, expedition party. The quick-slot skill keys
                               (ORD_EF_QSLOT) did not bump it (ui_teams.c builds a v2 ChangeMapRequested by hand); an
                               older peer rejects only those 4 orders (trailing bytes, -9) */
#define ORD_MAX_SEL  32     /* selection guids carried per order */
#define ORD_MAX_LIST 64     /* guid lists (IndexList, selector target lists, run near list) */
#define ORD_MAX_ACTS 64     /* candidate actions recorded in one InteractionRunner.Run */
#define ORD_MAX_DELIV 128   /* trade deliveries */
#define ORD_STR      256    /* longest string carried (skill keys, choice keys, ui text) */

typedef void *ord_obj;
typedef struct { uint8_t b[16]; } ord_guid;   /* raw .NET Guid bytes (same memory as rig.h Guid) */

/* ---------------------------------------------------------------- buffers */
typedef struct { uint8_t *p; size_t n, cap; int err; } ord_wbuf;   /* p malloc'd; free(p) after use */
typedef struct { const uint8_t *p; size_t n, i; int err; } ord_rbuf;
void ow_u8(ord_wbuf *w, uint8_t v);
void ow_u16(ord_wbuf *w, uint16_t v);
void ow_i32(ord_wbuf *w, int32_t v);
void ow_guid(ord_wbuf *w, const ord_guid *g);
void ow_str(ord_wbuf *w, const char *s);          /* NULL -> null string */
uint8_t  or_u8(ord_rbuf *r);
uint16_t or_u16(ord_rbuf *r);
int32_t  or_i32(ord_rbuf *r);
void     or_guid(ord_rbuf *r, ord_guid *g);
int      or_str(ord_rbuf *r, char *out, size_t cap); /* -1 null, else length; err on overflow */

/* ---------------------------------------------------------------- Tier A event table */
enum { ORD_F_I32 = 1, ORD_F_U8, ORD_F_GUID, ORD_F_NGUID, ORD_F_NI32, ORD_F_NU8, ORD_F_STR, ORD_F_ENT,
       ORD_F_LGUID, ORD_F_TRADE, ORD_F_V2I };
#define ORD_EF_RAYCAST 1u   /* DropItemRequested: carries the GetRayCastPlayerEntities result */
#define ORD_EF_TRADE   2u   /* TradeCommitRequested: SessionId is rewritten to the local session on replay */
#define ORD_EF_BLOCK   4u   /* never sent: dropped in multiplayer (cheat console, quick-load, ...) */
#define ORD_EF_SELOWNER 8u  /* handler reads the selection: owner = first selected entity */
#define ORD_EF_RECRUIT 16u  /* the replay reports the new unit(s) to the recruit hook (owner = issuer) */
#define ORD_EF_PARTY   32u  /* carries the issuer's ExpeditionPlanPartyContext (active + member Guids) */
#define ORD_EF_EQUIP   64u  /* gear-mode check before sending (host lobby option) */
#define ORD_EF_QSLOT  128u  /* quick-slot index event: carries the issuer's skill key(s), resolved by key on apply */
typedef struct { uint8_t kind; uint16_t off; } ord_field;
typedef struct {
    uint16_t id; const char *ns, *name; uint8_t nf; const ord_field *f; uint32_t flags;
    int16_t map_off;   /* offset of the event's MapType field, -1 none */
    int8_t owner;      /* index in f[] of the owning unit's Guid / IEntity field, -1 none */
} ord_evdef;
extern const ord_evdef *const ord_event_table;
extern const int ord_event_count;
const ord_evdef *ord_event_by_id(uint16_t id);
const ord_evdef *ord_event_by_name(const char *name);

/* ---------------------------------------------------------------- trade request (TradeCommitRequest) */
typedef struct { ord_guid slot; int32_t dest; ord_guid owner; int32_t amount; } ord_delivery;
typedef struct { int32_t map; ord_guid session; int n; ord_delivery d[ORD_MAX_DELIV]; } ord_trade;

/* ---------------------------------------------------------------- game access (real: ord_game.c, fake: ord_test) */
typedef struct ord_env {
    int     (*ent_guid)(ord_obj e, ord_guid *out);             /* 1 ok, 0 null / not an entity */
    ord_obj (*ent_find)(int map, const ord_guid *g);           /* NULL = not found */
    int     (*str_get)(ord_obj s, char *out, size_t cap);      /* -1 null, else utf8 length */
    ord_obj (*str_new)(const char *utf8);
    ord_obj (*ev_new)(const ord_evdef *d);                     /* zeroed event object */
    int     (*lguid_get)(ord_obj list, ord_guid *out, int max);/* -1 null, else count (<= max) */
    ord_obj (*lguid_new)(const ord_guid *g, int n);
    int     (*trade_get)(ord_obj req, ord_trade *out);         /* 0 ok */
    ord_obj (*trade_new)(const ord_trade *t);
    void    (*set_ref)(ord_obj obj, void *slot, ord_obj val);   /* managed pointer store (GC write barrier) */
} ord_env;

/* ---------------------------------------------------------------- Interaction (Refactor.Control.Interaction, 0xD8 bytes)
 * Data form: entities as guids. Offsets: ord_test/OFFSETS.md. */
#define ORD_IA_SIZE 0xD8
typedef struct {
    int32_t type; uint8_t show_user, hide_target; int32_t durability;
    char target_text[ORD_STR]; int target_text_null; char invalid_msg[ORD_STR]; int invalid_msg_null;
    uint8_t is_possible;
    uint8_t has_user, has_target, has_item; ord_guid user, target, item;
    int32_t index, input_type;
    /* UIInput InteractionUIInput (+0x48); TradeCommit, ParamGuidList and the two callbacks are not carried */
    int32_t ui_type, p1, p2, p3;
    char ps1[ORD_STR]; int ps1_null; char ps2[ORD_STR]; int ps2_null;
    ord_guid pg1, pg2; uint8_t pg3_has; ord_guid pg3;
    uint8_t pb1, pb2, pb3;
    int32_t task_type;
} ord_ia;
void ord_ia_from_mem(const ord_env *env, const void *mem, ord_ia *out);
void ord_ia_to_mem(const ord_env *env, const ord_ia *ia, int map, void *mem);   /* mem: ORD_IA_SIZE zeroed,
                                                     NOT on the managed heap (plain stores: the C stack copy passed to Execute) */

/* ---------------------------------------------------------------- skill target selector descriptor */
enum { ORD_SEL_NONE = 0, ORD_SEL_SPECIFIC = 1, ORD_SEL_SPECIFIC_USABLE = 2, ORD_SEL_FROM_LIST = 3,
       ORD_SEL_SELF = 4, ORD_SEL_CLOSEST_HOSTILE = 5, ORD_SEL_GRID = 6, ORD_SEL_TO_GRID = 7 };
typedef struct {
    uint8_t kind, inner;           /* inner: the entity selector kind wrapped by ORD_SEL_TO_GRID */
    uint8_t has_ent; ord_guid ent; /* SPECIFIC / SPECIFIC_USABLE */
    int n; ord_guid list[ORD_MAX_LIST];   /* FROM_LIST */
    int32_t x, y, flag;            /* GRID: GridData.Position / Flag */
} ord_seldesc;

/* ---------------------------------------------------------------- messages */
enum {
    ORD_K_EVENT = 1,       /* Tier A: ev id + fields (+ raycast unit) */
    ORD_K_MOVE_ALL = 2,    /* MoveOrder.All(select, pos) */
    ORD_K_MOVE_TRY = 3,    /* MoveOrder.TryMove(select) */
    ORD_K_MOVE_NEAR = 4,   /* MoveOrder.Near(unit, pos) outside a Run */
    ORD_K_WP_RESERVE = 5,  /* WaypointOrder.Reserve(select, pos) */
    ORD_K_WP_CLEAR = 6,    /* WaypointOrder.ClearWayPoint(select) */
    ORD_K_RUN = 7,         /* InteractionRunner.Run(pos, select): candidate ActionOrder.Execute list + Near list */
    ORD_K_ACTION = 8,      /* ActionOrder.Execute outside a Run */
    ORD_K_USE_SKILL = 9,   /* UseSkillMode.UseSkill(user, target, skill, selector, item) */
    ORD_K_AUTO_SKILL = 10, /* SkillComponent.ChangeAutoSkillData(key) on a unit */
    ORD_K_OBSERVER = 11,   /* ObserverAggregator.Record(InputObserver(type)) */
    ORD_K_MODE_CLICK = 12, /* <BuildMode>.LeftClickInteraction(pos, shift), modes 1..5 (Set 2) */
    ORD_K_MODE_DRAG = 13,  /* <BuildMode>.DragSelection(start, end) */
    ORD_K_MAX
};
typedef struct { int32_t map, sel_group; int nsel; ord_guid sel[ORD_MAX_SEL]; } ord_ctx;
typedef struct { ord_guid unit; ord_ia ia; } ord_act;

typedef struct ord_msg {
    uint16_t kind;
    ord_ctx ctx;
    /* EVENT */
    const ord_evdef *ev; ord_obj evobj; uint8_t has_ray; ord_guid ray;
    /* MOVE_*, WP_*, RUN */
    int32_t x, y;
    ord_guid unit;                         /* MOVE_NEAR, ACTION, AUTO_SKILL */
    /* RUN / ACTION */
    int nact; ord_act act[ORD_MAX_ACTS];   /* ACTION uses act[0] */
    int nnear; ord_guid near_units[ORD_MAX_LIST];
    /* USE_SKILL */
    uint8_t has_user, has_target, has_item; ord_guid user, target, item;
    char key[ORD_STR];                     /* USE_SKILL skill key, AUTO_SKILL key */
    ord_seldesc sel;
    /* OBSERVER */
    int32_t input_type;
    /* EVENT with ORD_EF_PARTY */
    uint8_t party_active; int nparty; ord_guid party[ORD_MAX_SEL];
    /* MODE_CLICK / MODE_DRAG: mode (ControlMode 1..5), pos x,y (+ x2,y2 for drag), shift;
       ConstructionTargetControl state: key (in `key`), rotation, drag start */
    int32_t mode, x2, y2, rotation, dsx, dsy; uint8_t shift, has_ckey;
    /* EVENT with ORD_EF_QSLOT: the issuer's skill key at the event's Index (qkey) and, for MoveQuickSlot, at its
       TargetIndex (qkey2); "" = empty slot or no such slot */
    char qkey[ORD_STR], qkey2[ORD_STR];
    /* Set: 1 = the skill sat in an issuer slot (its handler does not learn it again). Unset: 1 = the key sits in another
       issuer slot too (its handler does not forget the skill). Each copy makes its handler take the same branch. */
    uint8_t qreg;
} ord_msg;

/* Serialize m (EVENT: reads m->evobj through env). 0 ok. */
int ord_msg_write(const ord_env *env, ord_wbuf *w, const ord_msg *m);
/* Parse (EVENT: builds m->evobj through env, entity fields resolved on m->ctx.map). 0 ok, <0 bad. */
int ord_msg_read(const ord_env *env, const uint8_t *p, size_t n, ord_msg *m);

/* Guid of the unit an order is about (mp ownership check); zero = global. Call on a captured message. */
void ord_owner(const ord_env *env, const ord_msg *m, ord_guid *out);

/* Map change captured from the UI (multiplayer: goes to the vote, ui_map_propose, not straight to the net).
 * 1 = enter dungeon (ChangeMapRequested to MapType Dungeon), 2 = return to base (to Base),
 * 3 = next floor (GoDownDungeonRequested), 0 = not a map change. */
int ord_map_change_kind(const ord_msg *m);

/* EVENT helpers (used by the two functions above, exposed for tests) */
int     ord_ev_write(const ord_env *env, ord_wbuf *w, const ord_evdef *d, ord_obj ev);
ord_obj ord_ev_read(const ord_env *env, ord_rbuf *r, const ord_evdef *d, int map);

/* ---------------------------------------------------------------- quick slots by skill key (ORD_EF_QSLOT)
 * ToggleAutoSkill / SetQuickSlot / UnsetQuickSlot / MoveQuickSlot name a quick-slot INDEX. Copies with a different
 * slot layout (weapon-slot mod: 5 slots on its host, 4 on a stock guest) would act on different skills. So the order
 * carries the issuer's skill key at that index, and each copy finds the key in its OWN slots on apply.
 * A copy without a slot for the key applies only the skill part of the handler (auto-use flag, learn / forget). */
#define ORD_QS_MAX 16
typedef struct { int n; char k[ORD_QS_MAX][ORD_STR]; } ord_qslots;   /* one unit's quick slots; "" = empty */
enum { ORD_QS_TOGGLE = 1, ORD_QS_SET, ORD_QS_UNSET, ORD_QS_MOVE };
/* 0 = not a quick-slot index event; else ORD_QS_*. Index offsets: ord_qs_fields. */
int ord_qs_kind(const ord_evdef *d);
/* the event's Index / TargetIndex (-1 none) / Skill string field offsets (-1 none), from the event table */
void ord_qs_fields(const ord_evdef *d, int *idx_off, int *tgt_off, int *skill_off);
/* capture: fill m->qkey / m->qkey2 / m->qreg from the issuer's slots s of the event's unit (NULL = none) */
void ord_qs_capture(const ord_env *env, ord_msg *m, const ord_qslots *s);
/* the local slot for key: hint if it holds key, else the first slot holding it, else -1 ("" key: -1) */
int ord_qs_resolve(const ord_qslots *s, const char *key, int hint);
enum {
    ORD_QP_NONE = 0,   /* nothing to do here (empty slot on the issuer, or a layout-only change with no slot here) */
    ORD_QP_RUN,        /* slot writes pre[], run the handler with Index = idx (TargetIndex = idx2 for Move), writes post[] */
    ORD_QP_AUTO,       /* no slot here: SkillComponent.ChangeAutoSkillData(key) (what the handler does on the issuer) */
    ORD_QP_WRITE       /* layout only (Move with one side missing here): writes post[], no handler */
};
#define ORD_QP_W (ORD_QS_MAX + 2)
typedef struct { int32_t slot; char key[ORD_STR]; } ord_qwrite;     /* slot <- key ("" = empty) */
typedef struct {
    int act;
    int32_t idx, idx2;
    char key[ORD_STR];                                               /* AUTO */
    int npre, npost; ord_qwrite pre[ORD_QP_W], post[ORD_QP_W];
} ord_qplan;
/* what to do on this copy (s = its slots of the event's unit) for a received message m (ORD_EF_QSLOT) */
void ord_qs_plan(const ord_env *env, const ord_msg *m, const ord_qslots *s, ord_qplan *p);
