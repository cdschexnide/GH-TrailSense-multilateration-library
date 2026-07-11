# TrailSense Multilateration Library

A dependency-free C library and CLI that turns raw multi-node RF detections plus
surveyed anchor positions into triangulated positions (latitude, longitude, an
accuracy estimate, and a confidence tier). The core allocates nothing on the
heap, keeps no global state, is reentrant, and links only `libm`.

## Quick look

Build it and run the bundled example:

```sh
cmake -S . -B build && cmake --build build
./build/trailsense-triangulate < examples/sample-input.json
```

That reads detections and anchor positions as JSON on stdin and writes
triangulated positions as JSON on stdout. You can also link the library and call
`ts_triangulate()` directly from C.

## Documentation

**Just want the essentials?** [Getting started](GETTING-STARTED.md) and
[Integrating](INTEGRATING.md) are a brief on-ramp: build it, call it, and consume
the output.

The full documentation is in [`docs/`](docs/), as five focused PDFs:

- **[Usage Guide](docs/TrailSense-Multilateration-Usage-Guide.pdf)**: quickstart,
  build and link, call from C, use the CLI, handle non-ideal results, and how the
  model works. Read this to learn and use the library.
- **[C API Reference](docs/TrailSense-Multilateration-C-API-Reference.pdf)**: the
  C functions, structs, enums, status codes, and the memory and capacity contract.
- **[Output Reference](docs/TrailSense-Multilateration-Output-Reference.pdf)**:
  the position output contract (coordinate frame, units, accuracy, confidence,
  error ellipse, JSON form). Applies to both the C API and CLI output.
- **[CLI Reference](docs/TrailSense-Multilateration-CLI-Reference.pdf)**: the
  `trailsense-triangulate` command input, output, and exit-status contract.
- **[Using the Output](docs/TrailSense-Multilateration-Using-the-Output.pdf)**:
  what you build in your own system to consume the positions.

## What it produces

For each device heard by enough nodes, one position: latitude and longitude in
degrees, an accuracy estimate in meters, a confidence tier, the participating
anchor count, and an error ellipse. Two anchors give an approximate fix; three or
more give a full multilateration fix. The C API Reference, the Output Reference,
and the Using the Output guide (all in [`docs/`](docs/)) explain how to consume
these.

## Tests

`ctest --test-dir build` runs the parity gate and a CLI golden test that validate
the build.

## Requirements

CMake 3.16 or newer and a C11 compiler. No other dependencies (links `libm`).
