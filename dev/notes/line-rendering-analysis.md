# Analisis Line Rendering — Channel Packing & Update Strategy

## ⚠️ OBSOLETE untuk C++ QRhi Pipeline (2026-06-24)

Dokumen ini ditulis untuk **QML ShaderEffect** dengan constraint:
1 sampler per ShaderEffect, tidak ada SSBO, tidak ada compute shader.

**Semua constraint tersebut TIDAK BERLAKU untuk pipeline C++ QRhi:**
- ✅ **Multi-sampler binding**: `QRhiShaderResourceBinding` support binding banyak sampler sekaligus
- ✅ **SSBO / Storage buffer**: `QRhiBuffer::StorageBuffer` (perlu verifikasi di GLES2 backend)
- ✅ **Compute shader**: `QRhiComputePipeline` (perlu verifikasi di GLES2 backend)
- ✅ **Custom vertex shader**: berfungsi penuh di C++ QRhi

**Yang masih relevan dari dokumen ini:**
- Struktur data `Line { endpoint, velocity, color, color_velocity, width }`
- Spring dynamics formula (`place_lines.comp.wgsl:138-145`)
- Parameter reference (grid_spacing, line_length, line_width, dll)
- Vertex shader expansion (line.wgsl) — bisa di-port langsung
- Fragment shader effects (fade-in, anti-alias, endpoint rendering)

**Yang perlu di-rethink:**
- Channel packing strategy → sekarang pakai SSBO, bukan encoding ke RGBA8
- Multi-pass sequential untuk update posisi → compute shader bisa baca semua input dalam 1 pass
- Constraint 1 sampler → sekarang multi-sampler binding berfungsi penuh

---

## DEFINITIVE TEST: 2-Sampler Binding di Quickshell Runtime

### Date
2025-06-18

### Method
Texture A (solid red, binding 0) + Texture B (solid blue, binding 1).
Shader output: `R=texA.r, G=texB.b, B=0, A=1`.
Prediksi: **KUNING** (1,1,0) jika binding 1 benar, **MERAH** (1,0,0) jika binding 1 gagal.

### Test Setup
- Runtime: Quickshell 0.2.1 (revision 7511545), Qt 6.11.1
- Sources: 50×50 visible Rectangles (red + blue), captured via `ShaderEffectSource { live: true }`
- ShaderEffect 50×50 output, grim screenshot analyzed numerically
- Shader: `shaders/compiled/binding_verify_test.qsb`

### Result
**MERAH** — `(255, 0, 0)` di seluruh area ShaderEffect.

- `texA.r = 1.0` → binding 0 membaca texture merah dengan benar ✓
- `texB.b = 0.0` → binding 1 membaca texture DENGAN BLUE CHANNEL 0 ✗

Ini membuktikan binding 1 membaca **texture yang SALAH** (bukan blue square).
Nilai B=0 konsisten dengan texture merah yang memiliki komponen biru = 0.

### Verdict
**2-sampler GAGAL di Quickshell.** Binding 1 tidak membaca texture yang
ditentukan. Perilaku ini SAMA dengan sandbox standalone Qt 6.11 — tidak ada
perbedaan. Multi-sampler bug adalah bug Qt 6.11 yang universal, bukan
artifak lingkungan test tertentu.

### Implikasi
- Spring dynamics line rendering **TIDAK FEASIBLE** dengan fragment shader
- Implementasi harus lanjut ke **Opsi A (implicit lines)** — fragment-shader-only
  flow visualization tanpa persistent state
- Critical Discovery AGENTS.md dikonfirmasi: 1 sampler per ShaderEffect adalah
  HARD LIMIT yang tidak bisa di-workaround dengan 2 samplers di binding berbeda

---

Berdasarkan analisis `place_lines.comp.wgsl` (flux-reference), `place_lines.vert`
(flux-gl), current pipeline `FluxSimulation.qml`, dan constraint teknis yang sudah
diverifikasi.

---

## 1. Kebutuhan Data per Line

### 1.1 Struktur Data (dari `place_lines.comp.wgsl:2-8`)

| Field           | Type   | Bytes | Channels RGBA8 |
|-----------------|--------|-------|-----------------|
| endpoint        | vec2   | 8     | 2 (RG)          |
| velocity        | vec2   | 8     | 2 (BA)          |
| color           | vec4   | 16    | 4 (1 texel)     |
| color_velocity  | vec3   | 12    | 3 (+ 1 spare)   |
| width           | f32    | 4     | —               |
| **Total**       | —      | **48** | **≥3 texels**   |

### 1.2 Untuk Update Posisi SAJA (spring dynamics)

Input yang dibutuhkan per line tiap frame:
| Data                        | Source            | Channels |
|-----------------------------|-------------------|----------|
| endpoint (prev frame)       | line state buffer | 2        |
| line_velocity (prev frame)  | line state buffer | 2        |
| fluid_velocity (current)    | sim output        | 2        |
| **Total input**             |                   | **6**    |

→ **6 float values** dari **2 sumber berbeda**
→ **1 RGBA8 texel = 4 channel** → TIDAK CUKUP untuk 6 values

### 1.3 Spring Dynamics Formula (dari `place_lines.comp.wgsl:138-145`)

```
variance = mix(1 - line_variance, 1, 0.5 + 0.5 * noise)
velocity_delta_boost = mix(3.0, 25.0, 1 - variance)
momentum_boost = mix(3.0, 5.0, variance)

v_new = (1 - dt * momentum_boost) * v_line
      + (line_length * fluid_vel - endpoint) * velocity_delta_boost * dt

endpoint_new = endpoint + dt * v_new
```

Ada dependency: `endpoint` dipakai untuk hitung `v_new`, lalu `v_new` dipakai
untuk update `endpoint`. Ini sekuensial dalam 1 thread — tidak bisa dipisah
jadi 2 pass terpisah.

---

## 2. The Core Problem

### 2.1 Satu ShaderEffect butuh 2 sampler sekaligus

```glsl
// Yang INGIN dilakukan:
layout(binding = 0) uniform sampler2D lineState;      // endpoint + line_vel
layout(binding = 1) uniform sampler2D simOutput;       // fluid velocity
```

Untuk update posisi line, fragment shader harus BACA:
- `endpoint` dari line state buffer (frame sebelumnya)
- `line_velocity` dari line state buffer (frame sebelumnya)
- `fluid_velocity` dari simulation output (frame sekarang)

→ **6 values, 2 samplers, 1 ShaderEffect** — blocked oleh 1-sampler constraint.

### 2.2 Kenapa Channel Packing Tidak Cukup

Upayakan 6 values → 4 channel (1 RGBA8):
- endpoint.xy = 2 channels
- line_vel.xy = 2 channels  
- fluid_vel.xy = 2 lagi → TIDAK MUAT

Alternatif: hapus line_velocity (no momentum) → 4 values → MUAT:
```
RG = endpoint.xy (bias-encoded)
BA = fluid_vel.xy (bias-encoded)
```
TAPI ini berarti kehilangan momentum/spring dynamics. Lines jadi "instantaneous
flow tracers" yang langsung mengikuti velocity field tanpa inersia.

### 2.3 Verifikasi Constraint

Constrains yang suda terverifikasi:
- **1-sampler per ShaderEffect**: Diverifikasi di sandbox Qt 6.11 standalone
  (`switch_test.frag`, swap test → binding 1 baca texture SALAH)
- **Tidak ada SSBO/storage buffer**: Diverifikasi di sandbox (`line_test.vert`
  dengan SSBO di binding 1 → segfault karena binding 1 dipakai Qt untuk UBO)
- **Tidak ada transform feedback**: Qt RHI tidak support
- **Custom vertex shader works di Quickshell**: Diverifikasi 2025-06-18
  (test_vertex.vert jalan 5s tanpa segfault)

---

## 3. Eksplorasi: Multi-Pass Sequential

### 3.1 Ide: Pisah Update Jadi 3 Sub-Pass

```
Pass A: Resample velocity ke grid (1 sampler: sim output)
  → Input: simOutput (128×128)
  → Output: gridVelBuffer (9×10 RGBA8, RG = fluid_vel.xy)
  → Tiap texel = velocity di satu basepoint

Pass B: Update line state (butuh... 2 samplers lagi!)
  → Butuh: lineState (dari frame N-1) + gridVelBuffer
  → STUCK: masih butuh 2 sumber sekaligus
```

Pass A bisa dilakukan dengan 1 sampler. Pass B gagal karena butuh 2 input.

### 3.2 Multi-Pass dengan Data Merge

```
Pass A: Resample velocity (1 sampler)
  → Output: velocity_buffer (grid-sized, RG = vel)

Pass B: Merge velocity ke line state (1 sampler: velocity_buffer)
  → Baca velocity_buffer
  → Output ke: fresh_line_buffer (grid-sized, RG = endpoint, BA = vel)
  → TAPI endpoint dari mana? Line state dari frame N-1 tidak tersedia!

Pass C: Update posisi (1 sampler: fresh_line_buffer)
  → Baca endpoint + vel dari fresh_line_buffer
  → Hitung endpoint baru
  → Output ke: updated_line_buffer
  → TAPI fresh_line_buffer hanya punya endpoint DARI MANA?
```

Pass B HARUS baca velocity buffer + old line state → 2 samplers. Circular.

### 3.3 Multi-Pass dengan Pre-encode

Teknik yang dipakai pipeline simulation untuk pressure: on-the-fly computation.

Idenya: jangan simpan line state di texture terpisah. Encode line state
LANGSUNG di grid texel bersama dengan fluid velocity.

Setelah Pass A (resample velocity ke grid):
```
Grid buffer: RG = fluid_vel.xy, BA = (spare)
```

Lalu Pass B: baca grid buffer, encode endpoint ke channel BA, output:
```
Grid buffer v2: RG = fluid_vel.xy, BA = endpoint.xy
```

Lalu Pass C: baca grid buffer v2, update endpoint dari BA + RG:
```
endpoint_baru = BA + dt * RG  ← ini tracking langsung, no spring!
```

TAPI: untuk spring dynamics, perlu `line_velocity` (momentum) yang butuh
channel sendiri → butuh 2 texels per line = 6 channel.

### 3.4 Multi-Pass dengan 2 Texels per Line

Layout line data (2 texels per line, grid-sized texture with doubled height):
```
Texel 0 (baris genap): RG = endpoint.xy, BA = line_vel.xy
Texel 1 (baris ganjil): RG = fluid_vel.xy, BA = 1.0
```

Pipeline:
```
Pass A (resample): 
  1 sampler: simOutput
  Output ke texels baris ganjil: RG = fluid_vel.xy
  
  → Berhasil! TAPI endpoint di baris genap dari FRAME SEBELUMNYA
    perlu dipertahankan. Bagaimana?

  Solusi: SIMPAN texel baris genap. Tapi untuk menulis ulang,
  perlu baca old line state → 2 samplers lagi.
```

**KESIMPULAN**: Multi-pass sequential tidak bisa bypass 1-sampler limit.
Setiap pass hanya bisa baca 1 texture. Update line state butuh 2 sumber
(line state lama + velocity baru) → minimal 2 samplers dalam 1 pass.

---

## 4. Eksplorasi: Hybrid Encode ke Satu Texture

### 4.1 Ide: Texture Tunggal untuk Semua Data

Buat texture 128×130 (atau 128×256) yang mengandung:
- Baris 0-127: simulation data (velocity 128×128)
- Baris 128+: line state per-grid-point

Line update pass membaca texture ini dengan 1 sampler:
- Sample simulation area untuk fluid velocity
- Sample line state area untuk endpoint + line_vel

Masalah: line state butuh AKSES RANDOM ke baris ke-128+ berdasarkan line index.
UV computation tidak bisa map line index ke grid position secara trivial dalam
fragment shader.

### 4.2 Fragment Shader Tidak Bisa "Pilih" Texel

Fragment shader dipanggil per-pixel output. Untuk update line state, kita
ingin invoke shader ONCE per line (90 kali), bukan per-pixel output (128×128 = 16384).

Kalau fragment shader jalan untuk setiap pixel di output 128×128:
- 16384 invokasi untuk update 90 lines → waste
- Setiap invokasi harus ngecek: "apakah pixel ini adalah basepoint?"
  → conditional branch → tidak deterministik → GPU-unfriendly

### 4.3 Grid-Sized Output Texture

Solution: buat OUTPUT texture untuk line update berukuran grid (9×10 = 90 texels),
BUKAN sebesar simSize (128×128).

ShaderEffect dengan `width: gridCols` dan `height: gridRows` → fragment shader
dipanggil tepat 90 kali. Tiap invokasi tahu index line dari `qt_TexCoord0`.

TAPI ShaderEffect dengan ukuran 9×10:
- Hanya bisa menampung 1 texel per line
- 1 texel = 4 channel → bisa pakai untuk endpoint.xy (2) + fluid_vel.xy (2)
- TIDAK ADA channel untuk line_velocity (momentum)!

Dengan 2 texel per line (texture 18×10 atau 9×20):
- Masih 1 sampler
- Tapi BUTUH MENULIS ke texel 0 (endpoint) dan texel 1 (fluid_vel)
  → Fragment shader hanya output 1 fragColor → tidak bisa menulis ke 2 texels
  tanpa multi-pass.

### 4.4 Kesimpulan Hybrid

Hybrid encode tidak bisa solve masalah fundamental: **butuh >4 nilai per line
untuk spring dynamics**, dan **fragment shader hanya bisa output 1 FragColor**
(4 channel) per invokasi.

---

## 5. Opsi yang Tersedia

### Opsi A: Implicit Lines (No State)

"Fragment-shader-only flow visualization."

- Tidak ada line state persistent
- Line dirender sebagai visualisasi langsung dari velocity field
- Setiap pixel: hitung jarak ke basepoint terdekat, gambar segmen arah velocity

**Keuntungan**:
- ✅ 1 sampler cukup (sim output saja)
- ✅ Tidak ada state management
- ✅ Implementasi paling sederhana
- ✅ Bisa digabung dengan visualize.frag

**Kerugian**:
- ❌ Tidak ada spring dynamics
- ❌ Tidak ada momentum / inersia
- ❌ Lines "kaku" — mengikuti flow sempurna tanpa delay
- ❌ Visual kurang organik

**Channel requirement**: 0 (cuma baca sim output, tidak perlu line buffer)

### Opsi B: Re-Verify 2-Sampler di Quickshell

Jika 2 sampler BEKERJA di Quickshell (berbeda dari sandbox standalone),
maka full spring dynamics bisa diimplementasi.

**Kenapa mungkin berbeda**:
- Quickshell punya rendering path berbeda (PanelWindow + ShellRoot)
- Custom vertex shader juga aman di Quickshell (beda dari sandbox)
- `switch_test.qsb` di Quickshell: LOAD tanpa error (belum verified visual)

**Test yang diperlukan**:
1. Buat 2 texture dengan nilai KNOWN dan BERBEDA (misal merah dan biru solid)
2. Shader yang blend hasil keduanya (expected: ungu kalau binding 1 berfungsi)
3. Render di Quickshell, `grabToImage()` hasilnya
4. Bandingkan pixel secara numerik dengan expected value

**Jika binding 1 bekerja → implementasi penuh**:
```
ShaderEffect {
  property var simTex: sim.output
  property var lineTex: lineStateBuffer  // dari frame N-1
  fragmentShader: "line_update.qsb"
  // → Di shader: binding 0 = simTex, binding 1 = lineTex
}
```

**Jika binding 1 gagal → fallback ke opsi lain**.

**Rekomendasi**: Lakukan test ini dulu sebelum memutuskan arsitektur.
Biaya test: ~30 menit.

### Opsi C: CPU-Side Update (FrameAnimation + grabToImage)

Update line state di JavaScript, render di GPU.

Pipeline:
```
FrameAnimation:
  1. grabToImage() sim output → baca pixel velocity → array CPU
  2. Update line positions di JS array (spring dynamics)
  3. Generate QML Repeater / Canvas dengan posisi baru
  
  → Line rendered via Qt Quick shapes (bukan ShaderEffect)
```

**Keuntungan**:
- ✅ Spring dynamics penuh bisa diimplementasi
- ✅ Tidak ada constraint sampler
- ✅ CPU-side state mudah dikelola

**Kerugian**:
- ❌ `grabToImage()` intermittent di Wayland (lihat Known Issues)
- ❌ CPU-GPU round trip tiap frame → latency
- ❌ Frame rate terbatas (grabToImage async, ~30fps)
- ❌ 90 spring dynamics update di JS → overhead CPU
- ❌ Line rendering perlu QML Shape items → overhead scene graph

**Viability**: Rendah untuk 60fps screensaver. grabToImage bottleneck.

### Opsi D: Simplified Model (No Momentum, Lerp-Based)

Modifikasi spring dynamics untuk menghilangkan dependency line_velocity:

```
// Instead of:
//   v_new = (1 - damping) * v_line + spring_force * dt
//   endpoint_new = endpoint + v_new * dt

// Use:
//   target = basepoint + fluid_vel * line_length
//   endpoint_new = endpoint + (target - endpoint) * lerp_factor
```

Ini LERP sederhana tanpa momentum. Hanya butuh:
- endpoint (2 values, dari prev frame) → 2 channel
- fluid_vel (2 values, dari sim) → 2 channel
- **Total**: 4 values = 1 RGBA8 texel ✅

**Cara update (1 sampler, 1 pass, grid-sized output texture)**:
```
// Prepast: simpan endpoint.xy + fluid_vel.xy di 1 texture
// Shader tiap frame:
vec4 t = texture(lineData, uv);  // RG = endpoint, BA = fluid_vel
vec2 endpoint = t.rg * 2.0 - 1.0;
vec2 fluidVel = t.ba * 2.0 - 1.0;

vec2 target = basepoint + fluidVel * LINE_LENGTH;
vec2 newEndpoint = endpoint + (target - endpoint) * SPRING_CONSTANT;

fragColor = vec4(newEndpoint * 0.5 + 0.5, t.ba);  // BA = fluid_vel (passthrough, akan di-replace prepass berikutnya)
```

**Pipeline 2-pass**:
```
Clock cycle N:
  Pass Prep (1 sampler: sim output, 1 sampler: prev line data)
    → Baca fluid_vel dari sim output
    → Baca endpoint dari prev line data (frame N-1)
    → Tulis ke lineData_N: RG = endpoint_N-1, BA = fluid_vel_N
    
    MASALAH: masih 2 sampler!
```

**Revised pipeline 3-pass**:
```
Pass A: Write fluid velocity ke buffer (1 sampler: sim output)
  → Output velBuffer: RG = vel, BA = 1.0

Pass B: Combine with previous endpoint (1 sampler: velBuffer)
  → Baca velBuffer (vel)
  → ??? endpoint dari prev frame tidak ada di velBuffer
  
  STUCK lagi.
```

**Revised pipeline 4-pass**:
```
Pass A: Write fluid velocity (1 sampler: sim output)
  → Output velBuffer: RG = vel, BA = 1.0

Pass B: Write prev endpoint to pass-through buffer (1 sampler: prev line state)
  → Baca prev line state → tulis ke passBuffer: RG = endpoint, BA = 1.0
  → Butuh SAMPLER KEDUA untuk menggabungkan? Tidak — pass B hanya baca 1 texture (prev line state) dan tulis output.
  → TAPI outputnya hanya 4 channel — belum ada velocity.

Pass C: Combine (butuh 2 samplers: velBuffer + passBuffer) → STUCK
```

**MASALAH FUNDAMENTAL**: Setiap penggabungan data dari 2 sumber butuh 1 pass
dengan 2 sampler. Tidak bisa dipecah karena fragment shader cuma bisa baca 1 texture.

**Kesimpulan Opsi D**: Dengan 1 sampler, 2-pass linear pipeline cuma bisa
membawa 1 sumber data. Untuk update line yang butuh 2 sumber (line state +
velocity), tidak bisa.

Kecuali: data digabung di dalam texture YANG SAMA. Tapi ini berarti line state
texture HARUS mengandung velocity yang suda di-resample. Dan untuk me-resample
velocity ke line state texture, butuh pass yang baca sim output → tulis ke
line state. Tapi pass itu juga HARUS baca old line state untuk pertahankan
endpoint → lagi-lagi 2 sampler.

### Opsi E: Pre-Fill line state texture dengan Grid Pattern

Grid basepoints FIXED setiap frame (tidak bergerak). Kita BISA encode
basepoint.xy langsung di shader sebagai constant.

```
// Di shader line update:
const float GRID_SPACING = 15.0 / 128.0;
const int GRID_COLS = 9;
const int GRID_ROWS = 10;

void main() {
    vec2 uv = qt_TexCoord0;  // (0..1, 0..1) di grid-sized texture
    
    // Derive basepoint dari pixel position:
    int col = int(uv.x * GRID_COLS);
    int row = int(uv.y * GRID_ROWS);
    vec2 basepoint = vec2(col * GRID_SPACING, row * GRID_SPACING);
    
    // Baca line state dari 1 sampler:
    vec4 t = texture(lineState, uv);  // RG = endpoint, BA = line_vel
    vec2 endpoint = t.rg * 2.0 - 1.0;
    vec2 lineVel = t.ba * 2.0 - 1.0;
    
    // Butuh fluid_vel di basepoint — SAMPLER KEDUA!
    // vec2 fluidVel = texture(simOutput, basepoint).rg * 2.0 - 1.0;
    
    // STILL: 2 sampler
}
```

Basepoint bisa di-derive dari UV (tidak perlu disimpan di texture). TAPI
fluid velocity tetap butuh sampler ke-2 dari simulation output.

### Opsi F: Resample Velocity KE Line State Texture via Multi-Sample

**KEY INSIGHT**: Kalau grid spacing = 15 pixels, dan sim size = 128, maka
garis grid basepoints ada di koordinat:
```
col * 15 / 128  (untuk col = 0..8)
```

Kalau kita bikin line state texture dengan channel B berisi FLUID VELOCITY,
gimana caranya ngisi B dengan fluid velocity dari sim output?

**Pass A: Resample langsung ke line state texture**
```
Input: sim_output (128×128, sampler binding 0)
Output: lineState (texture 9×10, RGBA8)

// Shader:
float col = floor(qt_TexCoord0.x * 9);
float row = floor(qt_TexCoord0.y * 10);
vec2 baseUV = vec2(col * 15.0 / 128.0 + 7.5 / 128.0, row * 15.0 / 128.0 + 7.5 / 128.0);
vec2 vel = texture(simOutput, baseUV).rg * 2.0 - 1.0;

// TAPI tidak ada endpoint dari prev frame! 
fragColor = vec4(0.0, 0.0, vel.x * 0.5 + 0.5, vel.y * 0.5 + 0.5);
// RG = 0 (endpoint unknown)
// BA = vel (fluid velocity)
```

Pass ini cuma bisa nulis fluid velocity. Endpoint (yang harus bertahan dari
frame N-1 ke frame N) hilang.

**Solusi**: TIGA pass sequential?

```
Frame N:
  Pass A (resample): baca sim output → tulis velBuffer (RG=vel)
  Pass B (read velBuffer, read... old line state): 
    → BUTUH 2 SAMPLERS (lagi-lagi)
```

### Opsi G: Temporal Accumulation (2-Frame Buffer)

**Ide**: Gunakan frame N-1 dan frame N sebagai 2 sumber dalam satu texture.

Line state texture diperluas jadi 18×10 (setara 2 texels per line):
```
Texel 0: RG = endpoint (N-1), BA = line_vel (N-1)
Texel 1: RG = fluid_vel (N), BA = endpoint_target (N)
```

Pipeline:
```
Pass A (frame N): 
  1 sampler: simOutput
  Output velBuffer: fluid_vel untuk setiap basepoint
  
  Sekarang kita punya:
  - Old line state (dari frame N-1): endpoint, line_vel
  - velBuffer: fluid_vel
  
  TAPI UNTUK MENGGABUNGKAN ke satu texture, butuh 2 sampler
  (old line state + velBuffer)!
```

Masih sama.

---

## 6. Kesimpulan

### 6.1 Ringkasan

**Dengan 1-sampler constraint, stateful line update (spring dynamics)
tidak bisa dilakukan di fragment shader.**

Alasan: setiap update butuh read dari 2 sumber (state lama + velocity baru),
tapi fragment shader cuma bisa 1 sampler. Channel packing tidak membantu
karena 6 > 4.

### 6.2 Satu-Satunya Workaround: Vertex Shader (Opsi H)

**Insight**: Di Quickshell, custom vertex shader TIDAK SEGFAULT (beda dari
sandbox standalone). Vertex shader bisa dipanggil dengan gl_VertexID berbeda
per-vertex dalam satu quad.

**Pendekatan**:
1. Satu ShaderEffect dengan ukuran penuh
2. Custom vertex shader di-call 4 kali (satu quad) ATAU dengan instancing
3. Vertex shader membaca line data texture (1 sampler, binding 0)
4. Untuk tiap vertex, vertex shader bisa sample texture pada UV tertentu
5. Fragment shader hanya render output yang suda diposisikan vertex shader

TAPI: satu ShaderEffect = satu quad = 4 vertices. Hanya bisa render SATU line.
Untuk 90 lines, butuh 90 ShaderEffect items.

**Available approach**: Bikin 90 ShaderEffect items via QML Repeater.

Setiap item:
- Ukuran: 1×1 pixel (hanya trigger)
- Posisi: diluar layar (offscreen rendering)
- vertex shader: membaca line data dari texture → expand jadi quad → posisikan di endpoint
- fragment shader: render warna garis

**Kenapa ini bisa**: Vertex shader bisa panggil `texture()` untuk baca data.
Cuma 1 sampler (line data). Fragment shader hanya render tanpa sampler tambahan.

**Channel layout for 1 sampler line data**:
```
Per line: endpoint.xy (RG), line_vel.xy (BA) = 1 texel
→ 90 texels = 90×1 texture
→ Di vertex shader sampled via UV

TAPI: fluid_vel dari mana?
```

Fluid_vel perlu di-sample dari sim output. Vertex shader tidak bisa baca
2 sampler.

**Kecuali**: Kita resample fluid_vel KE line data texture tiap frame,
via grid-sized ShaderEffect (9×10) dengan 1 sampler (sim output).

Pipeline lengkap:
```
Pass A (prepass, grid-sized ShaderEffect):
  1 sampler: simOutput (128×128)
  Output ke: lineData_N (9×10 RGBA8)
  → RG = endpoint (dari MANA?)
  → BA = fluid_vel.xy (dari simOutput)
  
  TETAP BUTUH endpoint dari prev frame → SAMPLER KEDUA
```

**DEADLOCK TERAKHIR**: Untuk menulis endpoint + fluid_vel ke satu texture,
butuh baca 2 sumber (old line data + sim output). Tidak bisa.

### 6.3 Final Verdict

| Opsi | Spring Dynamics | 1 Sampler | Visual Quality | Feasibility |
|------|----------------|-----------|----------------|-------------|
| A: Implicit (no state) | ❌ | ✅ | Medium | ✅ |
| B: Re-verify 2-sampler | ✅ (if passes) | ✅ | High | ⏳ Test needed |
| C: CPU-side | ✅ | N/A | Medium | ❌ Low FPS |
| D: Simplified lerp | ❌ (no momentum) | ❌ (still 2 samplers) | Low | ❌ |
| E-H: Vertex/SSBO/Other | ✅ | ❌ (still 2 samplers) | High | ❌ |

**Jawaban akhir**: Channel packing TIDAK BISA solve line update problem.

### 6.4 Rekomendasi

1. **Test Opsi B dulu**: Re-verify 2-sampler binding di Quickshell.
   Ini test kecil (~30 menit) yang bisa mengubah kesimpulan di atas.
   Jika binding 1 bekerja di Quickshell, full spring dynamics feasible.

2. **Jika Opsi B fails**: Implementasi Opsi A (implicit lines).
   Gunakan fragment shader yang visualisasi velocity field langsung sebagai
   "lines" tanpa persistent state.

3. **Jika mau spring dynamics dengan kualitas tinggi**:
   Pertimbangkan render lines di CPU (Opsi C) tapi dengan mitigasi Wayland
   grabToImage issue.
