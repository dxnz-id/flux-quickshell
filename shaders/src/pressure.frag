#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 texel = vec2(1.0 / RES);

    vec4 tc = texture(simTex, uv);
    float divergence = tc.a * 2.0 - 1.0;
    float pressure = tc.b * 2.0 - 1.0;

    float pL = texture(simTex, uv - vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pR = texture(simTex, uv + vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pB = texture(simTex, uv - vec2(0.0, texel.y)).b * 2.0 - 1.0;
    float pT = texture(simTex, uv + vec2(0.0, texel.y)).b * 2.0 - 1.0;

    // Neumann BC: dp/dn = 0 at edges
    if (uv.x < texel.x) pL = pressure;
    if (uv.x > 1.0 - texel.x) pR = pressure;
    if (uv.y < texel.y) pB = pressure;
    if (uv.y > 1.0 - texel.y) pT = pressure;

    float newP = 0.25 * (pL + pR + pB + pT - divergence);

    fragColor = vec4(tc.rg, newP * 0.5 + 0.5, tc.a);
}
