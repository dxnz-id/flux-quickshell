#version 440

layout(binding = 0) uniform sampler2D divergenceTex;
layout(binding = 1) uniform sampler2D pressureTex;
layout(binding = 8, std140) uniform FluidUniforms {
    float uTimestep;
    float uDissipation;
    float uAlpha;
    float uRbeta;
    float uCenterFactor;
    float uStencilFactor;
    float uNoiseMultiplier;
} u;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(pressureTex, 0);
    vec2 samplePos = vec2(pos) / vec2(size);

    float pressure = texelFetch(pressureTex, pos, 0).x;
    float divergence = texelFetch(divergenceTex, pos, 0).x;

    float l = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(-1, 0)).x;
    float r = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(1, 0)).x;
    float b = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(0, -1)).x;
    float t = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(0, 1)).x;

    if (pos.x == 0)
        l = pressure;
    else if (pos.x == size.x - 1)
        r = pressure;
    if (pos.y == 0)
        b = pressure;
    else if (pos.y == size.y - 1)
        t = pressure;

    float newPressure = u.uRbeta * (l + r + b + t + u.uAlpha * divergence);

    fragColor = vec4(newPressure, 0.0, 0.0, 0.0);
}
