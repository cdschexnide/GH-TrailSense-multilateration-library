/*
 * fixtures.gen.h — GENERATED from the TrailSense backend. DO NOT EDIT BY HAND.
 *
 * Parity fixtures for the C triangulation component. Every expected value
 * was produced by calling the LIVE backend triangulation functions (the
 * parity oracle), not by re-deriving the math.
 *
 * Backend:  the TrailSense backend
 * Backend SHA:   93ff202e246bc9d46a47d6cb0580b9ac0ca6074c
 * Backend SHA (short): 93ff202
 * Generated:     2026-07-01T03:20:53.117Z
 *
 * Representation: backend Date(t) <-> C ts_ms=t (uint32); macHash hex <-> mac_hash[4].
 */
#ifndef TS_TRI_FIXTURES_GEN_H
#define TS_TRI_FIXTURES_GEN_H

#define TS_FIXTURES_BACKEND_SHA "93ff202e246bc9d46a47d6cb0580b9ac0ca6074c"
#define TS_FIXTURES_BACKEND_SHA_SHORT "93ff202"

#include <stddef.h>

/* ===== pathloss ===== */
typedef struct { const char *name; int band; int rssi; int a_cdb; int n_milli; int expected_distance_cm; } ts_fx_pathloss_t;
static const ts_fx_pathloss_t TS_FX_PATHLOSS[] = {
  {"wifi_default_rssi_-60",0,-60,0,0,7943},
  {"ble_default_rssi_-60",1,-60,0,0,2512},
  {"cell_default_rssi_-60",2,-60,0,0,17783},
  {"wifi_override_A30_n2.5_rssi_-60",0,-60,3000,2500,8318},
  {"wifi_exponent0_rssi_-22",0,-22,0,0,100},
  {"wifi_clamp_low_rssi_0",0,0,0,0,50},
  {"wifi_clamp_high_rssi_-127",0,-127,0,0,65500},
};
#define TS_FX_PATHLOSS_N (sizeof(TS_FX_PATHLOSS)/sizeof(TS_FX_PATHLOSS[0]))

typedef struct { const char *name; int band; int tx_dbm; int a_cdb; int n_milli; } ts_fx_bandprior_t;
static const ts_fx_bandprior_t TS_FX_BANDPRIOR[] = {
  {"wifi",0,18,4000,2000},
  {"ble",1,8,4000,2000},
  {"cell",2,23,3800,2000},
};
#define TS_FX_BANDPRIOR_N (sizeof(TS_FX_BANDPRIOR)/sizeof(TS_FX_BANDPRIOR[0]))

/* ===== enu (forward) ===== */
typedef struct { const char *name; double lat; double lon; double ref_lat; double ref_lon; double exp_east_cm; double exp_north_cm; } ts_fx_enu_fwd_t;
static const ts_fx_enu_fwd_t TS_FX_ENU_FWD[] = {
  {"fwd_nonequatorial",-24.6512,25.9123,-24.65,25.91,23270.407668555923,-13358.400000008416},
  {"fwd_ref_at_origin",-24.65,25.91,-24.65,25.91,0,0},
};
#define TS_FX_ENU_FWD_N (sizeof(TS_FX_ENU_FWD)/sizeof(TS_FX_ENU_FWD[0]))

/* ===== enu (inverse) ===== */
typedef struct { const char *name; double east_cm; double north_cm; double ref_lat; double ref_lon; double exp_lat; double exp_lon; } ts_fx_enu_inv_t;
static const ts_fx_enu_inv_t TS_FX_ENU_INV[] = {
  {"inv_roundtrip",23270.407668555923,-13358.400000008416,-24.65,25.91,-24.6512,25.9123},
  {"inv_coslat_floor_near_pole",100000,0,89.99999,10,89.99999,18.98311174991017},
};
#define TS_FX_ENU_INV_N (sizeof(TS_FX_ENU_INV)/sizeof(TS_FX_ENU_INV[0]))

/* ===== compute_weight ===== */
typedef struct { const char *name; int rssi; unsigned int age_ms; double exp_weight; } ts_fx_cw_t;
static const ts_fx_cw_t TS_FX_CW[] = {
  {"nominal",-60,0u,30},
  {"rssi_clamp_low",-100,0u,7},
  {"rssi_clamp_high",0,0u,75},
  {"age_weight_floor",-50,40000u,3},
  {"min_1_floor",-95,1000000u,1},
  {"floor_div_behavior",-67,300u,24},
};
#define TS_FX_CW_N (sizeof(TS_FX_CW)/sizeof(TS_FX_CW[0]))

/* ===== multilaterate ===== */
/* backend_measurement_count is INFORMATIONAL: the C contract does not expose
 * survivor count. The test verifies outlier behavior via position+accuracy. */
typedef struct { double east_cm; double north_cm; double distance_cm; double weight; } ts_fx_ml_meas_t;
typedef struct { const char *name; ts_fx_ml_meas_t meas[8]; int n_meas; int exp_ret;
  double exp_east_cm; double exp_north_cm; double exp_accuracy_cm; int backend_measurement_count; } ts_fx_ml_t;
static const ts_fx_ml_t TS_FX_ML[] = {
  {"clean_3_anchor",{{0,0,1414,50},{3000,0,2236,50},{0,3000,2236,50}},3,0,999.9798981766454,999.9798981766454,0.11302301471141618,3},
  {"4_good_1_bad_outlier_rejected",{{0,0,1414,50},{3000,0,2236,50},{0,3000,2236,50},{3000,3000,2828,50},{1500,1500,8000,50}},5,0,908.4250163322754,908.4250163322754,77.35360101673021,4},
  {"lone_outlier_3_good_NOT_rejected_median_breakdown",{{0,0,1414,50},{3000,0,2236,50},{0,3000,2236,50},{3000,3000,50,50}},4,0,1903.23385792392,1903.23385792392,472.24823956596816,4},
  {"n_lt_3_null",{{0,0,1414,50},{3000,0,2236,50}},2,-1,0,0,0,-1},
};
#define TS_FX_ML_N (sizeof(TS_FX_ML)/sizeof(TS_FX_ML[0]))

/* ===== bilaterate (2-node approximate localization) ===== */
typedef struct { const char *name; ts_fx_ml_meas_t meas[2]; int n_meas; int exp_ret;
  double exp_east_cm; double exp_north_cm; double exp_accuracy_cm; } ts_fx_bi_t;
#define TS_FX_CONF_BILATERATION 40
static const ts_fx_bi_t TS_FX_BI[] = {
  {"on_baseline_h0",{{0,0,400,50},{1000,0,600,50}},2,0,400,0,0},
  {"off_baseline_accuracy_is_perp",{{0,0,500,50},{1000,0,670.820393249937,50}},2,0,400,0,300},
  {"coincident_null",{{500,500,100,50},{500,500,100,50}},2,-1,0,0,0},
  {"non_reaching_clamped_h0",{{0,0,100,50},{1000,0,100,50}},2,0,500,0,0},
  {"along_track_clamp_to_far",{{0,0,5000,50},{1000,0,100,50}},2,0,1000,0,4898.979485566356},
};
#define TS_FX_BI_N (sizeof(TS_FX_BI)/sizeof(TS_FX_BI[0]))

/* ===== ellipse (2-node error ellipse) ===== */
/* meter fields stay small on these adequate-baseline fixtures -> compared with
 * an absolute 1 cm tolerance in parity_test.c; major_axis_deg is absolute in [0,180). */
typedef struct { const char *name;
  double a1e; double a1n; double a2e; double a2n;
  double r1; double r2; double n1; double n2;
  double foot_e; double foot_n; double h; double sigma_shadow; double sigma_tx;
  double exp_semi_major_m; double exp_semi_minor_m; double exp_major_axis_deg; } ts_fx_ellipse_t;
static const ts_fx_ellipse_t TS_FX_ELLIPSE[] = {
  {"ew_baseline_along_dominates_deg90",0,0,3000,0,1500,1500,2,2,1500,0,600,7,6,8.547911050954522,6,90},
  {"ns_baseline_along_dominates_deg0",0,0,0,3000,1500,1500,2,2,0,1500,600,7,6,8.547911050954522,6,0},
  {"diagonal_cross_dominates_deg135",0,0,3000,3000,1500,1500,2,2,1500,1500,600,0,0,12.426406871192858,0,135},
  {"h_dominant_perpendicular_deg0",0,0,2000,0,5099.019513592785,5099.019513592785,2,2,1000,0,5000,1,0,50,21.166255935696913,0},
  {"mirror_only_sigmas_zero",0,0,3000,0,1500,1500,2,2,1500,0,600,0,0,6,0,0},
  {"shadow_only_symmetric",0,0,3000,0,1500,1500,2,2,1500,0,600,7,0,8.547911050954522,6,90},
  {"tx_only_asymmetric",0,0,3000,0,1200,1800,2,2,1200,0,0,0,6,4.144653167389283,0,90},
  {"h_zero_along_only",0,0,3000,0,1500,1500,2,2,1500,0,0,7,6,8.547911050954522,0,90},
  {"near_degenerate_short_baseline",0,0,1,0,1500,1500,2,2,0.5,0,600,7,6,25643.733152863566,6,90},
  {"inconsistent_ranges_cross_is_gap",0,0,3000,0,500,500,2,2,1500,0,0,0,0,20,0,0},
};
#define TS_FX_ELLIPSE_N (sizeof(TS_FX_ELLIPSE)/sizeof(TS_FX_ELLIPSE[0]))

/* ===== correlate ===== */
typedef struct { int node_id; unsigned char mac[4]; int band; int rssi; unsigned int ts_ms; } ts_fx_corr_obs_t;
typedef struct { int node_id; int rssi; unsigned int ts_ms; } ts_fx_corr_node_t;
typedef struct { unsigned char mac[4]; int band; ts_fx_corr_node_t nodes[32]; int n_nodes; } ts_fx_corr_win_t;
typedef struct { const char *name; ts_fx_corr_obs_t obs[16]; int n_obs; ts_fx_corr_win_t wins[8]; int n_wins; } ts_fx_corr_t;
static const ts_fx_corr_t TS_FX_CORR[] = {
  {"three_distinct_nodes_one_window",
    {
      {1,{0xaa,0xbb,0xcc,0xdd},0,-50,1000000u},
      {2,{0xaa,0xbb,0xcc,0xdd},0,-60,1000100u},
      {3,{0xaa,0xbb,0xcc,0xdd},0,-70,1000200u}
    },3,
    {
      {{0xaa,0xbb,0xcc,0xdd},0,{{1,-50,1000000u},{2,-60,1000100u},{3,-70,1000200u}},3},
    },1},
  {"two_distinct_nodes_no_window",
    {
      {1,{0xaa,0xbb,0xcc,0xdd},0,-60,1000000u},
      {2,{0xaa,0xbb,0xcc,0xdd},0,-60,1000100u},
      {1,{0xaa,0xbb,0xcc,0xdd},0,-60,1000200u}
    },3,
    {
      {{0xaa,0xbb,0xcc,0xdd},0,{{1,-60,1000200u},{2,-60,1000100u}},2},
    },1},
  {"same_node_twice_latest_ts_wins",
    {
      {1,{0xaa,0xbb,0xcc,0xdd},0,-45,1000500u},
      {1,{0xaa,0xbb,0xcc,0xdd},0,-85,1000000u},
      {2,{0xaa,0xbb,0xcc,0xdd},0,-60,1000100u},
      {3,{0xaa,0xbb,0xcc,0xdd},0,-70,1000200u}
    },4,
    {
      {{0xaa,0xbb,0xcc,0xdd},0,{{1,-45,1000500u},{2,-60,1000100u},{3,-70,1000200u}},3},
    },1},
  {"two_bands_separate_windows",
    {
      {1,{0xaa,0xbb,0xcc,0xdd},0,-60,1000000u},
      {2,{0xaa,0xbb,0xcc,0xdd},0,-60,1000000u},
      {3,{0xaa,0xbb,0xcc,0xdd},0,-60,1000000u},
      {1,{0xaa,0xbb,0xcc,0xdd},1,-60,1000000u},
      {2,{0xaa,0xbb,0xcc,0xdd},1,-60,1000000u}
    },5,
    {
      {{0xaa,0xbb,0xcc,0xdd},0,{{1,-60,1000000u},{2,-60,1000000u},{3,-60,1000000u}},3},
      {{0xaa,0xbb,0xcc,0xdd},1,{{1,-60,1000000u},{2,-60,1000000u}},2},
    },2},
};
#define TS_FX_CORR_N (sizeof(TS_FX_CORR)/sizeof(TS_FX_CORR[0]))

/* ===== solve ===== */
typedef struct { int node_id; double lat; double lon; int a_cdb; int n_milli; int enrolled; } ts_fx_solve_anchor_t;
typedef struct { const char *name; unsigned char mac[4]; int band;
  ts_fx_corr_node_t nodes[16]; int n_nodes; ts_fx_solve_anchor_t anchors[16]; int n_anchors;
  int exp_ret; double exp_lat; double exp_lon; double exp_accuracy_m;
  int exp_confidence; int exp_measurement_count; int exp_peak_rssi;
  double exp_error_semi_major_m; double exp_error_semi_minor_m; double exp_error_major_axis_deg; } ts_fx_solve_t;
static const ts_fx_solve_t TS_FX_SOLVE[] = {
  {"clean_3_anchor_conf70",{0xde,0xad,0xbe,0xef},0,
    {{1,-45,1000000u},{2,-49,1000000u},{3,-49,1000000u}},3,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1}
    },3,
    0,40.00008797787561,-74.99988515308661,0.13997496638284643,70,3,-45,0.13997496638284643,0.13997496638284643,0},
  {"four_anchor_conf85",{0xde,0xad,0xbe,0xef},0,
    {{1,-45,1000000u},{2,-49,1000000u},{3,-49,1000000u},{4,-51,1000000u}},4,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1},
      {4,40.0002694933525,-74.99964820141322,4000,2000,1}
    },4,
    0,40.00009653751965,-74.99987397933347,0.67243551042839,85,4,-45,0.67243551042839,0.67243551042839,0},
  {"unenrolled_dropped_still_3_conf70",{0xde,0xad,0xbe,0xef},0,
    {{1,-45,1000000u},{2,-49,1000000u},{3,-49,1000000u},{4,-51,1000000u}},4,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1},
      {4,40.0002694933525,-74.99964820141322,4000,2000,0}
    },4,
    0,40.00008797787561,-74.99988515308661,0.13997496638284643,70,3,-45,0.13997496638284643,0.13997496638284643,0},
  {"missing_anchor_drops_to_2_bilaterates",{0xde,0xad,0xbe,0xef},0,
    {{1,-45,1000000u},{2,-49,1000000u},{3,-49,1000000u}},3,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1}
    },2,
    0,40,-74.99988305730275,16.073952217536142,40,2,-45,16.073952217536142,10.01038821965766,90},
  {"unenrolled_drops_to_2_bilaterates",{0xde,0xad,0xbe,0xef},0,
    {{1,-45,1000000u},{2,-49,1000000u},{3,-49,1000000u}},3,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,0}
    },3,
    0,40,-74.99988305730275,16.073952217536142,40,2,-45,16.073952217536142,10.01038821965766,90},
  {"collinear_anchors_rejected_null",{0xde,0xad,0xbe,0xef},0,
    {{1,-46,1000000u},{2,-46,1000000u},{3,-55,1000000u}},3,
    {
      {1,40,-75,4000,2000,1},
      {2,40.00000089831118,-74.99964820141322,4000,2000,1},
      {3,40.00000179662235,-74.99929640282645,4000,2000,1}
    },3,
    -1,0,0,0,0,0,0,0,0,0},
  {"two_anchor_bilateration_conf40",{0xde,0xad,0xbe,0xef},0,
    {{1,-46,1000000u},{2,-46,1000000u}},2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1}
    },2,
    0,40,-74.9998241007066,9.544122595527165,40,2,-46,9.544122595527165,5.120790954431945,90},
  {"two_anchor_offbaseline_accuracy",{0xde,0xad,0xbe,0xef},0,
    {{1,-46,1000000u},{2,-46,1000000u}},2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1}
    },2,
    0,40,-74.9998241007066,9.544122595527165,40,2,-46,9.544122595527165,5.120790954431945,90},
  {"two_anchor_coincident_null",{0xde,0xad,0xbe,0xef},0,
    {{1,-39,1000000u},{2,-39,1000000u}},2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-75,4000,2000,1}
    },2,
    -1,0,0,0,0,0,0,0,0,0},
  {"two_node_one_enrolled_null",{0xde,0xad,0xbe,0xef},0,
    {{1,-46,1000000u},{2,-46,1000000u}},2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,0}
    },2,
    -1,0,0,0,0,0,0,0,0,0},
  {"two_anchor_short_baseline_dropped",{0xde,0xad,0xbe,0xef},0,
    {{1,-26,1000000u},{2,-26,1000000u}},2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99996482014133,4000,2000,1}
    },2,
    -1,0,0,0,0,0,0,0,0,0},
};
#define TS_FX_SOLVE_N (sizeof(TS_FX_SOLVE)/sizeof(TS_FX_SOLVE[0]))

/* ===== triangulate (E2E) ===== */
typedef struct { unsigned char mac[4]; int band; double lat; double lon; double accuracy_m;
  int confidence; int measurement_count; int peak_rssi;
  double error_semi_major_m; double error_semi_minor_m; double error_major_axis_deg; } ts_fx_tri_pos_t;
typedef struct { const char *name; ts_fx_corr_obs_t obs[32]; int n_obs;
  ts_fx_solve_anchor_t anchors[16]; int n_anchors; ts_fx_tri_pos_t pos[8]; int n_pos; } ts_fx_tri_t;
static const ts_fx_tri_t TS_FX_TRI[] = {
  {"happy_path_one_position",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-45,1000000u},
      {2,{0xde,0xad,0xbe,0xef},0,-49,1000000u},
      {3,{0xde,0xad,0xbe,0xef},0,-49,1000000u}
    },3,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1}
    },3,
    {
      {{0xde,0xad,0xbe,0xef},0,40.00008797787561,-74.99988515308661,0.13997496638284643,70,3,-45,0.13997496638284643,0.13997496638284643,0},
    },1},
  {"single_node_zero_positions",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-58,1000000u},
      {1,{0xde,0xad,0xbe,0xef},0,-60,1000040u}
    },2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1}
    },3,
    {
    },0},
  {"two_node_bilateration_one_position",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-46,1000000u},
      {2,{0xde,0xad,0xbe,0xef},0,-46,1000000u}
    },2,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1}
    },3,
    {
      {{0xde,0xad,0xbe,0xef},0,40,-74.9998241007066,9.544122595527165,40,2,-46,9.544122595527165,5.120790954431945,90},
    },1},
  {"multi_subset_one_solvable",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-45,1000000u},
      {2,{0xde,0xad,0xbe,0xef},0,-49,1000000u},
      {3,{0xde,0xad,0xbe,0xef},0,-49,1000000u},
      {1,{0xca,0xfe,0xba,0xbe},0,-58,1000000u}
    },4,
    {
      {1,40,-75,4000,2000,1},
      {2,40,-74.99964820141322,4000,2000,1},
      {3,40.0002694933525,-75,4000,2000,1}
    },3,
    {
      {{0xde,0xad,0xbe,0xef},0,40.00008797787561,-74.99988515308661,0.13997496638284643,70,3,-45,0.13997496638284643,0.13997496638284643,0},
    },1},
  {"collinear_anchors_zero_positions",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-46,1000000u},
      {2,{0xde,0xad,0xbe,0xef},0,-46,1000000u},
      {3,{0xde,0xad,0xbe,0xef},0,-55,1000000u}
    },3,
    {
      {1,40,-75,4000,2000,1},
      {2,40.00000089831118,-74.99964820141322,4000,2000,1},
      {3,40.00000179662235,-74.99929640282645,4000,2000,1}
    },3,
    {
    },0},
  {"bench_triangle_regression",
    {
      {1,{0xde,0xad,0xbe,0xef},0,-58,1000000u},
      {2,{0xde,0xad,0xbe,0xef},0,-63,1000040u},
      {3,{0xde,0xad,0xbe,0xef},0,-67,1000080u}
    },3,
    {
      {1,45,-93,4000,2000,1},
      {2,45.00009,-93,4000,2000,1},
      {3,45.000045,-92.99991,4000,2000,1}
    },3,
    {
      {{0xde,0xad,0xbe,0xef},0,44.99956869063384,-93.0012342398438,33.17156859728809,70,3,-58,33.17156859728809,33.17156859728809,0},
    },1},
};
#define TS_FX_TRI_N (sizeof(TS_FX_TRI)/sizeof(TS_FX_TRI[0]))

#endif /* TS_TRI_FIXTURES_GEN_H */
