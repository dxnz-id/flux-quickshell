# AGENTS.md — flux-quickshell

Dokumen ini adalah konteks utama untuk semua AI agent yang bekerja di repo ini.
Baca seluruh dokumen sebelum mengerjakan task apapun.

---

## CRITICAL: Pipeline Telah Migrasi ke C++ QRhi Plugin

Simulasi fluida sekarang dijalankan sebagai **C++ QRhi plugin** (`FluidSim` QML namespace),
BUKAN QML ShaderEffect chain. Ini perubahan arsitektur fundamental.

### Implikasi

- **Multi-sampler binding BERFUNGSI penuh** di C++ QRhi via `QRhiShaderResourceBinding`.
  Constraint 1 sampler hanya berlaku untuk QML `ShaderEffect`, tidak untuk pipeline solver.
- **Parameter via uniform buffer** berfungsi penuh via C++.
- **Unbuked encoding** tidak diperlukan untuk pipeline solver (RGBA16F internal textures).
- **Custom vertex shader** berfungsi penuh di C++ QRhi pipeline.
- **Compute shader (`QRhiComputePipeline`)** — **TERVERIFIKASI** support di OpenGL 4.6 GLES2 backend.
  `QRhiComputePipeline::create()` sukses, dispatch bekerja, `imageStore` dengan `UsedWithLoadStore` berfungsi.
- **Storage buffer (`QRhiBuffer::StorageBuffer`)** — API tersedia (`bufferLoadStore` binding + `StorageBuffer` usage),
  tapi `imageStore` adalah jalur yang lebih stabil untuk komunikasi compute → fragment.
- Pipeline tidak lagi dibatasi format `ShaderEffectSource` (RGBA8 clamp).
- Internal textures: `RGBA16F` (velocity, advection, noise) dan `R32F` (pressure, divergence).

### Yang Lama (dianggap histori)

`ShaderEffect` + `ShaderEffectSource` multi-pass chain. Ditinggalkan karena limitasi
1 sampler per ShaderEffect membuat MacCormack advection tidak bisa diimplementasikan.

---

## Apa Ini

Fluid simulation screensaver untuk lockscreen Hyprland, terinspirasi
dari [sandydoo/flux](https://github.com/sandydoo/flux). Dibangun sebagai
Quickshell QML component yang diintegrasikan ke fork end-4 dots-hyprland.

Ini adalah **fresh start**. Iterasi sebelumnya (di project terpisah) sudah
menemukan beberapa lesson learned kritis yang dicatat di bawah — jangan
re-discover bug yang sama.

---

## Lokasi di Filesystem

```
/home/dxnz/Downloads/flux-port/
├── dots-hyprfork/       ← fork end-4 dots-hyprland
├── flux-reference/      ← clone sandydoo/flux (REFERENSI SAJA, jangan copy-paste)
└── flux-quickshell/     ← repo ini (project root, jalankan opencode dari sini)
```

Path referensi WGSL: `../flux-reference/flux/src/shaders/`
(relatif dari root `flux-quickshell/`)

---

## Struktur Repo

```
flux-quickshell/
├── AGENTS.md
├── README.md
├── .gitignore
├── qml/                    ← komponen QML (FluxBackground.qml, dll), DI-TRACK GIT
├── plugin/
│   └── shaders/            ← C++ pipeline shader source (.frag/.vert/.comp), DI-TRACK GIT
└── dev/
    ├── shader-sandbox/     ← standalone Qt app untuk test shader
    │   └── build/          ← GITIGNORED, regenerable via cmake
    ├── notes/               ← dokumentasi teknis
    └── scratch/             ← eksperimen bebas, GITIGNORED
```

---

## Lesson Learned (WAJIB DIPATUHI — dari iterasi sebelumnya)

| Aturan                                                                          | Alasan                                                                                                      |
| ------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `qsb --glsl "440"`, BUKAN `--qt6`                                               | Shader butuh `texelFetch`/`textureSize` yang hanya support di GLSL 330+; `--qt6` output ESSL 100 tidak support |
| GLSL source HARUS `#version 420` (bukan 150)                                      | `layout(binding=N)` untuk sampler dan `layout(location=N)` untuk I/O hanya support di GLSL 420+; qsb --glsl 440 akan compile ke 440 |
| Tidak ada custom vertex shader                                                  | Segfault di Qt 6.11 standalone; diverifikasi AMAN di Quickshell runtime (2025-06-18) |
| Bias encoding `v*0.5+0.5` untuk signed values                                   | `ShaderEffectSource` format RGBA8 clamp negatif ke 0                                                        |
| Parameter via params texture (Canvas → ShaderEffectSource), BUKAN uniform block | Qt ShaderEffect tidak support QML property → uniform block member mapping (sudah diverifikasi runtime FAIL) |
| Source code TIDAK BOLEH hanya ada di `build/`                                   | Risiko hilang permanen — `build/` regenerable, source code tidak                                            |
| Commit di setiap milestone                                                      | Hindari kehilangan progress jika session tertutup tidak sengaja                                             |
| Nama file context: `AGENTS.md` (dengan S)                                       | Konvensi OpenCode — dibaca otomatis, beda dari `AGENT.md` yang tidak dikenali                               |
| C++ plugin: QSGRenderNode + QSGSimpleTextureNode                                | `QSGRenderNode` draw tidak visible karena scissor test; `QSGSimpleTextureNode` wrapping `displayTex` via `createTextureFromRhiTexture()` adalah workaround yang terverifikasi |
| `QQuickItem::update()` (bukan `window()->update()`) untuk animasi frame-by-frame | `window()->update()` di threaded mode tidak trigger sync, jadi `updatePaintNode()` tidak pernah dipanggil. `QQuickItem::update()` mark item dirty + trigger sync via `QQuickWindowPrivate::dirty(Content)`. |

---

## Arsitektur

- **Plugin**: C++ QML module di `plugin/`, build via CMake. Output: `libfluidsim.so` + `libfluidsimplugin.so`.
- **Rendering**: Qt RHI, backend OpenGL 4.6 (default di sistem target, bukan Vulkan).
- **Shader compile**: `qsb --glsl "440"` (bukan `--qt6`), karena ESSL 100 output tidak support `texelFetch`/`textureSize`.
- **Engine (C++)** `FluidSimEngine`: Semua pipeline solver (advection, diffusion, pressure, noise) via QRhi draw commands.
  Multi-pass sequential dalam satu `QRhiCommandBuffer` via `beginPass`/`endPass` pairs.
- **Display**: `FluidSimItem` (QQuickItem) → own GL context (share dengan SG) + own QRhi. Engine step di `EngineStepJob` (QRunnable) via `QQuickWindow::AfterSwapStage`. Display readback `displayTex` → `QImage` → `QSGImageNode` di `updatePaintNode()`. Ini adalah satu-satunya pipeline stabil untuk separate QRhi architecture di Qt 6.11.
- **Noise**: CPU-generated hash-based value noise (3 octave fbm), upload RGBA16F setiap frame.
- **Uniform blocks**: Parameter hardcode di shader karena QSB strips `layout(binding=N)` dari GLSL 440 output,
  dan Qt RHI OpenGL backend tidak rekonstruksi dari SPIR-V reflection.
- **Quickshell version**: 0.2.1 (revision 7511545) linked against Qt 6.11.1 (sama dengan sandbox)
- **Target integrasi**: Quickshell lockscreen di dots-hyprfork:
  - Lock entry: `dots/.config/quickshell/ii/shell.qml` → `IllogicalImpulseFamily` → `Lock` → `LockScreen` → `LockSurface`
  - Lock path aktual (ii): `dots/.config/quickshell/ii/modules/ii/lock/LockSurface.qml`
  - Lock path aktual (waffle): `dots/.config/quickshell/ii/modules/waffle/lock/WaffleLock.qml`
  - LockScreen base (common): `dots/.config/quickshell/ii/modules/common/panels/lock/LockScreen.qml`
  - LockContext (PAM + state): `dots/.config/quickshell/ii/modules/common/panels/lock/LockContext.qml`

### State Machine Lockscreen (Target Akhir)

```
Manual lock (Super+L)  →  wallpaper + UI langsung visible
Idle timeout            →  Flux background, UI hidden
Mouse/keyboard saat Flux →  UI fade in
Idle 3s di lockscreen    →  UI fade out
Password benar (Flux)    →  fade out 600ms → desktop
Password benar (Normal)  →  langsung ke desktop
```

Detail mekanisme IPC (GlobalShortcut/IpcHandler) perlu diverifikasi ulang
di source Quickshell aktual sebelum implementasi — JANGAN asumsikan
`hyprctl dispatch global` tanpa cek dulu.

### Verified Lock IPC Mechanism (2025-06-18)

Dari source aktual `dots-hyprfork`:

- **Lock trigger**: `LockScreen.qml` punya `IpcHandler { target: "lock"; function activate() { root.lock(); } }`
  dan `GlobalShortcut { name: "lock"; onPressed: root.lock }`. Keduanya manggil `root.lock()`.
- **Lock function**: `GlobalStates.screenLocked = true` → `WlSessionLock.locked = true` →
  `WlSessionLockSurface` muncul. **Bukan** `hyprctl dispatch global`.
- **IpcHandler pattern**: `target` string (e.g., "lock", "bar", "panelFamily") + methods
  (`activate()`, `focus()`, etc.). Dipanggil via Quickshell IPC internal (bukan DBus atau hyprctl).
- **GlobalShortcut pattern**: `name` + `description` + `onPressed`/`onReleased`.
  Didaftarkan di Hyprland sebagai keybind. Nama didaftarkan via `quickshell.shortcuts` config.
- **Custom signal for lockFlux**: Bisa add `IpcHandler { target: "lockFlux"; function activate() { ... } }`
  dan `GlobalShortcut { name: "lockFlux"; ... }` di `LockScreen.qml` untuk trigger berbeda.
  Atau tambah property baru di `GlobalStates.qml` seperti `fluxMode: bool`.

---

## Pipeline Simulasi (Navier-Stokes)

Urutan render pass per frame (28 phases, eksekusi dalam satu `step()` call):

```
advect_fwd (0) → advect_rev (1) → adjust (2) → diffuse×3 (3-5)
  → inject_noise (6) → divergence (7) → pressure×19 (8-26)
  → subtract_gradient (27) → displayTex heatmap
```

Timer-driven loop (16ms interval, FluidSimItem → QSGRenderNode.prepare() → engine step).

Detail matematika dan parameter default harus didokumentasikan di
`dev/notes/navier-stokes-ref.md` berdasarkan analisis WGSL di
`../flux-reference/flux/src/shaders/` — bukan dari asumsi atau memori.

---

## Referensi Teknis

- WGSL asli: `../flux-reference/flux/src/shaders/` (baca untuk paham logika, JANGAN copy-paste)
- Parameter default: `../flux-reference/flux/src/settings.rs` (atau lokasi serupa, verifikasi path aktual)
- Quickshell docs: https://quickshell.outfoxxed.me/docs/
- Quickshell source: https://github.com/outfoxxed/quickshell
- dots-hyprfork structure: `../dots-hyprfork/dots/.config/quickshell/`
- Qt ShaderEffect: https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html
- Qt qsb tool: https://doc.qt.io/qt-6/qtshadertools-qsb.html

---

## Current State

> **Update bagian ini secara manual setiap ada progress signifikan.**
> Jangan biarkan bagian ini telat update dibanding kode aktual.

### Selesai

- [x] Setup struktur folder dan AGENTS.md
- [x] Analysis WGSL flux-reference + `dev/notes/navier-stokes-ref.md`
- [x] Setup `dev/shader-sandbox/` — compile dan runtime verification phase 1
- [x] Discovered Qt 6.11 critical bugs (QML ShaderEffect):
  - Multi-sampler ShaderEffect bug (output flat)
  - Premultiplied alpha storage in layers
  - qsb --qt6 rejects bare uniforms
- [x] **Migrasi ke C++ QRhi plugin** — pipeline solver via QRhi draw commands
- [x] Pipeline 28 phases: advect_fwd → advect_rev → adjust → diffuse×3 → inject_noise → divergence → pressure×19 → subtract_gradient
- [x] MacCormack advection implemented (forward + reverse + adjust, matching flux-reference)
- [x] CPU-generated hash-based value noise (3 octave fbm), upload RGBA16F per frame
- [x] Uniform buffer parameter passing (works in C++ QRhi)
- [x] Display pipeline: velocityTex → display_frag (heatmap) → displayTex RGBA8 128×128
- [x] **Display via QSGSimpleTextureNode**: `QQuickWindow::createTextureFromRhiTexture()` wrapping engine's displayTex
- [x] Engine step in QSGRenderNode::prepare() — runs before scene graph main pass
- [x] Frame readback verification (half-float → float → print at every 5th frame)
- [x] Verified stable simulation (no blow-up, values evolve every frame)
- [x] `FluidSimItem` QQuickItem with `FluidSim` QML plugin
- [x] `FluxBackground.qml` — fullscreen component wrapping FluidSimItem
- [x] qsb compiler flag: `--glsl "440"` (not `--qt6`) karena butuh texelFetch/textureSize
- [x] **Fixed index order in QSGGeometryNode display quad**: `{0,1,2, 1,2,3}` (bukan `{0,1,2, 0,2,3}`) untuk `GL_TRIANGLES`. Indeks `{0,2,3}` kedua membuat kedua triangle share LEFT EDGE (v0-v2), overlap di left half, miss right half → coverage 75%. Indeks `{1,2,3}` membuat mereka share diagonal v1-v2, form solid quad → coverage 100%.
- [x] **100% window coverage verified** via grim capture + ImageMagick quadrant analysis (mean.g 0.9997+ di semua quadrant dengan solid green test shader)
- [x] **Debug mode system**: 5 mode display (Normal/Fluid/Noise/Pressure/Divergence) dengan `display_debug.frag` (bias decode + contrast 2.0, match referensi). Display texture 256×256 RGBA8. Mode switching via `FluidSimItem.debugMode` property + 5 tombol QML.
- [x] **Diffuse boundary safety fix**: Added `clampPos()` to `pass_diffuse.frag` to prevent out-of-bounds `texelFetch` reads (matching reference's ClampToEdge sampler → Neumann implicit).
- [x] **All pipeline shaders verified for boundary safety** via reference WGSL comparison. No-slip only in `subtract_gradient`, NOT in diffuse. Component-wise (`bc.x=0` at x-walls, `bc.y=0` at y-walls) already correct in `pass_subtract.frag`.
- [x] **QSGTexture::Nearest** filtering on display texture to prevent bilinear blur.
- [x] **Diffuse formula param fix**: `stencil_factor` 0.25→0.0625, `center_factor` 0→12.0 menghitung ulang dari `dt=0.016667, visc=5.0`. `centerFactor=1/(visc*dt)=12.0`, `stencilFactor=1/(4+centerFactor)=0.0625`. Nilai lama (~1/4, ~0) membuat diffusion 4× terlalu agresif.
- [x] **Subtract gradient sampler fix**: `pressureTex` binding `nearest`→`linear`. Referensi pakai linear sampler untuk pressure gradient smooth. Dengan linear: `l=0.5*(p[i-1]+p[i])`, `r=0.5*(p[i]+p[i+1])` → gradien `=0.5*(r-l)=0.25*(p[i+1]-p[i-1])`. Nearest: gradien `=0.5*(p[i+1]-p[i-1])` (2× over-correction). Fix dari perbandingan WGSL reference.
- [x] **Noise 3D simplex C++ CPU (1:1 WGSL)**: Port `snoise3D` dari `generate_noise.comp.wgsl` ke C++ static functions. Output `simplexNoisePair(v) → (snoise(v), snoise(v+vec3(8,-8,0)))`.
- [x] **Channel blending CPU**: Update `updateNoiseChannels()` tick → `offset_1 += inc`; pada `offset_1 > 1000` blend ke `offset_2`; scale oscillate `* (1+0.15*sin(0.01*t*TAU))`. `CHANNEL_CFG.inc` diperbaiki ke (0.001, 0.006, 0.012) dari reference.
- [x] **Noise multiplier fix**: `m_noiseMultiplier` 0.1→0.45 (match `settings.rs`).
- [x] **Center-aligned UV sampling**: CPU noise menggunakan `(x+0.5)/size, (y+0.5)/size` (match reference `texel_position`).
- [x] **Inject noise 1:1 reference**: `pass_inject_noise.frag` → linear sampler untuk noiseTex, UV-based sampling `gl_FragCoord.xy/velSize`, `timestep * noise` scaling (`(1/60) * noise`).
- [x] **m_noiseMultiplier fixed** to 0.45 (match reference).

### Resolved (2026-06-24) — QSGRenderNode Crash + Display Pipeline
- [x] **QSGRenderNode crash fixed**: QSGRenderNode with beginPass/endPass in prepare() crashes on all platforms (headless + desktop Hyprland). Fix: use own QRhi (shared GL context) via `EngineStepJob` (QRunnable) at `QQuickWindow::AfterSwapStage`.
- [x] **Texture sharing across contexts verified**: No explicit sync (glFinish) needed — QRhi::endOffscreenFrame's glFlush is sufficient for shared GL context texture visibility on Mesa/AMD.

### Resolved (2026-06-24 PM)
- [x] **Display pipeline refactored for QSGImageNode**: `updatePaintNode()` now returns `QSGImageNode` from `window()->createImageNode()` instead of manual `QSGGeometryNode`. This is the KEY fix — nodes created via `QQuickWindow` factory methods (`createRectangleNode`, `createImageNode`) render correctly, while `new QSGGeometryNode()` or `new QSGSimpleTextureNode()` do NOT render in Qt 6.11 RHI mode.
- [x] **Readback-based display pipeline**: EngineStepJob reads back displayTex (RGBA8 256×256) every frame via `readBackTexture()`. `storeReadback()` stores data thread-safely. `updatePaintNode()` creates QImage from readback data → `createTextureFromImage()` → set on QSGImageNode. This bridges our separate QRhi's texture to the SG's texture system, bypassing `createTextureFromRhiTexture()` which doesn't work across QRhi instances.
- [x] **Readback throttling**: `m_readbackPending` atomic flag prevents readback queue buildup (only one in-flight readback at a time).

### Resolved (2026-06-24 Night) — Direct Texture Sharing All Approaches Failed
- [x] **`createTextureFromRhiTexture` cross-RHI crash confirmed**: crash (SIGABRT/SIGSEGV) saat digunakan dengan texture dari QRhi yang berbeda dari SG's Rhi. Tidak ada workaround via `getResource(RhiResource)` — pointer QRhi dari SG tetap crash.
- [x] **`QRhiTexture::createFrom(NativeTexture)` crash**: membuat texture wrapper di SG's Rhi yang wrapping GL memory milik engine. `createTextureFromRhiTexture` return texture valid tapi SG render crash.
- [x] **`QQuickWindowPrivate::createTextureFromNativeTexture` crash**: private API yang bikin QSGTexture dari native GLuint. Crash (SIGABRT) karena assertion internal Qt.
- [x] **GPU blit (`QOpenGLExtraFunctions::glCopyImageSubData`) crash**: blit dari engine displayTex ke SG displayTex, lalu SG render dari texture tujuan. Crash di `createTextureFromRhiTexture`.
- [x] **Kesimpulan**: Di Qt 6.11, tidak ada API (publik/private) yang bisa bikin QSGTexture wrapping native GL texture lintas QRhi instance. Readback-based display adalah satu-satunya pipeline yang stabil.
- [x] **CPU ~32%** pada 60fps — overhead dari `createTextureFromImage()` tiap frame (create texture GPU baru + upload 256KB). Acceptable untuk screensaver. Grab analysis confirmed semua mode debug render benar, coverage 100%.

### Resolved (2026-06-24 Late) — Animation Fix: `window()->update()` → `update()`
- [x] **Root cause found**: `QQuickWindow::update()` in threaded mode doesn't trigger sync → `updatePaintNode()` never called → display shows first frame forever. Fix: call `QQuickItem::update()` which marks item as dirty, forcing sync + `updatePaintNode()` every frame.
- [x] **Animation confirmed working**: interior pixel (64,64) evolves every frame (yellow→orange→red→orange) via heatmap display. Constant `000080ff` at (0,0) is correct — boundary velocity is always zero.
- [x] **Debug logging removed**: noisy `ENGINE STEP` and `DISPLAY pixel` logs cleaned up.

### Selesai (Line Rendering)
- [x] Phase 0: QRhi Compute + imageStore diverifikasi berfungsi di GLES2 backend (2026-06-24)
- [x] Phase 1: Compute shader untuk spring dynamics particle update (imageStore ke texture)
- [x] Phase 2: draw_lines shaders (instanced vertex + fragment, matching flux reference)
- [x] Phase 3: C++ pipeline integration + additive blend compositing ke displayTex
- [x] Phase 4: Fragment shader AA — fade*fwidth smoothEdges (2026-06-27)
- [x] Phase 5: Simplex noise (`snoise(vec3)`) in compute shader + noise uniforms (offset1/offset2/blendFactor) + `tickLineNoise()` C++ matching reference
- [x] Phase 6: 3 color modes (Original/ColorWheel/ImageTexture) with color texture binding
- [x] Phase 7: Endpoint rendering — `draw_endpoint_vs.vert` + `draw_endpoint_fs.frag` matching reference endpoint.wgsl (top/bottom premultiplied alpha + side detection + smoothEdges)

### Selesai
- [x] **Configurable simulation parameters via QML** — 10 parameters exposed via Q_PROPERTY (colorMode, viscosity, noiseMultiplier, timestep, dissipation, pressureIterations, lineVariance, lineWidthMultiplier, zoom, msaaSampleCount). `m_fluidUniformBuf` revived and bound at `binding=8` to all solver shaders. `GpuNoiseParams.noiseMultiplier` replaces hardcoded NOISE_MULT.
- [x] **MSAA 4x configurable** — `msaaSampleCount` Q_PROPERTY (1/2/4, default 4). `createDisplayPass()` creates MSAA + resolve textures. `recreateLineGraphicsPipelines()` ensures pipelines match current MSAA sample count.
- [x] **RGBA32F state texture** — `m_lineStateTex` format RGBA16F → RGBA32F. Compute shader `layout(rgba32f)` must match C++ `QRhiTexture::RGBA32F` — mismatch silently corrupts state data.
- [x] **Config.qml bridge** — `fluid {}` section added to dots-hyprfork Config.qml with all 10 parameters.
- [x] **Large dead code cleanup** — removed Direction/PushConstants structs, 4 unused getters, 6 unused #include, 10 stale .qsb dari git, `*.qsb` gitignored. Deleted orphan QML (`test_fluid.qml`, `compare-main.qml`, `line-test.qml`, `quickshell-test/*`, `shell.qml`, `FluxSimulation.qml`), root `shaders/`, `qml/shaders` symlink, `plugin/compiled/`.
- [x] **Sandbox pure black background** — `#0d0d1a` → `#000`
- [x] **QTimer → QML Timer** — `onFrameTick()` pindah ke public slots, C++ QTimer dihapus. FluxBackground, test_fluid, shell.qml pake Timer 16ms di QML.
- [x] **Opaque black display clear color** — `QColor(0,0,0,0)` → `QColor(0,0,0,255)` di display render pass, mencegah window background tembus.
- [x] **QSB search path fix** — `FluidSimShaders::shaderPath()` parse `QML2_IMPORT_PATH` env var untuk quickshell compatibility.

### In Progress
- [ ] Quickshell lockscreen integration — `FluxBackground.qml` → `LockScreen.qml`

### Belum Dimulai
- [ ] Integrasi ke dots-hyprfork lockscreen (`LockScreen.qml` + `GlobalStates`)
- [ ] Lock state machine (Flux mode vs Normal mode)

### Known Issues

- 2026-06-24: `QSGGeometryNode` dan `QSGSimpleTextureNode` yang dibuat manual dengan `new` TIDAK RENDER di Qt 6.11 RHI mode pada Hyprland/Wayland. Fix: gunakan `window()->createRectangleNode()` atau `window()->createImageNode()` untuk node yang render dengan benar.
- 2026-06-24: `QQuickWindow::createTextureFromRhiTexture()` tidak bekerja untuk texture dari QRhi yang berbeda (separate context). Fix: readback→QImage→createTextureFromImage pipeline.
- 2026-06-24: Qt 6.11 cleanup ordering bug: `releaseResources()` dipanggil dari item destructor setelah GL context di-destroy. Tidak fixable dari plugin side. Crash pada app exit saat `QRhi` destructor.
- 2026-06-25: **QSB loading priority bug**: `FluidSimShaders::loadShader()` searches `applicationDirPath() + "/shaders/"` FIRST. Sandbox app (`dev/shader-sandbox/build/shader_sandbox`) has old QSB copies in its own `build/shaders/` dir that take priority over newly compiled ones in `plugin/build/FluidSim/shaders/`. Fix: sandbox CMake copies from plugin build dir (`../../plugin/build/FluidSim/shaders/`) instead of stale `shaders/compiled/`. **If you change shaders and the app still shows old behavior, check that QSB files in the sandbox build dir are updated.**
- 2026-06-26: **Tiled texture layout required for state texture**: Line state texture must use `{256, texH}` format, NOT `{stateTexels, 1}` (very wide 1-row). `texelFetch` in vertex shader returns zeros when texture width exceeds implementation-dependent threshold on GLES2 backend. Always tile state textures to max width 256.
- 2026-06-27: **RGBA32F required for state texture** — `m_lineStateTex` format changed from RGBA16F to RGBA32F. `initLineState()` data updated from `qfloat16*` to `float*`. Compute shader `layout(rgba16f)` → `layout(rgba32f)` — **FORMAT MUST MATCH** between C++ and GLSL, otherwise state data silently corrupts.

---

## Configurable Simulation Parameters

Semua simulation parameter bisa di-set via QML properties pada `FluidSimItem` (di-expose via `FluidSimItem` Q_PROPERTY dan di-bridge ke shader via uniform buffer `FluidUniforms` pada binding=8 atau `GpuNoiseParams` pada binding=0).

| QML Property | Type | Default | Range | Shader Binding | Effects |
|---|---|---|---|---|---|
| `colorMode` | int | 0 | 0-2 | `LineUniforms.color_mode` | Line color scheme: 0=Original, 1=ColorWheel, 2=ImageTexture |
| `viscosity` | float | 5.0 | 0.1-20.0 | `FluidUniforms` → diffuse | Kekentalan fluida. Semakin besar, fluida lebih kental (diffuse lebih lambat) |
| `noiseMultiplier` | float | 0.45 | 0.0-2.0 | `GpuNoiseParams.noiseMultiplier` | Kekuatan noise penggerak fluida |
| `timestep` | float | 0.016667 | 0.001-0.1 | `FluidUniforms` → advect/adjust/inject | Kecepatan simulasi per frame. Default = 1/60 |
| `dissipation` | float | 0.0 | 0.0-1.0 | `FluidUniforms` → advect | Energy loss per frame. 0 = no loss |
| `pressureIterations` | int | 19 | 1-50 | C++ loop count | Kualitas solver pressure. Lebih tinggi = lebih akurat, lebih berat |
| `lineVariance` | float | 0.55 | 0.0-2.0 | `LineUniforms.line_variance` | Seberapa wiggly garis flow |
| `lineWidthMultiplier` | float | 1.0 | 0.1-5.0 | `LineUniforms.line_width` | Scale ketebalan garis |
| `zoom` | float | 1.6 | 0.5-5.0 | `LineUniforms.zoom` | Zoom level tampilan |
| `msaaSampleCount` | int | 4 | 1,2,4 | C++ `m_rpDescRGBA8` | MSAA sample count untuk line rendering |

### Cara pakai via QML

```qml
FluidSimItem {
    viscosity: 8.0
    noiseMultiplier: 0.6
    colorMode: 1
    pressureIterations: 25
}
```

### Cara integrasi dengan Config.qml (dots-hyprfork)

Di `ii/modules/common/Config.qml`, tambah section `fluid`:
```qml
property JsonObject fluid: JsonObject {
    property int colorMode: 0
    property real viscosity: 5.0
    property real noiseMultiplier: 0.45
    property real timestep: 0.016667
    property real dissipation: 0.0
    property int pressureIterations: 19
    property real lineVariance: 0.55
    property real lineWidthMultiplier: 1.0
    property real zoom: 1.6
    property int msaaSampleCount: 4
}
```

Di `FluxBackground.qml` atau view yang pake `FluidSimItem`:
```qml
import qs.modules.common

FluidSimItem {
    colorMode: Config.options.fluid.colorMode
    viscosity: Config.options.fluid.viscosity
    // ... dst
}
```

### Aliran data

```
Config.options.fluid.viscosity (JSON file persisted)
  → FluxBackground.qml property binding
    → FluidSimItem.viscosity (Q_PROPERTY)
      → FluidSimEngine::setViscosity(8.0)
        → m_paramsDirty = true
        → step(): updateUniforms() → upload GPU buffer
        → pass_diffuse.frag baca u.uStencilFactor, u.uCenterFactor
```

---

## CRITICAL: qsb Compilation Flag (C++ Pipeline Shaders)

**SELALU gunakan `--glsl "440"` untuk shader yang dipakai di C++ QRhi pipeline.**

```bash
qsb --glsl "440" input.frag -o output.qsb
```

### Kenapa

Shader di C++ QRhi pipeline menggunakan `texelFetch` dan `textureSize` yang butuh
GLSL 330+ atau ESSL 100+. QSB dengan `--qt6` mengompilasi untuk ESSL 100 (OpenGL ES)
yang tidak support texelFetch. Dengan `--glsl "440"`, output mencakup GLSL 440
yang support texelFetch, sementara Qt RHI OpenGL backend menggunakan GLSL 330+.

### Note

- `layout(binding=N)` didukung di GLSL 420+. QSB output untuk GLSL 440 tetap
  mempertahankan binding.
- Shader untuk display (display_frag.frag, screen_display.frag) bisa pakai
  `--qt6` karena hanya pakai `texture()` biasa, tapi pipeline konsisten pakai `--glsl "440"`.

### ShaderEffect QML (Referensi Historis)

Shader yang dipakai di `ShaderEffect` QML HARUS pakai `--qt6`.
Contoh: shader di `qml/shaders/` untuk FluxSimulation.qml (referensi historis).

Testing: Selalu test `ShaderEffect` baru dengan warna solid dulu sebelum logika
kompleks. Jika output tidak sesuai tanpa error di terminal, cek dulu flag
compile sebelum curiga ke logika shader.

---

## Constraints

- Jangan copy-paste source dari `../flux-reference/` — pahami logika, tulis ulang
- Jangan edit file di `dots-hyprfork` yang bisa tertimpa update upstream
  (hindari folder yang bukan `custom/` kecuali memang harus dimodifikasi)
- Jangan gunakan custom vertex shader — segfault di Qt 6.11 standalone; diverifikasi AMAN di Quickshell runtime (2025-06-18)
- Selalu commit ke git setelah progress signifikan — JANGAN biarkan file
  penting hanya ada di direktori `build/` yang gitignored
- Code comment dalam bahasa Inggris, dokumentasi dan narasi dalam bahasa Indonesia
- Selalu cite file atau dokumentasi spesifik saat propose implementasi.
  Jika tidak bisa, tandai eksplisit sebagai "perlu verifikasi"

## Critical Discoveries (Iterasi Saat Ini)

> **Catatan**: Bagian ini mendokumentasikan constraint QML `ShaderEffect` approach
> yang telah ditinggalkan. Pipeline final menggunakan C++ QRhi plugin yang tidak
> memiliki constraint ini. Disimpan sebagai referensi historis.

### Multi-Sampler Bug di Qt 6.11 (Historis — QML ShaderEffect only)

ShaderEffect dengan LEBIH DARI SATU `layout(binding=N) uniform sampler2D`
(binding 0, 1, ...) GAGAL membaca texture kedua dengan benar.

**Verified via test 2025-06-17**:
- 1 sampler + `texture()` calls multiple times (berbeda UV) → **OK** ✓
- 2 sampler (`binding=0` + `binding=1`) dengan QML `property var` berbeda → **FAIL** ✗
  - texA (binding 0) membaca texture pertama dengan benar
  - texB (binding 1+) membaca texture YANG SALAH — bukan texture yang ditentukan
  - Tidak ada error message dari Qt atau QSB; hanya data yang tidak cocok
  - Efek terjadi dengan tepat 2 sampler (bukan hanya 3+)

**All existing working shaders** (divergence, subtract_gradient, dll) hanya pakai
1 sampler dengan multiple `texture()` calls pada UV offset berbeda.

**WORKAROUND**: encode semua field dalam SATU texture RGBA8, hanya gunakan
binding 0. Untuk operasi yang butuh multiple texture input, gunakan multi-pass
sequential (beberapa ShaderEffect berantai).

### Premultiplied Alpha pada Layer

Qt 6 secara internal menggunakan **premultiplied alpha**. ShaderEffect output
`(R, G, B, A)` disimpan sebagai `(R*A, G*A, B*A, A)` di layer. Shader berikutnya
yang membaca texture via `texture()` mendapatkan nilai **premultiplied**.

**WAJIB output `A = 1.0`** di setiap fragColor. Dengan A=1.0, premultiplication
tidak mengubah nilai RGB. Jika A < 1.0, shader berikutnya perlu decode:
`trueColor = sampledColor / sampledAlpha` — dan A bisa 0 (velocity = -1),
menyebabkan division by zero.

### qsb --qt6 Rejects Bare Uniforms

`qsb --qt6` pertama kompilasi ke SPIR-V (Vulkan), yang melarang bare uniforms:
```glsl
uniform float uResolution;  // ERROR: not allowed in Vulkan GLSL
```

Solusi alternatif yang SUDAH DICOBE dan GAGAL:
- Uniform block (`layout(binding=1, std140) uniform Params { float u; };`) —
  kompilasi OK, tapi QML property → UBO member mapping tidak jalan
- Bare uniform → qsb reject
- Kesimpulan: **TIDAK BISA passing parameter via uniform/UBO ke ShaderEffect**

**WORKAROUND**: Hardcode parameter (seperti `const float RES = 128.0;`)
langsung di GLSL source. Untuk parameter dinamis (time, timestep), perlu
di-encode ke dalam texture channel.

### GrabToImage Intermittent di Wayland

`grabToImage()` terkadang tidak menulis file ketika stdout/stderr di-redirect
ke `/dev/null`. Bekerja konsisten saat stderr dibiarkan terhubung ke terminal.
Kemungkinan Wayland compositor scheduling issue.

### Channel-Packed Pipeline: Satu Texture, Banyak Field

Dengan constraint di atas, layout texture 128×128 RGBA8 yang WORKING:
- **RG**: velocity (bias-encoded signed vec2)
- **B**: pressure atau divergence (bias-encoded scalar, multiplexed)
- **A**: 1.0 (alpha, tidak untuk data)

Pipeline projection 3-pass (per iterasi):
1. **Baca velocity dari RG, pressure dari B** (satu texture, 1 sampler)
2. **Hitung divergence** dari velocity neighbors (central difference)
3. **Hitung pressure** dari pressure neighbors + divergence (Jacobi: 0.25*(Σp - div))
4. **Output** ke RGBA8: RG=velocity (pass-through), B=pressure_baru, A=1.0

Chain: diff3 → press1(div+Jacobi) → press2(div+Jacobi) → ... → press19 → subtract_gradient

Divergence dihitung ON-THE-FLY dari velocity neighbors di setiap pass
(lebih murah daripada iterasi terpisah dengan constraint channel terbatas,
karena biaya texture fetch dari 1 sampler mendominasi).

### 1×1 Rectangle untuk Dynamic Time Passing

Dynamic time (iTime) bisa dipassing ke shader tanpa uniform melalui
Rectangle 1×1 + ShaderEffectSource:

```qml
property real t: 0.0
Timer { interval: 40; running: true; repeat: true;
    onTriggered: { t = (t + 0.02) % 1.0; } }
Rectangle {
    id: timeRect; width: 1; height: 1; visible: false
    color: Qt.rgba(t, t, t, 1.0)
}
ShaderEffectSource {
    id: timeSource
    sourceItem: timeRect; live: true; hideSource: true
}
```

Shader membaca `timeTexture` yang merupakan 1×1 ShaderEffectSource.
Karena Rectangle di-render oleh Qt Quick engine (bukan shader), warna
bisa diubah via QML property t tanpa perlu uniform GLSL.

**Constraint**: Untuk noise shader yang butuh velocity + time dalam
1 sampler, perlu prep-pass: baca velocity texture, encode time ke B
channel, output ke satu texture. Nanti di pipeline final dengan
FrameAnimation Quickshell.

### Image { source: ShaderEffectSource } Tidak Bekerja di Qt 6.11

`Image { source: srcInit }` tidak dapat menampilkan ShaderEffectSource yang
`live: true; hideSource: true` — output hitam pekat tanpa error. **WORKAROUND**:
Gunakan ShaderEffect passthrough sebagai display:

```qml
ShaderEffect {
    width: simSize; height: simSize
    property var simTex: srcInit
    fragmentShader: "shaders/passthrough.qsb"
}
```

Shader passthrough minimal (`shaders/src/passthrough.frag`):
```glsl
#version 420
layout(binding = 0) uniform sampler2D simTex;
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = texture(simTex, qt_TexCoord0); }
```

### MacCormack Implementation (C++ QRhi Plugin)

MacCormack advection correction (forward → reverse → adjust) diimplementasikan
penuh di pipeline C++ QRhi:

1. **Forward advection** (pass_advect.frag): semi-Lagrangian backward → `advectionFwdTex`
2. **Reverse advection** (pass_advect_rev.frag): direction=-1.0 → `advectionRevTex`
3. **Adjust** (pass_adjust.frag): `vel = forward + 0.5 * (vel_original - reverse)`, clamped

Multi-sampler binding (`advectionFwdTex` + `advectionRevTex` + `vel`) berfungsi
penuh di QRhi via `QRhiShaderResourceBinding`.

Tidak ada limitasi channel packing atau multi-pass — semua binding bekerja di QRhi.

### 1 Pressure Jacobi Iterasi: ∇p ≈ 0 di Interior

Pipeline 3-pass (init → divergence+pressure → subtract_gradient) dengan
1 Jacobi iteration untuk pressure menghasilkan:

- **Velocity change hanya di boundary** (no-slip BC via subtract_gradient,
  misal x=0: -0.992 → +0.004)
- **Pressure field**: seragam awal 0.004, setelah iterasi jadi bervariasi
  `[-0.004, 0.004]` — tapi hampir konstan karena neighbor pressure=0 di mana-mana
- **Interior velocity tidak berubah**: `p ≈ -0.25 * div` (konstan) → `∇p ≈ 0`

Ini MATEMATIS BENAR untuk 1 iterasi dengan initial pressure=0. Pressure
information butuh beberapa iterasi untuk propagate dari boundary ke interior.
Flux reference pakai **19 iterasi** untuk hasil convergen.

**Implikasi**: Test pipeline penuh (setelah advection+diffusion+noise) HARUS
menggunakan ≥4 iterasi pressure, target 19 iterasi. 1 iterasi cukup untuk
verifikasi data flow, tidak cukup untuk visual effect.

### Velocity-Based Noise (Self-Evolving)

Untuk mengatasi limitasi passing time dinamis ke shader (tanpa uniform, tanpa
2 sampler), noise.frag menggunakan **Z coordinate = `length(vel) * 10.0 + zOffset`**
sebagai sumbu waktu untuk 3D Simplex noise. Hasil:

- Noise pattern berubah saat velocity berubah (setiap frame karena advection)
- Tidak perlu external time input (self-evolving)
- Auto-correlation ~0.95 (smooth spatial structure, bukan per-pixel noise)
- Fluida terus berevolusi tanpa settle ke steady state (verified: diff ~35 antar
  frame setelah 5s)
- Parameter: 3 octaves (scale 2.8/15/30, multiplier 1.0/0.7/0.5, zOffset 0/50)

### Timer-Driven Ping-Pong Rendering

Continuous re-render dicapai dengan timer 16ms (≈60fps) dan 2
ShaderEffectSource yang capture dari passSubtract yang sama:

```qml
property bool _pingA: true
Timer {
    interval: 16; running: root.running; repeat: true
    onTriggered: {
        passNoise.simTex = root._pingA ? srcFinalA : srcFinalB
        root._pingA = !root._pingA
    }
}
```

Alternating reference objects memastikan property change signal setiap frame,
yang memicu re-render chain. Tanpa ini, chain render sekali dan berhenti
karena tidak ada dependency circular yang terdeteksi Qt scene graph.

---

## Area Rawan Hallucination

Verifikasi ke source/dokumentasi aktual, jangan andalkan memori untuk:

- **Quickshell API** — dokumentasi sparse, method mudah dikarang. Cek source
  `dots-hyprfork` atau Quickshell GitHub langsung.
- **Path module lockscreen di dots-hyprfork** — bisa berubah antar versi
  fork, selalu `grep`/`find` dulu sebelum asumsi path.
- **Mekanisme IPC Quickshell** (GlobalShortcut vs IpcHandler vs lainnya) —
  cek source aktual, ada kemungkinan beberapa API berbeda fungsi meski
  terlihat serupa.
- **Qt ShaderEffect behavior spesifik versi** — Qt 6.11 punya beberapa quirk
  (segfault custom vertex shader, flag qsb yang benar) yang mungkin tidak
  berlaku di versi Qt lain.

Jika tidak yakin → tandai eksplisit "perlu verifikasi", jangan confiden
tanpa referensi konkret.

---

## Session Summary (2026-06-23)

### What was done
- **QSGRenderNode engine step restored and finalized** (commit `ce63c96`):
  - `FluidDisplayNode` (QSGRenderNode) drives `engine->step(cb, dt)` in `prepare()` using the scene graph's command buffer on the render thread
  - `render()` is no-op (display handled by sibling `QSGGeometryNode` + `QSGOpaqueTextureMaterial`)
  - Destructor no longer calls `releaseResources()` (avoids double-free — unique_ptr handles cleanup)
  - `releaseResources()` resets engine early while GL context is valid
- **Confirmed all alternative engine step approaches failed** in Qt 6.11 headless:
  - `scheduleRenderJob` at all stages (BeforeSynchronizing, AfterSynchronizing, AfterSwap): `beginOffscreenFrame` returns false (frame already active on scene graph's RHI)
  - `beforeRendering` signal: no `commandBuffer()` API in Qt 6.11 QQuickWindow
  - Separate RHI with shared context: engine RHI's `beginOffscreenFrame` returns false (makeCurrent on fallback surface fails)
  - GUI thread direct `beginOffscreenFrame`: qFatal abort
- **Cleanup crash root cause identified**: Qt 6.11 calls `releaseResources()` from `QQuickItemPrivate::derefWindow()` during item destructor — AFTER the QQuickWindow's GL context is destroyed. Engine's `QRhiGraphicsPipeline` destructor crashes when calling into GL with no context. This is a Qt 6.11 ordering bug, not fixable from our side.

### Pre-existing issues
- **Qt 6.11 cleanup crash**: GL context destroyed before `releaseResources()` called on items. Crash at exit in `FluidSimEngine::releaseResources()` → `QRhiGraphicsPipeline::~QRhiGraphicsPipeline()`. Accept as pre-existing Qt bug.
- **grabToImage works**: non-zero pixel images confirmed from sandbox (R mean 34, G mean 32, B mean 45). Occasional crash during grab is from QSGRenderNode state conflict with QSGRhiLayer::grab() in headless.

### Next
- Verify on desktop (Hyprland session) — QSGRenderNode should work without crashes on real GPU
- Begin line rendering implementation (Phase 1: grid/SSBO, Phase 2: place/draw/endpoint shaders, Phase 3: C++ pipeline integration)

## Changelog

### 2026-06-22 — Session: Simplex Noise CPU 1:1 + Diffuse Formula + Inject Noise 1:1 Reference

**Changes**:
1. **`FluidSimEngine.cpp`** — Replaced hash-based value noise (fbm) with 3D simplex noise ported 1:1 from WGSL `generate_noise.comp.wgsl`:
   - `snoise3D()` — complete port of simplex noise (mod289 + permute + gradient tables + falloff)
   - `simplexNoisePair()` — returns (snoise(v), snoise(v+vec3(8,-8,0))) matching `make_noise_pair`
   - Channel blending in CPU upload loop: for each channel, `scale * texelPos` → `make_noise_pair(..., offset_1)` → optional blend with offset_2 → sum across 3 channels → `* global_multiplier`
   - `CHANNEL_CFG.inc` fixed to reference values: 0.001, 0.006, 0.012
   - `m_noiseMultiplier` fixed from 0.1 to 0.45 (match `settings.rs`)
   - Center-aligned UV `(x+0.5)/size` matching reference `texel_position`

2. **`pass_diffuse.frag`** — `stencil_factor` 0.25→0.0625, `center_factor` 0→12.0. Calculated from `dt=0.016667, visc=5.0`: `centerFactor=1/(visc*dt)=12.0`, `stencilFactor=1/(4+centerFactor)=0.0625`. Previous values made diffusion 4× too aggressive.

3. **`FluidSimEngine.cpp` (subtract gradient)** — `pressureTex` sampler `nearest→linear`. Linear gradient: `0.25*(p[i+1]-p[i-1])`, nearest: `0.5*(p[i+1]-p[i-1])` (2× over-correction).

4. **`pass_inject_noise.frag`** — Rewritten to match reference:
   - Linear sampler for noiseTex (was nearest/texelFetch)
   - UV-based sampling: `gl_FragCoord.xy / velSize` (was `texelFetch` position mapping)
   - `timestep * noise` scaling: `(1/60) * noise` (was `1.0 * noise` — 60× too aggressive)

5. **`FluidSimEngine.h`** — `m_noiseMultiplier` default 0.1→0.45

**Known Issue**: Intermittent segfault (~10% rate) in `QSGBatchRenderer::Renderer::renderRhiRenderNode` when `shader_sandbox` runs after rebuild. Pre-existing Qt timing issue, not related to noise changes.

### 2026-06-22 (PM) — Session: Diffuse Boundary Safety + All Pipeline Shaders Verified Safe

**Changes**:
1. **`pass_diffuse.frag`** — Added explicit `clampPos()` for `texelFetch` boundary safety.
   - Bare `texelFetch(tex, pos+offset, 0)` at x=0 with offset=-1 reads from invalid address → garbage memory → simulation blow-up.
   - Fix: `clampPos()` clamps to `[0, size-1]`, matching reference's `ClampToEdge` sampler behavior.
   - Matches reference: diffuse pass uses `textureSampleLevel` with `nearest_sampler` (ClampToEdge) → neighbor = self at walls → Neumann implicit (du/dn=0).
2. **Verified all shaders for boundary safety**:
   - `pass_divergence.frag` — **FIXED** prev session: `clampPos()` for offscreen reads.
   - `pass_pressure.frag` — **SAFE**: uses `textureLodOffset` (ClampToEdge inherent).
   - `pass_subtract.frag` — **SAFE**: uses `textureLodOffset` + component-wise no-slip BC (`bc.x=0` at x-edges, `bc.y=0` at y-edges), matching reference.
   - `pass_inject_noise.frag` — **SAFE**: pure volume operation, no edge reads.
   - `pass_advect.frag` / `pass_advect_rev.frag` / `pass_adjust.frag` — **SAFE**: use `texture()` (ClampToEdge) or `textureLod()`.
3. **Reference WGSL boundary condition analysis**:
   - No-slip is ONLY applied in `subtract_gradient` (projection step), NOT in diffuse.
   - Diffuse pass uses Jacobi iteration with ClampToEdge sampler → Neumann implicit (du/dn=0) at boundaries.
   - No-slip in `subtract_gradient` is **component-wise**: only the velocity component NORMAL to the wall is zeroed. Our `pass_subtract.frag` already implements this correctly.
   - `inject_noise` applies no boundary conditions (pure volume operation).
4. **Previous fixes this session (carried forward)**:
   - `QSGTexture::Nearest` filtering on display texture to prevent bilinear blur.
   - UV clamping fix in display shaders (`texelFetch` with explicit `dstPos * srcSize / DISPLAY_SIZE`).
   - QSGGeometryNode index order `{0,1,2, 1,2,3}` for 100% coverage.

**Metodologi**: Reference WGSL diff (`../flux-reference/flux/shader/`) → boundary check per shader → fix only unsafe shaders.

### 2026-06-22 — Session: C++ QRhi Plugin Finalized Display via QSGSimpleTextureNode

**Terobosan**: Display pipeline berhasil via `QSGSimpleTextureNode` + `QQuickWindow::createTextureFromRhiTexture()`.
QSGRenderNode hanya untuk engine step (no-op render).

**Changes**:
1. **`FluidSimItem.h/cpp`** — Refactor `updatePaintNode()` returns `QSGNode` with 2 children:
   - `QSGRenderNode` (FluidDisplayNode): step engine di `prepare()`, `render()` no-op
   - `QSGSimpleTextureNode`: wrap `displayTex` via `createTextureFromRhiTexture()`
2. **`FluidDisplayNode`** — Simplified: no pipelines, no draw code, just `prepare()` → engine step
3. **`FluidSimItem.h`** — Removed all pipeline/buffer/renderpass members from FluidDisplayNode
4. **`AGENTS.md`** — Updated all sections for new architecture

**Verified**: Visual output confirmed via grim analysis (49.8% non-background pixels, heatmap colors visible).
No crashes, no errors.

**Metodologi**: Scan kode → runtime verification (jalankan kedua simulasi,
capture screenshot, analisis pixel numerik) → fix berurutan prioritas.

**Bug Fixes Applied**:

1. **`diffuse.frag`** — Hapus double no-slip boundary condition
   - Sebelum: boundary di-zero 2× (sekali via neighbor zeroing, sekali via explicit override)
   - Sesudah: hanya neighbor zeroing (first pass tetap, second override dihapus)
   - Efek: border ring delta 134pt → 108pt; saturated pixels 3.8% → 2.5%

2. **`noise.frag`** — Turunkan `GLOBAL_MULT` dari `0.6` ke `0.45`
   - Match ke referensi `noise_multiplier = 0.45`
   - Efek: velocity saturation berkurang 35%, simulasi lebih controlled

**Remaining Known Gap**: Line rendering (flow_lines) masih menggunakan
instantaneous velocity per-pixel (stateless). Referensi menggunakan
spring-dynamics particle system dengan momentum (stateful).

**Dengan C++ QRhi pipeline, gap ini bisa diatasi:**
- SSBO/storage buffer → line state (endpoint, velocity, color) persistent per basepoint
- Multi-sampler binding → baca velocity field + line state dalam 1 pass
- Compute shader dengan `imageStore` → update posisi spring dynamics, write ke texture
- Compute `imageStore` terverifikasi berfungsi (2026-06-24).
- SSBO `bufferLoadStore` API tersedia, tapi perlu diverifikasi runtime.
- Alternatif: compute → imageStore ke texture → fragment shader baca texture (proven path).

## Session Summary (2026-06-26) — Line Rendering Complete

### Root cause: `texelFetch` in vertex shader returns zeros with very-wide 1-row texture
Line state texture was `{stateTexels, 1}` with `stateTexels = m_lineCount * 3` (up to 28416).
`texelFetch(uStateTex, ivec2(base, 0), 0)` returned `(0, 0, 0, 0)` for all texels when the
texture width > some threshold (likely related to GL `GL_TEXTURE_2D` implementation limits
or alignment in Qt RHI GLES2 backend).

**Fix**: Change texture to tiled layout `{256, texH}` with `texH = ceil(stateTexels / 256)`.
Update both compute and vertex shaders to compute texel coordinates as:
```glsl
int texW = 256;
ivec2 p0 = ivec2(base % texW, base / texW);
```

### Previous attempts that failed
- `UsedAsSampledTexture` flag: doesn't exist in Qt 6.11 QRhi (all textures sampled by default)
- GPU blit / texture copy: Qt 6.11 crash bug
- Computing with endpoint values: produced correct positions but `texelFetch` returned zero
- Dummy `texelFetch` calls worked fine; only using the RETURNED values exposed the bug

### Verified working
- Compute → imageStore writes visible to draw → texelFetch reads (via swap in same command buffer)
- No explicit memory barrier needed in Qt 6.11 GLES2 backend between compute imageStore and vertex texelFetch
- Additive blend (SrcAlpha, One) with non-premultiplied fragment output
- Instanced draw with `gl_InstanceIndex` + texelFetch-based per-instance state reading
- Full spring-dynamics simulation (velocity-driven endpoint, color, width, opacity)
- Display readback at frame 120: mean=(22.3 13.4 21.7 37.9) max=(255 255 255)

## Session Summary (2026-06-27) — Exact-Match Line Rendering Complete

### Changes
1. **Fragment shader AA** (`draw_lines_fs.frag`): Replaced `smoothstep(0, 1.5, d)` with reference formula: `fade * smoothEdges` using `fwidth(vVertex.x)` for screen-space antialiasing (matching `line.wgsl`).
2. **Variance noise** (`line_update.comp`): Ported `snoise(vec3)` 3D simplex noise from reference WGSL `generate_noise.comp.wgsl`. Added noise uniforms (`noiseScaleX/Y`, `noiseOffset1/2`, `noiseBlendFactor`) and `colorMode` switching. 3 modes:
   - Mode 0 (Original): `clamp(vec2(1,0.66) * (0.5+vel), 0, 1)` with blue channel 0.5
   - Mode 1 (ColorWheel): `atan2(vel.y, vel.x)` → `getColor()` with hardcoded Plasma 6-color palette
   - Mode 2 (ImageTexture): `texture(uColorTex, 2.0*vel + 0.5)` from 256×1 RGBA8 rainbow gradient
3. **Endpoint rendering**: New `draw_endpoint_vs.vert` + `draw_endpoint_fs.frag` matching reference `endpoint.wgsl`:
   - Vertex position: `vec2(aspect,1)*zoom*(basepoint*2-1) + endpoint + 0.5*line_width*width*vertex`
   - Top/bottom half detection via cross product with midpointVector
   - Top half: `vec4(color.rgb, endpointOpacity)` → endpointOpacity=1.0 (threshold=1, brightness=1)
   - Bottom half: `vec4(color.rgb*(1-color.a), 1)` — premultiplied alpha reverse math
   - Smooth edges: `1.0 - smoothstep(1-fwidth(dist), 1, dist)`
4. **C++ `tickLineNoise()`** (`FluidSimEngine.cpp`): Matches reference `tick()` — `BASE_OFFSET=0.0015`, `BLEND_THRESHOLD=4.0`, `perturb=1+0.2*sin(0.01*elapsed*TAU)`. Called in `step()` before `stepLines()`.
5. **Stale test shaders removed**: `test_compute.comp`, `test_instancing_*`, `test_solid.comp` (relics from earlier exploration).

### Verified
- Sandbox runs clean to frame 540+ without GL errors, pipeline failures, or crashes
- All pipelines (compute + line draw + endpoint draw) execute successfully every frame
- Color texture (256×1 RGBA8) renders rainbow gradient for ImageTexture mode
- Endpoint draw call uses same vertex + basepoint buffers as lines

## Session Summary (2026-06-27) — MSAA + RGBA32F + Config Bridge

### Changes
1. **MSAA 4x configurable** (`FluidSimEngine.cpp/h`): `createDisplayPass()` creates MSAA render target + resolve texture when `m_msaaSamples > 1`. `msaaSampleCount` Q_PROPERTY (1/2/4, default 4).
2. **`recreateLineGraphicsPipelines()`** extracted from `createLinePipelines()` — called from init and `checkResize()` to keep line graphics pipelines matching current `m_rpDescRGBA8` (MSAA sample count).
3. **RGBA32F state texture**: `m_lineStateTex` format RGBA16F → RGBA32F. `initLineState()` data `qfloat16*` → `float*`. Compute shader `layout(rgba16f)` → `layout(rgba32f)` — format mismatch between C++ texture and GLSL imageStore was silently corrupting state data.
4. **AA reverted to 1px**: `smoothstep(0.5 - edgeWidth, 0.5, xOffset)` — matching reference `line.wgsl` exactly (no `2.0*` multiplier).
5. **Config.qml bridge**: Added `fluid {}` section to `dots-hyprfork/dots/.config/quickshell/ii/modules/common/Config.qml` with all 10 parameters.
6. **Cleanup**: Removed 8 stale `.comp` compute shaders (experimental implementations superseded by fragment-based solver pipeline). Removed dead `testComputeAndSSBO()` function (shader already gone, function never called).

### Resolved Issues
- **RGBA32F/GLSL format mismatch** (critical): compute shader `layout(rgba16f)` with RGBA32F C++ texture → half-float writes with full-float reads scrambled data — all previous MSAA + AA tests were reading corrupted state, explaining "ga ngefek" user reports.
- **2px AA overkill**: reference uses `fwidth` without multiplier; our `2.0*fwidth` made edges unnecessarily blurry.

### Visual Quality
- User confirmed "jauh lebih halus" after RGBA32F fix + MSAA 4x + 1px AA.
- RGBA32F format mismatch was the root cause of all previous roughness.
- Line quality now matches reference at equivalent resolution.

## Session Summary (2026-06-27 Mid) — Large Dead Code Cleanup

### Changes
1. **Removed stale structs/buffers**: `struct Direction` + `m_directionBuf` (never bound, wasting 48B/frame upload), `struct PushConstants` + `m_pushConstantBuf` (never bound, 16B/frame), `m_pendingDisplayUploadBatch` (dead code path), `m_diffusionIterations` (unused, diffuse hardcoded to 3 iterations).
2. **Removed 4 unused inline getters**: `curVelTex()`, `nxtVelTex()`, `curPressTex()`, `nxtPressTex()` — never called.
3. **Removed 6 unused `#include`** directives.
4. **10 stale `.qsb` removed from git tracking**; `*.qsb` added to `.gitignore`.
5. **`fullscreen_quad.vert`**: removed unused `inTexCoord`/`vTexCoord` (was wasting 2×4 bytes per vertex).
6. **`display_debug.frag`**: removed unused `DISPLAY_SIZE` constant.
7. **`draw_lines_vs.vert`**: fixed truncated `LineUniforms` struct — was 12 floats, now 20 (added missing noise/color fields).
8. **5 orphan QML files deleted**: `compare-main.qml`, `line-test.qml`, `quickshell-test/*` (broken, unused).
9. **`plugin/test_fluid.qml`**, **`qml/shell.qml`**, **`qml/FluxSimulation.qml`** deleted (obsolete).
10. **`qml/shaders` symlink** and **root `shaders/` dir** deleted (broken paths).
11. **`plugin/compiled/`** deleted (stale untracked QSB files).
12. **Sandbox background**: `#0d0d1a` → `#000` (pure black).

## Session Summary (2026-06-27 Late) — Rapihkan + Opaque Black + QML Timer

### Changes
1. **FrameAnimation → QML Timer**: FrameAnimation tidak trigger di sandbox/quickshell (butuh animation driver aktif). Ganti dengan `Timer { interval: 16 }` di QML level. `onFrameTick()` pindah dari `private slots` ke `public slots` agar callable dari QML JavaScript. Semua QTimer-related code dihapus dari `FluidSimItem`.
2. **Opaque black display clear color**: `QColor(0,0,0,0)` → `QColor(0,0,0,255)` di `stepLines()` render pass. Area tanpa garis sekarang opaque black, window background (`#111`) tidak tembus.
3. **Plugin rebuild**: Opaque black fix perlu rebuild plugin library (`plugin/build/libfluidsimplugin.so`) — sandbox link langsung ke source dan sudah rebuild, tapi quickshell widget load .so dan perlu rebuild terpisah.

### Resolved Issues
- **Window background tembus**: Display pass clear color `(0,0,0,0)` + `QSGImageNode` alpha blending → window `#111` terlihat. Fix: opaque black clear color `(0,0,0,255)` + additive blend → alpha=255 di semua pixel.
- **FrameAnimation tidak jalan**: `FrameAnimation.triggered` tidak fire di sandbox `QQmlApplicationEngine` maupun quickshell widget. Ganti dengan `Timer` 16ms yang reliable di semua Qt Quick context.
