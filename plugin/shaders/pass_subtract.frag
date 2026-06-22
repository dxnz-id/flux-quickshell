#version 440
layout(binding = 0) uniform sampler2D pressureTex;
layout(binding = 1) uniform sampler2D velocityTex;
layout(location = 0) out vec4 fragColor;
void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(velocityTex, 0);
    vec2 samplePos = vec2(pos) / vec2(size);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;

    float pL = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(-1, 0)).x;
    float pR = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(1, 0)).x;
    float pD = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(0, -1)).x;
    float pU = textureLodOffset(pressureTex, samplePos, 0.0, ivec2(0, 1)).x;

    vec2 gradP = vec2(pR - pL, pU - pD) * 0.5;
    vec2 newVel = velocity - gradP;

    vec2 bc = vec2(1.0);
    if (pos.x == 0 || pos.x == size.x - 1)
        bc.x = 0.0;
    if (pos.y == 0 || pos.y == size.y - 1)
        bc.y = 0.0;
    newVel *= bc;

    fragColor = vec4(newVel, 0.0, 1.0);
}
