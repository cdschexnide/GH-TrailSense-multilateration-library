/*
 * internal.h — private module contracts shared across the C sources. NOT
 * installed. Each module ports one backend file 1:1; the contracts below mirror
 * the backend's module boundaries so the parity tests can exercise each layer.
 *
 * Backend ground truth (the TrailSense backend):
 *   pathloss.c     <- the TrailSense backend pathloss step
 *   enu.c          <- the TrailSense backend ENU step
 *   multilaterate.c<- the TrailSense backend (computeWeight + multilaterate2dRobust)
 *   correlate.c    <- the TrailSense backend correlate step
 *   solve.c        <- the TrailSense backend solveWindow step
 *   triangulate.c  <- the TrailSense backend (pure correlate->solve part)
 */
#ifndef TS_TRI_INTERNAL_H
#define TS_TRI_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "trailsense_triangulate.h"

/*
 * Static bounds (host library, all-stack, reentrant — no malloc, no globals).
 * A batch with more distinct (mac,band) groups than TS_MAX_GROUPS, or a group
 * with more distinct nodes than TS_MAX_NODES_PER_GROUP, is processed up to the
 * cap; the CLI warns on stderr when a cap is hit (the core never prints).
 *
 * TS_MAX_GROUPS and TS_MAX_NODES_PER_GROUP are PUBLIC and documented in
 * trailsense_triangulate.h (included above) so a direct library linker knows
 * the truncation bounds.
 */

/*
 * 2-node error-ellipse uncertainty constants (<- backend TS_SHADOWING_SIGMA_DB /
 * TS_TX_POWER_SIGMA_DB). BOTH are CONFIGURABLE, sourced-as-TYPICAL literature
 * values -- NOT hardware-measured. TS_SHADOWING_SIGMA_DB (7 dB): outdoor
 * log-normal shadowing std (typical 6-8 dB), the independent per-anchor range
 * noise. TS_TX_POWER_SIGMA_DB (6 dB): device-to-device unknown-TX (DRSS) spread,
 * COMMON-MODE (correlated) across the two ranges because TX cancels in the ratio.
 */
#define TS_SHADOWING_SIGMA_DB 7.0
#define TS_TX_POWER_SIGMA_DB  6.0

/*
 * Minimum anchor baseline (METERS) for a 2-node bilateration fix (<- backend
 * TS_MIN_BILATERATION_BASELINE_M). CONFIGURABLE. The 2-node along-baseline
 * semi-axis scales as ~r/d and grows without bound as the baseline d->0 (only the
 * quadratic 1/det^2 blow-up was removed; a linear 1/d remains), so a merely
 * near-coincident anchor pair -- e.g. one sensor surveyed twice with GPS jitter --
 * would emit a confident (confidence 40) multi-kilometre ellipse. ts_solve_window
 * drops any 2-node group with a baseline shorter than this (NO position),
 * mirroring how the >=3 path rejects degenerate collinear geometry. The
 * coincident-eps guard (1e-6 cm) only catches EXACT overlap; this is the stronger
 * meaningful-distance floor. Tune per deployment.
 */
#define TS_MIN_BILATERATION_BASELINE_M 5.0

/* ---- pathloss.c (<- the TrailSense backend pathloss step) ---- */
/* OUTDOOR band prior. 0 on success, -1 if band invalid. */
int ts_band_prior_get(uint8_t band, int32_t *tx_dbm, int32_t *a_cdb, int32_t *n_milli);
/* RSSI -> distance in cm: round(d_m*100) clamped [50,65500].
 * a_cdb <= 0 OR n_milli <= 0 => use band-prior default for that field.
 * Returns the distance (>=0) or -1 if band invalid. */
int ts_pathloss_distance_cm(uint8_t band, int8_t rssi, int32_t a_cdb, int32_t n_milli);
/* Effective path-loss exponent n (REAL units) for the 2-node error ellipse:
 * n_milli_override <= 0 => band default; then floor n >= 0.5. Mirrors the
 * backend resolvePathLossN (a SEPARATE helper from the distance model, which
 * keeps its own unchanged resolution so the >=3 path stays byte-identical). */
double ts_resolve_pathloss_n(uint8_t band, int32_t n_milli_override);

/* ---- enu.c (<- the TrailSense backend ENU step) ---- */
void ts_gps_to_enu_cm(double lat, double lon, double ref_lat, double ref_lon,
                      double *east_cm, double *north_cm);
void ts_enu_cm_to_gps(double east_cm, double north_cm, double ref_lat, double ref_lon,
                      double *lat, double *lon);

/* ---- multilaterate.c (<- the TrailSense backend multilaterate step) ---- */
typedef struct {
    double east_cm;
    double north_cm;
    double distance_cm;
    double weight;
} ts_measurement_t;

/* Per-measurement initial weight (computeWeight, quality fixed at 75). */
double ts_compute_weight(int8_t rssi, uint32_t age_ms);

/* Geometric-diversity / collinearity gate (<- backend checkGeometricDiversity).
 * Rejects degenerate anchor geometry BEFORE the solver runs, in the same cm ENU
 * units the measurements carry. Returns true if BOTH gates pass:
 *   - bounding-box diagonal >= TS_MIN_GEOMETRY_CM, and
 *   - minor/major spread ratio (integer percent) >= TS_MIN_SPREAD_RATIO.
 * n < 2 -> false (cannot form a baseline). */
bool ts_check_geometric_diversity(const ts_measurement_t *m, size_t n);

/* Robust IRLS multilateration. Returns 0 and fills the out params on success;
 * returns -1 if n < 3 or fewer than 3 survivors remain. */
int ts_multilaterate_2d_robust(const ts_measurement_t *m, size_t n,
                               double *out_east_cm, double *out_north_cm,
                               double *out_accuracy_cm);

/* 1-sigma error ellipse for a 2-node bilateration fix (<- backend
 * bilaterationEllipse). Semi-axes in METERS, major-axis compass bearing in
 * [0,180) (0=N, CW). All inputs in cm except n1/n2 (effective path-loss
 * exponent, real units) and the two sigmas (dB). Axis-aligned closed form: an
 * ALONG-baseline axis (range noise on the along-track foot -- a genuine ~1-sigma,
 * unbounded as the baseline d->0) and an ACROSS-baseline axis (max of the mirror
 * half-separation h and the geometric-inconsistency gap -- a deterministic bound,
 * NOT a 1-sigma). NO matrix inverse. NEVER emits NaN/Inf: clamped sqrt args,
 * single eps-guarded divide by the baseline length. */
typedef struct {
    double semi_major_m;
    double semi_minor_m;
    double major_axis_deg;
} ts_ellipse_t;

void ts_bilateration_ellipse(double a1_east, double a1_north,
                             double a2_east, double a2_north,
                             double r1, double r2,
                             double n1, double n2,
                             double foot_east, double foot_north,
                             double h,
                             double sigma_shadow_db, double sigma_tx_db,
                             ts_ellipse_t *out);

/* 2-node approximate localization (bilateration) (<- backend bilaterate2Anchors).
 * Reports the range-consistent point ON the anchor baseline (foot of the
 * perpendicular from the true position); out_accuracy_cm is the off-baseline
 * half-distance h, the honest CEP. FAILS LOUD (returns -1) if n != 2 or the two
 * anchors are coincident (baseline < TS_BILATERATION_COINCIDENT_EPS_CM), never
 * emitting NaN. Returns 0 and fills the out params on success. The >=3
 * geometric-diversity gate is intentionally NOT applied here (2 points are
 * trivially collinear). */
int ts_bilaterate_2_anchors(const ts_measurement_t *m, size_t n,
                            double *out_east_cm, double *out_north_cm,
                            double *out_accuracy_cm);

/* ---- correlate.c (<- the TrailSense backend correlate step) ---- */
typedef struct {
    int32_t  node_id;
    int8_t   rssi;
    uint32_t ts_ms;   /* the selected (latest) sample's timestamp */
} ts_group_node_t;

typedef struct {
    uint8_t         mac_hash[4];
    uint8_t         band;
    ts_group_node_t nodes[TS_MAX_NODES_PER_GROUP];
    size_t          n_nodes;
} ts_window_t;

/* Build the single (mac_hash, band) window from a batch: per distinct node, the
 * latest-by-ts_ms sample. Returns true if >=3 distinct nodes participate (the
 * triangulation gate), false otherwise. Caps node count at TS_MAX_NODES_PER_GROUP. */
bool ts_build_window(const ts_obs_t *obs, size_t n_obs,
                     const uint8_t mac_hash[4], uint8_t band, ts_window_t *out);

/* Batch correlate: one window per distinct (mac_hash, band) with >=3 nodes.
 * Writes up to win_cap windows; sets *n_win. Returns TS_OK or a negative code. */
int ts_correlate(const ts_obs_t *obs, size_t n_obs,
                 ts_window_t *win, size_t win_cap, size_t *n_win);

/* ---- solve.c (<- the TrailSense backend solveWindow step) ---- */
/* Solve one window against the anchor table: resolve each node to an enrolled
 * anchor (drop missing/unenrolled), require >=3 participants, centroid-ENU,
 * per-anchor distance+weight, multilaterate, back-transform. Returns 0 and fills
 * *out on a fix; -1 if <3 participants or the solver returns no fix. */
int ts_solve_window(const ts_window_t *win,
                    const ts_anchor_t *anchors, size_t n_anchors,
                    ts_position_t *out);

#endif /* TS_TRI_INTERNAL_H */
