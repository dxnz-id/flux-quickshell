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
- Pipeline tidak lagi dibatasi format `ShaderEffectSource` (RGBA8 clamp).
- Internal textures: `RGBA16F` (velocity, advection, noise) dan `R32F` (pressure, divergence).

### Yang Lama (referensi historis di `qml/FluxSimulation.qml`)

`ShaderEffect` + `ShaderEffectSource` multi-pass chain. Ditinggalkan karena limitasi
1 sampler per ShaderEffect membuat MacCormack advection tidak bisa diimplementasikan.
Disimpan sebagai referensi, tidak digunakan di pipeline final.

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
├── shaders/
│   ├── src/              ← GLSL source (.frag), DI-TRACK GIT
│   └── compiled/          ← hasil qsb (.qsb), DI-TRACK GIT
├── qml/                    ← komponen QML (FluxBackground.qml, dll), DI-TRACK GIT
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

---

## Arsitektur

- **Plugin**: C++ QML module di `plugin/`, build via CMake. Output: `libfluidsim.so` + `libfluidsimplugin.so`.
- **Rendering**: Qt RHI, backend OpenGL 4.6 (default di sistem target, bukan Vulkan).
- **Shader compile**: `qsb --glsl "440"` (bukan `--qt6`), karena ESSL 100 output tidak support `texelFetch`/`textureSize`.
- **Engine (C++)** `FluidSimEngine`: Semua pipeline solver (advection, diffusion, pressure, noise) via QRhi draw commands.
  Multi-pass sequential dalam satu `QRhiCommandBuffer` via `beginPass`/`endPass` pairs.
- **Display**: `FluidSimItem` (QQuickItem) → `QSGRenderNode` anak (step di `prepare()`) + `QSGSimpleTextureNode` anak (display).
  Engine render ke `displayTex` (RGBA8 heatmap 128×128), QSGSimpleTextureNode sampel displayTex ke screen.
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

### Belum Dimulai
- [ ] Line rendering (spring dynamics, stateful particle system)
- [ ] Integrasi ke dots-hyprfork lockscreen (`LockScreen.qml` + `GlobalStates`)
- [ ] Lock state machine (Flux mode vs Normal mode)
- [ ] Quickshell FrameAnimation integration (replace timer-driven loop)

### Known Issues

- 2025-06-17: Diffuse dan pressure solver di flux-reference pakai **Jacobi iteration**
  (bukan Gauss-Seidel). Shader membaca neighbor dari input texture yang sama
  dan menulis ke output texture terpisah.
- 2025-06-17: Multi-sampler ShaderEffect bug di Qt 6.11 — setiap
  ShaderEffect hanya boleh punya SATU `sampler2D`. Tambahan sampler (kedua,
  ketiga) menyebabkan output flat tanpa error. **Tidak relevan untuk C++ plugin**.
- 2025-06-17: 1 Jacobi pressure iteration menghasilkan ∇p ≈ 0 di
  interior (numpy verified). Ini matematis benar, bukan bug.
- 2025-06-22: QSB strips `layout(binding=N)` dari GLSL 440 output;
  Qt RHI OpenGL backend tidak rekonstruksi dari SPIR-V reflection.
  Workaround: hardcode semua parameter sebagai constant di shader.
- 2025-06-22: `QSGRenderNode` draw tidak visible saat BoundedRectRendering.
  Workaround: QSGSimpleTextureNode untuk display, QSGRenderNode hanya untuk engine step.
- 2025-06-22: QSGTexture dari `createTextureFromRhiTexture()` tidak set ownership;
  leak QSGTexture object (acceptable, <100 bytes).
-   2025-06-22: **Topology bug fix**: `TriangleStrip` + `setTopology()` for all engine pipelines.
  Also fixed QSGGeometryNode display quad index order `{0,1,2, 1,2,3}` for full coverage.
- 2025-06-22: **QSGGeometryNode index order `{0,1,2, 0,2,3}` untuk `GL_TRIANGLES` hanya render 75%.**
  Dua triangle share left edge (v0-v2), overlap di left 50%, miss right 50%. Fix: `{0,1,2, 1,2,3}`,
  dua triangle share diagonal v1-v2, coverage 100%. Ditemukan via ImageMagick quadrant analysis.

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

## Changelog

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
spring-dynamics particle system dengan momentum (stateful). Ini gap
fundamental yang butuh rethinking arsitektur line rendering.
