#version 440

layout(binding = 0) uniform sampler2D velocityTex;
layout(binding = 8, std140) uniform FluidUniforms {
    float uTimestep;
    float uDissipation;
    float uAlpha;
    float uRbeta;
    float uCenterFactor;
    float uStencilFactor;
} u;

layout(location = 0) out vec4 fragColor;

ivec2 clampPos(ivec2 p, ivec2 size) {
    return clamp(p, ivec2(0), size - ivec2(1));
}

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(velocityTex, 0);
    vec2 c = texelFetch(velocityTex, pos, 0).xy;
    vec2 l = texelFetch(velocityTex, clampPos(pos + ivec2(-1, 0), sz), 0).xy;
    vec2 r = texelFetch(velocityTex, clampPos(pos + ivec2( 1, 0), sz), 0).xy;
    vec2 b = texelFetch(velocityTex, clampPos(pos + ivec2(0, -1), sz), 0).xy;
    vec2 t = texelFetch(velocityTex, clampPos(pos + ivec2(0,  1), sz), 0).xy;
    vec2 newVel = u.uStencilFactor * (l + r + b + t + u.uCenterFactor * c);
    fragColor = vec4(newVel, 0.0, 0.0);
}
