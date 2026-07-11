/*
 * parity_test.c — the parity gate against the bench-proven backend.
 *
 * Consumes test/fixtures.gen.h (generated from the LIVE TrailSense backend
 * functions, SHA-stamped) and asserts that each C
 * module reproduces the backend's output.
 *
 * Comparison rules (per design §8):
 *   - EXACT on integer / enum / count / string fields and on the null-vs-nonnull
 *     gates (ret 0 / -1, position counts).
 *   - <=1 cm tolerance on floats. lat/lon are compared by projecting the degree
 *     delta to meters (|dlat|*111320 <= 0.01 m, longitude scaled by cos(lat));
 *     cm fields compare |a-b| <= 1.0 cm; meter fields (accuracy_m and the error-
 *     ellipse semi-axes, now bounded) compare |a-b| <= 0.01 m; bearings <= 1e-6.
 *
 * Usage:
 *   parity_test            run all groups
 *   parity_test <group>    run one of:
 *     pathloss enu compute_weight multilaterate bilaterate ellipse correlate solve triangulate
 *
 * Exit nonzero if any run group fails.
 *
 * NOTE on multilaterate: the C contract (ts_multilaterate_2d_robust) does NOT
 * expose the post-outlier survivor count, so this test verifies outlier
 * rejection through the resulting position + accuracy (which fully encode it).
 * The backend survivor count is carried in the fixture for transparency only.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "trailsense_triangulate.h"
#include "internal.h"
#include "fixtures.gen.h"

/* ------------------------------------------------------------------ helpers */

#define DEG_TO_RAD 0.0174532925199433
#define TOL_CM 1.0    /* <=1 cm on cm-valued floats */
#define TOL_M 0.01    /* <=0.01 m == 1 cm on meter-valued floats */
#define TOL_EXACT 1e-6 /* integer-valued doubles (computeWeight) */
#define TOL_DEG 1e-6   /* absolute tol on bearings in [0,180) */

static bool approx(double a, double b, double tol) { return fabs(a - b) <= tol; }

/* lat/lon parity: project the degree delta to meters and require <=1 cm. */
static bool geo_close(double lat, double lon, double exp_lat, double exp_lon) {
    double dlat_m = fabs(lat - exp_lat) * 111320.0;
    double cos_lat = cos(exp_lat * DEG_TO_RAD);
    double dlon_m = fabs(lon - exp_lon) * 111320.0 * fabs(cos_lat);
    return dlat_m <= TOL_M && dlon_m <= TOL_M;
}

static bool mac_eq(const uint8_t a[4], const unsigned char b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/* Sort a node list by node_id (small N, insertion sort on a copy). */
static void sort_nodes(ts_fx_corr_node_t *n, int count) {
    for (int i = 1; i < count; i++) {
        ts_fx_corr_node_t key = n[i];
        int j = i - 1;
        while (j >= 0 && n[j].node_id > key.node_id) {
            n[j + 1] = n[j];
            j--;
        }
        n[j + 1] = key;
    }
}

/* ------------------------------------------------------------------ pathloss */

static int run_pathloss(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_PATHLOSS_N; i++) {
        const ts_fx_pathloss_t *c = &TS_FX_PATHLOSS[i];
        int got = ts_pathloss_distance_cm((uint8_t)c->band, (int8_t)c->rssi,
                                          c->a_cdb, c->n_milli);
        if (got != c->expected_distance_cm) {
            printf("  FAIL pathloss[%s]: got %d, expected %d (band=%d rssi=%d a=%d n=%d)\n",
                   c->name, got, c->expected_distance_cm, c->band, c->rssi, c->a_cdb, c->n_milli);
            fails++;
        }
    }
    for (size_t i = 0; i < TS_FX_BANDPRIOR_N; i++) {
        const ts_fx_bandprior_t *c = &TS_FX_BANDPRIOR[i];
        int32_t tx = 0, a = 0, n = 0;
        int rc = ts_band_prior_get((uint8_t)c->band, &tx, &a, &n);
        if (rc != 0 || tx != c->tx_dbm || a != c->a_cdb || n != c->n_milli) {
            printf("  FAIL bandprior[%s]: rc=%d got tx=%d a=%d n=%d, expected tx=%d a=%d n=%d\n",
                   c->name, rc, tx, a, n, c->tx_dbm, c->a_cdb, c->n_milli);
            fails++;
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ enu */

static int run_enu(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_ENU_FWD_N; i++) {
        const ts_fx_enu_fwd_t *c = &TS_FX_ENU_FWD[i];
        double e = 0, n = 0;
        ts_gps_to_enu_cm(c->lat, c->lon, c->ref_lat, c->ref_lon, &e, &n);
        if (!approx(e, c->exp_east_cm, TOL_CM) || !approx(n, c->exp_north_cm, TOL_CM)) {
            printf("  FAIL enu_fwd[%s]: got (%.6f,%.6f), expected (%.6f,%.6f)\n",
                   c->name, e, n, c->exp_east_cm, c->exp_north_cm);
            fails++;
        }
    }
    for (size_t i = 0; i < TS_FX_ENU_INV_N; i++) {
        const ts_fx_enu_inv_t *c = &TS_FX_ENU_INV[i];
        double lat = 0, lon = 0;
        ts_enu_cm_to_gps(c->east_cm, c->north_cm, c->ref_lat, c->ref_lon, &lat, &lon);
        if (!geo_close(lat, lon, c->exp_lat, c->exp_lon)) {
            printf("  FAIL enu_inv[%s]: got (%.9f,%.9f), expected (%.9f,%.9f)\n",
                   c->name, lat, lon, c->exp_lat, c->exp_lon);
            fails++;
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ compute_weight */

static int run_compute_weight(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_CW_N; i++) {
        const ts_fx_cw_t *c = &TS_FX_CW[i];
        double got = ts_compute_weight((int8_t)c->rssi, (uint32_t)c->age_ms);
        if (!approx(got, c->exp_weight, TOL_EXACT)) {
            printf("  FAIL compute_weight[%s]: got %.6f, expected %.6f (rssi=%d age=%u)\n",
                   c->name, got, c->exp_weight, c->rssi, c->age_ms);
            fails++;
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ multilaterate */

static int run_multilaterate(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_ML_N; i++) {
        const ts_fx_ml_t *c = &TS_FX_ML[i];
        ts_measurement_t m[8];
        for (int k = 0; k < c->n_meas; k++) {
            m[k].east_cm = c->meas[k].east_cm;
            m[k].north_cm = c->meas[k].north_cm;
            m[k].distance_cm = c->meas[k].distance_cm;
            m[k].weight = c->meas[k].weight;
        }
        double east = 0, north = 0, acc = 0;
        int rc = ts_multilaterate_2d_robust(m, (size_t)c->n_meas, &east, &north, &acc);
        int got_ret = (rc == 0) ? 0 : -1;
        if (got_ret != c->exp_ret) {
            printf("  FAIL multilaterate[%s]: ret got %d, expected %d\n",
                   c->name, got_ret, c->exp_ret);
            fails++;
            continue;
        }
        if (c->exp_ret == 0) {
            if (!approx(east, c->exp_east_cm, TOL_CM) ||
                !approx(north, c->exp_north_cm, TOL_CM) ||
                !approx(acc, c->exp_accuracy_cm, TOL_CM)) {
                printf("  FAIL multilaterate[%s]: got (e=%.4f n=%.4f acc=%.4f), "
                       "expected (e=%.4f n=%.4f acc=%.4f)\n",
                       c->name, east, north, acc,
                       c->exp_east_cm, c->exp_north_cm, c->exp_accuracy_cm);
                fails++;
            }
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ bilaterate */

static int run_bilaterate(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_BI_N; i++) {
        const ts_fx_bi_t *c = &TS_FX_BI[i];
        ts_measurement_t m[2];
        for (int k = 0; k < c->n_meas; k++) {
            m[k].east_cm = c->meas[k].east_cm;
            m[k].north_cm = c->meas[k].north_cm;
            m[k].distance_cm = c->meas[k].distance_cm;
            m[k].weight = c->meas[k].weight;
        }
        double east = 0, north = 0, acc = 0;
        int rc = ts_bilaterate_2_anchors(m, (size_t)c->n_meas, &east, &north, &acc);
        int got_ret = (rc == 0) ? 0 : -1;
        if (got_ret != c->exp_ret) {
            printf("  FAIL bilaterate[%s]: ret got %d, expected %d\n",
                   c->name, got_ret, c->exp_ret);
            fails++;
            continue;
        }
        if (c->exp_ret == 0) {
            if (!approx(east, c->exp_east_cm, TOL_CM) ||
                !approx(north, c->exp_north_cm, TOL_CM) ||
                !approx(acc, c->exp_accuracy_cm, TOL_CM)) {
                printf("  FAIL bilaterate[%s]: got (e=%.4f n=%.4f acc=%.4f), "
                       "expected (e=%.4f n=%.4f acc=%.4f)\n",
                       c->name, east, north, acc,
                       c->exp_east_cm, c->exp_north_cm, c->exp_accuracy_cm);
                fails++;
            }
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ ellipse */

static int run_ellipse(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_ELLIPSE_N; i++) {
        const ts_fx_ellipse_t *c = &TS_FX_ELLIPSE[i];
        ts_ellipse_t got;
        ts_bilateration_ellipse(c->a1e, c->a1n, c->a2e, c->a2n, c->r1, c->r2,
                                c->n1, c->n2, c->foot_e, c->foot_n, c->h,
                                c->sigma_shadow, c->sigma_tx, &got);
        /* meter axes: absolute 1 cm (now bounded). bearing: absolute 1e-6. */
        if (!approx(got.semi_major_m, c->exp_semi_major_m, TOL_M) ||
            !approx(got.semi_minor_m, c->exp_semi_minor_m, TOL_M) ||
            !approx(got.major_axis_deg, c->exp_major_axis_deg, TOL_DEG)) {
            printf("  FAIL ellipse[%s]: got (maj=%.6g min=%.6g deg=%.9f), "
                   "expected (maj=%.6g min=%.6g deg=%.9f)\n",
                   c->name, got.semi_major_m, got.semi_minor_m, got.major_axis_deg,
                   c->exp_semi_major_m, c->exp_semi_minor_m, c->exp_major_axis_deg);
            fails++;
        }
    }
    return fails;
}

/* ------------------------------------------------------------------ correlate */

/* Compare two node lists (as sets, sorted by node_id). Returns true on match. */
static bool nodes_match(const ts_group_node_t *got, size_t got_n,
                        const ts_fx_corr_node_t *exp, int exp_n) {
    if ((int)got_n != exp_n) return false;
    ts_fx_corr_node_t a[32], b[32];
    for (size_t i = 0; i < got_n; i++) {
        a[i].node_id = got[i].node_id;
        a[i].rssi = got[i].rssi;
        a[i].ts_ms = got[i].ts_ms;
    }
    for (int i = 0; i < exp_n; i++) b[i] = exp[i];
    sort_nodes(a, (int)got_n);
    sort_nodes(b, exp_n);
    for (int i = 0; i < exp_n; i++) {
        if (a[i].node_id != b[i].node_id || a[i].rssi != b[i].rssi ||
            a[i].ts_ms != b[i].ts_ms) {
            return false;
        }
    }
    return true;
}

static int run_correlate(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_CORR_N; i++) {
        const ts_fx_corr_t *c = &TS_FX_CORR[i];
        ts_obs_t obs[16];
        for (int k = 0; k < c->n_obs; k++) {
            obs[k].node_id = c->obs[k].node_id;
            memcpy(obs[k].mac_hash, c->obs[k].mac, 4);
            obs[k].band = (uint8_t)c->obs[k].band;
            obs[k].rssi = (int8_t)c->obs[k].rssi;
            obs[k].ts_ms = c->obs[k].ts_ms;
        }
        ts_window_t win[TS_MAX_GROUPS];
        size_t n_win = 0;
        int rc = ts_correlate(obs, (size_t)c->n_obs, win, TS_MAX_GROUPS, &n_win);
        if (rc != TS_OK) {
            printf("  FAIL correlate[%s]: ts_correlate returned %d\n", c->name, rc);
            fails++;
            continue;
        }
        if ((int)n_win != c->n_wins) {
            printf("  FAIL correlate[%s]: got %zu windows, expected %d\n",
                   c->name, n_win, c->n_wins);
            fails++;
            continue;
        }
        /* Match each expected window to a distinct produced window (as a set). */
        bool used[8] = {false};
        bool ok = true;
        for (int e = 0; e < c->n_wins; e++) {
            const ts_fx_corr_win_t *ew = &c->wins[e];
            bool matched = false;
            for (size_t g = 0; g < n_win; g++) {
                if (used[g]) continue;
                if (win[g].band != ew->band || !mac_eq(win[g].mac_hash, ew->mac)) continue;
                if (nodes_match(win[g].nodes, win[g].n_nodes, ew->nodes, ew->n_nodes)) {
                    used[g] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                printf("  FAIL correlate[%s]: expected window (band=%d, %d nodes) not produced\n",
                       c->name, ew->band, ew->n_nodes);
                ok = false;
            }
        }
        if (!ok) fails++;
    }
    return fails;
}

/* ------------------------------------------------------------------ solve */

static void fill_window(ts_window_t *w, const unsigned char mac[4], int band,
                        const ts_fx_corr_node_t *nodes, int n_nodes) {
    memcpy(w->mac_hash, mac, 4);
    w->band = (uint8_t)band;
    w->n_nodes = (size_t)n_nodes;
    for (int i = 0; i < n_nodes; i++) {
        w->nodes[i].node_id = nodes[i].node_id;
        w->nodes[i].rssi = (int8_t)nodes[i].rssi;
        w->nodes[i].ts_ms = nodes[i].ts_ms;
    }
}

static void fill_anchors(ts_anchor_t *out, const ts_fx_solve_anchor_t *in, int n) {
    for (int i = 0; i < n; i++) {
        out[i].node_id = in[i].node_id;
        out[i].lat = in[i].lat;
        out[i].lon = in[i].lon;
        out[i].path_loss_a_cdb = in[i].a_cdb;
        out[i].path_loss_n_milli = in[i].n_milli;
        out[i].enrolled = (uint8_t)in[i].enrolled;
    }
}

static int run_solve(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_SOLVE_N; i++) {
        const ts_fx_solve_t *c = &TS_FX_SOLVE[i];
        ts_window_t win;
        fill_window(&win, c->mac, c->band, c->nodes, c->n_nodes);
        ts_anchor_t anchors[16];
        fill_anchors(anchors, c->anchors, c->n_anchors);
        ts_position_t pos;
        memset(&pos, 0, sizeof(pos));
        int rc = ts_solve_window(&win, anchors, (size_t)c->n_anchors, &pos);
        int got_ret = (rc == 0) ? 0 : -1;
        if (got_ret != c->exp_ret) {
            printf("  FAIL solve[%s]: ret got %d, expected %d\n", c->name, got_ret, c->exp_ret);
            fails++;
            continue;
        }
        if (c->exp_ret != 0) continue; /* null case: ret matched, nothing else to check */

        bool ok = true;
        if (!mac_eq(pos.mac_hash, c->mac)) { printf("  FAIL solve[%s]: mac mismatch\n", c->name); ok = false; }
        if (pos.band != c->band) { printf("  FAIL solve[%s]: band %d != %d\n", c->name, pos.band, c->band); ok = false; }
        if (pos.confidence != c->exp_confidence) { printf("  FAIL solve[%s]: confidence %d != %d\n", c->name, pos.confidence, c->exp_confidence); ok = false; }
        if (pos.measurement_count != c->exp_measurement_count) { printf("  FAIL solve[%s]: mcount %d != %d\n", c->name, pos.measurement_count, c->exp_measurement_count); ok = false; }
        if (pos.peak_rssi != (int8_t)c->exp_peak_rssi) { printf("  FAIL solve[%s]: peak_rssi %d != %d\n", c->name, pos.peak_rssi, c->exp_peak_rssi); ok = false; }
        if (!geo_close(pos.lat, pos.lon, c->exp_lat, c->exp_lon)) {
            printf("  FAIL solve[%s]: pos got (%.9f,%.9f), expected (%.9f,%.9f)\n",
                   c->name, pos.lat, pos.lon, c->exp_lat, c->exp_lon);
            ok = false;
        }
        if (!approx(pos.accuracy_m, c->exp_accuracy_m, TOL_M)) {
            printf("  FAIL solve[%s]: accuracy_m %.5f != %.5f\n", c->name, pos.accuracy_m, c->exp_accuracy_m);
            ok = false;
        }
        /* Error-ellipse parity. Meter axes use absolute 1 cm (now bounded);
         * bearing is absolute. On the >=3 path these are isotropic (== accuracy_m,
         * deg 0), so the same checks hold. */
        if (!approx(pos.error_semi_major_m, c->exp_error_semi_major_m, TOL_M)) {
            printf("  FAIL solve[%s]: error_semi_major_m %.6g != %.6g\n", c->name, pos.error_semi_major_m, c->exp_error_semi_major_m);
            ok = false;
        }
        if (!approx(pos.error_semi_minor_m, c->exp_error_semi_minor_m, TOL_M)) {
            printf("  FAIL solve[%s]: error_semi_minor_m %.6g != %.6g\n", c->name, pos.error_semi_minor_m, c->exp_error_semi_minor_m);
            ok = false;
        }
        if (!approx(pos.error_major_axis_deg, c->exp_error_major_axis_deg, TOL_DEG)) {
            printf("  FAIL solve[%s]: error_major_axis_deg %.9f != %.9f\n", c->name, pos.error_major_axis_deg, c->exp_error_major_axis_deg);
            ok = false;
        }
        if (!ok) fails++;
    }
    return fails;
}

/* ------------------------------------------------------------------ triangulate */

static bool pos_match(const ts_position_t *got, const ts_fx_tri_pos_t *exp) {
    if (!mac_eq(got->mac_hash, exp->mac)) return false;
    if (got->band != exp->band) return false;
    if (got->confidence != exp->confidence) return false;
    if (got->measurement_count != exp->measurement_count) return false;
    if (got->peak_rssi != (int8_t)exp->peak_rssi) return false;
    if (!geo_close(got->lat, got->lon, exp->lat, exp->lon)) return false;
    if (!approx(got->accuracy_m, exp->accuracy_m, TOL_M)) return false;
    /* Error-ellipse parity (absolute 1 cm on the meter axes, absolute on deg). */
    if (!approx(got->error_semi_major_m, exp->error_semi_major_m, TOL_M)) return false;
    if (!approx(got->error_semi_minor_m, exp->error_semi_minor_m, TOL_M)) return false;
    if (!approx(got->error_major_axis_deg, exp->error_major_axis_deg, TOL_DEG)) return false;
    return true;
}

static int run_triangulate(void) {
    int fails = 0;
    for (size_t i = 0; i < TS_FX_TRI_N; i++) {
        const ts_fx_tri_t *c = &TS_FX_TRI[i];
        ts_obs_t obs[32];
        for (int k = 0; k < c->n_obs; k++) {
            obs[k].node_id = c->obs[k].node_id;
            memcpy(obs[k].mac_hash, c->obs[k].mac, 4);
            obs[k].band = (uint8_t)c->obs[k].band;
            obs[k].rssi = (int8_t)c->obs[k].rssi;
            obs[k].ts_ms = c->obs[k].ts_ms;
        }
        ts_anchor_t anchors[16];
        fill_anchors(anchors, c->anchors, c->n_anchors);
        ts_position_t out[TS_MAX_GROUPS];
        size_t out_n = 0;
        int rc = ts_triangulate(obs, (size_t)c->n_obs, anchors, (size_t)c->n_anchors,
                                out, TS_MAX_GROUPS, &out_n);
        if (rc != TS_OK) {
            printf("  FAIL triangulate[%s]: ts_triangulate returned %d\n", c->name, rc);
            fails++;
            continue;
        }
        if ((int)out_n != c->n_pos) {
            printf("  FAIL triangulate[%s]: got %zu positions, expected %d\n",
                   c->name, out_n, c->n_pos);
            fails++;
            continue;
        }
        bool used[8] = {false};
        bool ok = true;
        for (int e = 0; e < c->n_pos; e++) {
            bool matched = false;
            for (size_t g = 0; g < out_n; g++) {
                if (used[g]) continue;
                if (pos_match(&out[g], &c->pos[e])) {
                    used[g] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                printf("  FAIL triangulate[%s]: expected position (mac=%02x%02x%02x%02x band=%d) not matched\n",
                       c->name, c->pos[e].mac[0], c->pos[e].mac[1], c->pos[e].mac[2],
                       c->pos[e].mac[3], c->pos[e].band);
                ok = false;
            }
        }
        if (!ok) fails++;
    }
    return fails;
}

/* ------------------------------------------------------------------ driver */

typedef struct {
    const char *name;
    int (*fn)(void);
} group_t;

static const group_t GROUPS[] = {
    {"pathloss", run_pathloss},
    {"enu", run_enu},
    {"compute_weight", run_compute_weight},
    {"multilaterate", run_multilaterate},
    {"bilaterate", run_bilaterate},
    {"ellipse", run_ellipse},
    {"correlate", run_correlate},
    {"solve", run_solve},
    {"triangulate", run_triangulate},
};
#define N_GROUPS (sizeof(GROUPS) / sizeof(GROUPS[0]))

static int run_one(const group_t *g) {
    int fails = g->fn();
    printf("[%s] %s\n", fails == 0 ? "PASS" : "FAIL", g->name);
    return fails;
}

int main(int argc, char **argv) {
    printf("parity_test: backend SHA %s\n", TS_FIXTURES_BACKEND_SHA_SHORT);

    if (argc > 1) {
        for (size_t i = 0; i < N_GROUPS; i++) {
            if (strcmp(argv[1], GROUPS[i].name) == 0) {
                int fails = run_one(&GROUPS[i]);
                return fails == 0 ? 0 : 1;
            }
        }
        fprintf(stderr, "parity_test: unknown group '%s'\n", argv[1]);
        fprintf(stderr, "  groups: pathloss enu compute_weight multilaterate bilaterate ellipse correlate solve triangulate\n");
        return 2;
    }

    int total_failed_groups = 0;
    for (size_t i = 0; i < N_GROUPS; i++) {
        if (run_one(&GROUPS[i]) != 0) total_failed_groups++;
    }
    printf("parity_test: %d/%zu groups passed\n", (int)(N_GROUPS - total_failed_groups), N_GROUPS);
    return total_failed_groups == 0 ? 0 : 1;
}
