# flux-quickshell

Fluid simulation screensaver untuk Hyprland lockscreen, terinspirasi
[sandydoo/flux](https://github.com/sandydoo/flux). Dibangun sebagai
Quickshell QML component dengan C++ QRhi rendering plugin.

## Arsitektur

**C++ QRhi plugin** (`FluxEngine` QML namespace) — BUKAN QML `ShaderEffect` chain.
Navier-Stokes solver dijalankan sebagai multi-pass GPU pipeline via Qt RHI:

- 28 phases per frame (MacCormack advection → Jacobi diffusion ×3 →
  3D simplex noise → divergence → Jacobi pressure ×19 → subtract gradient)
- Internal textures: RGBA16F (velocity, advection, noise) dan R32F (pressure, divergence)
- Display via QSGSimpleTextureNode wrapping engine displayTex (RGBA8 heatmap 256×256)
- CPU-generated 3D simplex noise (1:1 port dari WGSL reference), upload RGBA16F per frame

## Status

### Selesai

- Full Navier-Stokes pipeline (28-phase) via C++ QRhi — 1:1 dengan flux-reference
- MacCormack advection (forward + reverse + adjust)
- Jacobi diffusion (3 iterasi) dan Jacobi pressure (19 iterasi)
- 3D simplex noise CPU dengan channel blending + scale oscillation — 1:1 reference
- 5 debug modes: Normal (heatmap), Noise, Fluid (raw velocity), Pressure, Divergence
- `FluxItem` QQuickItem + `FluxEngine` QML plugin
- `FluxBackground.qml` fullscreen component

### Dalam Progress

- Line rendering (spring-dynamics particle system)
- Integrasi ke dots-hyprfork lockscreen
- Lock state machine (Flux mode vs Normal mode)

## Build

Requires Qt 6.11+, Quickshell 0.2.1.

```bash
# Build plugin
cd plugin && cmake -Bbuild && cmake --build build -j8

# Shader compilation via cmake (qsb --glsl "440")
```

Gunakan `QML_IMPORT_PATH=plugin/build` untuk mengimpor modul `FluxEngine 1.0`.

## Penggunaan QML

```qml
import FluxEngine 1.0

FluxItem {
    anchors.fill: parent
    simSize: 128
    running: true
    debugMode: 0  // 0=Normal 1=Noise 2=Fluid 3=Pressure 4=Divergence
}
```

Lihat `AGENTS.md` untuk detail arsitektur dan konteks development lengkap.
