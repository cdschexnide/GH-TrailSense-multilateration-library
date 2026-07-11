/* correlate.c — port of the TrailSense backend correlate step.
 *
 * One window per distinct (mac_hash, band). Within a window, per distinct
 * node_id the latest sample by ts_ms wins (the backend overwrites only on a
 * strictly-greater receivedAt; a true tie keeps the first-seen sample — see
 * the TrailSense backend). A window is emitted when >= 2 distinct nodes participate;
 * this off-device path lowers the firmware n_anchors >= 3 gate to reach the
 * solver's 2-node bilateration path (count-based routing lives in solve). */
#include <string.h>
#include "internal.h"

/* Emit a window at >= 2 distinct nodes: 2 feed the solver's bilateration
 * (approximate) path, >= 3 feed IRLS. This off-device path intentionally lowers
 * the firmware's n_anchors >= 3 gate to support 2-unit deployments; the
 * count-based routing itself lives in ts_solve_window. */
#define TS_MIN_ANCHORS 2

bool ts_build_window(const ts_obs_t *obs, size_t n_obs,
                     const uint8_t mac_hash[4], uint8_t band, ts_window_t *out) {
    memcpy(out->mac_hash, mac_hash, 4);
    out->band = band;
    out->n_nodes = 0;

    for (size_t i = 0; i < n_obs; i++) {
        const ts_obs_t *o = &obs[i];
        if (o->band != band || memcmp(o->mac_hash, mac_hash, 4) != 0) {
            continue;
        }

        /* Per distinct node_id, keep the latest-by-ts_ms sample. */
        size_t slot = out->n_nodes;
        for (size_t k = 0; k < out->n_nodes; k++) {
            if (out->nodes[k].node_id == o->node_id) {
                slot = k;
                break;
            }
        }

        if (slot < out->n_nodes) {
            /* Existing node: overwrite only on a strictly-later sample. */
            if (o->ts_ms > out->nodes[slot].ts_ms) {
                out->nodes[slot].rssi = o->rssi;
                out->nodes[slot].ts_ms = o->ts_ms;
            }
        } else if (out->n_nodes < TS_MAX_NODES_PER_GROUP) {
            /* New distinct node (cap at TS_MAX_NODES_PER_GROUP). */
            out->nodes[slot].node_id = o->node_id;
            out->nodes[slot].rssi = o->rssi;
            out->nodes[slot].ts_ms = o->ts_ms;
            out->n_nodes++;
        }
    }

    return out->n_nodes >= TS_MIN_ANCHORS;
}

int ts_correlate(const ts_obs_t *obs, size_t n_obs,
                 ts_window_t *win, size_t win_cap, size_t *n_win) {
    /* Enumerate the distinct (mac_hash, band) keys (cap at TS_MAX_GROUPS),
     * then build one window per key via ts_build_window (single source of
     * truth for the grouping rule). */
    struct { uint8_t mac[4]; uint8_t band; } keys[TS_MAX_GROUPS];
    size_t n_keys = 0;

    for (size_t i = 0; i < n_obs; i++) {
        const ts_obs_t *o = &obs[i];
        bool seen = false;
        for (size_t k = 0; k < n_keys; k++) {
            if (keys[k].band == o->band && memcmp(keys[k].mac, o->mac_hash, 4) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen && n_keys < TS_MAX_GROUPS) {
            memcpy(keys[n_keys].mac, o->mac_hash, 4);
            keys[n_keys].band = o->band;
            n_keys++;
        }
    }

    size_t out_n = 0;
    for (size_t k = 0; k < n_keys; k++) {
        ts_window_t w;
        if (ts_build_window(obs, n_obs, keys[k].mac, keys[k].band, &w)) {
            if (out_n < win_cap) {
                win[out_n] = w;
                out_n++;
            }
        }
    }

    *n_win = out_n;
    return TS_OK;
}
