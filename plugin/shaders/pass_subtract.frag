#version 440
layout(binding = 0, std140) uniform PushConstants {
    vec4 uVelocityScale;
    float uCenterFactor;
    float uStencilFactor;
    float uInvRes;
    float uPadding;
};
layout(binding = 1) uniform sampler2D pressureTex;
layout(binding = 2) uniform sampler2D velocityTex;
layout(location = 0) out vec4 fragColor;
void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 pL = texelFetch(pressureTex, pos - ivec2(1, 0), 0).xy;
    vec2 pR = texelFetch(pressureTex, pos + ivec2(1, 0), 0).xy;
    vec2 pD = texelFetch(pressureTex, pos - ivec2(0, 1), 0).xy;
    vec2 pU = texelFetch(pressureTex, pos + ivec2(0, 1), 0).xy;
    vec2 gradP = vec2(pR.x - pL.x, pU.x - pD.x) * 0.5;
    vec2 newVel = velocity - gradP;
    fragColor = vec4(newVel, 0.0, 1.0);
}
