/*
 * trailsense_triangulate.h — public C-ABI for the TrailSense triangulation
 * component. Turns raw multi-node detections + surveyed anchor positions into
 * triangulated positions.
 *
 * This is a C port of the bench-proven TrailSense backend pipeline:
 * correlate -> solve -> multilaterate, with the pathloss + ENU helpers.
 * Floating-point IRLS, centroid-ENU reference, surveyed-anchor-table driven.
 * See docs/ for usage, the C API reference, and how the model works.
 *
 * The library allocates nothing on the heap and keeps no global state; every
 * call is reentrant. Link against libm.
 */
#ifndef TRAILSENSE_TRIANGULATE_H
#define TRAILSENSE_TRIANGULATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TS_TRI_VERSION_MAJOR 1
#define TS_TRI_VERSION_MINOR 0

/*
 * Static capacity bounds. The library allocates nothing; every buffer is
 * caller- or stack-owned, so these are hard limits on a single ts_triangulate()
 * call. At most TS_MAX_GROUPS distinct (mac_hash, band) groups are processed,
 * and at most TS_MAX_NODES_PER_GROUP distinct nodes within any one group.
 *
 * TRUNCATION BEHAVIOR: input beyond either bound is SILENTLY DROPPED by the
 * core (the core never prints) — groups past TS_MAX_GROUPS, and nodes past
 * TS_MAX_NODES_PER_GROUP within a group, are discarded, yielding a PARTIAL
 * result (still TS_OK). A direct library linker that must not lose any
 * group/node has to batch its input under these caps. The bundled CLI detects
 * an over-cap input and warns on stderr (still exits 0 with the partial
 * result); a direct linker gets no such warning and must enforce the bounds.
 */
#define TS_MAX_GROUPS          64
#define TS_MAX_NODES_PER_GROUP 32

/* Bands — match the low 2 bits of band_chan on the wire (docs/02-data-contract.md). */
typedef enum {
    TS_BAND_WIFI = 0,
    TS_BAND_BLE  = 1,
    TS_BAND_CELL = 2
} ts_band_t;

/*
 * One raw observation: one node's sighting of one device.
 * ts_ms is used only for (a) latest-per-node selection within a (mac_hash,band)
 * group and (b) age-weighting within that group — never to subdivide the batch
 * into sub-windows (the caller owns windowing; see design §4).
 */
typedef struct {
    int32_t  node_id;
    uint8_t  mac_hash[4];
    uint8_t  band;        /* ts_band_t */
    int8_t   rssi;        /* dBm */
    uint32_t ts_ms;
} ts_obs_t;

/*
 * One surveyed anchor (sensor node) position + optional per-anchor path-loss
 * override. path_loss_a_cdb <= 0 OR path_loss_n_milli <= 0 => use the band-prior
 * default for that field. enrolled == 0 => the anchor is excluded.
 */
typedef struct {
    int32_t  node_id;
    double   lat;              /* degrees */
    double   lon;              /* degrees */
    int32_t  path_loss_a_cdb;  /* centi-dB; <=0 => band default */
    int32_t  path_loss_n_milli;/* milli;    <=0 => band default */
    uint8_t  enrolled;         /* 0 => skip this anchor */
} ts_anchor_t;

/* One triangulated position. */
typedef struct {
    uint8_t  mac_hash[4];
    uint8_t  band;              /* ts_band_t */
    double   lat;               /* degrees */
    double   lon;               /* degrees */
    double   accuracy_m;        /* weighted CEP, meters (2-node: == error_semi_major_m) */
    int32_t  measurement_count; /* participating anchors (NOT post-outlier survivors) */
    int32_t  confidence;        /* 40 (==2, approximate bilateration), 70 (==3), 85 (>=4) */
    int8_t   peak_rssi;         /* strongest participant rssi, dBm */
    /* Error ellipse. 2-node (==2): MIXED semantics -- the ellipse is NOT a single
     * Gaussian. The along-baseline axis is a genuine ~1-sigma statistical range
     * uncertainty; the cross-baseline axis is the DETERMINISTIC mirror-ambiguity
     * bound (max of the mirror half-separation h and the geometric-inconsistency
     * gap) -- a hard bound on the perpendicular displacement, NOT a 1-sigma /
     * confidence radius. Consumers MUST NOT treat the cross axis as a confidence
     * radius. The along-baseline axis is unbounded as the baseline d->0, so a
     * minimum-baseline gate (TS_MIN_BILATERATION_BASELINE_M) drops near-coincident
     * 2-node pairs. >=3: ISOTROPIC placeholder -- semi_major == semi_minor ==
     * accuracy_m, major_axis_deg == 0 (3+-node covariance not computed). */
    double   error_semi_major_m;  /* semi-major axis, meters (see mixed semantics above) */
    double   error_semi_minor_m;  /* semi-minor axis, meters (see mixed semantics above) */
    double   error_major_axis_deg;/* compass bearing of semi-major axis: 0=N, CW, [0,180) */
} ts_position_t;

/* Status codes (negative = error). */
typedef enum {
    TS_OK                = 0,
    TS_ERR_NULL_ARG      = -1,
    TS_ERR_OUT_TOO_SMALL = -2,  /* out_cap too small for the positions produced */
    TS_ERR_BAD_BAND      = -3
} ts_status_t;

/*
 * Correlate `obs` by (mac_hash, band), keep the latest sample per node, and
 * solve each group by its distinct enrolled+resolvable anchor count: 0/1 -> no
 * position; exactly 2 -> approximate bilateration (confidence 40); >=3 -> robust
 * IRLS multilateration (confidence 70 for ==3, 85 for >=4). The count at solve
 * time is the only selector, so adding a 3rd unit auto-upgrades a group from
 * bilateration to full triangulation. Writes up to `out_cap` positions into
 * `out`; sets *out_n to the number written. Returns TS_OK on success (including
 * 0 positions) or a negative ts_status_t. Caller owns all buffers; the library
 * allocates nothing.
 */
int ts_triangulate(const ts_obs_t *obs, size_t n_obs,
                   const ts_anchor_t *anchors, size_t n_anchors,
                   ts_position_t *out, size_t out_cap, size_t *out_n);

/*
 * The OUTDOOR band prior (TX dBm, A centi-dB, n milli) for a band. Lets a caller
 * inspect or seed the defaults. Returns TS_OK or TS_ERR_BAD_BAND.
 */
int ts_band_prior(uint8_t band, int32_t *tx_dbm, int32_t *a_cdb, int32_t *n_milli);

#ifdef __cplusplus
}
#endif

#endif /* TRAILSENSE_TRIANGULATE_H */
