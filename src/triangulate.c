/* triangulate.c — top-level ts_triangulate(): correlate -> solve per window.
 * Ports the pure transform of the TrailSense backend. STUB: Pass-2 task. */
#include "internal.h"

int ts_band_prior(uint8_t band, int32_t *tx_dbm, int32_t *a_cdb, int32_t *n_milli) {
    return ts_band_prior_get(band, tx_dbm, a_cdb, n_milli) == 0 ? TS_OK : TS_ERR_BAD_BAND;
}

int ts_triangulate(const ts_obs_t *obs, size_t n_obs,
                   const ts_anchor_t *anchors, size_t n_anchors,
                   ts_position_t *out, size_t out_cap, size_t *out_n) {
    if (!obs || !anchors || !out || !out_n) {
        return TS_ERR_NULL_ARG;
    }
    *out_n = 0;

    /* Fail loud on an unrepresentable band. The wire domain is 0..3, so band 3
     * IS representable, but the path-loss model only defines bands 0..2.
     * ts_pathloss_distance_cm returns -1 for band>2; rather than let that -1
     * become a confident garbage distance downstream, reject the whole batch
     * and emit NO positions — mirroring the backend throw (the TrailSense backend). */
    for (size_t i = 0; i < n_obs; i++) {
        if (obs[i].band > 2) {
            *out_n = 0;
            return TS_ERR_BAD_BAND;
        }
    }

    ts_window_t windows[TS_MAX_GROUPS];
    size_t n_win = 0;
    int rc = ts_correlate(obs, n_obs, windows, TS_MAX_GROUPS, &n_win);
    if (rc != TS_OK) {
        return rc;
    }

    size_t count = 0;
    for (size_t w = 0; w < n_win; w++) {
        ts_position_t pos;
        if (ts_solve_window(&windows[w], anchors, n_anchors, &pos) == 0) {
            if (count >= out_cap) {
                /* Fail loud: more solved positions than the caller's buffer. */
                *out_n = count;
                return TS_ERR_OUT_TOO_SMALL;
            }
            out[count] = pos;
            count++;
        }
    }

    *out_n = count;
    return TS_OK;
}
