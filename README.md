# vellum

A beautiful, lightweight, open-source native code editor built for everyone, from legacy
hardware to modern machines. Minimal, fast, cross-platform, and designed to stay out of
your way.

## Build

Vellum builds with Clang, CMake, and Ninja. SDL3 3.2.0 or newer is required.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release` (optimized) and `bench` (optimized with debug info, used for
the rendering benchmark). Each preset writes to its own directory under `build/`.

The debug preset enables AddressSanitizer and UBSan and treats warnings as errors.

## Architecture

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module boundaries and design.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan and exit criteria.
- [`docs/architecture/`](docs/architecture/) — approved architectural decisions (ADRs).

## Licence

See [LICENSE](LICENSE).
