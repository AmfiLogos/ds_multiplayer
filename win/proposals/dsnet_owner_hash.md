# Proposal: owner-table hashes in mp status (for the cross-play check). Not applied (src/net + mp_game.c are other lanes').

dsnet_client.c (after dsnet_item_owner):
```c
/* order-independent hashes of the sim owner tables (units, items) as players see them (reg_view) */
static uint64_t tag_hash(const simstate *s, const tag *t, int n) {
    uint64_t h = 0;
    for (int i = 0; i < n; i++) {
        uint64_t x = 0xCBF29CE484222325ULL; int o = reg_view(s, t[i].owner);
        for (int k = 0; k < 16; k++) { x ^= t[i].guid[k]; x *= 0x100000001B3ULL; }
        x ^= (uint8_t)o; x *= 0x100000001B3ULL;
        h += x;                                   /* sum: the table order (swap-remove) does not matter */
    }
    return h;
}
void dsnet_owner_hashes(dsnet_client *c, uint64_t *units, uint64_t *items, int *nunits, int *nitems) {
    pthread_mutex_lock(&c->mu);
    *units = tag_hash(&c->sim, c->sim.own, c->sim.nown); *items = tag_hash(&c->sim, c->sim.item, c->sim.nitem);
    *nunits = c->sim.nown; *nitems = c->sim.nitem;
    pthread_mutex_unlock(&c->mu);
}
```
dsnet.h: `void dsnet_owner_hashes(dsnet_client *c, uint64_t *units, uint64_t *items, int *nunits, int *nitems);`

mp_game.c mp_game_status(): add `"own_hash":"%016llx","item_hash":"%016llx","n_own":%d,"n_item":%d`.
Note: the table changes inside the dsnet command stream, so compare both peers PAUSED at the same step
(mp2_test settle_both()), e.g. in the "units identical on both at the same tick (paused)" check.
