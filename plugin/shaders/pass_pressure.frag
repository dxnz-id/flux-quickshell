#version 440

layout(binding = 1) uniform sampler2D divergenceTex;
layout(binding = 2) uniform sampler2D pressureTex;

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

    float r_beta = 0.25f;
    float alpha = -1.0f;
    float newPressure = r_beta * (l + r + b + t + alpha * divergence);

    fragColor = vec4(newPressure, 0.0, 0.0, 1.0);
}
