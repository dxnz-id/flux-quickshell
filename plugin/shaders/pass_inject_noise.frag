#version 440

layout(binding = 0) uniform sampler2D noiseTex;
layout(binding = 1) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 velSize = textureSize(velocityTex, 0);
    ivec2 noiseSize = textureSize(noiseTex, 0);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;

    ivec2 noisePos = ivec2(pos.x * noiseSize.x / velSize.x, pos.y);
    vec2 noise = texelFetch(noiseTex, noisePos, 0).xy;

    float velocityScale = 1.0f;
    vec2 newVel = velocity + velocityScale * noise;
    fragColor = vec4(newVel, 0.0, 1.0);
}
