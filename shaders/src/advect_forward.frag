#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;
const float TIMESTEP = 1.0 / 60.0;  // match reference fluid_timestep = 1/60

void main() {
    vec2 uv = qt_TexCoord0;

    vec2 vel = texture(simTex, uv).rg * 2.0 - 1.0;

    vec2 advectedUv = uv - TIMESTEP * vel / RES;

    vec2 newVel = texture(simTex, advectedUv).rg * 2.0 - 1.0;

    float pressure = texture(simTex, uv).b;

    fragColor = vec4(newVel * 0.5 + 0.5, pressure, 1.0);
}
