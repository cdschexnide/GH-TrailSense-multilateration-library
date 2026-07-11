/* enu.c — port of the TrailSense backend ENU step (equirectangular ENU projection). */
#include <math.h>
#include "internal.h"

#define METERS_PER_DEG_LAT 111320.0
#define DEG_TO_RAD         0.0174532925199433
#define COS_LAT_FLOOR      0.001

void ts_gps_to_enu_cm(double lat, double lon, double ref_lat, double ref_lon,
                      double *east_cm, double *north_cm) {
    double north = (lat - ref_lat) * METERS_PER_DEG_LAT * 100.0;
    double cos_lat = cos(ref_lat * DEG_TO_RAD); /* multiplier here: no floor */
    double east = (lon - ref_lon) * METERS_PER_DEG_LAT * cos_lat * 100.0;
    if (north_cm) *north_cm = north;
    if (east_cm)  *east_cm = east;
}

void ts_enu_cm_to_gps(double east_cm, double north_cm, double ref_lat, double ref_lon,
                      double *lat, double *lon) {
    double north_m = north_cm / 100.0;
    double out_lat = ref_lat + north_m / METERS_PER_DEG_LAT;

    double east_m = east_cm / 100.0;
    double cos_lat = cos(ref_lat * DEG_TO_RAD); /* divisor here: apply floor */
    if (cos_lat < COS_LAT_FLOOR) {
        cos_lat = COS_LAT_FLOOR;
    }
    double out_lon = ref_lon + east_m / (METERS_PER_DEG_LAT * cos_lat);

    if (lat) *lat = out_lat;
    if (lon) *lon = out_lon;
}
