#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 texel = vec2(1.0 / RES);

    vec4 tc = texture(simTex, uv);
    vec2 v = tc.rg * 2.0 - 1.0;

    float pL = texture(simTex, uv - vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pR = texture(simTex, uv + vec2(texel.x, 0.0)).b * 2.0 - 1.0;
    float pB = texture(simTex, uv - vec2(0.0, texel.y)).b * 2.0 - 1.0;
    float pT = texture(simTex, uv + vec2(0.0, texel.y)).b * 2.0 - 1.0;

    vec2 bc = vec2(1.0);
    if (uv.x < texel.x || uv.x > 1.0 - texel.x) bc.x = 0.0;
    if (uv.y < texel.y || uv.y > 1.0 - texel.y) bc.y = 0.0;

    vec2 newV = bc * (v - 0.5 * vec2(pR - pL, pT - pB));

    fragColor = vec4(newV * 0.5 + 0.5, tc.b, 1.0);
}
