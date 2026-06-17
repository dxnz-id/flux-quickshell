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
- **Uniform block** (`layout(std140, binding=N) uniform`) → QML property tidak
  bisa di-mapping ke member-nya. Gunakan params texture (Canvas → sampler2D).
- **ShaderEffectSource format RGBA8** → clamp nilai negatif ke 0. Semua shader
  yang output velocity/pressure/noise harus bias encode: `v*0.5+0.5` saat
  output, `v*2.0-1.0` saat dibaca kembali.

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

Nanti akan diperluas saat shader ditulis. Gunakan `textureLod` dengan
`float(texelIndex) + 0.5) / textureSize(params, 0).x` untuk akses per texel.

### Time Texture

Format: 1×1 RGBA8.
```
R = elapsed_time (second)
G = timestep (delta time)
B/A = _unused
```

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
