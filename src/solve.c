/* solve.c — port of the TrailSense backend solveWindow step.
 *
 * Resolve each window node to an enrolled anchor (drop missing/unenrolled),
 * require >= 3 participants, project to a centroid-ENU frame, build per-anchor
 * (distance, weight) measurements, robust-multilaterate, and back-transform.
 * confidence + measurement_count key on the PARTICIPATING anchor count (NOT
 * the post-outlier survivor count). */
#include <math.h>
#include <string.h>
#include "internal.h"

/* Minimum participants to emit ANY fix. 0/1 -> no position; ==2 -> approximate
 * bilateration (confidence TS_CONF_BILATERATION); >=3 -> IRLS multilateration.
 * The count at solve time is the ONLY selector: a 3rd enrolled unit auto-routes
 * a group from the 2-node path to the >=3 path with no other config change. */
#define TS_MIN_ANCHORS      2
#define TS_CONF_BILATERATION 40

int ts_solve_window(const ts_window_t *win,
                    const ts_anchor_t *anchors, size_t n_anchors,
                    ts_position_t *out) {
    /* (0) Defensive: never emit a position from an invalid band. The model
     * defines bands 0..2 only; ts_pathloss_distance_cm returns -1 for band>2,
     * which would otherwise be cast into a confident garbage distance below.
     * Keeps the existing 0/-1 contract (the parity harness depends on it). */
    if (win->band > 2) {
        return -1;
    }

    /* (1) Resolve each window node to an enrolled anchor; drop missing/unenrolled. */
    const ts_anchor_t *part_anchor[TS_MAX_NODES_PER_GROUP];
    int8_t            part_rssi[TS_MAX_NODES_PER_GROUP];
    uint32_t          part_ts[TS_MAX_NODES_PER_GROUP];
    size_t            np = 0;

    for (size_t i = 0; i < win->n_nodes; i++) {
        const ts_anchor_t *a = NULL;
        /* Last-wins on duplicate node_id, mirroring the backend's
         * Map<nodeId, AnchorPos> (the TrailSense backend, anchors.get): a later
         * duplicate in the table overwrites an earlier one. Scan all; keep the
         * last match (do NOT break on the first). */
        for (size_t j = 0; j < n_anchors; j++) {
            if (anchors[j].node_id == win->nodes[i].node_id) {
                a = &anchors[j];
            }
        }
        if (a == NULL || a->enrolled == 0) {
            continue;
        }
        part_anchor[np] = a;
        part_rssi[np] = win->nodes[i].rssi;
        part_ts[np] = win->nodes[i].ts_ms;
        np++;
    }

    /* (2) < 2 participants (0 or 1) -> no fix. 2 -> bilateration; >=3 -> IRLS. */
    if (np < TS_MIN_ANCHORS) {
        return -1;
    }

    /* (3) ENU reference = centroid (mean lat/lon) of participants. */
    double sum_lat = 0.0, sum_lon = 0.0;
    for (size_t p = 0; p < np; p++) {
        sum_lat += part_anchor[p]->lat;
        sum_lon += part_anchor[p]->lon;
    }
    double ref_lat = sum_lat / (double)np;
    double ref_lon = sum_lon / (double)np;

    /* (4) newest participating ts_ms (age reference). */
    uint32_t newest_ts = part_ts[0];
    for (size_t p = 1; p < np; p++) {
        if (part_ts[p] > newest_ts) {
            newest_ts = part_ts[p];
        }
    }

    /* (5) per anchor: distance, weight, ENU projection -> measurement. */
    ts_measurement_t meas[TS_MAX_NODES_PER_GROUP];
    int8_t peak_rssi = part_rssi[0];
    for (size_t p = 0; p < np; p++) {
        const ts_anchor_t *a = part_anchor[p];
        int distance_cm = ts_pathloss_distance_cm(win->band, part_rssi[p],
                                                  a->path_loss_a_cdb,
                                                  a->path_loss_n_milli);
        uint32_t age_ms = newest_ts - part_ts[p];
        double weight = ts_compute_weight(part_rssi[p], age_ms);
        double east = 0.0, north = 0.0;
        ts_gps_to_enu_cm(a->lat, a->lon, ref_lat, ref_lon, &east, &north);
        meas[p].east_cm = east;
        meas[p].north_cm = north;
        meas[p].distance_cm = (double)distance_cm;
        meas[p].weight = weight;
        if (part_rssi[p] > peak_rssi) {
            peak_rssi = part_rssi[p];
        }
    }

    /* (5b-2) 2-node path: bilateration. SKIP the geometric-diversity gate (two
     * points are trivially collinear -- that gate is a >=3 concept). Coincident
     * anchors fail loud inside ts_bilaterate_2_anchors (-1), same no-fix
     * contract. Confidence = TS_CONF_BILATERATION, measurement_count = 2. */
    if (np == 2) {
        /* Minimum-baseline gate (P1b): reject a near-coincident anchor pair before
         * solving. The along-baseline semi-axis scales as ~r/d and is unbounded as
         * d->0, so a pair only a few meters apart (e.g. one sensor surveyed twice
         * with GPS jitter) would otherwise emit a confident multi-km ellipse. The
         * coincident-eps guard inside ts_bilaterate_2_anchors only catches EXACT
         * overlap; this is the stronger meaningful-distance floor. NO position on
         * failure (-1), mirroring the >=3 collinearity rejection. */
        double baseline_m = hypot(meas[1].east_cm - meas[0].east_cm,
                                  meas[1].north_cm - meas[0].north_cm) / 100.0;
        if (baseline_m < TS_MIN_BILATERATION_BASELINE_M) {
            return -1;
        }

        double bi_e = 0.0, bi_n = 0.0, bi_acc = 0.0;
        if (ts_bilaterate_2_anchors(meas, np, &bi_e, &bi_n, &bi_acc) != 0) {
            return -1;
        }
        double bi_lat = 0.0, bi_lon = 0.0;
        ts_enu_cm_to_gps(bi_e, bi_n, ref_lat, ref_lon, &bi_lat, &bi_lon);

        /* Error ellipse. n1/n2 are the EFFECTIVE path-loss exponents (same
         * >0-sentinel + 0.5 floor as the distance model). h = bi_acc (off-
         * baseline half-distance). accuracy_m is now the 1-sigma semi-major. */
        double n1 = ts_resolve_pathloss_n(win->band, part_anchor[0]->path_loss_n_milli);
        double n2 = ts_resolve_pathloss_n(win->band, part_anchor[1]->path_loss_n_milli);
        ts_ellipse_t ell;
        ts_bilateration_ellipse(meas[0].east_cm, meas[0].north_cm,
                                meas[1].east_cm, meas[1].north_cm,
                                meas[0].distance_cm, meas[1].distance_cm,
                                n1, n2, bi_e, bi_n, bi_acc,
                                TS_SHADOWING_SIGMA_DB, TS_TX_POWER_SIGMA_DB, &ell);

        memcpy(out->mac_hash, win->mac_hash, 4);
        out->band = win->band;
        out->lat = bi_lat;
        out->lon = bi_lon;
        out->accuracy_m = ell.semi_major_m;
        out->measurement_count = 2;
        out->confidence = TS_CONF_BILATERATION;
        out->peak_rssi = peak_rssi;
        out->error_semi_major_m = ell.semi_major_m;
        out->error_semi_minor_m = ell.semi_minor_m;
        out->error_major_axis_deg = ell.major_axis_deg;
        return 0;
    }

    /* ---- >=3 path (UNCHANGED) ---- */

    /* (5b) geometric-diversity / collinearity gate (backend
     * checkGeometricDiversity). Degenerate geometry (too-tight bounding box or
     * near-collinear anchors) -> no fix, same -1 contract as below. */
    if (!ts_check_geometric_diversity(meas, np)) {
        return -1;
    }

    /* (6) robust multilateration. */
    double east = 0.0, north = 0.0, acc_cm = 0.0;
    if (ts_multilaterate_2d_robust(meas, np, &east, &north, &acc_cm) != 0) {
        return -1;
    }

    /* (7) back-transform with the SAME reference. */
    double lat = 0.0, lon = 0.0;
    ts_enu_cm_to_gps(east, north, ref_lat, ref_lon, &lat, &lon);

    /* (8) fill the position; counts key on PARTICIPATING anchor count. Error
     * ellipse is ISOTROPIC on the >=3 path (3+-node covariance not computed):
     * semi_major == semi_minor == accuracy_m, bearing 0. Position + accuracy_m
     * are UNCHANGED from the pre-feature output. */
    double accuracy_m = acc_cm / 100.0;
    memcpy(out->mac_hash, win->mac_hash, 4);
    out->band = win->band;
    out->lat = lat;
    out->lon = lon;
    out->accuracy_m = accuracy_m;
    out->measurement_count = (int32_t)np;
    out->confidence = (np >= 4) ? 85 : 70;
    out->peak_rssi = peak_rssi;
    out->error_semi_major_m = accuracy_m;
    out->error_semi_minor_m = accuracy_m;
    out->error_major_axis_deg = 0.0;
    return 0;
}
