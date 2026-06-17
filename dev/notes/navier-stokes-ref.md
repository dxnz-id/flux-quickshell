# Catatan Navier-Stokes — flux-reference

Berdasarkan analisis shader WGSL di `../flux-reference/flux/shader/` dan
Rust orkestrasi di `../flux-reference/flux/src/`.

## Arsitektur Render Pass

Semua pass simulasi adalah **compute shader** (bukan render pass). Hanya
line rendering yang menggunakan render pass (vertex + fragment shader).

### Urutan Compute Pass per Frame (dari `flux.rs`)

```
noise_generator.update_buffers(queue, timestep)
  ↓
noise_generator.generate()
  → output: noise_texture (RG texture, Simplex noise)
  ↓
fluid.advect_forward()
  → semi-Lagrangian forward advection (direction=+1)
  → input: velocity → output: forward_advected
  ↓
fluid.advect_reverse()
  → semi-Lagrangian reverse advection (direction=-1)
  → input: forward_advected → output: reverse_advected
  ↓
fluid.adjust_advection()
  → MacCormack correction: forward + 0.5*(original - reverse)
  → clamped to min/max of 4 neighbors
  → output: velocity (swapped ping-pong)
  ↓
fluid.diffuse() × N_iter (default=3)
  → Jacobi iteration untuk viscous diffusion
  → output: velocity (swapped every iteration)
  ↓
noise_generator.inject_noise_into()
  → v_new = v + timestep * noise
  → output: velocity (swapped)
  ↓
fluid.calculate_divergence()
  → central difference divergence
  → output: divergence_texture (R32Float)
  ↓
fluid.solve_pressure() × N_iter (default=19)
  → Jacobi iteration untuk Poisson equation
  → output: pressure (swapped every iteration)
  ↓
fluid.subtract_gradient()
  → v_new = v - ∇p
  → output: velocity (swapped)
```

Setelah compute pass, baru render pass:
```
lines.place_lines(velocity)     → compute: update line positions
lines.draw_lines()              → render: draw line quads
lines.draw_endpoints()          → render: draw endpoint quads
```

### Catatan Penting

- Velocity texture: RGBA16Float, ping-pong antara 2 texture (index 0 dan 1).
  `last_velocity_index` menentukan texture mana yang jadi input dan output,
  di-swap setiap write pass.
- Pressure texture: R32Float (atau RGBA16Float fallback), ping-pong ganda.
- Forward/reverse advection texture: texture perantara untuk MacCormack.
- Divergence texture: single texture (hanya ditulis sekali per frame).

## Persamaan Matematika

### 1. Advection (Semi-Lagrangian + MacCormack)

#### Forward advection (`advect.comp.wgsl`)

Persamaan semi-Lagrangian:
```
x_advected = x - dt * v(x)
v_new(x) = v(x_advected) / decay
decay = 1 + dissipation * dt
```

GLSL pseudocode:
```glsl
vec2 pos = (gl_GlobalInvocationID.xy + 0.5) / textureSize(velocity);
vec2 advected = (gl_GlobalInvocationID.xy + 0.5) - timestep * velocity;
advected /= textureSize(velocity);
float decay = 1.0 + dissipation * timestep;
vec2 newVel = texture(velocity, advected).xy / decay;
```

Parameter `direction` (+1 untuk forward, -1 untuk reverse) mengontrol
tanda di depan `timestep * velocity`.

#### MacCormack correction (`adjust_advection.comp.wgsl`)

```
v_adjusted = v_forward + 0.5 * (v_original - v_reverse)
v_new = clamp(v_adjusted, min_neighbor, max_neighbor)
```

GLSL pseudocode:
```glsl
vec2 v_forward = texelFetch(forwardTexture, uv, 0).xy;
vec2 v_reverse = texelFetch(reverseTexture, uv, 0).xy;
vec2 v_original = texelFetch(velocity, uv, 0).xy;

vec2 adjusted = v_forward + 0.5 * (v_original - v_reverse);

// Clamp ke min/max dari 4 neighbor untuk stabilitas
vec2 l = texture(velocity, samplePos, vec2(-1, 0)).xy;
vec2 r = texture(velocity, samplePos, vec2(1, 0)).xy;
vec2 b = texture(velocity, samplePos, vec2(0, -1)).xy;
vec2 t = texture(velocity, samplePos, vec2(0, 1)).xy;
vec2 minV = min(l, min(r, min(b, t)));
vec2 maxV = max(l, max(r, max(b, t)));
vec2 newVel = clamp(adjusted, minV, maxV);
```

Boundary sample position dihitung sebagai `floor(advected_position + 1.0) + 0.5`.

### 2. Diffusion (`diffuse.comp.wgsl`)

Metode: **Jacobi iteration** (bukan Gauss-Seidel — lihat catatan di bawah).

Persamaan:
```
∇²v = (1/ν) * dv/dt

center_factor = 1 / (ν * dt)
stencil_factor = 1 / (4 + center_factor)

v_new = stencil_factor * (v_left + v_right + v_bottom + v_top + center_factor * v)
```

GLSL pseudocode:
```glsl
// center_factor = 1 / (viscosity * timestep)
// stencil_factor = 1 / (4 + center_factor)

vec2 v = texelFetch(velocity, uv, 0).xy;
vec2 l = texture(velocity, samplePos, vec2(-1, 0)).xy;
vec2 r = texture(velocity, samplePos, vec2(1, 0)).xy;
vec2 b = texture(velocity, samplePos, vec2(0, -1)).xy;
vec2 t = texture(velocity, samplePos, vec2(0, 1)).xy;

vec2 newVel = stencil_factor * (l + r + b + t + center_factor * v);
```

Catatan: meskipun disebut "Gauss-Seidel" di AGENTS.md dan komentar Rust,
implementasi aktualnya adalah Jacobi karena semua neighbor dibaca dari
input texture yang SAMA (velocity_texture), bukan dari output yang sudah
diupdate. Hasil ditulis ke out_texture yang terpisah. Ping-pong antar
frame menjadikannya Jacobi iteration biasa.

### 3. Divergence (`divergence.comp.wgsl`)

Central difference di cell center:
```
div = 0.5 * ((r.x - l.x) + (t.y - b.y))
```

GLSL pseudocode:
```glsl
float l = texture(velocity, samplePos, vec2(-1, 0)).x;
float r = texture(velocity, samplePos, vec2(1, 0)).x;
float b = texture(velocity, samplePos, vec2(0, -1)).y;
float t = texture(velocity, samplePos, vec2(0, 1)).y;

float div = 0.5 * ((r - l) + (t - b));
```

### 4. Pressure Solve (`solve_pressure.comp.wgsl`)

Metode: **Jacobi iteration** untuk Poisson equation:
```
∇²p = div

alpha = -1.0
r_beta = 0.25

p_new = r_beta * (p_left + p_right + p_bottom + p_top + alpha * div)
```

GLSL pseudocode:
```glsl
// alpha = -1.0, r_beta = 0.25

float p = texelFetch(pressure, uv, 0).x;
float div = texelFetch(divergence, uv, 0).x;

float l = texture(pressure, samplePos, vec2(-1, 0)).x;
float r = texture(pressure, samplePos, vec2(1, 0)).x;
float b = texture(pressure, samplePos, vec2(0, -1)).x;
float t = texture(pressure, samplePos, vec2(0, 1)).x;

// Boundary condition: Neumann dp/dn = 0
// Implemented via texture clamping (clamp-to-edge) + manual copy at edges
if (uv.x == 0)        l = p;
if (uv.x == size-1)   r = p;
if (uv.y == 0)        b = p;
if (uv.y == size-1)   t = p;

float newP = r_beta * (l + r + b + t + alpha * div);
```

Neumann boundary: `dp/dn = 0` di tepi grid. Implementasi: di boundary,
nilai neighbor yang keluar grid disalin dari nilai pressure cell itu sendiri
(bukan dari clamp-to-edge sampler saja — ada manual check tambahan).

### 5. Subtract Gradient (`subtract_gradient.comp.wgsl`)

```
v_new = boundary_condition * (v - ∇p)

∇p = (0.5 * (r - l), 0.5 * (t - b))
```

GLSL pseudocode:
```glsl
float l = texture(pressure, samplePos, vec2(-1, 0)).x;
float r = texture(pressure, samplePos, vec2(1, 0)).x;
float b = texture(pressure, samplePos, vec2(0, -1)).x;
float t = texture(pressure, samplePos, vec2(0, 1)).x;

// No-slip boundary: velocity = 0 di tepi
vec2 bc = vec2(1.0);
if (uv.x == 0 || uv.x == size.x-1) bc.x = 0.0;
if (uv.y == 0 || uv.y == size.y-1) bc.y = 0.0;

vec2 v = texelFetch(velocity, uv, 0).xy;
vec2 newV = bc * (v - 0.5 * vec2(r - l, t - b));
```

Dua boundary condition sekaligus:
1. **No-slip**: velocity = 0 di boundary (dengan zero-kan komponen yang relevan)
2. **Neumann pressure**: dp/dn = 0 (dari clamp-to-edge sampler — lihat komentar
   di shader: tekanan yang terbaca di luar boundary akan sama dengan nilai
   boundary karena clamping, jadi gradient normalnya 0)

## Noise Generation

### Algoritma

Menggunakan **3D Simplex noise** (fungsi `snoise` di `generate_noise.comp.wgsl`).
Implementasi standard dengan:
- Permutation polynomial: `permute(x) = mod289(((x * 34.0) + 1.0) * x)`
- 7×7 gradient points mapped to octahedron
- Normalized gradients
- 4-corner interpolation with `m = max(0.6 - d², 0)⁴`
- Final scaling by 42.0

### Konfigurasi Channel

Setiap channel noise memiliki struct:
```glsl
struct Channel {
    vec2  scale;         // noise scale per axis
    float offset_1;      // current time offset for noise slice
    float offset_2;      // secondary offset for blending
    float blend_factor;  // 0..1 blend between offset_1 and offset_2
    float multiplier;    // amplitude multiplier
};
```

### Parameter Default

Dari `settings.rs` (3 channel):

| Channel | Scale | Multiplier | offset_increment |
|---------|-------|------------|------------------|
| 0       | 2.8   | 1.0        | 0.001            |
| 1       | 15.0  | 0.7        | 0.006            |
| 2       | 30.0  | 0.5        | 0.012            |

Global `noise_multiplier`: 0.45

### Mekanisme Blend

Dari `noise.rs` — metode `tick()`:
- `offset_1` bertambah sebesar `channel_settings.offset_increment` setiap frame
- Scale juga berosilasi: `scale * (1.0 + 0.15 * sin(0.01 * elapsed * TAU))`
- Saat `offset_1 > BLEND_THRESHOLD` (1000.0), `offset_2` mulai bergerak dan
  `blend_factor` naik, sehingga noise kedua mulai di-mix in
- Saat `blend_factor > 1.0`: `offset_1 = offset_2; offset_2 = 0; blend_factor = 0`
  — siklus berulang. Ini mencegah artifact periodik.

### Noise Output

- Texture format: RG32Float (atau RGBA16Float fallback)
- Setiap texel menyimpan **2 nilai noise** (`vec2`) — untuk 2 sumbu velocity
- Noise di-generate per texel position di UV space `(texel + 0.5) / size`
- Multiple channel di-sum: `noise = Σ(channel.make_noise(texel_pos))`
- Final: `out = noise * global_multiplier`

### Inject Noise

`inject_noise.comp.wgsl`:
```
v_new(x) = v(x) + timestep * noise(x)
```

Ini terjadi **setelah diffusion** dan **sebelum divergence + pressure**.

## Line Rendering

### Struktur Data Line

Dari `place_lines.comp.wgsl`:
```glsl
struct Line {
    vec2  endpoint;       // posisi ujung garis di UV space
    vec2  velocity;       // kecepatan garis (momentum)
    vec4  color;          // RGBA (premultiplied)
    vec3  color_velocity; // perubahan warna per frame
    float width;          // lebar garis (0..1, dari smoothstep)
};
```

### Grid / Basepoints

Dari `grid.rs`:
- Grid uniform di UV space `[0..1] × [0..1]`
- `grid_spacing = 15` (default) pixel antar titik
- `columns = floor(width / grid_spacing) + 1`
- `rows = floor((height / width) * columns) + 1`
- Total `line_count = rows * columns`
- Basepoints: array `vec2` posisi UV dari setiap titik grid

### Line Update (place_lines.comp.wgsl)

Spring-dynamics pada endpoint:
```
variance = mix(1 - line_variance, 1, 0.5 + 0.5 * noise)
velocity_delta_boost = mix(3.0, 25.0, 1 - variance)
momentum_boost = mix(3.0, 5.0, variance)

v_new = (1 - dt * momentum_boost) * v_line
      + (line_length * vel_field - endpoint) * velocity_delta_boost * dt

endpoint_new = endpoint + dt * v_new
```

Ini adalah sistem spring: endpoint tertarik ke arah `line_length * velocity_field`
dengan konstanta pegas `velocity_delta_boost`, sementara momentum di-damping
oleh `momentum_boost`. Ada juga noise per-line (`line_noise_scale`) yang
memvariasikan `variance` antar garis.

Width dan opacity berdasarkan velocity magnitude:
```
width_boost = saturate(2.5 * length(velocity));
new_width = smoothstep(0, 1, width_boost);
opacity = smoothstep(0, 1, width_boost);
```

### Color Modes

1. **Original** (mode 0): `color.rgb = vec3(saturate(vec2(1.0, 0.66) * (0.5 + velocity)), 0.5)`
2. **Color wheel** (mode 1): `angle = atan2(velocity.y, velocity.x)`; lookup color buffer
3. **Image** (mode 2): `color = texture(color_texture, 2.0 * velocity + 0.5)`

Color juga memiliki momentum (smooth transition):
```
c_vel_new = c_vel * (1 - color_momentum_boost * dt)
          + (target_color - color.rgb) * color_delta_boost * dt
color_new.rgb = saturate(color.rgb + dt * c_vel_new)
```

### Vertex Shader (line.wgsl)

Setiap Line di-render sebagai quad 2 segitiga (6 vertex):
```
LINE_VERTICES = [-0.5, 0,  -0.5, 1,  0.5, 1,  -0.5, 0,  0.5, 1,  0.5, 0];
```

Vertex shader expand tiap titik jadi quad:
- Basis X: perpendicular ke arah endpoint (dari basepoint ke endpoint)
  `x_basis = normalize(vec2(-endpoint.y, endpoint.x))`
- Posisi: `point = aspect_zoom * (basepoint * 2 - 1) + endpoint * vertex.y + line_width * width * x_basis * vertex.x`
- Transform ke screen space via `view_matrix`

### Fragment Shader (line.wgsl)

- Fade-in: `alpha *= smoothstep(line_offset, 1.0, vertex.y)`
  — garis mulai transparan di bagian bawah (dekat basepoint)
- Smooth edges: `alpha *= 1 - smoothstep(0.5 - edge_width, 0.5, abs(vertex.x))`
  — tepi garis anti-alias

### Endpoint Rendering (endpoint.wgsl)

- Quad lebih besar: `ENDPOINT_VERTICES = [-1,-1, -1,1, 1,-1, 1,-1, -1,1, 1,1]`
- Posisi: `point = aspect_zoom * (basepoint * 2 - 1) + endpoint + 0.5 * line_width * width * vertex`
- Fragment shader deteksi sisi endpoint via cross product untuk bedakan
  top (ujung) vs bottom (sambung ke line)
- Top half: color dengan endpoint_opacity (lebih terang)
- Bottom half: reverse-blend color untuk seamless overlap dengan line

## Parameter Default

Dari `settings.rs`:

### Simulasi

| Parameter              | Default | Deskripsi                         |
|------------------------|---------|-----------------------------------|
| fluid_size             | 128     | Ukuran texture simulasi (N×N)     |
| fluid_timestep         | 1/60    | Timestep simulasi (16.67ms)       |
| fluid_frame_rate       | 60.0    | Frame rate target                  |
| viscosity              | 5.0     | Viskositas (diffusion coefficient) |
| velocity_dissipation   | 0.0     | Dissipasi velocity per frame       |
| diffusion_iterations   | 3       | Iterasi Jacobi untuk diffusion     |
| pressure_iterations    | 19      | Iterasi Jacobi untuk pressure      |
| pressure_mode          | ClearWith(0.0) | Reset pressure ke 0 tiap frame |

### Noise

| Parameter        | Default | Deskripsi                   |
|------------------|---------|-----------------------------|
| noise_multiplier | 0.45    | Global amplitude noise      |
| channel[0].scale | 2.8     | Large-scale noise           |
| channel[0].mult  | 1.0     |                             |
| channel[0].inc   | 0.001   | Offset increment per frame  |
| channel[1].scale | 15.0    | Medium-scale noise          |
| channel[1].mult  | 0.7     |                             |
| channel[1].inc   | 0.006   |                             |
| channel[2].scale | 30.0    | Fine detail noise           |
| channel[2].mult  | 0.5     |                             |
| channel[2].inc   | 0.012   |                             |

### Line Rendering

| Parameter         | Default | Deskripsi                        |
|-------------------|---------|----------------------------------|
| line_length       | 450.0   | Panjang garis (di-scale)          |
| line_width        | 9.0     | Lebar garis (di-scale)            |
| line_begin_offset | 0.4     | Offset awal fade-in               |
| line_variance     | 0.55    | Variance antar garis (0..1)       |
| grid_spacing      | 15      | Jarak antar basepoint (pixel)     |
| view_scale        | 1.6     | Zoom factor visual                |

## Diagram Multi-Pass

```
Frame n:
  ┌──────────────────────────────────────────────────┐
  │ noise.update_buffers(timestep)                    │
  │   → update channel offsets + scale oscillation    │
  ├──────────────────────────────────────────────────┤
  │ noise.generate()                                  │
  │   → 3D Simplex noise, Σ 3 channel                 │
  │   → output: noise_texture (RG32Float)             │
  ├──────────────────────────────────────────────────┤
  │                                                    │
  │  ┌── FLUID SUB-STEP (sub-frame, fixed timestep) ──┐│
  │  │                                                 ││
  │  │ advect_forward()                                ││
  │  │   → semi-Lagrangian: v(x - dt*v)                ││
  │  │   → output: forward_advected                    ││
  │  │                                                 ││
  │  │ advect_reverse()                                ││
  │  │   → re-advect forward result backward            ││
  │  │   → output: reverse_advected                    ││
  │  │                                                 ││
  │  │ adjust_advection()                              ││
  │  │   → MacCormack: fwd + 0.5*(orig - rev)          ││
  │  │   → clamp to neighbor bounds                    ││
  │  │   → output: velocity (swap)                     ││
  │  │                                                 ││
  │  │ diffuse() × 3                                   ││
  │  │   → Jacobi: stencil*(Σneighbor + cf*v)          ││
  │  │   → output: velocity (swap each iter)           ││
  │  │                                                 ││
  │  │ inject_noise_into()                             ││
  │  │   → v += timestep * noise                       ││
  │  │   → output: velocity (swap)                     ││
  │  │                                                 ││
  │  │ calculate_divergence()                          ││
  │  │   → div = 0.5*((r.x-l.x)+(t.y-b.y))            ││
  │  │   → output: divergence_texture                  ││
  │  │                                                 ││
  │  │ solve_pressure() × 19                           ││
  │  │   → Jacobi: 0.25*(Σneighbor_p - div)            ││
  │  │   → Neumann BC at edges                         ││
  │  │   → output: pressure (swap each iter)           ││
  │  │                                                 ││
  │  │ subtract_gradient()                             ││
  │  │   → v -= 0.5*(∇p); no-slip at edges            ││
  │  │   → output: velocity (swap)                     ││
  │  └─────────────────────────────────────────────────┘│
  │                                                    │
  ├──────────────────────────────────────────────────┤
  │ lines.place_lines(velocity)                        │
  │   → spring dynamics per line endpoint              │
  │   → output: line buffer (swap)                     │
  ├──────────────────────────────────────────────────┤
  │ lines.draw_lines() + draw_endpoints()              │
  │   → render quads with fade-in + smooth edges        │
  └──────────────────────────────────────────────────┘

Catatan: fluid sub-step bisa terjadi 0, 1, atau beberapa kali per frame
tergantung akumulasi waktu. `fluid_frame_time` diakumulasi dari delta time
real, lalu dikurangi `fluid_timestep` setiap sub-step selesai.
```
