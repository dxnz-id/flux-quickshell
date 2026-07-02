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

## Features

- **Real-time Navier-Stokes fluid simulation** — 29-pass pipeline running
  on your GPU at 60 FPS
- **MacCormack advection** — high-accuracy fluid transport (forward +
  reverse + correction pass)
- **Flow lines** — spring-dynamics particle system that follows the fluid,
  creating organic animated strokes
- **6 color presets** — Original, 3 ColorWheel palettes, 2 ImageTexture variants
- **13 configurable parameters** — tune viscosity, speed, zoom, line
  thickness, and more from QML
- **5 debug views** — inspect velocity, pressure, noise, divergence, or
  the raw fluid field
- **Multi-monitor** — each screen runs its own independent simulation
- **Efficient** — separate GPU context, runs only when `running: true`

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
