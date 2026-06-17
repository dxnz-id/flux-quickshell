# AGENTS.md — flux-quickshell

Dokumen ini adalah konteks utama untuk semua AI agent yang bekerja di repo ini.
Baca seluruh dokumen sebelum mengerjakan task apapun.

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
| Tidak ada custom vertex shader                                                  | Segfault di Qt 6.11                                                                                         |
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
- **Target integrasi**: Quickshell lockscreen di dots-hyprfork — path module lock
  perlu diverifikasi ulang dari source aktual, jangan asumsikan dari memori
  (cek `dots-hyprfork/dots/.config/quickshell/`)

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

---

## Pipeline Simulasi (Navier-Stokes)

Urutan render pass per frame:

```
noise → advect (forward → reverse → adjust, MacCormack)
      → diffuse ×3 (Gauss-Seidel)
      → divergence
      → pressure ×N (Jacobi atau Red-Black Gauss-Seidel, mulai N=4, target N=19)
      → subtract_gradient
      → (swap velocity ping-pong untuk frame berikutnya)
```

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
- [x] Verified: init → divergence+pressure → subtract_gradient (3 passes, correct values)
### Belum Dimulai

- [ ] Tulis shader: advect (forward, reverse, adjust)
- [ ] Tulis shader: diffuse
- [ ] Tulis shader: noise
- [ ] Verifikasi visual fluid simulation di sandbox (warna bergerak organik, tidak explode)
- [ ] Verifikasi Quickshell API: FrameAnimation, Singleton/QtObject pattern, GlobalShortcut/IpcHandler, SessionLockSurface — cek source aktual, jangan asumsi
- [ ] `FluxBackground.qml` untuk komponen animasi
- [ ] `LockState` untuk state machine mode Normal/Flux
- [ ] Line rendering (garis/partikel seperti Flux asli — vertex shader, hindari custom vertex shader Qt 6 jika segfault masih terjadi)
- [ ] Integrasi ke dots-hyprfork lockscreen

### Known Issues

- 2025-06-17: Diffuse dan pressure solver di flux-reference pakai **Jacobi iteration**
  (bukan Gauss-Seidel). Shader membaca neighbor dari input texture yang sama
  dan menulis ke output texture terpisah. Ini harusnya dicatat di pipeline
  description AGENTS.md agar tidak salah saat porting ke GLSL multi-pass.
- 2025-06-17 (Terbaru): Multi-sampler ShaderEffect bug di Qt 6.11 — setiap
  ShaderEffect hanya boleh punya SATU `sampler2D`. Tambahan sampler (kedua,
  ketiga) menyebabkan output flat tanpa error. Workaround: channel-packed
  single-texture dengan A=1.0.
- 2025-06-17: `grabToImage()` intermittent di Wayland — kadang tidak
  menulis file saat stdout/stderr di-redirect ke /dev/null.

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
- Jangan gunakan custom vertex shader — segfault di Qt 6.11
- Selalu commit ke git setelah progress signifikan — JANGAN biarkan file
  penting hanya ada di direktori `build/` yang gitignored
- Code comment dalam bahasa Inggris, dokumentasi dan narasi dalam bahasa Indonesia
- Selalu cite file atau dokumentasi spesifik saat propose implementasi.
  Jika tidak bisa, tandai eksplisit sebagai "perlu verifikasi"

## Critical Discoveries (Iterasi Saat Ini)

### Multi-Sampler Bug di Qt 6.11

ShaderEffect dengan LEBIH DARI SATU `sampler2D` (binding 0, 1, ...) menghasilkan
output flat (R=128 untuk bias-encoded value). Bahkan dengan `layout(binding=N)`
yang benar di QSB reflection. **WORKAROUND**: encode semua field dalam SATU
texture RGBA8, hanya gunakan binding 0.

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

Pipeline 3-pass untuk projection step:
1. `init_velocity` — RG=velocity, B=0 (pressure), A=1.0
2. `divergence+pressure` — hitung divergence dari velocity neighbors,
   hitung pressure dari neighbors pressure + divergence, update B
3. `subtract_gradient` — hitung gradien pressure, update velocity di RG
   (pressure di B di-pass-through)

Divergence dihitung ON-THE-FLY dari velocity neighbors di setiap pass
(lebih murah daripada iterasi terpisah dengan constraint channel terbatas).

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
