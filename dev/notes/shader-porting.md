# Catatan Porting Shader: WGSL → GLSL (Qt ShaderEffect)

## CRITICAL: qsb Compilation Flag

**SELALU gunakan `--qt6` untuk shader yang dipakai di ShaderEffect QML.**

### Benar
```bash
qsb --qt6 input.frag -o output.qsb
```

### Salah (menyebabkan white/black output, silent failure)
```bash
qsb --glsl "450" input.frag -o output.qsb
qsb --glsl "440,330,150,100 es" input.frag -o output.qsb
```

### Kenapa
`--qt6` menggunakan name-based attribute matching yang cocok dengan default
vertex shader Qt Quick. Flag `--glsl` dengan versi tinggi (330+) menghasilkan
`layout(location=N)` eksplisit untuk attribute yang tidak cocok dengan vertex
shader internal Qt → silent linker fallback → output putih atau hitam tanpa
error yang jelas. Sudah diverifikasi runtime di Qt 6.11.1, OpenGL 4.6,
Mesa Intel.

### Constraint Lain yang Sudah Diverifikasi
- **Custom vertex shader** → segfault di Qt 6.11. Jangan dipakai.
- **ShaderEffectSource format RGBA8** → clamp nilai negatif ke 0. Semua shader
  yang output velocity/pressure/noise harus bias encode: `v*0.5+0.5` saat
  output, `v*2.0-1.0` saat dibaca kembali.

### Uniform Passing — Dua Masalah Berbeda

Ada dua masalah TERPISAH yang sering tertukar. Pahami bedanya:

#### 1. Bare Uniform — COMPILE-TIME REJECTION

```glsl
#version 420
uniform float uTime;            // ❌ Ditulis di GLSL source
```

**qsb --qt6** pertama-tama kompilasi source ke SPIR-V (untuk Vulkan RHI).
Vulkan GLSL **melarang** `uniform` untuk non-opaque types di global scope.
Error saat kompilasi:

```
ERROR: 'non-opaque uniforms outside a block' : not allowed when using GLSL for Vulkan
```

**Verdict**: GAGAL TOTAL. Tidak ada kompromi. Gunakan pendekatan non-uniform
(1×1 texture, channel packing, atau hardcoded constants).

#### 2. Uniform Block — RUNTIME MAPPING FAILURE

```glsl
#version 420
layout(std140, binding = 1) uniform Params {
    float uTime;
};
```

**qsb --qt6** berhasil compile ke SPIR-V dan cross-compile ke target backend.
ShaderEffect bisa di-load tanpa error.

Tapi QML tidak bisa mapping property ke member UBO:

```qml
// Property ini TIDAK akan masuk ke Params.uTime:
ShaderEffect {
    property real uTime: 0.5
}
```

Qt RHI tidak memiliki mekanisme untuk meng-update uniform block members
dari QML properties secara dinamis. Hasil: shader jalan, nilai uTime tetap
default (0.0 atau undefined).

**Verdict**: COMPILE SUKSES, RUNTIME GAGAL (silent — shader tetap render,
tapi nilai uniform salah).

#### Ringkasan

| Jenis | qsb --qt6 | Runtime Mapping | Deteksi Error |
|---|---|---|---|
| `uniform float uTime;` (bare) | REJECTED | — | Error jelas saat compile |
| `layout(std140) uniform Params { float u; };` (block) | OK | FAIL (silent) | Tidak ada error — output salah tanpa pemberitahuan |

**Implikasi**: Kedua pendekatan uniform tidak bisa dipakai untuk passing
parameter ke ShaderEffect. Satu-satunya cara yang berfungsi adalah:
1. **Sampler2D** (melalui QML `property var` → binding layout)
2. **1×1 Rectangle + ShaderEffectSource** (untuk scalar/vector values)
3. **Encoding data ke channel RGBA texture yang sudah ada** (channel packing)
4. **Hardcoded constants** di GLSL source

## Mapping Tipe Data WGSL → GLSL

| WGSL | GLSL | Catatan |
|---|---|---|
| `f32` | `float` | |
| `vec2<f32>` | `vec2` | |
| `vec3<f32>` | `vec3` | |
| `vec4<f32>` | `vec4` | |
| `u32` | `uint` | |
| `i32` | `int` | |
| `mat4x4<f32>` | `mat4` | |
| `texture_2d<f32>` | `sampler2D` | Di ShaderEffect: uniform sampler2D nama_sampler; |
| `texture_storage_2d<rgba16float, write>` | N/A (imageStore) | **Tidak bisa porting langsung.** ShaderEffect hanya support `sampler2D` read-only. Multi-pass harus via `ShaderEffectSource` ping-pong. |
| `texture_storage_2d<r32float, write>` | N/A | Sama seperti di atas. |
| `@group(N) @binding(M)` | `layout(binding=M)` | Di GLSL untuk ShaderEffect, binding harus match convention. |
| `var<uniform>` | `uniform` | |
| `var<storage, read>` | `buffer` (SSBO) | Tidak support di ShaderEffect. |
| `textureLoad(t, coord, lod)` | `texelFetch(sampler, ivec2(coord), lod)` | |
| `textureSampleLevel(t, s, coord, lod)` | `textureLod(sampler, coord, lod)` | |
| `textureSampleLevel(t, s, coord, lod, offset)` | `textureLodOffset(sampler, coord, lod, ivec2(offset))` | |
| `textureDimensions(t)` | `textureSize(sampler, lod)` | |

## Mapping Texture/Sampler

### WGSL (Compute Shader)

Di WGSL, texture binding dan sampler binding terpisah:
```wgsl
@group(0) @binding(1) var linear_sampler: sampler;
@group(0) @binding(2) var nearest_sampler: sampler;
@group(1) @binding(0) var velocity_texture: texture_2d<f32>;
```

### GLSL (ShaderEffect Fragment Shader)

Di Qt ShaderEffect, sampler dan texture digabung jadi `sampler2D`:
```glsl
uniform sampler2D velocity_texture;
// Qt automatically binds the sampler with filtering
```

Sampler filtering dikontrol dari QML `ShaderEffect`:
```qml
ShaderEffect {
  property var velocityTexture: ShaderEffectSource { ... }
  // Qt automatically uses linear filtering for ShaderEffectSource
}
```

### Storage Textures (Write)

WGSL bisa write langsung ke storage texture via `textureStore`. Ini **tidak
mungkin** di ShaderEffect fragment shader. Untuk multi-pass simulation:

1. Pass A: ShaderEffect membaca input texture → fragment shader compute hasil
2. Output pass A: `ShaderEffectSource` (menangkap hasil render pass A)
3. Pass B: ShaderEffect membaca `ShaderEffectSource` dari pass A sebagai input

### Storage Buffers (SSBO)

WGSL bisa baca `var<storage, read>` seperti array of struct. Ini **tidak
mungkin** di ShaderEffect. Alternatif:
- Untuk parameter: encode ke dalam texture kecil (params texture)
- Untuk data per-instance (seperti Line struct): tidak ada padanan langsung.
  Perlu redesign (misal render langsung di CPU-side dengan instancing via
  QML, atau shader texture-based approach).

## Multi-Pass Architecture (untuk Qt ShaderEffect)

Karena fragment shader tidak bisa write ke arbitrary texture, kita perlu
mengaplikasikan pola **ping-pong dengan ShaderEffectSource**:

```
Frame n:
  ┌─ noise.frag ─────────────────────────┐
  │  input: paramsTexture, timeTexture    │
  │  output: noiseTexture (via recursive) │
  └──────────────────────────────────────┘
  ↓
  ┌─ advect_forward.frag ────────────────┐
  │  input: velocityTexture, paramsTex    │
  │  output: forwardTexture               │
  └──────────────────────────────────────┘
  ↓
  ┌─ advect_reverse.frag ────────────────┐
  │  input: forwardTexture, paramsTex     │
  │  output: reverseTexture               │
  └──────────────────────────────────────┘
  ↓
  ┌─ adjust_advection.frag ──────────────┐
  │  input: forward, reverse, velocity,   │
  │         paramsTex                     │
  │  output: velocityTexture (swap)       │
  └──────────────────────────────────────┘
  ↓ ... dan seterusnya
```

Setiap pass adalah `ShaderEffect` + `ShaderEffectSource` di QML.
`ShaderEffectSource` punya property `live: false` (hanya update saat
`scheduleUpdate()` dipanggil).

## Params Texture Convention

### Binding Convention
- Binding 8 = `paramsTexture` (parameter simulasi)
- Binding 9 = `timeTexture` (waktu)

### Layout Params Texture

Format: 1×N RGBA8 texture, di-encode sebagai `sampler2D`.

```
Texel 0: (timestep, dissipation, viscosity, _unused)
Texel 1: (center_factor, stencil_factor, alpha, r_beta)
Texel 2: (noise_multiplier, _unused, _unused, _unused)
Texel 3+: (reserved untuk channel noise — detail menyusul)
```

Akses per texel: `texture(paramsTexture, vec2((float(texelIndex) + 0.5) / 8.0, 0.5))`.
`texelFetch` tidak bisa dipakai karena qsb --qt6 target GLSL ES 1.00.

### Time Texture

Format: 1×1 RGBA8.
```
R = elapsed_time (second)
G = timestep (delta time)
B/A = _unused
```

## Shader Helper Pattern

Setiap shader GLSL membutuhkan helper function yang konsisten untuk
membaca params texture dan time texture. Karena `#include` tidak reliable
di Qt qsb/GLSL ES, helper berikut di-copy ke setiap file `.frag`.

```glsl
// === GLSL version & layout qualifiers ===
// HARUS #version 420 karena layout(binding=N) untuk sampler tidak
// support di version < 420. qsb --qt6 akan cross-compile ke GLSL 120/150
// otomatis tanpa masalah.
#version 420

layout(binding = 0) uniform sampler2D velocityTexture;
layout(binding = 8) uniform sampler2D paramsTexture;   // 8x1 RGBA8 — parameter simulasi
layout(binding = 9) uniform sampler2D timeTexture;     // 1x1 RGBA8 — waktu (IEEE 754 float32)

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Decode IEEE 754 float32 dari vec4 RGBA8 (4 byte)
float decodeFloat(vec4 v) {
    uvec4 b = uvec4(v * 255.0 + 0.5);
    uint bits = (b.r << 24u) | (b.g << 16u) | (b.b << 8u) | b.a;
    return uintBitsToFloat(bits);
}

// Decode normalized param dari texel index dengan range [minVal, maxVal]
float readNormParam(int texelIndex, int channel, float minVal, float maxVal) {
    vec2 uv = vec2((float(texelIndex) + 0.5) / 8.0, 0.5);
    vec4 p = texture(paramsTexture, uv);
    float norm = channel == 0 ? p.r : channel == 1 ? p.g : channel == 2 ? p.b : p.a;
    return minVal + norm * (maxVal - minVal);
}

vec2 getResolution() {
    vec4 p0 = texture(paramsTexture, vec2(0.5 / 8.0, 0.5));
    return p0.rg * 1024.0;
}

float getTimestep() {
    return readNormParam(1, 0, 0.001, 0.1);
}

float getDissipation() {
    return readNormParam(1, 1, 0.0, 5.0);
}

float getCenterFactor() {
    return readNormParam(2, 0, 0.0, 10.0);
}

float getStencilFactor() {
    return readNormParam(2, 1, 0.0, 1.0);
}

float getITime() {
    return decodeFloat(texture(timeTexture, vec2(0.5)));
}
```

### Layout Params Texture (Updated)

| Texel | UV (center) | Channel | Parameter | Range |
|-------|------------|---------|-----------|-------|
| 0 | 0.5/8.0 | RG | resolution / 1024 | 0..1 |
| 0 | 0.5/8.0 | BA | _unused | — |
| 1 | 1.5/8.0 | R | timestep (norm) | [0.001, 0.1] |
| 1 | 1.5/8.0 | G | dissipation (norm) | [0.0, 5.0] |
| 1 | 1.5/8.0 | BA | _unused | — |
| 2 | 2.5/8.0 | R | center_factor (norm) | [0.0, 10.0] |
| 2 | 2.5/8.0 | G | stencil_factor (norm) | [0.0, 1.0] |
| 2 | 2.5/8.0 | BA | _unused | — |
| 3-7 | — | — | reserved (noise channels, dll) | — |

## Runtime Verification — Phase 1

### Date
2025-06-17

### Shaders Tested
- `shaders/src/divergence.frag` → `compiled/divergence.qsb` ✅
- `shaders/src/pressure.frag` → `compiled/pressure.qsb` ✅
- `shaders/src/subtract_gradient.frag` → `compiled/subtract_gradient.qsb` ✅
- `shaders/src/init_velocity.frag` → `compiled/init_velocity.qsb` ✅

### Test Method
Sandbox Qt Quick app di `dev/shader-sandbox/`:
1. Inisialisasi velocity field (divergent flow: `v = (uv - 0.5) * 2.0`, bias-encoded)
2. Divergence pass → output divergence texture
3. Pressure pass (single Jacobi iteration, pressure dimulai dari 0) → output pressure
4. Subtract gradient pass → output corrected velocity (ditampilkan di layar)

### Results
| Check | Status | Notes |
|-------|--------|-------|
| App load tanpa crash | ✅ | Timeout exit (124) setelah 2 detik |
| Shader error di terminal | ✅ | Zero — semua .qsb ter-compile dan ter-load |
| Binding correct | ✅ | qt_TexCoord0 + layout(location) pattern work |
| paramsTexture readable | ✅ | getResolution() memberikan nilai yang benar |
| timeTexture readable | ✅ | decodeFloat pattern work (dari FrameAnimation) |

### Key Learnings
1. `#version 150` doesn't support `layout(binding=N)` for samplers. Use `#version 420`.
2. `--qt6` flag is correct for output. The SOURCE GLSL version can be higher.
3. All inputs/outputs need explicit `layout(location=N)` — including `qt_TexCoord0`.
4. `texelFetch()` rejected by GLSL ES 1.00 target — use `texture()` with centered UV.
5. qsb will correctly cross-compile `#version 420` source to GLSL 120/150 targets.

## Key Differences in Approach

### Compute Shader → Fragment Shader

| Aspek | WGSL Compute | GLSL Fragment (ShaderEffect) |
|---|---|---|
| Invocation | `@workgroup_size(16,16,1)` | Setiap pixel = 1 fragment |
| Output | `textureStore` langsung | `gl_FragColor` atau `fragColor` |
| Input sampling | `textureLoad` / `textureSampleLevel` | `textureLod` / `texelFetch` |
| Coordinate | Global invocation ID (pixel) | UV (0..1) normalised |
| Multiple outputs | Multiple storage textures | 1 output color (per GLSL) |
| Shared memory | workgroup shared memory | Tidak ada |

### Boundary Condition

Di WGSL compute shader, boundary condition di-handle manual dengan check
`global_id.x == 0u` dll. Di fragment shader, boundary bisa pakai
`GL_CLAMP_TO_EDGE` (default Qt) atau manual check di shader.

Untuk Neumann boundary (pressure solver), kita perlu manual check karena
GL_CLAMP_TO_EDGE clamp UV ke [0, 1], yang untuk sample di luar boundary
akan clamp ke edge — ini sebenarnya sudah mendekati Neumann bc secara
natural (gradien = 0 di luar edge). Tapi untuk no-slip boundary
(subtract_gradient), kita perlu set velocity = 0 di edge.

### Bias Encoding

```
// Encoding (output dari shader):
float encode(float value) { return value * 0.5 + 0.5; }
vec2  encode(vec2  value) { return value * 0.5 + 0.5; }

// Decoding (input ke shader):
float decode(float value) { return value * 2.0 - 1.0; }
vec2  decode(vec2  value) { return value * 2.0 - 1.0; }
```

Terapkan di setiap shader yang:
- Menulis velocity (keluar-masuk bias encoding)
- Menulis pressure
- Menulis noise
- Membaca divergence

Tidak perlu bias encoding untuk:
- Parameter texture (semua nilai unsigned)
- Time texture
- Color output final (display)
