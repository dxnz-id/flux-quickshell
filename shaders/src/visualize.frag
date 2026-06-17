#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

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

void main() {
    vec4 tc = texture(simTex, qt_TexCoord0);
    vec2 v = tc.rg * 2.0 - 1.0;
    float speed = length(v);
    float clamped = clamp(speed * 0.7, 0.0, 1.0);
    fragColor = vec4(speedColor(clamped), 1.0);
}
