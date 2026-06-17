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

### Sedang Dikerjakan

- [ ] Verifikasi WGSL flux-reference + isi `dev/notes/`

### Belum Dimulai

- [ ] Analisis WGSL flux-reference → `dev/notes/navier-stokes-ref.md`
- [ ] Analisis porting WGSL → GLSL → `dev/notes/shader-porting.md`
- [ ] Tulis shader: divergence, pressure, subtract_gradient
- [ ] Tulis shader: advect (forward, reverse, adjust)
- [ ] Tulis shader: diffuse
- [ ] Tulis shader: noise
- [ ] Setup `dev/shader-sandbox/` untuk test shader terisolasi
- [ ] Verifikasi visual fluid simulation di sandbox (warna bergerak organik, tidak explode)
- [ ] Verifikasi Quickshell API: FrameAnimation, Singleton/QtObject pattern, GlobalShortcut/IpcHandler, SessionLockSurface — cek source aktual, jangan asumsi
- [ ] `FluxBackground.qml` untuk komponen animasi
- [ ] `LockState` untuk state machine mode Normal/Flux
- [ ] Line rendering (garis/partikel seperti Flux asli — vertex shader, hindari custom vertex shader Qt 6 jika segfault masih terjadi)
- [ ] Integrasi ke dots-hyprfork lockscreen

### Known Issues

- (kosong, isi seiring development — selalu tulis tanggal dan konteks
  supaya mudah ditelusuri)

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
- Jangan gunakan uniform block (`layout(std140, binding=N) uniform`) untuk
  parameter dari QML — sudah terverifikasi tidak jalan
- Jangan gunakan custom vertex shader — segfault di Qt 6.11
- Selalu commit ke git setelah progress signifikan — JANGAN biarkan file
  penting hanya ada di direktori `build/` yang gitignored
- Code comment dalam bahasa Inggris, dokumentasi dan narasi dalam bahasa Indonesia
- Selalu cite file atau dokumentasi spesifik saat propose implementasi.
  Jika tidak bisa, tandai eksplisit sebagai "perlu verifikasi"

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
