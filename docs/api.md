# QML API Reference

---

## Importing

```qml
import FluxEngine 1.0
```

Make sure `QML2_IMPORT_PATH` points to the plugin build directory.
See [build.md](build.md) for setup instructions.

---

## Minimal Example

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

---

## FluxBackground.qml

A convenience wrapper around `FluxItem` with a built-in timer and FPS
counter. All `FluxItem` properties are exposed as aliases.

> `colorPreset` is aliased as `colorMode` in `FluxBackground.qml`.
> Both names refer to the same property.

```qml
FluxBackground {
    anchors.fill: parent
    running: true
    viscosity: 5.0
}
```

The `import "qml"` path works when your QML file is at the project root
or inside a `qml/` directory. Adjust the path if your file structure
differs.

---

## Properties

### Simulation Control

| Property | Type | Default | Description |
|---|---|---|---|
| `running` | bool | `false` | Start or stop the simulation. When `false`, the engine is idle. |
| `frameCount` | int | `0` | (read-only) Total frames rendered since start. |
| `simSize` | int | `128` | Internal simulation resolution (NxN). Higher = more detail, more GPU cost. |
| `debugMode` | int | `5` | What to show on screen. See [Debug Modes](#debug-modes). |

### Fluid Parameters

| Property | Type | Default | Description |
|---|---|---|---|
| `viscosity` | float | `5.0` | Fluid thickness. Higher = slower, smoother. Lower = fast and sharp. Range: 0.1-20.0 |
| `noiseMultiplier` | float | `0.45` | Noise energy. Higher = more turbulence. Set to 0 to watch the fluid wind down. Range: 0.0-2.0 |
| `timestep` | float | `0.01667` | Simulation speed (default = 1/60). Higher values speed things up but can cause instability. Range: 0.001-0.1 |
| `dissipation` | float | `0.0` | Energy loss per frame. 0 = fluid moves forever. Range: 0.0-1.0 |
| `pressureIterations` | int | `19` | Jacobi solver iterations per frame. More = more accurate. Range: 1-50 |

### Line Rendering

| Property | Type | Default | Description |
|---|---|---|---|---|
| `colorPreset` | int | `0` | Line color scheme. See [Color Presets](#color-presets) below. Range: 0-5 |
| `lineVariance` | float | `0.55` | Per-line randomness. Higher = more variation between lines. Range: 0.0-2.0 |
| `lineWidthMultiplier` | float | `1.0` | Line thickness multiplier. Range: 0.1-5.0 |
| `zoom` | float | `1.6` | Display zoom level. Higher = zoomed in. Range: 0.5-5.0 |

### Diagnostic

| Property | Type | Default | Description |
|---|---|---|---|
| `diagStep` | int | `5` | Initialization diagnostic. Leave at 5 for normal use. See [development.md](development.md). |

---

## Debug Modes

| Value | Name | Display |
|---|---|---|
| 0 | Heatmap | Velocity field: blue (slow) → yellow → red (fast) |
| 1 | Noise | Raw noise texture driving the fluid |
| 2 | Fluid | Raw velocity field (RG vectors) |
| 3 | Pressure | Pressure field |
| 4 | Divergence | Divergence field — errors the pressure solver corrects |
| 5 | Lines | Flow lines — the default and primary display mode |

---

## Color Presets

The `colorPreset` property accepts values 0 through 5. Internally these
map to three shader modes, with multiple palette options for Color Wheel:

| Value | Name | Description |
|---|---|---|
| 0 | Original | Blue/teal tones from velocity magnitude |
| 1 | Color Wheel — Plasma | Rainbow from flow direction angle (dark purple to orange) |
| 2 | Color Wheel — Poolside | Rainbow from flow direction angle (light blues) |
| 3 | Image Texture — Gradient | Diagonal gradient from a lookup texture |
| 4 | Image Texture — Noise | FBM noise texture as the color source |
| 5 | Color Wheel — Freedom | Blue and gold from flow direction angle |

---

## Signals

| Signal | Emitted when |
|---|---|
| `runningChanged()` | `running` changes |
| `frameCountChanged(int count)` | Each frame is rendered |
| `simSizeChanged()` | `simSize` changes |
| `debugModeChanged()` | `debugMode` changes |
| `colorPresetChanged()` | `colorPreset` changes |
| `viscosityChanged()` | `viscosity` changes |
| `noiseMultiplierChanged()` | `noiseMultiplier` changes |
| `timestepChanged()` | `timestep` changes |
| `dissipationChanged()` | `dissipation` changes |
| `pressureIterationsChanged()` | `pressureIterations` changes |
| `lineVarianceChanged()` | `lineVariance` changes |
| `lineWidthMultiplierChanged()` | `lineWidthMultiplier` changes |
| `zoomChanged()` | `zoom` changes |
| `diagStepChanged()` | `diagStep` changes |

---

## Methods

| Method | Description |
|---|---|
| `onFrameTick()` | Advance the simulation by one frame. Call this from a `Timer` every 16ms. Nothing happens without it. |

---

## Full Example

```qml
import FluxEngine 1.0
import QtQuick

FluxItem {
    id: fluid
    anchors.fill: parent
    running: true

    // Fluid
    viscosity: 8.0
    noiseMultiplier: 0.6
    timestep: 0.016667
    dissipation: 0.0
    pressureIterations: 19

    // Display
    debugMode: 5
    zoom: 1.6

    // Lines
    colorPreset: 1
    lineVariance: 0.55
    lineWidthMultiplier: 1.2

    Timer {
        interval: 16
        running: parent.running
        repeat: true
        onTriggered: parent.onFrameTick()
    }
}
```
