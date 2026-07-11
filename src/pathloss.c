/* pathloss.c — port of the TrailSense backend pathloss step (OUTDOOR log-distance model). */
#include <math.h>
#include "internal.h"

/* OUTDOOR profile band priors, indexed by band: 0=WIFI, 1=BLE, 2=CELL. */
typedef struct {
    int32_t tx_dbm;
    int32_t a_cdb;
    int32_t n_milli;
} ts_band_prior_t;

static const ts_band_prior_t BAND_PRIORS[] = {
    { 18, 4000, 2000 }, /* WIFI */
    {  8, 4000, 2000 }, /* BLE  */
    { 23, 3800, 2000 }, /* CELL */
};

#define D_M_MIN          0.5
#define D_M_MAX          655.0
#define DISTANCE_CM_MIN  50
#define DISTANCE_CM_MAX  65500

int ts_band_prior_get(uint8_t band, int32_t *tx_dbm, int32_t *a_cdb, int32_t *n_milli) {
    if (band > 2) {
        return -1;
    }
    const ts_band_prior_t *p = &BAND_PRIORS[band];
    if (tx_dbm)  *tx_dbm = p->tx_dbm;
    if (a_cdb)   *a_cdb = p->a_cdb;
    if (n_milli) *n_milli = p->n_milli;
    return 0;
}

int ts_pathloss_distance_cm(uint8_t band, int8_t rssi, int32_t a_cdb, int32_t n_milli) {
    if (band > 2) {
        return -1;
    }
    const ts_band_prior_t *prior = &BAND_PRIORS[band];

    int32_t tx = prior->tx_dbm;
    /* Sentinel: a_cdb <= 0 OR n_milli <= 0 => use the band-prior default
     * for THAT field. TX always comes from the band prior.
     * NOTE: this <=0 sentinel is a CLI-omission convenience (an omitted
     * per-anchor override parses to 0 and falls back here); it is NOT backend
     * behavior — the TrailSense backend applies overrides literally and they
     * are positive-only in practice. */
    if (a_cdb <= 0)   a_cdb = prior->a_cdb;
    if (n_milli <= 0) n_milli = prior->n_milli;

    double a = a_cdb / 100.0;
    double n = n_milli / 1000.0;
    if (n < 0.5) {
        n = 0.5;
    }

    double d_m = pow(10.0, (tx - rssi - a) / (10.0 * n));
    if (d_m < D_M_MIN) d_m = D_M_MIN;
    if (d_m > D_M_MAX) d_m = D_M_MAX;

    long distance_cm = (long)round(d_m * 100.0);
    if (distance_cm < DISTANCE_CM_MIN) distance_cm = DISTANCE_CM_MIN;
    if (distance_cm > DISTANCE_CM_MAX) distance_cm = DISTANCE_CM_MAX;

    return (int)distance_cm;
}

/* Effective path-loss exponent n (real units) for the 2-node error ellipse.
 * SEPARATE additive helper; ts_pathloss_distance_cm above is untouched so the
 * >=3 distance/position path stays byte-identical. n_milli_override <= 0 => band
 * default; then floor n >= 0.5. Mirrors the backend resolvePathLossN. */
double ts_resolve_pathloss_n(uint8_t band, int32_t n_milli_override) {
    const ts_band_prior_t *prior = (band <= 2) ? &BAND_PRIORS[band] : &BAND_PRIORS[0];
    int32_t n_milli = (n_milli_override > 0) ? n_milli_override : prior->n_milli;
    double n = n_milli / 1000.0;
    if (n < 0.5) {
        n = 0.5;
    }
    return n;
}
