# AGENTS.md — flux-quickshell

Dokumen ini adalah konteks utama untuk semua AI agent yang bekerja di repo ini.
Baca seluruh dokumen sebelum mengerjakan task apapun.

---

## CRITICAL: 1 Sampler per ShaderEffect — HARD LIMIT

**Setiap ShaderEffect HANYA boleh punya SATU `layout(binding=0) uniform sampler2D`. Tidak ada exception.**

### Terverifikasi
- 2 sampler terpisah (binding 0 + binding 1) → binding 1+ membaca texture yang SALAH. Diverifikasi dengan swap test di sandbox standalone DAN Quickshell runtime (numerical grim test).
- Ini bukan limitasi channel capacity (RGBA masih 4 channel penuh tersedia), tapi limitasi JUMLAH sampler2D uniform.
- Efek terjadi dengan tepat 2 sampler (bukan hanya 3+).

### Implikasi Desain
Setiap shader yang butuh data dari >1 sumber HARUS:
1. Pack semua data yang diperlukan ke channel RGBA dari 1 texture (lewat prep-pass jika perlu), ATAU
2. Pecah jadi multi-pass sequential, masing-masing pass baca 1 texture saja

### Pattern yang Terverifikasi Jalan
- **Self-sampling dengan UV offset**: 1 sampler, dipanggil berkali-kali dengan UV berbeda untuk baca neighbor (kiri/kanan/atas/bawah). Dipakai di pressure.frag, subtract_gradient.frag, diffuse.frag.
- **1×1 ShaderEffectSource untuk scalar value** (misal time): Rectangle 1×1 dengan `color: Qt.rgba(t,t,t,1)`, di-capture via ShaderEffectSource, dibaca sebagai sampler. Terverifikasi PASS untuk passing dynamic value tanpa uniform.
- **Channel packing**: simpan beberapa scalar/vector berbeda di channel RGBA berbeda dari TEXTURE YANG SAMA (misal RG=velocity, B=pressure, A=1.0).

### Yang TIDAK Bisa Dilakukan
- ShaderEffect dengan `property var texA` + `property var texB`, lalu shader declare `layout(binding=0) uniform sampler2D texA;` dan `layout(binding=1) uniform sampler2D texB;` — binding 1 akan membaca texture yang salah.
- Line rendering dengan spring dynamics (stateful): butuh baca line state lama + fluid velocity baru = 2 sampler. Tidak bisa dengan 1 sampler. Analisis lengkap di `dev/notes/line-rendering-analysis.md`.

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
| `qsb --qt6`, BUKAN `--glsl "450"`                                               | Flag versi tinggi → silent white/black output, attribute mismatch dengan default vertex shader Qt 6         |
| GLSL source HARUS `#version 420` (bukan 150)                                      | `layout(binding=N)` untuk sampler dan `layout(location=N)` untuk I/O hanya support di GLSL 420+; qsb --qt6 akan cross-compile otomatis ke GLSL 120/150 |
| Tidak ada custom vertex shader                                                  | Segfault di Qt 6.11 standalone; diverifikasi AMAN di Quickshell runtime (2025-06-18) |
| Bias encoding `v*0.5+0.5` untuk signed values                                   | `ShaderEffectSource` format RGBA8 clamp negatif ke 0                                                        |
| Parameter via params texture (Canvas → ShaderEffectSource), BUKAN uniform block | Qt ShaderEffect tidak support QML property → uniform block member mapping (sudah diverifikasi runtime FAIL) |
| Source code TIDAK BOLEH hanya ada di `build/`                                   | Risiko hilang permanen — `build/` regenerable, source code tidak                                            |
| Commit di setiap milestone                                                      | Hindari kehilangan progress jika session tertutup tidak sengaja                                             |
| Nama file context: `AGENTS.md` (dengan S)                                       | Konvensi OpenCode — dibaca otomatis, beda dari `AGENT.md` yang tidak dikenali                               |

---

## Arsitektur

- **Rendering**: Qt RHI, backend OpenGL 4.6 (default di sistem target, bukan Vulkan)
- **Shader**: Fragment shader via `ShaderEffect`, multi-pass via `ShaderEffectSource` ping-pong
- **Parameter**: Params texture (Canvas 1×8 RGBA8) + time texture (Canvas 1×1 RGBA8)
- **Binding convention**: binding 8 = `paramsTexture`, binding 9 = `timeTexture`
  (lanjutkan convention ini untuk binding lain agar konsisten)
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

Urutan render pass per frame:

```
noise (velocity-based Z, self-evolving Simplex 3D)
  → advect (forward semi-Lagrangian, MacCormack di-skip)
  → diffuse ×3 (Jacobi, VISCOSITY=5, TIMESTEP=1)
  → divergence + pressure ×19 (Jacobi, Neumann BC dp/dn=0)
  → subtract_gradient (no-slip BC)
  → output → feedback ke noise input (frame berikutnya)
```

Timer-driven loop (16ms interval, alternating ShaderEffectSource references).

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
- [x] Tulis shader GLSL: init_velocity, divergence+pressure, subtract_gradient
- [x] Setup `dev/shader-sandbox/` — compile dan runtime verification phase 1
- [x] Discovered Qt 6.11 critical bugs:
  - Multi-sampler ShaderEffect bug (output flat)
  - Premultiplied alpha storage in layers
  - qsb --qt6 rejects bare uniforms
- [x] Working channel-packed pipeline (single texture, A=1.0, hardcoded RES)
- [x] Verified: init → divergence+pressure → subtract_gradient (3 passes, correct values via numpy diff)
- [x] Fix display: `Image { source: ShaderEffectSource }` tidak bisa render live ShaderEffectSource di Qt 6.11 — ganti dengan `ShaderEffect` passthrough
- [x] Verifikasi numeric bahwa 1 Jacobi iteration untuk pressure menghasilkan ∇p ≈ 0 di interior (matematis benar, secara visual tidak terlihat)
- [x] Tulis shader passthrough (`shaders/src/passthrough.frag`) untuk display pipeline
- [x] Tulis shader: advect_forward (semi-Lagrangian, forward-only)
- [x] Verifikasi advect_forward di sandbox (pixel values berubah, range tereduksi)
- [x] Dokumentasi keputusan skip MacCormack (limitasi 1 sampler, 6 values > 4 channels)
- [x] Verifikasi pipeline multi-pass di sandbox (7 pass chain: init → ∇·+p → ∇-p → advect → diffuse×3 → noise)
- [x] Tulis shader: diffuse (Jacobi iteration, 3 chain, verified convergent)
- [x] Tulis shader: noise (3D Simplex procedural + inject, auto-correlation verified)
- [x] Verified: 1×1 ShaderEffectSource via Rectangle bisa jadi single sampler pattern untuk passing time dinamis
- [x] Verified: `layout(location=N) uniform float` dan `uniform vec4 color` — KEDUANYA ditolak qsb --qt6 (Vulkan SPIR-V melarang bare uniforms)
- [x] FluxSimulation.qml — reusable Qt Quick component with full pipeline:
  noise → advect → diffuse×3 → pressure×19 → subtract_gradient → feedback loop
- [x] Dynamic noise via velocity-based Z coordinate (length(vel) * 10.0 + zOffset) — verified self-evolving
- [x] Timer-driven ping-pong (2 ShaderEffectSources, alternating simTex) for continuous re-render, verified ~60fps
- [x] visualize.frag — velocity magnitude heat map (blue→cyan→green→yellow→red)
- [x] Stability test 10s: mean stable ~83, continuous frame-to-frame diff ~35, no blow-up
- [x] 90% bright pixels visualization with tuned color mapping
- [x] FluxSimulation.qml pressure chain extended to 19 iterations (matching flux-reference default)
- [x] Verified 19 iterations vs 8: max velocity 229 vs 255 (better divergence-free), same FPS ~60, mean stable ~99, std ~72
- [x] frameCount property exposed for external FPS measurement
- [x] Quickshell API verification:
  - FrameAnimation tersedia (Quickshell-native, frame-synced, `frameTime` variable available in onTriggered)
  - IpcHandler + GlobalShortcut digunakan luas di dots-hyprfork (verified lock IPC: `GlobalStates.screenLocked` → `WlSessionLock` → `WlSessionLockSurface`)
  - SessionLockSurface structure mapped (LockScreen → LockSurface → UI components)
  - ShaderEffect test in Quickshell: passthrough (1 sampler) OK, switch_test (2 samplers) loads without errors
- [x] Custom vertex shader test in Quickshell: minimal passthrough vertex shader runs 5s+ without segfault
  (berbeda dari sandbox standalone yang segfault — Quickshell rendering path kompatibel dengan vertex shader custom)
- [x] Complete line rendering feasibility analysis (`dev/notes/line-rendering-analysis.md`):
  - Mapped data requirements for spring-dynamics line update (6 values per line)
  - Analyzed channel packing capacity (max 4 values per RGBA8 texel)
  - Explored multi-pass sequential, hybrid encode, and vertex shader approaches
  - **Finding**: Channel packing CANNOT solve the line update problem — 6 > 4
  - **Root cause**: Spring dynamics requires reading BOTH old line state AND current fluid velocity simultaneously (2 samplers)
  - Recommended next step: re-verify 2-sampler binding behavior in Quickshell runtime

### Belum Dimulai
- [ ] `FluxBackground.qml` untuk komponen fullscreen
- [ ] `LockState` untuk state machine mode Normal/Flux
- [ ] Line rendering (Opsi A — implicit lines via visualize.frag, no persistent state)
- [ ] Integrasi ke dots-hyprfork lockscreen
- [ ] Dynamic time via 1×1 Rectangle + prepass (velocity+time dalam 1 sampler)

### Selesai (lihat di atas untuk hasil detail)
- [x] Re-verify 2-sampler binding 1 behavior IN Quickshell runtime — **DEFINITIVE: GAGAL**

### Known Issues

- 2025-06-17: Diffuse dan pressure solver di flux-reference pakai **Jacobi iteration**
  (bukan Gauss-Seidel). Shader membaca neighbor dari input texture yang sama
  dan menulis ke output texture terpisah.
- 2025-06-17: Multi-sampler ShaderEffect bug di Qt 6.11 — setiap
  ShaderEffect hanya boleh punya SATU `sampler2D`. Tambahan sampler (kedua,
  ketiga) menyebabkan output flat tanpa error.
- 2025-06-17: `grabToImage()` intermittent di Wayland — kadang tidak
  menulis file saat stdout/stderr di-redirect ke /dev/null.
- 2025-06-17: 1 Jacobi pressure iteration menghasilkan ∇p ≈ 0 di
  interior (numpy verified). Ini matematis benar, bukan bug.
- 2025-06-18: Bare uniforms (`uniform float`, `uniform vec4 color`) ditolak qsb --qt6
  karena kompilasi melalui SPIR-V (Vulkan GLSL melarang bare uniforms).
- 2025-06-18: Velocity-based Z noise dapat menyebabkan limit cycle (frekuensi stabil).
  Mitigasi: offset berbeda per channel + spatial coupling via advection.
- 2025-06-18: FluxSimulation.qml pressure chain = 19 iterations (bukan parameter dinamis).
  Untuk ubah jumlah iterasi: tambah/kurang instance ShaderEffect + ShaderEffectSource di chain.
- 2025-06-18: 2-sampler binding 1 **DEFINITIVELY GAGAL** di Quickshell runtime (sama dengan sandbox standalone).
  Binding 1 membaca texture binding 0 — diverifikasi secara numerik via grim screenshot analysis
  (`binding_verify_test.frag` → output MERAH (255,0,0), bukan KUNING (255,255,0)).
  Ini adalah HARD LIMIT Qt 6.11 RHI — tidak ada workaround dengan binding index berbeda.

---

## CRITICAL: qsb Compilation Flag

**SELALU gunakan `--qt6` untuk shader yang dipakai di ShaderEffect QML.**

```bash
qsb --qt6 input.frag -o output.qsb
```

### Salah (menyebabkan white/black output, silent failure)

```bash
qsb --glsl "450" input.frag -o output.qsb
qsb --glsl "440,330,150,100 es" input.frag -o output.qsb
qsb --glsl "450" --vulkan input.frag -o output.qsb
```

### Kenapa

`--qt6` menggunakan name-based attribute matching yang cocok dengan default
vertex shader Qt Quick. Flag `--glsl` dengan versi tinggi (330+) menghasilkan
`layout(location=N)` eksplisit untuk attribute yang tidak cocok dengan vertex
shader internal Qt → silent linker fallback → output putih atau hitam tanpa
error yang jelas.

### Testing

Selalu test `ShaderEffect` baru dengan warna solid dulu sebelum logika
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

### Multi-Sampler Bug di Qt 6.11

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

### MacCormack Correction di-skip (Future Improvement)

MacCormack advection correction (forward → reverse → adjust) membutuhkan
3 texture input (forward_vel, reverse_vel, original_vel) = 6 float values.
Dengan limit 1 sampler (lihat Multi-Sampler Bug), hanya 4 channel RGBA
tersedia — tidak muat untuk 6 nilai tanpa multi-pass packing.

**Keputusan**: Forward-only semi-Lagrangian advection untuk sekarang.

**Potential future improvement**: Implementasikan MacCormack via multi-pass
sequential dengan intermediate texture packing:
1. Forward output: RG=forward, B=0.5*original.x, A=1.0
2. Reverse output: RG=reverse, B=forward.x, A=forward.y (bawa forward dari step 1)
3. Adjust: butuh juga 0.5*original.y — belum ada solusi channel tanpa pecah
   menjadi 4+ pass (terlalu banyak untuk screensaver)

Semi-Lagrangian forward-only sudah cukup baik untuk visual screensaver.
Jika hasil terasa kurang sharp/detail di pipeline penuh, revisit opsi
multi-pass packing nanti.

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

### 2026-06-18 — Session: Empirical Revision + Bug Fixes

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

3. **`FluxSimulation.qml`** — Reorder pipeline ke urutan referensi
   - Sebelum: `noise → advect → diffuse×3 → pressure×19 → subtract`
   - Sesudah: `advect → diffuse×3 → noise → pressure×19 → subtract`
   - Timer sekarang alternates `passAdvect.simTex` (bukan `passNoise.simTex`)

4. **`flow_lines.frag`** — Fix `GRID_SIZE` dari `0.05` ke `0.117`
   - Match ke referensi `grid_spacing = 15px / 128px = 0.117`
   - `LINE_WIDTH` disesuaikan 0.003 → 0.006 agar tetap visible

**Shaders Recompiled**: `diffuse.qsb`, `noise.qsb`, `flow_lines.qsb`

**Remaining Known Gap**: Line rendering (flow_lines) masih menggunakan
instantaneous velocity per-pixel (stateless). Referensi menggunakan
spring-dynamics particle system dengan momentum (stateful). Ini gap
fundamental yang butuh rethinking arsitektur line rendering.
