/* multilaterate.c — port of the TrailSense backend multilaterate step
 * (computeWeight + multilaterate2dRobust, robust IRLS, MESH path). */
#include <math.h>
#include "internal.h"

#define TRI_MAX_IRLS_ROUNDS        3
#define TRI_MAX_ITERATIONS         20
#define TRI_CONVERGENCE_CM         50.0
#define TRI_OUTLIER_THRESHOLD_MULT 3.0
#define TRI_MIN_MEASUREMENTS       3
#define DIST_FLOOR_CM              10.0
#define MEDIAN_FLOOR_CM            100.0

/* Geometric-diversity / collinearity gate constants (<- backend
 * MIN_GEOMETRY_CM / MIN_SPREAD_RATIO). */
#define TS_MIN_GEOMETRY_CM         1000.0
#define TS_MIN_SPREAD_RATIO        30.0

/* Baseline-length floor (cm) below which two anchors are treated as coincident:
 * no baseline to project onto, and 2d would divide by ~zero. Matches the backend
 * BILATERATION_COINCIDENT_EPS_CM (1e-6). */
#define TS_BILATERATION_COINCIDENT_EPS_CM 1e-6

/* Per-measurement initial weight (quality fixed at 75).
 *   rssiWeight = clamp(rssi + 100, 10, 100)
 *   ageWeight  = max(100 - ageMs/300, 10)   (no upper clamp)
 *   weight     = max( floor( rssiWeight * ageWeight * 75 / 10000 ), 1 ) */
double ts_compute_weight(int8_t rssi, uint32_t age_ms) {
    double rssi_w = (double)rssi + 100.0;
    if (rssi_w < 10.0)  rssi_w = 10.0;
    if (rssi_w > 100.0) rssi_w = 100.0;

    double age_w = 100.0 - (double)age_ms / 300.0;
    if (age_w < 10.0) age_w = 10.0;

    double weight = floor((rssi_w * age_w * 75.0) / 10000.0);
    if (weight < 1.0) weight = 1.0;
    return weight;
}

/* Ascending insertion sort (small N: <= TS_MAX_NODES_PER_GROUP). */
static void sort_asc(double *v, size_t n) {
    for (size_t i = 1; i < n; i++) {
        double key = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > key) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = key;
    }
}

/* Median of an already-collected list, matching the backend:
 *   even count -> floor((mid_lo + mid_hi) / 2); odd -> middle element. */
static double median_of(double *sorted, size_t n) {
    if (n == 0) return 0.0;
    if (n == 1) return sorted[0];
    sort_asc(sorted, n);
    if (n % 2 == 0) {
        return floor((sorted[n / 2 - 1] + sorted[n / 2]) / 2.0);
    }
    return sorted[(n - 1) / 2];
}

/* Geometric-diversity / collinearity gate. 1:1 port of the backend
 * checkGeometricDiversity (the TrailSense backend):
 *   - bounding box of (east_cm, north_cm) -> spread_east, spread_north.
 *   - diagonal = hypot(spread_east, spread_north); FAIL if < TS_MIN_GEOMETRY_CM.
 *   - major = max(spreads), minor = min(spreads).
 *   - if major > 0: ratio_percent = floor(minor * 100 / major) (matches
 *     Math.floor); FAIL if < TS_MIN_SPREAD_RATIO (near-collinear).
 *   - n < 2 -> FAIL (cannot form a baseline). */
bool ts_check_geometric_diversity(const ts_measurement_t *m, size_t n) {
    if (n < 2) {
        return false;
    }

    double min_east = m[0].east_cm, max_east = m[0].east_cm;
    double min_north = m[0].north_cm, max_north = m[0].north_cm;
    for (size_t i = 0; i < n; i++) {
        if (m[i].east_cm < min_east)   min_east = m[i].east_cm;
        if (m[i].east_cm > max_east)   max_east = m[i].east_cm;
        if (m[i].north_cm < min_north) min_north = m[i].north_cm;
        if (m[i].north_cm > max_north) max_north = m[i].north_cm;
    }

    double spread_east = max_east - min_east;
    double spread_north = max_north - min_north;

    /* Diagonal gate: bounding box must be big enough to constrain a 2D fix. */
    double diagonal = hypot(spread_east, spread_north);
    if (diagonal < TS_MIN_GEOMETRY_CM) {
        return false;
    }

    /* Collinearity gate: minor-axis spread must be a meaningful fraction of the
     * major-axis spread, else the anchors lie on a near-straight line. */
    double major = spread_east > spread_north ? spread_east : spread_north;
    double minor = spread_east > spread_north ? spread_north : spread_east;

    if (major > 0.0) {
        /* Integer percent, matching Math.floor((minor * 100) / major). */
        double ratio_percent = floor((minor * 100.0) / major);
        if (ratio_percent < TS_MIN_SPREAD_RATIO) {
            return false;
        }
    }

    return true;
}

int ts_multilaterate_2d_robust(const ts_measurement_t *m, size_t n,
                               double *out_east_cm, double *out_north_cm,
                               double *out_accuracy_cm) {
    if (n < (size_t)TRI_MIN_MEASUREMENTS) {
        return -1;
    }
    /* Enforce the per-group node bound at the function boundary, not just by
     * caller discipline: the fixed-size scratch arrays below hold exactly
     * TS_MAX_NODES_PER_GROUP entries, so n beyond that would overflow them. */
    if (n > (size_t)TS_MAX_NODES_PER_GROUP) {
        return -1;
    }

    double weights[TS_MAX_NODES_PER_GROUP];
    bool outlier[TS_MAX_NODES_PER_GROUP];
    double residuals[TS_MAX_NODES_PER_GROUP];
    double surviving[TS_MAX_NODES_PER_GROUP];

    double weight_sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        weights[i] = m[i].weight;
        outlier[i] = false;
        weight_sum += weights[i];
    }
    if (weight_sum == 0.0) weight_sum = 1.0;

    /* Initial estimate: weighted centroid. */
    double init_x = 0.0, init_y = 0.0;
    for (size_t i = 0; i < n; i++) {
        init_x += m[i].east_cm * weights[i];
        init_y += m[i].north_cm * weights[i];
    }
    double x = init_x / weight_sum;
    double y = init_y / weight_sum;

    for (int round = 0; round < TRI_MAX_IRLS_ROUNDS; round++) {
        /* Gradient descent to convergence. */
        for (int iter = 0; iter < TRI_MAX_ITERATIONS; iter++) {
            double grad_x = 0.0, grad_y = 0.0;
            for (size_t i = 0; i < n; i++) {
                if (outlier[i]) continue;
                double dx = x - m[i].east_cm;
                double dy = y - m[i].north_cm;
                double dist = hypot(dx, dy);
                if (dist < DIST_FLOOR_CM) dist = DIST_FLOOR_CM;
                double residual = m[i].distance_cm - dist;
                grad_x += (residual * dx * weights[i]) / dist;
                grad_y += (residual * dy * weights[i]) / dist;
            }
            double step_x = grad_x / (2.0 * weight_sum);
            double step_y = grad_y / (2.0 * weight_sum);
            x += step_x;
            y += step_y;
            if (hypot(step_x, step_y) < TRI_CONVERGENCE_CM) {
                break;
            }
        }

        /* Residuals of surviving measurements. */
        size_t n_surv = 0;
        for (size_t i = 0; i < n; i++) {
            if (outlier[i]) continue;
            double dx = x - m[i].east_cm;
            double dy = y - m[i].north_cm;
            double dist = hypot(dx, dy);
            residuals[i] = fabs(m[i].distance_cm - dist);
            surviving[n_surv++] = residuals[i];
        }

        if (n_surv < (size_t)TRI_MIN_MEASUREMENTS) {
            return -1;
        }

        double median_residual = median_of(surviving, n_surv);
        if (median_residual < MEDIAN_FLOOR_CM) median_residual = MEDIAN_FLOOR_CM;
        double threshold = median_residual * TRI_OUTLIER_THRESHOLD_MULT;

        /* Single pass: outlier marking, down-weighting, weightSum rebuild. */
        int new_outliers = 0;
        weight_sum = 0.0;
        for (size_t i = 0; i < n; i++) {
            if (outlier[i]) continue;
            if (residuals[i] > threshold) {
                outlier[i] = true;
                weights[i] = 0.0;
                new_outliers++;
            } else if (residuals[i] > median_residual) {
                weights[i] = (weights[i] * median_residual) / residuals[i];
                if (weights[i] < 1.0) weights[i] = 1.0;
            }
            if (!outlier[i]) {
                weight_sum += weights[i];
            }
        }
        if (weight_sum == 0.0) weight_sum = 1.0;

        if (new_outliers == 0) {
            break;
        }
    }

    /* Weighted mean absolute residual (CEP) over survivors. */
    double residual_sum = 0.0;
    double total_weight = 0.0;
    for (size_t i = 0; i < n; i++) {
        if (outlier[i]) continue;
        double dx = x - m[i].east_cm;
        double dy = y - m[i].north_cm;
        double dist = hypot(dx, dy);
        double residual = fabs(m[i].distance_cm - dist);
        residual_sum += residual * weights[i];
        total_weight += weights[i];
    }

    double accuracy_cm = (total_weight > 0.0) ? (residual_sum / total_weight) : 65535.0;

    if (out_east_cm)     *out_east_cm = x;
    if (out_north_cm)    *out_north_cm = y;
    if (out_accuracy_cm) *out_accuracy_cm = accuracy_cm;
    return 0;
}

/* 2-node approximate localization (bilateration). 1:1 port of the backend
 * bilaterate2Anchors (the TrailSense backend). Reports the range-consistent point ON
 * the anchor baseline; out_accuracy_cm is the off-baseline half-distance h, the
 * honest CEP. FAILS LOUD (-1) on wrong arity or coincident anchors.
 *
 *   d = |P2 - P1|; d < eps -> -1 (no baseline).
 *   a = (r1^2 - r2^2 + d^2) / (2d), clamped to [0, d].
 *   foot = P1 + a*(P2-P1)/d.
 *   h^2 = r1^2 - a^2; h = (h^2 > 0) ? sqrt(h^2) : 0. */
int ts_bilaterate_2_anchors(const ts_measurement_t *m, size_t n,
                            double *out_east_cm, double *out_north_cm,
                            double *out_accuracy_cm) {
    if (n != 2) {
        return -1;
    }
    double dx = m[1].east_cm - m[0].east_cm;
    double dy = m[1].north_cm - m[0].north_cm;
    double d = hypot(dx, dy);
    if (d < TS_BILATERATION_COINCIDENT_EPS_CM) {
        return -1; /* coincident anchors: no baseline, would divide by ~zero. */
    }

    double r1 = m[0].distance_cm;
    double r2 = m[1].distance_cm;

    double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    if (a < 0.0) a = 0.0;
    if (a > d)   a = d;

    double ux = dx / d;
    double uy = dy / d;
    double east = m[0].east_cm + a * ux;
    double north = m[0].north_cm + a * uy;

    double h2 = r1 * r1 - a * a;
    double h = h2 > 0.0 ? sqrt(h2) : 0.0;

    if (out_east_cm)     *out_east_cm = east;
    if (out_north_cm)    *out_north_cm = north;
    if (out_accuracy_cm) *out_accuracy_cm = h;
    return 0;
}

/* 1-sigma error ellipse for a 2-node bilateration fix. 1:1 port of the backend
 * bilaterationEllipse (the TrailSense backend) -- SAME operation order for float
 * parity. Inputs in cm except n1/n2 (effective path-loss exponent, real units)
 * and the sigmas (dB). Semi-axes out in METERS, major-axis compass bearing in
 * [0,180).
 *
 * The 2-node estimator reports the baseline-projected FOOT (perpendicular
 * deliberately 0), so its honest uncertainty decomposes along two orthogonal
 * axes with NO matrix inverse (an earlier G^-1 Sigma_r G^-T form was DEGENERATE
 * -- the foot always lies on the baseline where the two anchor->target
 * directions are collinear, det(G)->0, semi-major ~1e10 m):
 *   - ALONG the baseline: range noise propagated onto the along-track coordinate
 *     a = (r1^2 - r2^2 + d^2)/(2d). The along-baseline semi-axis scales as ~r/d and
 *     grows without bound as the baseline d->0; only the quadratic 1/det^2 blow-up
 *     was eliminated (the det-clamp removed that term, but a linear 1/d remains).
 *     A minimum-baseline gate (TS_MIN_BILATERATION_BASELINE_M, applied in
 *     ts_solve_window) rejects degenerate near-coincident pairs before this runs.
 *   - ACROSS the baseline: the deterministic mirror-ambiguity half-separation h
 *     (target is +/-h off the baseline; the estimate sits at 0) -- a hard bound on
 *     the perpendicular displacement, NOT a Gaussian 1-sigma. It is raised to the
 *     geometric-inconsistency gap when the range circles cannot intersect (see the
 *     sigma_cross step below).
 * The only divide is by the baseline length d (eps-guarded); every sqrt arg is
 * clamped >= 0 -> never NaN/Inf. foot_east/foot_north are retained for call-site
 * + parity signature stability; the closed form does not need them. */
void ts_bilateration_ellipse(double a1_east, double a1_north,
                             double a2_east, double a2_north,
                             double r1, double r2,
                             double n1, double n2,
                             double foot_east, double foot_north,
                             double h,
                             double sigma_shadow_db, double sigma_tx_db,
                             ts_ellipse_t *out) {
    double ln10 = log(10.0);
    double eps = TS_BILATERATION_COINCIDENT_EPS_CM;
    (void)foot_east;
    (void)foot_north;

    /* Baseline geometry: unit baseline u_hat and unit perpendicular c_hat. */
    double d_east = a2_east - a1_east;
    double d_north = a2_north - a1_north;
    double d = hypot(d_east, d_north);
    double h_m = h / 100.0;
    if (d < eps) {
        /* Coincident anchors: no baseline. ts_bilaterate_2_anchors returns -1
         * before reaching here; this only guards the standalone call from a
         * 0-divide -> report a circular h ellipse rather than emit NaN. */
        out->semi_major_m = h_m;
        out->semi_minor_m = h_m;
        out->major_axis_deg = 0.0;
        return;
    }
    double ux = d_east / d;
    double uy = d_north / d;
    double cx = -uy;
    double cy = ux;

    /* (1) Per-anchor range variance (cm^2). C_i = ln(10)/(10 n_i). Shadowing is
     * INDEPENDENT; the unknown-TX term (g_i = C_i r_i) is COMMON-MODE (cancels
     * in the RSSI ratio) -> contributes sigmaTX^2 * g_i * g_j. */
    double c1 = ln10 / (10.0 * n1);
    double c2 = ln10 / (10.0 * n2);
    double t1 = c1 * sigma_shadow_db * r1;
    double t2 = c2 * sigma_shadow_db * r2;
    double var_shadow1 = t1 * t1;
    double var_shadow2 = t2 * t2;
    double g1 = c1 * r1;
    double g2 = c2 * r2;
    double sig_tx2 = sigma_tx_db * sigma_tx_db;
    double var_r1 = var_shadow1 + sig_tx2 * g1 * g1;
    double var_r2 = var_shadow2 + sig_tx2 * g2 * g2;
    double cov_r = sig_tx2 * g1 * g2;

    /* (2) Along-baseline axis: propagate range noise onto the along-track
     * coordinate a via da/dr1 = r1/d, da/dr2 = -r2/d. (For symmetric geometry
     * the common-mode TX term cancels here -- correct.) */
    double dadr1 = r1 / d;
    double dadr2 = -r2 / d;
    double var_along =
        dadr1 * dadr1 * var_r1 +
        dadr2 * dadr2 * var_r2 +
        2.0 * dadr1 * dadr2 * cov_r;
    if (var_along < 0.0) var_along = 0.0;
    double sigma_along_cm = sqrt(var_along);

    /* (3) Cross-baseline axis. Base value = the deterministic mirror-ambiguity
     * half-separation h (cm) -- a hard bound on the perpendicular displacement,
     * NOT a Gaussian 1-sigma. When the range circles are INCONSISTENT with the
     * baseline -- non-intersecting (d > r1+r2) or nested (d < |r1-r2|) -- the
     * along-track clamp forces h ~ 0, which would report a falsely-tight cross
     * axis on broken geometry. Raise the cross axis to the geometric-inconsistency
     * gap so an inconsistent-range fix reports an honestly-large cross axis. gap
     * == 0 on consistent geometry, so sigma_cross == h there (unchanged). The
     * !(gap>=0) test also clamps any NaN to 0. */
    double r_sum = r1 + r2;
    double r_diff = fabs(r1 - r2);
    double gap = d - r_sum;
    if (r_diff - d > gap) gap = r_diff - d;
    if (!(gap >= 0.0)) gap = 0.0;
    double sigma_cross_cm = (h >= gap) ? h : gap;

    /* (4) Semi-axes: larger is the major. Ties -> along (>=). */
    bool along_major = sigma_along_cm >= sigma_cross_cm;
    double semi_major_cm = along_major ? sigma_along_cm : sigma_cross_cm;
    double semi_minor_cm = along_major ? sigma_cross_cm : sigma_along_cm;

    /* (5) Orientation: major axis is u_hat when along dominates, else c_hat.
     * Compass bearing (0=N, CW) = atan2(east, north); fold into [0,180). PI
     * literal (== JS Math.PI) for portability + exact parity. */
    double v_e = along_major ? ux : cx;
    double v_n = along_major ? uy : cy;
    double deg = atan2(v_e, v_n) * 180.0 / 3.141592653589793;
    deg = fmod(deg, 180.0);
    if (deg < 0.0) deg += 180.0;

    out->semi_major_m = semi_major_cm / 100.0;
    out->semi_minor_m = semi_minor_cm / 100.0;
    out->major_axis_deg = deg;
}
