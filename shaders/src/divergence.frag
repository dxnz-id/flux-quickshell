#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 texel = vec2(1.0 / RES);

    vec4 tc = texture(simTex, uv);

    vec2 vL = texture(simTex, uv - vec2(texel.x, 0.0)).rg * 2.0 - 1.0;
    vec2 vR = texture(simTex, uv + vec2(texel.x, 0.0)).rg * 2.0 - 1.0;
    vec2 vB = texture(simTex, uv - vec2(0.0, texel.y)).rg * 2.0 - 1.0;
    vec2 vT = texture(simTex, uv + vec2(0.0, texel.y)).rg * 2.0 - 1.0;

    bool atBoundary = uv.x < texel.x || uv.x > 1.0 - texel.x ||
                       uv.y < texel.y || uv.y > 1.0 - texel.y;

    float divergence = atBoundary ? 0.0 : 0.5 * ((vR.x - vL.x) + (vT.y - vB.y));

    // One Jacobi pressure iteration using current pressure (B) and computed divergence
    float p = tc.b * 2.0 - 1.0;
    float pL = texture(simTex, uv - vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pR = texture(simTex, uv + vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pB = texture(simTex, uv - vec2(0.0, texel.y)).b * 2.0 - 1.0;
    float pT = texture(simTex, uv + vec2(0.0, texel.y)).b * 2.0 - 1.0;

    // Neumann BC: dp/dn = 0
    if (uv.x < texel.x) pL = p;
    if (uv.x > 1.0 - texel.x) pR = p;
    if (uv.y < texel.y) pB = p;
    if (uv.y > 1.0 - texel.y) pT = p;

    float newP = 0.25 * (pL + pR + pB + pT - divergence);

    fragColor = vec4(tc.rg, newP * 0.5 + 0.5, 1.0);
}
