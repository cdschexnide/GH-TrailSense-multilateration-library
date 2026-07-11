# Getting started

A brief guide to building the TrailSense multilateration library and getting your
first position. This is the fast on-ramp; the full library documentation covers
everything in depth.

## What it does

The library takes two inputs and produces one output.

**The two inputs:**

- **Observations (`obs`)** are the raw RF detections. Each observation is one
  sensor node reporting that it heard one device, and how strong that device's
  signal was. So a single observation says, in effect, "node 2 heard device
  `deadbeef` at signal strength -60 dBm at time T." The same device heard by
  three nodes produces three observations. These are the live, changing data:
  you collect them as your sensors report detections and hand a batch to the
  library. A single detection on its own only tells you how far away a device is
  (a stronger signal means closer), not where it is.

- **Anchors (`anchors`)** are the surveyed positions of your sensor nodes. Each
  anchor says "node 2 is located at this latitude and longitude." This is your
  reference map of where your nodes are. You control it: you set each node's
  location when you install it, and you can update it at any time (for example if
  a node is moved or resurveyed) by passing the new coordinates on the next call.
  The library needs to know where each node is in order to turn "device
  `deadbeef` is roughly this far from nodes 1, 2, and 3" into an actual position
  on the map.

The word "anchor" is used because these known, fixed node positions are what the
unknown device positions are anchored to.

**The output:** for each device that enough nodes heard, one triangulated
position: latitude, longitude, an accuracy estimate, and a confidence tier. The
library matches each device's observations against your anchor map and intersects
the distances. Two nodes hearing a device give an approximate fix; three or more
give a full fix.

## Build

Requires CMake 3.16 or newer and a C11 compiler. There are no other dependencies.

```sh
cmake -S . -B build
cmake --build build
```

This produces the static and shared libraries and the `trailsense-triangulate`
CLI in `build/`.

## Two ways to use it

The library ships in two forms, so you can use whichever fits your system. Both
run the exact same math and produce the same positions; the difference is only
how your system invokes them.

First, understand how often you will invoke it, because this is the same for both
forms. Each call solves one batch of detections for one moment in time: you hand
it the detections you have gathered so far plus your anchor map, and it returns
the positions for that moment. It keeps no state between calls. In a live
deployment your sensors keep hearing devices, so you call it over and over to keep
positions current, for example once per time window or whenever a fresh batch of
detections arrives. So expect to invoke it repeatedly, not once. That repeated
cadence is what makes the difference between the two forms matter.

The two forms:

- **The CLI** is a ready-to-run program. You do not write or compile any C: your
  system hands it the inputs as JSON text and reads the positions back as JSON
  text, and it works from any programming language. The tradeoff is that each
  batch starts a separate program, which adds a small per-call cost. At a modest
  cadence this is negligible; at a high cadence it adds up.
- **The C library** is compiled into your own program, and you call its function
  directly, in process, with no separate program to launch per batch. This is the
  most efficient path at high call rates, and the natural fit if your system is
  written in C or C++. The tradeoff is that you work in C: you fill C structs and
  manage the buffers (which is straightforward, and shown below).

If you are not sure, start with the CLI. Most systems can use it, and you can
switch to linking the library later without changing any of the math or the
positions you get.

### Option 1: The CLI (works from any language, no C required)

The build produces a command-line program called `trailsense-triangulate`. It
reads your inputs as JSON on standard input and writes the positions as JSON on
standard output. Try it now against the bundled example:

```sh
./build/trailsense-triangulate < examples/sample-input.json
```

The input JSON is an object with your two inputs, `{ "anchors": [...], "obs":
[...] }`, and the output is `{ "positions": [...] }`. In a real system you do not
type this at a terminal: your program runs `trailsense-triangulate` as a
subprocess, writes the input JSON to it, and reads the output JSON back. That
works the same way from Python, Node, Go, or anything that can start a process and
pass text to it. The full library documentation includes a worked example
(including Python).

### Option 2: Link the library and call it from C

Instead of running a separate program, you compile the library into your own C or
C++ program and call `ts_triangulate()` directly. You fill in two arrays (your
observations and your anchors), call the function, and it writes the positions
into a third array that you provide. Here is the shape of it:

```c
#include "trailsense_triangulate.h"

/* 1. Fill in your inputs (the full library documentation shows full values). */
ts_obs_t    obs[/* your observation count */]  = { /* ... */ };
ts_anchor_t anchors[/* your anchor count */]   = { /* ... */ };

/* 2. Provide an output buffer for the library to write positions into.
 *    You own this memory; the library never allocates. */
ts_position_t out[TS_MAX_GROUPS];
size_t out_n = 0;   /* the library sets this to how many positions it wrote */

/* 3. Call it. */
int rc = ts_triangulate(obs, n_obs, anchors, n_anchors,
                        out, TS_MAX_GROUPS, &out_n);

/* 4. On success, read the positions out. */
if (rc == TS_OK) {
    for (size_t i = 0; i < out_n; i++) {
        /* out[i].lat, out[i].lon, out[i].accuracy_m, out[i].confidence, ... */
    }
}
```

The key idea: the library allocates nothing and keeps no state between calls. You
own every buffer, you pass your inputs in, and it writes the results into the
output buffer you handed it. The full library documentation has a complete,
compilable program and the full API reference.

## Inputs and outputs at a glance

- Per observation (`ts_obs_t`): `node_id`, `mac_hash` (4 bytes), `band`
  (0 wifi, 1 ble, 2 cell), `rssi` (dBm), `ts_ms`.
- Per anchor (`ts_anchor_t`): `node_id`, `lat`, `lon` (degrees), optional
  path-loss overrides, an `enrolled` flag.
- Per device out (`ts_position_t`): `mac_hash`, `band`, `lat`, `lon` (degrees),
  `accuracy_m`, `confidence` (40, 70, or 85), `measurement_count`, `peak_rssi`,
  and an error ellipse.

## Next

- Consuming the positions in your own system: see the Integration section of
  this guide.
- For everything in depth: see the full library documentation.
