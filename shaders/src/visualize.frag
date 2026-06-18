#version 420

// Qt 6 ShaderEffect UBO convention:
// · Binding 0 = UBO with qt_Matrix + qt_Opacity + custom properties
// · Binding 1+ = samplers/textures
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;    // Qt built-in (required even in frag shader)
    float qt_Opacity;   // Qt built-in
    int   debugMode;    // custom: 0=Normal 1=Fluid 2=Noise 3=Pressure 4=Divergence
} ubuf;

layout(binding = 1) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// ── Helper ─────────────────────────────────────────────────────────────────
vec2 decode(vec2 v) { return v * 2.0 - 1.0; }

// ── Jet heatmap colormap ───────────────────────────────────────────────────
vec3 speedColor(float t) {
    vec3 c0 = vec3(0.0, 0.0, 0.2);
    vec3 c1 = vec3(0.0, 0.2, 0.5);
    vec3 c2 = vec3(0.0, 0.6, 0.8);
    vec3 c3 = vec3(0.2, 0.8, 0.3);
    vec3 c4 = vec3(0.8, 0.7, 0.0);
    vec3 c5 = vec3(0.9, 0.2, 0.1);
    vec3 c6 = vec3(0.2, 0.0, 0.0);
    if (t < 0.12) return mix(c0, c1, t / 0.12);
    if (t < 0.25) return mix(c1, c2, (t - 0.12) / 0.13);
    if (t < 0.40) return mix(c2, c3, (t - 0.25) / 0.15);
    if (t < 0.60) return mix(c3, c4, (t - 0.40) / 0.20);
    if (t < 0.80) return mix(c4, c5, (t - 0.60) / 0.20);
    return mix(c5, c6, (t - 0.80) / 0.20);
}

// ── Divergence colormap: blue=−, gray=0, red=+ ─────────────────────────────
vec3 divergenceColor(float d) {
    float t = clamp(d * 0.5 + 0.5, 0.0, 1.0);
    if (t < 0.5) return mix(vec3(0.0, 0.1, 0.85), vec3(0.45, 0.45, 0.45), t / 0.5);
    return mix(vec3(0.45, 0.45, 0.45), vec3(0.9, 0.1, 0.0), (t - 0.5) / 0.5);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 tc  = texture(simTex, uv);
    vec2 v   = decode(tc.rg);

    int mode = ubuf.debugMode;

    // ── 0: Normal — jet heatmap by speed ──────────────────────────────────
    if (mode == 0) {
        float clamped = clamp(length(v) * 0.7, 0.0, 1.0);
        fragColor = vec4(speedColor(clamped), ubuf.qt_Opacity);

    // ── 1: Fluid — raw vx/vy as R/G (matches reference debug mode) ─────────
    } else if (mode == 1) {
        fragColor = vec4(v * 0.5 + 0.5, 0.15, ubuf.qt_Opacity);

    // ── 2: Noise — absolute velocity channels ──────────────────────────────
    } else if (mode == 2) {
        fragColor = vec4(abs(v), clamp(length(v), 0.0, 1.0) * 0.4, ubuf.qt_Opacity);

    // ── 3: Pressure — B channel (pressure scalar stored by solver) ──────────
    } else if (mode == 3) {
        float p = tc.b;
        float pC = clamp((p - 0.5) * 4.0, -1.0, 1.0);
        fragColor = vec4(divergenceColor(pC), ubuf.qt_Opacity);

    // ── 4: Divergence — ∂vx/∂x + ∂vy/∂y ────────────────────────────────
    } else if (mode == 4) {
        vec2 texel = vec2(1.0 / 128.0);
        vec2 vR = decode(texture(simTex, uv + vec2(texel.x, 0.0)).rg);
        vec2 vL = decode(texture(simTex, uv - vec2(texel.x, 0.0)).rg);
        vec2 vU = decode(texture(simTex, uv + vec2(0.0, texel.y)).rg);
        vec2 vD = decode(texture(simTex, uv - vec2(0.0, texel.y)).rg);
        float div = (vR.x - vL.x) / (2.0 * texel.x)
                  + (vU.y - vD.y) / (2.0 * texel.y);
        fragColor = vec4(divergenceColor(clamp(div * 1.5, -1.0, 1.0)), ubuf.qt_Opacity);

    // ── 5: Lines — passthrough from flow_lines render (simTex = sim.lines) ─
    } else {
        fragColor = vec4(texture(simTex, uv).rgb, ubuf.qt_Opacity);
    }
}
