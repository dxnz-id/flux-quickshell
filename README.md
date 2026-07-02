# flux-quickshell

A real-time fluid simulation screensaver for Hyprland lockscreens.
Smooth, flowing patterns — like ink spreading through water — rendered
entirely on your GPU at 60 FPS.

Built as a native plugin for **Quickshell**, the QML shell framework for
Wayland compositors.

> Inspired by [sandydoo/flux](https://github.com/sandydoo/flux). Thank you
> for the beautiful work.

<a href="https://www.ko-fi.com/dxnzid">
<img src="https://cdn.ko-fi.com/cdn/kofi3.png?v=3" width="160" alt="ko-fi" />
</a>

---

## Quick Start

```qml
import FluxEngine 1.0
import QtQuick

FluxItem {
    anchors.fill: parent
    running: true

    Timer {
        interval: 16
        running: parent.running
        repeat: true
        onTriggered: parent.onFrameTick()
    }
}
```

See [docs/api.md](docs/api.md) for all available properties.

## Build & Install

```bash
cd plugin
cmake -Bbuild
cmake --build build -j$(nproc)
export QML2_IMPORT_PATH="$PWD/build"
```

Full instructions: [docs/build.md](docs/build.md)

---

## Backstory

I've always been fascinated by the macOS Drift screensaver — the way colors
drift and bloom across the screen like ink in water. [sandydoo/flux](https://github.com/sandydoo/flux)
captured that magic beautifully for the web. This is my take on it: a native
screensaver for Hyprland lockscreens, built with C++ and OpenGL, running
at 60 FPS with no browser overhead.

## Features

- **Smooth, flowing simulation** — ink-like patterns that drift, bloom, and
  evolve endlessly at 60 FPS
- **Animated flow lines** — organic strokes that follow the fluid, creating
  mesmerizing motion trails
- **6 hand-crafted color schemes** — Original, Plasma, Poolside, Freedom,
  Silver, and Rainbow
- **Fully customizable** — tweak speed, viscosity, zoom, line thickness, and
  more from QML
- **Multi-monitor** — each screen runs its own independent simulation
- **5 debug views** — peek under the hood at velocity, pressure, noise,
  or divergence fields
- **Lightweight** — separate GPU context, only runs when visible

## Requirements

| Dependency | Version |
|---|---|
| Qt | 6.11+ |
| Quickshell | 0.2.1+ |
| OpenGL | 4.6 (most modern GPUs) |
| CMake | 3.20+ |

## Documentation

> **Start here:** [docs/build.md](docs/build.md) to get the plugin running, then
> [docs/api.md](docs/api.md) for how to use it.

| File | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the engine works under the hood |
| [docs/pipeline.md](docs/pipeline.md) | All 29 simulation phases, step by step |
| [docs/api.md](docs/api.md) | QML properties, signals, and usage examples |
| [docs/build.md](docs/build.md) | Building, installing, and CI releases |
| [docs/development.md](docs/development.md) | Debugging, testing, and known Qt bugs |

## Credits

- [sandydoo/flux](https://github.com/sandydoo/flux) — The project that
  inspired this. Thank you for making it open source.
- [outfoxxed/quickshell](https://github.com/outfoxxed/quickshell) — The
  shell framework this plugin is built for.
