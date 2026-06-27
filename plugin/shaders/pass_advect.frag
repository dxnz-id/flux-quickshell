#version 440

layout(binding = 0) uniform sampler2D velocityTex;
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
    ivec2 size = textureSize(velocityTex, 0);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 samplePos = vec2(pos) + 0.5;
    float direction = 1.0f;
    vec2 advectedPos = (samplePos - direction * u.uTimestep * velocity) / vec2(size);
    float decay = 1.0 + u.uDissipation * u.uTimestep;
    vec2 newVel = textureLod(velocityTex, advectedPos, 0.0).xy / decay;

    fragColor = vec4(newVel, 0.0, 0.0);
}
