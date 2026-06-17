#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;
const float VISCOSITY = 5.0;
const float TIMESTEP = 1.0;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 texel = vec2(1.0 / RES);

    float centerFactor = 1.0 / (VISCOSITY * TIMESTEP);
    float stencilFactor = 1.0 / (4.0 + centerFactor);

    vec4 tc = texture(simTex, uv);
    vec2 v = tc.rg * 2.0 - 1.0;

    vec2 vL = texture(simTex, uv - vec2(texel.x, 0.0)).rg * 2.0 - 1.0;
    vec2 vR = texture(simTex, uv + vec2(texel.x, 0.0)).rg * 2.0 - 1.0;
    vec2 vB = texture(simTex, uv - vec2(0.0, texel.y)).rg * 2.0 - 1.0;
    vec2 vT = texture(simTex, uv + vec2(0.0, texel.y)).rg * 2.0 - 1.0;

    // No-slip: zero out out-of-domain neighbors
    if (uv.x < texel.x)     vL = vec2(0.0);
    if (uv.x > 1.0 - texel.x) vR = vec2(0.0);
    if (uv.y < texel.y)     vB = vec2(0.0);
    if (uv.y > 1.0 - texel.y) vT = vec2(0.0);

    vec2 newVel = stencilFactor * (vL + vR + vB + vT + centerFactor * v);

    // No-slip: zero velocity at all boundaries
    if (uv.x < texel.x || uv.x > 1.0 - texel.x ||
        uv.y < texel.y || uv.y > 1.0 - texel.y)
        newVel = vec2(0.0);

    fragColor = vec4(newVel * 0.5 + 0.5, tc.b, 1.0);
}
