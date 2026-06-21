#version 440

layout(binding = 0, std140) uniform PushConstants {
    vec4 uVelocityScale;
    float uCenterFactor;
    float uStencilFactor;
    float uInvRes;
    float uPadding;
};

layout(binding = 1) uniform sampler2D noiseTex;
layout(binding = 2) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 velSize = textureSize(velocityTex, 0);
    ivec2 noiseSize = textureSize(noiseTex, 0);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;

    // Map velocity position to noise texel
    ivec2 noisePos = ivec2(pos.x * noiseSize.x / velSize.x, pos.y);
    vec2 noise = texelFetch(noiseTex, noisePos, 0).xy;

    vec2 newVel = velocity + uVelocityScale.x * noise;
    fragColor = vec4(newVel, 0.0, 1.0);
}
