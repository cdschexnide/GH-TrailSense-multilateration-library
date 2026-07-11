/*
 * main.c — trailsense-triangulate CLI: JSON stdin -> ts_triangulate -> JSON stdout.
 *
 * Reads one batch of {anchors, obs} as JSON on stdin, calls the dependency-free
 * core library (ts_triangulate), and writes {positions} as JSON on stdout. This
 * is the "shell out to it" form for any host language; the core lib stays
 * dependency-free (libm only) and all JSON lives here in the CLI.
 *
 * JSON parser: a small, purpose-built, dependency-free tolerant parser for the
 * fixed input schema (two arrays of flat objects). No vendored header — there
 * was no reliable offline source for a permissively-licensed single-header
 * parser, and the fixed schema does not need a general-purpose one. Unknown keys
 * are skipped; malformed input fails loud to stderr with a nonzero exit.
 *
 * signal_type / fingerprint_hash rendering mirrors the backend oracle EXACTLY:
 *   (the TrailSense backend)
 *     const BAND_PREFIX = ['w_', 'b_', 'c_'];          // 0 wifi, 1 ble, 2 cell
 *     const SIGNAL_TYPE = ['wifi', 'bluetooth', 'cellular'];
 *     fingerprintHash = BAND_PREFIX[band] + macHash;
 *     signalType      = SIGNAL_TYPE[band];
 * mac_hash is rendered as 8 lowercase hex chars (canonical wire form), so
 * fingerprint_hash is e.g. "w_deadbeef".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "trailsense_triangulate.h"

/* CLI-side input caps. out_cap matches the library's group cap (TS_MAX_GROUPS=64). */
#define CLI_MAX_ANCHORS 256
#define CLI_MAX_OBS     8192
#define CLI_OUT_CAP     64

/* band -> backend field shapes (the TrailSense backend). */
static const char *const BAND_PREFIX[3] = { "w_", "b_", "c_" };
static const char *const SIGNAL_TYPE[3] = { "wifi", "bluetooth", "cellular" };

/* ------------------------------------------------------------------ errors */

static char g_err[256];

#define FAILF(...)                                              \
    do {                                                        \
        snprintf(g_err, sizeof g_err, __VA_ARGS__);             \
        return -1;                                              \
    } while (0)

/* ----------------------------------------------------------- tiny scanner */

static void skip_ws(const char **p) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    *p = s;
}

/* Scan past a JSON string literal (does not capture). Cursor must be at '"'. */
static int skip_string(const char **p) {
    const char *s = *p;
    if (*s != '"')
        FAILF("expected string");
    s++;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            if (!*s)
                FAILF("unterminated string escape");
        }
        s++;
    }
    if (*s != '"')
        FAILF("unterminated string");
    *p = s + 1;
    return 0;
}

/* Capture a JSON string literal into buf (with a minimal escape set). */
static int read_string(const char **p, char *buf, size_t cap) {
    const char *s = *p;
    size_t n = 0;
    if (*s != '"')
        FAILF("expected string");
    s++;
    while (*s && *s != '"') {
        char c = *s;
        if (c == '\\') {
            s++;
            switch (*s) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 0:    FAILF("unterminated string escape");
            default:   FAILF("unsupported string escape '\\%c'", *s);
            }
        }
        if (n + 1 >= cap)
            FAILF("string too long");
        buf[n++] = c;
        s++;
    }
    if (*s != '"')
        FAILF("unterminated string");
    buf[n] = '\0';
    *p = s + 1;
    return 0;
}

/* Read a JSON number into a double. Advances the cursor past the token. */
static int read_number(const char **p, double *out) {
    skip_ws(p);
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p)
        FAILF("expected number");
    /* Fail loud on non-finite input: strtod accepts "nan", "inf", and
     * overflowing magnitudes like 1e400 (-> +/-inf). Emitting those would
     * produce invalid JSON ("lat":nan) with a success exit. Reject here. */
    if (!isfinite(v))
        FAILF("non-finite number not allowed");
    *out = v;
    *p = end;
    return 0;
}

/* Read a JSON boolean (true/false). */
static int read_bool(const char **p, int *out) {
    const char *s = *p;
    if (strncmp(s, "true", 4) == 0) { *out = 1; *p = s + 4; return 0; }
    if (strncmp(s, "false", 5) == 0) { *out = 0; *p = s + 5; return 0; }
    FAILF("expected boolean");
}

/* Max nesting depth for skip_value recursion (defends the stack against
 * deeply-nested unknown JSON). */
#define SKIP_MAX_DEPTH 64

/* Skip an arbitrary JSON value (for unknown keys). Recursive; `depth` guards
 * against stack overflow on pathologically-nested input. */
static int skip_value_d(const char **p, int depth) {
    if (depth > SKIP_MAX_DEPTH)
        FAILF("JSON nesting too deep (> %d)", SKIP_MAX_DEPTH);
    skip_ws(p);
    char c = **p;
    if (c == '"')
        return skip_string(p);
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{') ? '}' : ']';
        (*p)++;
        skip_ws(p);
        if (**p == close) { (*p)++; return 0; }
        for (;;) {
            if (open == '{') {
                /* member: string : value */
                skip_ws(p);
                if (skip_string(p) != 0)
                    return -1;
                skip_ws(p);
                if (**p != ':')
                    FAILF("expected ':' in object");
                (*p)++;
            }
            if (skip_value_d(p, depth + 1) != 0)
                return -1;
            skip_ws(p);
            if (**p == ',') { (*p)++; continue; }
            if (**p == close) { (*p)++; return 0; }
            FAILF("expected ',' or '%c'", close);
        }
    }
    if (c == 't' || c == 'f') {
        int b;
        return read_bool(p, &b);
    }
    if (c == 'n') {
        if (strncmp(*p, "null", 4) == 0) { *p += 4; return 0; }
        FAILF("invalid literal");
    }
    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
        double d;
        return read_number(p, &d);
    }
    FAILF("unexpected character '%c'", c ? c : '0');
}

/* Skip an arbitrary JSON value (for unknown keys). */
static int skip_value(const char **p) {
    return skip_value_d(p, 0);
}

/* ----------------------------------------------------------- field helpers */

static int read_key(const char **p, char *buf, size_t cap) {
    skip_ws(p);
    if (read_string(p, buf, cap) != 0)
        return -1;
    skip_ws(p);
    if (**p != ':')
        FAILF("expected ':' after key \"%s\"", buf);
    (*p)++;
    skip_ws(p);
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "deadbeef" (8 hex chars) -> 4 bytes. */
static int parse_mac_hash(const char *s, uint8_t out[4]) {
    if (strlen(s) != 8)
        FAILF("mac_hash must be exactly 8 hex chars, got \"%s\"", s);
    for (int i = 0; i < 4; i++) {
        int hi = hexval(s[2 * i]);
        int lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            FAILF("mac_hash has non-hex char in \"%s\"", s);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* ----------------------------------------------------------- object parse */

static int parse_anchor(const char **p, ts_anchor_t *a) {
    skip_ws(p);
    if (**p != '{')
        FAILF("expected '{' for anchor object");
    (*p)++;

    /* defaults: enrolled=true, path-loss overrides 0 (=> band default). */
    a->node_id = 0;
    a->lat = 0.0;
    a->lon = 0.0;
    a->path_loss_a_cdb = 0;
    a->path_loss_n_milli = 0;
    a->enrolled = 1;
    int have_node = 0, have_lat = 0, have_lon = 0;

    skip_ws(p);
    if (**p == '}') { (*p)++; FAILF("anchor object is empty"); }

    for (;;) {
        char key[64];
        if (read_key(p, key, sizeof key) != 0)
            return -1;

        if (strcmp(key, "node_id") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            /* Range-check BEFORE the cast: a double outside int32 range is UB
             * to cast. (read_number already rejected non-finite.) */
            if (d < (double)INT32_MIN || d > (double)INT32_MAX)
                FAILF("anchor node_id out of int32 range: %g", d);
            a->node_id = (int32_t)d; have_node = 1;
        } else if (strcmp(key, "lat") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            a->lat = d; have_lat = 1;
        } else if (strcmp(key, "lon") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            a->lon = d; have_lon = 1;
        } else if (strcmp(key, "enrolled") == 0) {
            int b; if (read_bool(p, &b) != 0) return -1;
            a->enrolled = (uint8_t)b;
        } else if (strcmp(key, "path_loss_a_cdb") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            a->path_loss_a_cdb = (int32_t)d;
        } else if (strcmp(key, "path_loss_n_milli") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            a->path_loss_n_milli = (int32_t)d;
        } else {
            if (skip_value(p) != 0) return -1;
        }

        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; break; }
        FAILF("expected ',' or '}' in anchor object");
    }

    if (!have_node) FAILF("anchor missing required \"node_id\"");
    if (!have_lat)  FAILF("anchor (node_id=%d) missing required \"lat\"", a->node_id);
    if (!have_lon)  FAILF("anchor (node_id=%d) missing required \"lon\"", a->node_id);
    return 0;
}

static int parse_obs(const char **p, ts_obs_t *o) {
    skip_ws(p);
    if (**p != '{')
        FAILF("expected '{' for obs object");
    (*p)++;

    o->node_id = 0;
    memset(o->mac_hash, 0, 4);
    o->band = 0;
    o->rssi = 0;
    o->ts_ms = 0;
    int have_node = 0, have_mac = 0, have_band = 0, have_rssi = 0, have_ts = 0;

    skip_ws(p);
    if (**p == '}') { (*p)++; FAILF("obs object is empty"); }

    for (;;) {
        char key[64];
        if (read_key(p, key, sizeof key) != 0)
            return -1;

        if (strcmp(key, "node_id") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            /* Range-check BEFORE the cast (UB otherwise). */
            if (d < (double)INT32_MIN || d > (double)INT32_MAX)
                FAILF("obs node_id out of int32 range: %g", d);
            o->node_id = (int32_t)d; have_node = 1;
        } else if (strcmp(key, "mac_hash") == 0) {
            char s[64];
            if (read_string(p, s, sizeof s) != 0) return -1;
            if (parse_mac_hash(s, o->mac_hash) != 0) return -1;
            have_mac = 1;
        } else if (strcmp(key, "band") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            /* Range-check BEFORE the cast (UB otherwise). Wire band domain is
             * 0..3; band 3 is representable but unsupported by the model, so we
             * accept 0..3 here and let ts_triangulate fail loud on >2
             * (TS_ERR_BAD_BAND), mirroring the backend throw. */
            if (d < 0.0 || d > 3.0)
                FAILF("obs band out of wire range 0..3, got %g", d);
            o->band = (uint8_t)(int)d; have_band = 1;
        } else if (strcmp(key, "rssi") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            /* Range-check BEFORE the cast (UB otherwise). */
            if (d < -128.0 || d > 127.0)
                FAILF("obs rssi out of int8 range: %g", d);
            o->rssi = (int8_t)(int)d; have_rssi = 1;
        } else if (strcmp(key, "ts_ms") == 0) {
            double d; if (read_number(p, &d) != 0) return -1;
            if (d < 0.0 || d > 4294967295.0)
                FAILF("obs ts_ms out of uint32 range: %g", d);
            o->ts_ms = (uint32_t)d; have_ts = 1;
        } else {
            if (skip_value(p) != 0) return -1;
        }

        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; break; }
        FAILF("expected ',' or '}' in obs object");
    }

    if (!have_node) FAILF("obs missing required \"node_id\"");
    if (!have_mac)  FAILF("obs (node_id=%d) missing required \"mac_hash\"", o->node_id);
    if (!have_band) FAILF("obs (node_id=%d) missing required \"band\"", o->node_id);
    if (!have_rssi) FAILF("obs (node_id=%d) missing required \"rssi\"", o->node_id);
    if (!have_ts)   FAILF("obs (node_id=%d) missing required \"ts_ms\"", o->node_id);
    return 0;
}

/* ----------------------------------------------------------- top-level parse */

static int parse_input(const char *text,
                       ts_anchor_t *anchors, size_t *n_anchors,
                       ts_obs_t *obs, size_t *n_obs) {
    const char *p = text;
    *n_anchors = 0;
    *n_obs = 0;
    int have_anchors = 0, have_obs = 0;

    skip_ws(&p);
    if (*p != '{')
        FAILF("input must be a JSON object");
    p++;
    skip_ws(&p);
    if (*p == '}') FAILF("input object has neither \"anchors\" nor \"obs\"");

    for (;;) {
        char key[64];
        if (read_key(&p, key, sizeof key) != 0)
            return -1;

        if (strcmp(key, "anchors") == 0) {
            skip_ws(&p);
            if (*p != '[') FAILF("\"anchors\" must be an array");
            p++;
            skip_ws(&p);
            if (*p == ']') { p++; }
            else {
                for (;;) {
                    if (*n_anchors >= CLI_MAX_ANCHORS)
                        FAILF("too many anchors (max %d)", CLI_MAX_ANCHORS);
                    if (parse_anchor(&p, &anchors[*n_anchors]) != 0) return -1;
                    (*n_anchors)++;
                    skip_ws(&p);
                    if (*p == ',') { p++; continue; }
                    if (*p == ']') { p++; break; }
                    FAILF("expected ',' or ']' in anchors array");
                }
            }
            have_anchors = 1;
        } else if (strcmp(key, "obs") == 0) {
            skip_ws(&p);
            if (*p != '[') FAILF("\"obs\" must be an array");
            p++;
            skip_ws(&p);
            if (*p == ']') { p++; }
            else {
                for (;;) {
                    if (*n_obs >= CLI_MAX_OBS)
                        FAILF("too many obs (max %d)", CLI_MAX_OBS);
                    if (parse_obs(&p, &obs[*n_obs]) != 0) return -1;
                    (*n_obs)++;
                    skip_ws(&p);
                    if (*p == ',') { p++; continue; }
                    if (*p == ']') { p++; break; }
                    FAILF("expected ',' or ']' in obs array");
                }
            }
            have_obs = 1;
        } else {
            if (skip_value(&p) != 0) return -1;
        }

        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        FAILF("expected ',' or '}' at top level");
    }

    skip_ws(&p);
    if (*p != '\0')
        FAILF("trailing data after top-level object");
    if (!have_anchors) FAILF("input missing required \"anchors\" array");
    if (!have_obs)     FAILF("input missing required \"obs\" array");
    return 0;
}

/* ----------------------------------------------------------- stdin read */

static char *read_all_stdin(void) {
    size_t cap = 1 << 16, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        size_t got = fread(buf + len, 1, cap - 1 - len, stdin);
        len += got;
        if (got == 0) break;
    }
    if (ferror(stdin)) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* ----------------------------------------------------------- output */

static void print_positions(const ts_position_t *pos, size_t n) {
    fputs("{\"positions\":[", stdout);
    for (size_t i = 0; i < n; i++) {
        const ts_position_t *r = &pos[i];
        int band = r->band; /* core guarantees 0..2 for emitted positions */
        const char *prefix = (band >= 0 && band <= 2) ? BAND_PREFIX[band] : "?_";
        const char *stype  = (band >= 0 && band <= 2) ? SIGNAL_TYPE[band] : "unknown";

        if (i) fputc(',', stdout);
        printf("{\"mac_hash\":\"%02x%02x%02x%02x\",\"band\":%d,"
               "\"signal_type\":\"%s\",\"fingerprint_hash\":\"%s%02x%02x%02x%02x\","
               "\"lat\":%.9f,\"lon\":%.9f,\"accuracy_m\":%.9f,"
               "\"measurement_count\":%d,\"confidence\":%d,\"peak_rssi\":%d,"
               "\"error_semi_major_m\":%.9f,\"error_semi_minor_m\":%.9f,"
               "\"error_major_axis_deg\":%.9f}",
               r->mac_hash[0], r->mac_hash[1], r->mac_hash[2], r->mac_hash[3],
               band, stype,
               prefix, r->mac_hash[0], r->mac_hash[1], r->mac_hash[2], r->mac_hash[3],
               r->lat, r->lon, r->accuracy_m,
               r->measurement_count, r->confidence, r->peak_rssi,
               r->error_semi_major_m, r->error_semi_minor_m, r->error_major_axis_deg);
    }
    fputs("]}\n", stdout);
}

/* ------------------------------------------------------------- cap warnings */

/* Scratch for warn_on_caps (.bss, not the stack). Sized to the obs cap: a
 * distinct group or a distinct in-group node can exist per obs in the worst
 * case. */
static uint8_t g_grp_mac[CLI_MAX_OBS][4];
static uint8_t g_grp_band[CLI_MAX_OBS];
static int32_t g_grp_nodes[CLI_MAX_OBS];

/*
 * The core SILENTLY truncates input past TS_MAX_GROUPS distinct (mac_hash,band)
 * groups, and past TS_MAX_NODES_PER_GROUP distinct nodes within any group (it
 * never prints). This surfaces that truncation on stderr — fulfilling the
 * design's "the CLI warns on stderr when a cap is hit" contract. Warning only,
 * NOT fatal: the partial result is still emitted and the CLI still exits 0.
 */
static void warn_on_caps(const ts_obs_t *obs, size_t n_obs) {
    /* Enumerate distinct (mac_hash, band) groups, first-seen order. */
    size_t ng = 0;
    for (size_t i = 0; i < n_obs; i++) {
        bool seen = false;
        for (size_t k = 0; k < ng; k++) {
            if (g_grp_band[k] == obs[i].band &&
                memcmp(g_grp_mac[k], obs[i].mac_hash, 4) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            memcpy(g_grp_mac[ng], obs[i].mac_hash, 4);
            g_grp_band[ng] = obs[i].band;
            ng++;
        }
    }

    if (ng > (size_t)TS_MAX_GROUPS) {
        fprintf(stderr,
                "trailsense-triangulate: warning: %zu distinct (mac_hash,band) "
                "groups exceed cap TS_MAX_GROUPS=%d; %zu group(s) dropped "
                "(partial result)\n",
                ng, TS_MAX_GROUPS, ng - (size_t)TS_MAX_GROUPS);
    }

    /* Per-group distinct-node count. Check every group so an over-cap group is
     * named even if it sits past TS_MAX_GROUPS. */
    for (size_t k = 0; k < ng; k++) {
        size_t nn = 0;
        for (size_t i = 0; i < n_obs; i++) {
            if (obs[i].band != g_grp_band[k] ||
                memcmp(obs[i].mac_hash, g_grp_mac[k], 4) != 0) {
                continue;
            }
            bool seen = false;
            for (size_t m = 0; m < nn; m++) {
                if (g_grp_nodes[m] == obs[i].node_id) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                g_grp_nodes[nn++] = obs[i].node_id;
            }
        }
        if (nn > (size_t)TS_MAX_NODES_PER_GROUP) {
            fprintf(stderr,
                    "trailsense-triangulate: warning: group "
                    "(mac_hash=%02x%02x%02x%02x, band=%u) has %zu distinct "
                    "nodes, exceeds cap TS_MAX_NODES_PER_GROUP=%d; %zu node(s) "
                    "dropped (partial result)\n",
                    g_grp_mac[k][0], g_grp_mac[k][1], g_grp_mac[k][2],
                    g_grp_mac[k][3], (unsigned)g_grp_band[k], nn,
                    TS_MAX_NODES_PER_GROUP, nn - (size_t)TS_MAX_NODES_PER_GROUP);
        }
    }
}

/* ----------------------------------------------------------- main */

/* Large enough to live in .bss, not the stack. */
static ts_anchor_t   g_anchors[CLI_MAX_ANCHORS];
static ts_obs_t      g_obs[CLI_MAX_OBS];
static ts_position_t g_out[CLI_OUT_CAP];

int main(void) {
    char *text = read_all_stdin();
    if (!text) {
        fprintf(stderr, "trailsense-triangulate: failed to read stdin\n");
        return 1;
    }

    size_t n_anchors = 0, n_obs = 0;
    if (parse_input(text, g_anchors, &n_anchors, g_obs, &n_obs) != 0) {
        fprintf(stderr, "trailsense-triangulate: input error: %s\n", g_err);
        free(text);
        return 1;
    }
    free(text);

    /* Surface the core's silent truncation (groups/nodes past the caps) on
     * stderr before solving. Warning only — never fatal. */
    warn_on_caps(g_obs, n_obs);

    size_t out_n = 0;
    int rc = ts_triangulate(g_obs, n_obs, g_anchors, n_anchors,
                            g_out, CLI_OUT_CAP, &out_n);

    if (rc != TS_OK) {
        const char *msg;
        switch (rc) {
        case TS_ERR_NULL_ARG:      msg = "null argument"; break;
        case TS_ERR_OUT_TOO_SMALL: msg = "output buffer too small for the positions produced"; break;
        case TS_ERR_BAD_BAND:      msg = "bad band in observations"; break;
        default:                   msg = "unknown error"; break;
        }
        fprintf(stderr, "trailsense-triangulate: ts_triangulate failed (%d): %s\n", rc, msg);
        return 1;
    }

    print_positions(g_out, out_n);
    return 0;
}
