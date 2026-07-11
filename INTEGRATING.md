# Integrating: consuming the positions

A brief guide to what you build on your side to use the positions the library
produces. This is the fast on-ramp. The full library documentation has the
complete version, with a boundary diagram and a responsibilities table.

## The boundary

The library is a pure function: detections plus anchor positions go in,
triangulated positions come out. It does not persist anything, has no clock, does
not track targets over time, does not render, and does not alert. Everything
downstream of "a position came out" is your system's job.

```
  detections + your anchor table  ->  [ library or CLI ]  ->  positions
                                                                 |
                                                                 v
                                                           YOUR SYSTEM
                                       (persist, track, threshold, render, alert, monitor)
```

## What comes out

Per device: latitude and longitude (degrees), `accuracy_m` (meters), a confidence
tier (40 for a 2-node approximate fix, 70 for exactly 3 nodes, 85 for 4 or more),
`measurement_count`, `peak_rssi`, and an error ellipse. The full library
documentation has the complete output contract.

## Three things to get right

1. There is no timestamp in the output. Stamp each position with your own receive
   time.
2. For a 2-node fix, the error ellipse cross-axis is a mirror-ambiguity bound, not
   a confidence radius. Do not draw it as a 1-sigma circle.
3. The library emits latitude then longitude. If your mapping stack expects
   longitude then latitude (as GeoJSON does), swap them.

## What you build (checklist)

- **Own the anchor table.** Maintain `node_id` to surveyed `lat`/`lon` (plus
  optional path-loss overrides and the `enrolled` flag). You also own windowing:
  you decide which detections form one batch, since the library does not subdivide
  by time. Batch under the caps (`TS_MAX_GROUPS` = 64, `TS_MAX_NODES_PER_GROUP` =
  32) or the core silently drops the overflow.
- **Stamp time and handle staleness.** Attach your own timestamp on receipt; age
  out and reject stale fixes before acting on them.
- **Persist.** Store the positions. A common shape is an append-only history plus
  a keep-latest current state per `(mac_hash, band)`. Treat each position as a
  current-state reading, not something to average blindly.
- **Correlate over time.** A real target produces a stream of fixes, not one
  point. Track per `(mac_hash, band)`; add a motion or track filter if you need
  smoothing.
- **Threshold on confidence and accuracy.** Gate on the confidence tier and
  `accuracy_m` for your use case. There is no universal threshold: treat 40 as
  low-trust and approximate, 85 as high, and calibrate the rest to your needs.
- **Render honestly.** Plot `lat`/`lon`; draw the ellipse for 3-or-more-node
  fixes; for 2-node fixes respect the mixed-semantics ellipse noted above.
- **Alert and dedupe.** If you raise alerts, dedupe them so a stream of fixes for
  one target does not spam. Decide what a low-confidence or no-fix result means
  for your logic.
- **Monitor no-output.** A target heard by fewer than 2 nodes, or one with
  degenerate or collinear anchor geometry, simply produces no position. Watch for
  this so "no output" is not mistaken for "nothing there."

## Next

For the full integration guide, with the boundary diagram, the responsibilities
table, and detailed per-topic guidance, see the full library documentation.
